# Track-accuracy refinement — Tier 2 plan (topology-routed map matching)

Tier 1 (shipped) improved marker accuracy with **no new data pipeline**:

- **Direction-aware snapping** — `TrackService::matchToNetwork` charges a candidate
  track a heading-misalignment penalty, so a fix near a junction snaps to the
  track the train runs *along* rather than one it merely crosses.
- **Between-fix interpolation** — `TrainMarker.qml` glides along the rail between
  the periodic fixes (`CoordinateAnimation`), honouring the reduced-motion flag.
- **Stopped-train continuity (#2)** — a parked train's jittering fixes are held to
  the last snapped point instead of hopping to a neighbouring track.
- **Stopped-train station snap (#5, coarse)** — a parked train that has drifted off
  the network is pinned to its nearest scheduled station (station-centroid
  precision), using `/metadata/stations` coords + each train's route from
  `/live-trains`.

Tier 1's ceiling: it **cannot resolve which of two parallel running tracks** a
*moving* train is on, and station snapping is centroid-level, not platform-level.
Tier 2 removes that ceiling by matching against the real track **identity and
topology**, constrained to the train's **scheduled path**.

## Goal

For each train, constrain map-matching to the ordered sequence of *track
sections* its timetable implies, so:
- a moving train stays on the correct running track through double-track and
  junctions, and
- a stopped train pins to its actual **platform track** (`commercialTrack`), not
  the station centroid.

## Data source (confirmed available)

`infra-api` `raiteet.geojson` features (currently stripped to bare geometry by
`scripts/bake_rails.py`) already carry everything needed:

| field | use |
|-------|-----|
| `tunniste` | stable track OID (graph node id) |
| `geometria` | track centreline (already baked) |
| `paaraide` | is-main-track flag → weight running lines over sidings/loops |
| `kaupallinenNumero` | platform/track number → matches timetable `commercialTrack` |
| `seuraavatRaiteet` | **next tracks** → directed graph edges |
| `viereisetRaiteet` | adjacent/parallel tracks → disambiguation hints |
| `rautatieliikennepaikat` | owning operating point(s) → station ↔ track crosswalk |
| `ratakmvalit` | line number + km chainage → linear referencing / ordering |

Plus, already fetched at runtime:
- `/metadata/stations` — station short code → coordinate (+ `stationUICCode`).
- `/live-trains` — every train's ordered `timeTableRows` (station codes,
  `commercialTrack`, arrival/departure, actualTime).

The missing crosswalk — timetable `stationShortCode` ("HKI") ↔ infra-api
operating-point OID in `rautatieliikennepaikat` — is resolvable via the
`liikennepaikat` collection (operating points carry both a short code and the OID
used by `raiteet`). Verify the join key during step 1.

## Work breakdown

### 1. Re-bake with identity + topology (`scripts/bake_rails.py`)
- Keep per feature: `tunniste`, `paaraide`, `kaupallinenNumero`, `seuraavatRaiteet`,
  `viereisetRaiteet`, `rautatieliikennepaikat`, `ratakmvalit` (+ geometry).
- Additionally bake `liikennepaikat` (operating points) → emit a
  `stationShortCode → [track tunniste]` index and `OID → shortCode` crosswalk.
- Output stays the qCompress container; bump an internal schema version so the
  loader can detect old blobs. Expect a size increase — measure; compress fields
  we don't need to index out of the per-segment payload if it matters.
- Gotchas (see also the gzip memory): infra-api `latest` **307-redirects** to a
  versioned path (`/0.8/…`) — the script's urllib follows it, but any curl probe
  needs `-L`; **gzip is mandatory** on both infra-api and `/metadata/stations`.

### 2. In-app track graph (`TrackService`)
- Parse the richer blob into: segments (with `tunniste`, `paaraide`, station
  membership, platform number) + an adjacency map (`tunniste → seuraavat`).
- Build `stationShortCode → candidate platform tracks` and a spatial index
  (reuse the existing bbox cull; consider a grid for the routing queries).

### 3. Per-train route → track path
- From each train's `timeTableRows`, get the ordered operating points.
- Resolve consecutive station pairs to a track path via a shortest-path search
  over `seuraavatRaiteet` (Dijkstra/A* weighted by chainage length, biased toward
  `paaraide`). Cache per (departureDate, trainNumber); routes are static per run.
- This is the heavy compute — do it off the GUI thread (as geometry parsing
  already is) and memoise.

### 4. Route-constrained matching
- Replace today's "nearest of all segments" with "nearest point on the train's
  *current route window*" (the few track sections around its last known
  chainage), with the heading penalty retained as a tie-breaker.
- Track each train's **chainage position** along its path (1-D), advanced by
  `speed·Δt`, and only re-localise with hysteresis — this gives true continuity
  and lets interpolation follow the actual curve, not a great-circle chord.
- Stopped at a station: snap to the `kaupallinenNumero` track equal to the
  timetable's `commercialTrack` at that operating point (platform-level #5).

### 5. Validation (must be on live data)
- Tier-2 heuristics are inherently visual; they need eyeballing against the live
  map (parallel tracks at Pasila/Tampere, junctions, platforms). Add a debug
  overlay: draw the resolved route path + raw-vs-snapped fix for a selected
  train, and surface `trackOffsetMeters` / chosen `tunniste` in the detail panel.
- Add unit tests for the pure pieces: graph parse, station↔track crosswalk,
  shortest-path on a small fixture network, chainage projection.

## Risks / open questions
- **Route-search cost** for ~hundreds of live trains — mitigate with caching,
  background compute, and only routing the *visible/selected* trains first.
- **Crosswalk completeness** — some operating points (sidings, border points) may
  not map cleanly; fall back to Tier-1 behaviour per-train when unresolved.
- **Blob size** after keeping topology — measure; may warrant a separate
  topology resource from the render geometry.
- **Schedule vs. reality** — a re-routed/diverted train won't match its booked
  path; keep the geometric snap as a fallback when the fix is far from the route.

## Sequencing
Land as its own branch/PR after Tier 1. Suggested commits: (1) re-bake script +
new resource + schema version; (2) graph parse + crosswalk + tests; (3) route
search (off-thread, cached); (4) route-constrained matcher + chainage tracking;
(5) debug overlay + detail-panel diagnostics.
