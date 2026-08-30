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
      viereisetRaiteet    adjacent/parallel -> disambiguation hints
    (Infra fields the app never reads are intentionally NOT kept — the per-track
    `rautatieliikennepaikat` owning-OID list, `seuraavatRaiteet` next-track edges
    (near-empty in live data, 38 refs nationally, so the routing graph is
    reconstructed from geometry instead) and `ratakmvalit` linear referencing
    (~0.5 MB of dead weight in the blob). See TRACK_PROPS.)
  * `stations`: timetable `stationShortCode` -> { name, tracks:[track OID] }.
    The station <-> infra crosswalk is resolved here (offline) by UIC code
    against the union of the operating-points layer (infra
    `rautatieliikennepaikat.geojson`) and their finer parts
    (`liikennepaikanosat`), falling back to normalised name then nearest-point
    geometry; how each station resolved is printed, not baked (the app reads
    only name + tracks). See the module notes for why no single key suffices.

The reverse `operatingPoints` map (OID -> stationShortCode) is no longer emitted:
the app resolves stations via the `stations.tracks` lists alone, so baking the
reverse direction was dead weight (#15).

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

Sanity checks (printed, non-fatal): report_extent() flags geometry crossing the
tiled extent -- proof the tiling is cutting the network, so something beyond the
cut was never requested -- and report_gaps() flags main-track endpoints dangling
in open space, which draw as a break on the map and leave RailGraph's routing
graph disconnected. Both failures are otherwise silent; see their docstrings.

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
# N_MIN must clear Finland's southernmost rail -- the Hanko harbour branch at
# N 6_638_363. It used to be 6_640_000, which cut 1.6 km off the Hanko peninsula:
# the yard and station throat at Hanko asema lie entirely below that line, so no
# tile ever requested them and they were silently absent from the bake (only the
# one track straddling the boundary came back, leaving a visible stub-end short
# of the station). report_extent() below guards against this recurring.
N_MIN, N_MAX = 6_620_000, 7_780_000
STEP = 150_000

# Track properties kept per feature (everything Tier-2 matching needs; the bulky
# rest — speed limits, electrification, elements, etc. — is dropped). `geometria`
# is dropped because the GeoJSON `geometry` already carries the centreline.
# Three more infra fields are NOT baked because RailGraph::loadFromJson reads none:
#   * `rautatieliikennepaikat` (per-track owning operating-point OIDs) — the
#     station<->track crosswalk is resolved here at bake time from the ops' own
#     `raiteet`, not from this per-track list (#15).
#   * `seuraavatRaiteet` (next-track edges) — near-empty in live data (38 refs
#     nationally), so the routing graph is reconstructed from geometry endpoints +
#     `viereisetRaiteet` instead; this field never drove it.
#   * `ratakmvalit` (line number + km chainage) — ~0.5 MB; route chainage is
#     computed from geometry in RailGraph::buildPolyline, not linear referencing.
TRACK_PROPS = (
    "tunniste",
    "paaraide",
    "kaupallinenNumero",
    "viereisetRaiteet",
)

# Geo crosswalk fallback: accept a nearest operating point only within this many
# metres of the timetable station's coordinate.
GEO_MATCH_MAX_M = 1500.0

# Continuity check thresholds. A snapshot with a hole in a running line is
# silently wrong: the map draws a visible break, and because RailGraph welds
# tracks into nodes on a 10 m grid, the routing graph is left *disconnected*
# there, so Tier-2 route matching can't build a through-route across the hole.
# Upstream has shipped such holes (the Karjaa-Inkoo stretch of the Rantarata was
# split into two OIDs with a 1.8 km gap between them in the 2026-07-01 bake), so
# flag them here rather than waiting for someone to spot the break on the map.
GAP_MIN_M = 30.0      # below this RailGraph's 10 m node grid still welds the ends
GAP_MAX_M = 5000.0    # beyond this it's a genuine line end (buffer stop, border)

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
    """Resolve timetable stationShortCode -> infra tracks.

    Returns stations[shortCode] = {name, tracks:[OID]} — the two fields the app
    reads; how each station resolved (uic/name/geo + distance) is printed only.

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
    counts = {"uic": 0, "name": 0, "geo": 0, "unresolved": 0}
    for s in stations:
        code = s.get("stationShortCode")
        if not code:
            continue
        uic = s.get("stationUICCode")
        match, entry = None, None
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
                    match, entry = "geo", best[2]
        # A matched entry with no stable OID can't be cross-walked (it keys the
        # station<->op map); treat it as unresolved rather than KeyError-ing.
        oid = entry.get("tunniste") if entry is not None else None
        if oid is None:
            counts["unresolved"] += 1
            continue
        counts[match] += 1
        out[code] = {
            "name": entry.get("nimi"),
            "tracks": member_tracks(entry),
        }

    resolved = len(out)
    no_tracks = sum(1 for v in out.values() if not v["tracks"])
    print(f"  crosswalk: {resolved} stations resolved "
          f"(uic={counts['uic']} name={counts['name']} geo={counts['geo']}), "
          f"{counts['unresolved']} unresolved, {no_tracks} resolved-but-no-tracks")
    return out


def report_extent(features):
    """Warn when baked geometry pokes outside the tiled extent.

    A feature straddling the boundary is returned whole, so any vertex outside
    the extent is proof that the tiling cuts through the network -- and whatever
    lies *entirely* beyond the cut was never requested at all, so it is missing
    without any error to notice. Widen E_MIN/E_MAX/N_MIN/N_MAX until this is
    quiet. Returns the number of straddling tracks.
    """
    out = []
    for feat in features:
        geom = feat["geometry"]
        strands = geom["coordinates"] if geom["type"] == "MultiLineString" \
            else [geom["coordinates"]]
        pts = [p for strand in strands for p in strand]
        if not pts:
            continue
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        if min(xs) < E_MIN or max(xs) > E_MAX or min(ys) < N_MIN or max(ys) > N_MAX:
            out.append((feat["properties"].get("tunniste"), min(xs), min(ys)))
    if not out:
        print("  extent: all geometry inside the tiled extent")
        return 0
    print(f"  extent: {len(out)} track(s) cross the tiled extent -- widen it, "
          f"geometry fully beyond the edge was never requested:")
    for oid, x, y in out[:10]:
        print(f"    E{x:.0f} N{y:.0f}  {oid}")
    return len(out)


def report_gaps(features):
    """Warn about main-track endpoints that dangle in open space.

    Builds a 100 m spatial hash of every vertex, then asks of each main track's
    two endpoints: how far is the nearest vertex belonging to a *different*
    line? A running line is a chain, so that distance is normally ~0. Anything
    between GAP_MIN_M and GAP_MAX_M is a hole in the network; further than that
    (or nothing found at all) is a real terminus and is not reported.

    Returns the number of suspicious gaps.
    """
    cell = 100.0
    grid = {}
    lines = []
    for feat in features:
        geom = feat["geometry"]
        strands = geom["coordinates"] if geom["type"] == "MultiLineString" \
            else [geom["coordinates"]]
        for pts in strands:
            if len(pts) < 2:
                continue
            idx = len(lines)
            lines.append((feat["properties"], pts))
            for pt in pts:
                grid.setdefault((int(pt[0] // cell), int(pt[1] // cell)), []) \
                    .append((pt[0], pt[1], idx))

    def nearest_other(x, y, idx):
        """Distance to the closest vertex not on line `idx`, or None if none near."""
        best = None
        for r in range(1, int(GAP_MAX_M // cell) + 2):
            cx, cy = int(x // cell), int(y // cell)
            for dx in range(-r, r + 1):
                for dy in range(-r, r + 1):
                    # Only the newly added ring; inner cells were scanned already.
                    if r > 1 and max(abs(dx), abs(dy)) != r:
                        continue
                    for qx, qy, qi in grid.get((cx + dx, cy + dy), ()):
                        if qi == idx:
                            continue
                        d = math.hypot(qx - x, qy - y)
                        if best is None or d < best:
                            best = d
            # Everything beyond this ring is at least r*cell away, so we're done.
            if best is not None and best < r * cell:
                break
        return best

    gaps = []
    for idx, (props, pts) in enumerate(lines):
        if not props.get("paaraide"):
            continue
        for pt in (pts[0], pts[-1]):
            d = nearest_other(pt[0], pt[1], idx)
            if d is not None and GAP_MIN_M < d <= GAP_MAX_M:
                gaps.append((d, props.get("tunniste"), pt[0], pt[1]))

    gaps.sort(reverse=True)
    if not gaps:
        print("  continuity: no main-track gaps")
        return 0
    print(f"  continuity: {len(gaps)} dangling main-track endpoint(s) -- each is a "
          f"visible break in the map and a disconnected routing graph:")
    for d, oid, x, y in gaps[:10]:
        lat, lon = tm35fin_to_wgs84(x, y)
        print(f"    {d:8.0f} m  {lat:.5f},{lon:.5f}  {oid}")
    return len(gaps)


def main():
    tracks = collect_tracks()
    report_extent(tracks.values())
    report_gaps(tracks.values())
    track_ids = set(tracks.keys())
    stations = build_crosswalk(track_ids)

    out_fc = {
        "type": "FeatureCollection",
        "schemaVersion": SCHEMA_VERSION,
        "bakedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "crs": {"type": "name", "properties": {"name": "urn:ogc:def:crs:EPSG::3067"}},
        "features": list(tracks.values()),
        "stations": stations,
    }
    raw = json.dumps(out_fc, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    blob = struct.pack(">I", len(raw)) + zlib.compress(raw, 9)   # qCompress format
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(blob)
    print(f"wrote {OUT}  schema v{SCHEMA_VERSION}  "
          f"({len(raw)/1e6:.1f} MB raw -> {len(blob)/1e6:.1f} MB compressed)")


if __name__ == "__main__":
    main()
