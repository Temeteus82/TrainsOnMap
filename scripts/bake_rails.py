#!/usr/bin/env python3
"""Bake the Finnish rail network into a committed, compressed GeoJSON snapshot.

The Digitraffic infra-api caps how much geometry one request may return, so this
tiles the national extent (EPSG:3067 / TM35FIN metres) into regional bbox
requests, merges the features (de-duplicated by their `tunniste` OID), strips
everything but the geometry, and writes a Qt-compressed blob the app embeds as a
resource and loads at startup — so track geometry is no longer fetched on every
launch. The rail topology changes rarely; re-run this when it does.

Output: resources/rails.geojson.qz
  Format = 4-byte big-endian uncompressed length + zlib stream. This is exactly
  Qt's qCompress() container, so the app reads it with qUncompress() (Qt Core
  only — no zlib/find_package or build-time gunzip needed).

Usage: python3 scripts/bake_rails.py
"""
import gzip
import json
import struct
import sys
import time
import urllib.request
import zlib
from pathlib import Path

BASE = "https://rata.digitraffic.fi/infra-api/latest/raiteet.geojson"
USER_AGENT = "TrainsOnMap/0.1 (rail-geometry bake)"

# Rail-relevant extent of Finland in EPSG:3067 metres, tiled at 150 km. Tiles
# over sea / the far north simply come back empty.
E_MIN, E_MAX = 60_000, 740_000
N_MIN, N_MAX = 6_640_000, 7_780_000
STEP = 150_000

OUT = Path(__file__).resolve().parent.parent / "resources" / "rails.geojson.qz"


def fetch_tile(e0, n0, e1, n1):
    url = f"{BASE}?bbox={e0},{n0},{e1},{n1}"
    req = urllib.request.Request(url, headers={
        "Digitraffic-User": USER_AGENT,
        "Accept-Encoding": "gzip",     # infra-api requires it
    })
    with urllib.request.urlopen(req, timeout=120) as resp:
        raw = resp.read()
        if resp.headers.get("Content-Encoding") == "gzip":
            raw = gzip.decompress(raw)
        return json.loads(raw)


def main():
    features = {}          # tunniste -> geometry-only feature
    tiles = total = 0
    for e0 in range(E_MIN, E_MAX, STEP):
        for n0 in range(N_MIN, N_MAX, STEP):
            tiles += 1
            e1, n1 = e0 + STEP, n0 + STEP
            try:
                fc = fetch_tile(e0, n0, e1, n1)
            except Exception as ex:                      # noqa: BLE001
                print(f"  tile {e0},{n0} failed: {ex}", file=sys.stderr)
                continue
            got = 0
            for f in fc.get("features", []):
                key = f.get("properties", {}).get("tunniste") or id(f)
                if key in features:
                    continue
                features[key] = {"type": "Feature", "geometry": f["geometry"]}
                got += 1
            total += got
            print(f"  tile {e0},{n0}: +{got} (unique {len(features)})")
            time.sleep(0.2)                              # be a good API citizen
    print(f"{tiles} tiles, {len(features)} unique features")

    out_fc = {
        "type": "FeatureCollection",
        "crs": {"type": "name", "properties": {"name": "urn:ogc:def:crs:EPSG::3067"}},
        "features": list(features.values()),
    }
    raw = json.dumps(out_fc, separators=(",", ":")).encode("utf-8")
    blob = struct.pack(">I", len(raw)) + zlib.compress(raw, 9)   # qCompress format
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(blob)
    print(f"wrote {OUT}  ({len(raw)/1e6:.1f} MB raw -> {len(blob)/1e6:.1f} MB compressed)")


if __name__ == "__main__":
    main()
