#!/usr/bin/env python3
"""Bake the Finnish rail network into a committed, compressed snapshot.

The Digitraffic infra-api caps how much geometry one request may return, so this
tiles the national extent (EPSG:3067 / TM35FIN metres) into regional bbox
requests, merges the features (de-duplicated by their `tunniste` OID) and writes a
Qt-compressed blob the app embeds as a resource and loads at startup — so track
geometry is no longer fetched on every launch. The rail topology changes rarely;
re-run this when it does.

Schema v2 (Tier-2 track accuracy) keeps, per track, the *identity + topology*
fields needed for route-constrained map matching (not just bare geometry), and
additionally bakes a station crosswalk so the app can constrain matching to a
train's scheduled path:

  * Per track feature properties (see `TRACK_PROPS`):
      tunniste            stable track OID  (graph node id)
      paaraide            is-main-track flag (weight running lines over sidings)
      kaupallinenNumero   platform / commercial track number (~ timetable
                          `commercialTrack`)
      seuraavatRaiteet    next tracks       -> directed graph edges
      viereisetRaiteet    adjacent/parallel -> disambiguation hints
      rautatieliikennepaikat  owning operating-point OID(s) (track -> station)
      ratakmvalit         line number + km chainage (linear referencing)
  * `stations`: timetable `stationShortCode` -> { uic, opOid, name, match,
    distM, tracks:[track OID] }.  The station <-> infra crosswalk is resolved
    here (offline) by UIC code against the union of operating points
    (`rautatieliikennepaikat`) and their finer parts (`liikennepaikanosat`),
    falling back to normalised name then nearest-point geometry. See the module
    notes for why no single key suffices.
  * `operatingPoints`: infra operating-point / part OID -> `stationShortCode`
    (the reverse crosswalk, so a track's `rautatieliikennepaikat` OID can be
    named).

Why the crosswalk is non-trivial (verified against live data, 2026-06):
  * infra `lyhenne` ("Hel", "Psl") is NOT the timetable `stationShortCode`
    ("HKI", "PSL").
  * `uicKoodi` is null on many major *operating points* (Helsinki, Tampere) and
    its numbering there differs from `/metadata/stations`.
  * BUT the finer `liikennepaikanosat` (operating-point parts) carry a
    `uicKoodi` that matches `/metadata/stations.stationUICCode` (e.g. Pasila
    uic=10). Pasila is not its own operating point — its tracks belong to the
    Helsinki op — so the parts layer is what makes hub stations resolvable.
    UIC over union(ops, parts) resolves ~all passenger stations.
  * A part may list no `raiteet` of its own (e.g. Helsinki asema); we then fall
    back to its parent op's tracks via the part's `liikennepaikka` link.

Output: resources/rails.geojson.qz
  Format = 4-byte big-endian uncompressed length + zlib stream. This is exactly
  Qt's qCompress() container, so the app reads it with qUncompress() (Qt Core
  only — no zlib/find_package or build-time gunzip needed). A `schemaVersion`
  field lets the loader detect (and reject/ignore) an old geometry-only blob.

Network gotchas: infra-api `latest` 307-redirects to a versioned, build-numbered
path (urllib follows it automatically; a curl probe needs `-L`). gzip is
mandatory on both the infra-api and `/metadata/stations` — every request below
sends `Accept-Encoding: gzip` and decompresses the reply.

Usage: python3 scripts/bake_rails.py
"""
import gzip
import json
import math
import struct
import sys
import time
import urllib.request
import zlib
from pathlib import Path

SCHEMA_VERSION = 2

INFRA = "https://rata.digitraffic.fi/infra-api/latest"
STATIONS_URL = "https://rata.digitraffic.fi/api/v1/metadata/stations"
USER_AGENT = "TrainsOnMap/0.1 (rail-geometry bake)"

# Rail-relevant extent of Finland in EPSG:3067 metres, tiled at 150 km. Tiles
# over sea / the far north simply come back empty.
E_MIN, E_MAX = 60_000, 740_000
N_MIN, N_MAX = 6_640_000, 7_780_000
STEP = 150_000

# Track properties kept per feature (everything Tier-2 matching needs; the bulky
# rest — speed limits, electrification, elements, etc. — is dropped). `geometria`
# is dropped because the GeoJSON `geometry` already carries the centreline.
TRACK_PROPS = (
    "tunniste",
    "paaraide",
    "kaupallinenNumero",
    "seuraavatRaiteet",
    "viereisetRaiteet",
    "rautatieliikennepaikat",
    "ratakmvalit",
)

# Geo crosswalk fallback: accept a nearest operating point only within this many
# metres of the timetable station's coordinate.
GEO_MATCH_MAX_M = 1500.0

OUT = Path(__file__).resolve().parent.parent / "resources" / "rails.geojson.qz"


def fetch_json(url):
    """GET `url` as JSON, sending+decoding gzip and following the version 307."""
    req = urllib.request.Request(url, headers={
        "Digitraffic-User": USER_AGENT,
        "Accept-Encoding": "gzip",          # required by infra-api and stations
    })
    with urllib.request.urlopen(req, timeout=120) as resp:
        raw = resp.read()
        if resp.headers.get("Content-Encoding") == "gzip":
            raw = gzip.decompress(raw)
        return json.loads(raw)


def tm35fin_to_wgs84(e, n):
    """Inverse ETRS-TM35FIN (EPSG:3067) -> WGS84 (lat, lon) in degrees.

    Used only for the nearest-point crosswalk fallback, so a few-metre accuracy
    (verified against /metadata/stations coordinates) is plenty.
    """
    f = 1.0 / 298.257222101
    a = 6378137.0
    k0 = 0.9996
    lon0 = math.radians(27.0)
    e0 = 500000.0
    nn = f / (2 - f)
    big_a = a / (1 + nn) * (1 + nn**2 / 4 + nn**4 / 64)
    e2 = f * (2 - f)
    xi = n / (big_a * k0)
    eta = (e - e0) / (big_a * k0)
    b1 = nn / 2 - 2 / 3 * nn**2 + 37 / 96 * nn**3
    b2 = 1 / 48 * nn**2 + 1 / 15 * nn**3
    b3 = 17 / 480 * nn**3
    xip = xi - (b1 * math.sin(2 * xi) * math.cosh(2 * eta)
                + b2 * math.sin(4 * xi) * math.cosh(4 * eta)
                + b3 * math.sin(6 * xi) * math.cosh(6 * eta))
    etap = eta - (b1 * math.cos(2 * xi) * math.sinh(2 * eta)
                  + b2 * math.cos(4 * xi) * math.sinh(4 * eta)
                  + b3 * math.cos(6 * xi) * math.sinh(6 * eta))
    chi = math.asin(math.sin(xip) / math.cosh(etap))
    lat = chi + (e2 / 2 + 5 / 24 * e2**2) * math.sin(2 * chi) \
        + (7 / 48 * e2**2) * math.sin(4 * chi)
    lon = lon0 + math.atan(math.sinh(etap) / math.cos(xip))
    return math.degrees(lat), math.degrees(lon)


def haversine_m(lat1, lon1, lat2, lon2):
    r = 6371000.0
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp = math.radians(lat2 - lat1)
    dl = math.radians(lon2 - lon1)
    x = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(math.sqrt(x))


def collect_tracks():
    """Tile the national extent and return {tunniste: geometry-only-+-props feature}."""
    features = {}
    tiles = 0
    for e0 in range(E_MIN, E_MAX, STEP):
        for n0 in range(N_MIN, N_MAX, STEP):
            tiles += 1
            e1, n1 = e0 + STEP, n0 + STEP
            url = f"{INFRA}/raiteet.geojson?bbox={e0},{n0},{e1},{n1}"
            try:
                fc = fetch_json(url)
            except Exception as ex:                      # noqa: BLE001
                print(f"  tile {e0},{n0} failed: {ex}", file=sys.stderr)
                continue
            got = 0
            for f in fc.get("features", []):
                props = f.get("properties", {})
                key = props.get("tunniste") or id(f)
                if key in features:
                    continue
                kept = {k: props[k] for k in TRACK_PROPS if k in props}
                features[key] = {"type": "Feature", "geometry": f["geometry"],
                                 "properties": kept}
                got += 1
            print(f"  tile {e0},{n0}: +{got} (unique {len(features)})")
            time.sleep(0.2)                              # be a good API citizen
    print(f"{tiles} tiles, {len(features)} unique track features")
    return features


def build_crosswalk(track_ids):
    """Resolve timetable stationShortCode <-> infra tracks.

    Returns (stations, operating_points):
      stations[shortCode] = {uic, opOid, name, match, distM, tracks:[OID]}
      operating_points[OID] = shortCode    (reverse; op and part OIDs)

    `track_ids` is the set of track OIDs we actually baked, so member-track lists
    only reference geometry that ships in this blob.
    """
    ops = [f["properties"]
           for f in fetch_json(f"{INFRA}/rautatieliikennepaikat.geojson").get("features", [])]
    parts = [f["properties"]
             for f in fetch_json(f"{INFRA}/liikennepaikanosat.geojson").get("features", [])]
    stations = fetch_json(STATIONS_URL)
    print(f"  infra: {len(ops)} operating points, {len(parts)} parts; "
          f"{len(stations)} timetable stations")

    op_by_oid = {p["tunniste"]: p for p in ops if p.get("tunniste")}

    def member_tracks(entry):
        """Tracks for an op/part, falling back to the parent op when a part has
        none of its own (e.g. Helsinki asema). Intersected with baked geometry."""
        oid_list = entry.get("raiteet") or []
        if not oid_list and entry.get("liikennepaikka") in op_by_oid:
            oid_list = op_by_oid[entry["liikennepaikka"]].get("raiteet") or []
        return [oid for oid in oid_list if oid in track_ids]

    # UIC index over the union; parts (finer, e.g. Pasila) override coarse ops.
    by_uic = {}
    for p in ops:
        if p.get("uicKoodi") is not None:
            by_uic.setdefault(p["uicKoodi"], p)
    for p in parts:
        if p.get("uicKoodi") is not None:
            by_uic[p["uicKoodi"]] = p

    def norm(name):
        return (name or "").lower().replace(" asema", "") \
            .replace("ä", "a").replace("ö", "o").strip()

    by_name = {}
    for p in ops + parts:
        by_name.setdefault(norm(p.get("nimi")), p)

    geo_pts = []                          # (lat, lon, entry) for nearest fallback
    for p in ops + parts:
        loc = p.get("virallinenSijainti")
        if loc and len(loc) == 2:
            lat, lon = tm35fin_to_wgs84(loc[0], loc[1])
            geo_pts.append((lat, lon, p))

    out = {}
    op_to_code = {}
    counts = {"uic": 0, "name": 0, "geo": 0, "unresolved": 0}
    for s in stations:
        code = s.get("stationShortCode")
        if not code:
            continue
        uic = s.get("stationUICCode")
        match, dist, entry = None, 0.0, None
        if uic in by_uic:
            match, entry = "uic", by_uic[uic]
        elif norm(s.get("stationName")) in by_name:
            match, entry = "name", by_name[norm(s.get("stationName"))]
        else:
            lat, lon = s.get("latitude"), s.get("longitude")
            if lat is not None and geo_pts:
                best = min(geo_pts, key=lambda g: haversine_m(lat, lon, g[0], g[1]))
                d = haversine_m(lat, lon, best[0], best[1])
                if d <= GEO_MATCH_MAX_M:
                    match, dist, entry = "geo", d, best[2]
        # A matched entry with no stable OID can't be cross-walked (it keys the
        # station<->op map); treat it as unresolved rather than KeyError-ing.
        oid = entry.get("tunniste") if entry is not None else None
        if oid is None:
            counts["unresolved"] += 1
            continue
        counts[match] += 1
        out[code] = {
            "uic": uic,
            "opOid": oid,
            "name": entry.get("nimi"),
            "match": match,
            "distM": round(dist, 1),
            "tracks": member_tracks(entry),
        }
        op_to_code[oid] = code

    resolved = len(out)
    no_tracks = sum(1 for v in out.values() if not v["tracks"])
    print(f"  crosswalk: {resolved} stations resolved "
          f"(uic={counts['uic']} name={counts['name']} geo={counts['geo']}), "
          f"{counts['unresolved']} unresolved, {no_tracks} resolved-but-no-tracks")
    return out, op_to_code


def main():
    tracks = collect_tracks()
    track_ids = set(tracks.keys())
    stations, operating_points = build_crosswalk(track_ids)

    out_fc = {
        "type": "FeatureCollection",
        "schemaVersion": SCHEMA_VERSION,
        "bakedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "crs": {"type": "name", "properties": {"name": "urn:ogc:def:crs:EPSG::3067"}},
        "features": list(tracks.values()),
        "stations": stations,
        "operatingPoints": operating_points,
    }
    raw = json.dumps(out_fc, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    blob = struct.pack(">I", len(raw)) + zlib.compress(raw, 9)   # qCompress format
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(blob)
    print(f"wrote {OUT}  schema v{SCHEMA_VERSION}  "
          f"({len(raw)/1e6:.1f} MB raw -> {len(blob)/1e6:.1f} MB compressed)")


if __name__ == "__main__":
    main()
