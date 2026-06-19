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
| `seuraavatRaiteet` | ~~**next tracks** → directed graph edges~~ — **nearly empty in live data** (27/4936 features, 38 refs). Baked, but unusable as the routing graph; see step 3. |
| `viereisetRaiteet` | adjacent/parallel tracks → disambiguation hints (rich: 2354/4936 features, 23 k refs) |
| `rautatieliikennepaikat` | owning operating point(s) → station ↔ track crosswalk (4928/4936 features) |
| `ratakmvalit` | line number + km chainage → linear referencing / ordering (**100 %** populated) |

Plus, already fetched at runtime:
- `/metadata/stations` — station short code → coordinate (+ `stationUICCode`).
- `/live-trains` — every train's ordered `timeTableRows` (station codes,
  `commercialTrack`, arrival/departure, actualTime).

The missing crosswalk — timetable `stationShortCode` ("HKI") ↔ infra-api
operating-point OID — was **verified and resolved during step 1** (against live
data, 2026-06). The join key is **`uicKoodi` over the *union* of
`rautatieliikennepaikat` (operating points) and `liikennepaikanosat`
(operating-point *parts*)**, matched to `/metadata/stations.stationUICCode`.
Findings that shaped this:
- infra `lyhenne` ("Hel", "Psl") is **not** the timetable code ("HKI", "PSL").
- `uicKoodi` is **null on many major operating points** (Helsinki, Tampere) and
  its numbering there differs from `/metadata/stations`.
- The finer **`liikennepaikanosat`** layer carries `uicKoodi` matching the
  timetable (Pasila uic=10). Pasila is *not* its own operating point — its tracks
  sit under the Helsinki op — so the parts layer is what makes hubs resolvable.
- A part may have **no `raiteet` of its own** (e.g. Helsinki asema); fall back to
  the parent op's tracks via the part's `liikennepaikka` link.

Result: **557/563 stations resolved (214/215 with passenger traffic; only TVE
missing)** — 549 by UIC, 8 by nearest-point geo fallback (≤1500 m), the rest
name-normalised. The 6 unresolved are freight/junction points that don't appear
as passenger stops; per-train Tier-1 fallback covers them.

## Work breakdown

### 1. Re-bake with identity + topology (`scripts/bake_rails.py`) — ✅ DONE
- ✅ Keeps per feature: `tunniste`, `paaraide`, `kaupallinenNumero`,
  `seuraavatRaiteet`, `viereisetRaiteet`, `rautatieliikennepaikat`, `ratakmvalit`
  (+ geometry; the property-form `geometria` is dropped as a dup of `geometry`).
- ✅ Bakes the station crosswalk from `rautatieliikennepaikat` **and**
  `liikennepaikanosat`: emits `stations` (`stationShortCode → {uic, opOid, name,
  match, distM, tracks:[tunniste]}`) and `operatingPoints` (`OID → shortCode`).
  Member-track lists are intersected with the baked track set and fall back to the
  parent op when a part lists none (see the crosswalk notes above).
- ✅ Stays the qCompress container; adds top-level `schemaVersion: 2` + `bakedAt`.
  The current geometry-only loader ignores the new fields, so the app still
  builds and runs on the v2 blob (verified with the `windows-llvm` preset).
- **Size:** 4936 tracks, 557 stations → **2.6 MB** compressed (8.7 MB raw), up
  from 2.38 MB — **+9 %** for all the topology + crosswalk. No need yet to intern
  OIDs or split topology into a separate resource; revisit only if step 2/3 add
  more per-track payload.
- Gotchas confirmed: infra-api `latest` **307-redirects** to a versioned,
  build-numbered path (`/0.8/<build>/…`) — urllib follows it, a curl probe needs
  `-L`; **gzip is mandatory** on both infra-api and `/metadata/stations`. The
  collection list lives behind Springfox at `/infra-api/v2/api-docs?group=0.8`.

### 2. In-app track graph (`RailGraph` + `TrackService`) — ✅ DONE
- ✅ New pure `RailGraph` (QtCore + QtPositioning only, so it's unit-testable)
  parses the v2 blob into tracks (`tunniste`, `paaraide`, `kaupallinenNumero`,
  `ratanumero`, projected polyline) + the station crosswalk. `TrackService` owns
  it (built on the worker thread) and derives the render segments from it, so the
  rendering / Tier-1 path is unchanged.
- ✅ Routing adjacency is **reconstructed from geometry** (not `seuraavat`): tracks
  sharing a 10 m-quantised endpoint node are connected, with `viereisetRaiteet`
  adding parallel edges. Verified to span the network (HKI reaches all the hubs).
- ✅ `stationShortCode → member tracks` resolved from the baked crosswalk; the
  existing bbox cull is retained for rendering. (No separate grid — route queries
  run on the precomputed polylines, not a spatial search.)

### 3. Per-train route → track path — ✅ DONE
- From each train's `timeTableRows`, the ordered station codes are collected in
  `DigitrafficClient` and handed to `TrackService::precomputeRoutes`.
- ⚠️ **`seuraavatRaiteet` is not a usable graph** (38 edges nationally), so
  connectivity comes from the geometry endpoint graph + `viereisetRaiteet` (above).
  `RailGraph::routePath` runs multi-source/target **Dijkstra** per consecutive
  station pair (edge weight = track length, ×1.4 for sidings to bias `paaraide`),
  stitching the legs into one ordered track path.
- ✅ Off the GUI thread + memoised: `precomputeRoutes` dedupes identical routes by
  key and resolves polylines in a `QtConcurrent` task, swapping the cache in on
  the GUI thread; a 60 s refresh only computes genuinely new routes.

### 4. Route-constrained matching — ✅ DONE
- ✅ `RailGraph::projectOntoRoute` snaps a fix to the nearest point on the train's
  resolved route polyline, **windowed** around its last chainage + `speed·Δt`
  (hysteresis: a worse-than-accept windowed hit forces a global re-acquire). The
  result carries the 1-D `chainage`, carried across fixes in the model `Row`.
- ✅ Stopped at a station → `platformSnap` to the member track whose
  `kaupallinenNumero` equals the timetable `commercialTrack` (platform-level #5).
- ✅ `TrainListModel::applyOne` calls `matchOnRoute` when a route is available and
  falls back to the Tier-1 nearest+heading matcher otherwise (or when the fix is
  off the booked route — diversion), keeping the stopped-train continuity (#2).

### 5. Validation — ✅ DONE (unit tests + overlay); live eyeballing still advised
- ✅ `tests/tst_railgraph.cpp` (QtTest, wired into CTest) covers the pure pieces:
  v2-blob rejection, fixture parse, endpoint-graph route stitching, chainage
  projection, platform snapping, route-key ordering, **plus a smoke test over the
  real baked blob** (HKI→PSL→TPE resolves; an on-route point projects back <1 m).
  All 9 pass.
- ✅ Debug overlay (Main.qml): the selected train's resolved route polyline
  (accent), a raw→snapped connector, and a hollow ring at the raw fix; the detail
  panel shows `on route / nearest track · N m off · <tunniste>`.
- ⏳ Still worth a human pass on the live map (parallel tracks at Pasila/Tampere,
  junctions, platforms) — those heuristics are inherently visual and can't be
  fully judged from unit tests. The app builds + runs cleanly with live data.

## Risks / open questions
- **Route-search cost** for ~hundreds of live trains — mitigate with caching,
  background compute, and only routing the *visible/selected* trains first.
- **Crosswalk completeness** — measured: 557/563 stations resolve (only 6
  freight/junction points unmatched, none passenger). Fall back to Tier-1 per
  train when unresolved.
- **Blob size** after keeping topology — measured: **2.6 MB (+9 %)**; no action
  needed for now. A separate topology resource / OID interning is only worth it
  if later steps add more per-track payload.
- **Schedule vs. reality** — a re-routed/diverted train won't match its booked
  path; keep the geometric snap as a fallback when the fix is far from the route.

## Sequencing
Land as its own branch/PR after Tier 1. Suggested commits: (1) re-bake script +
new resource + schema version; (2) graph parse + crosswalk + tests; (3) route
search (off-thread, cached); (4) route-constrained matcher + chainage tracking;
(5) debug overlay + detail-panel diagnostics.
