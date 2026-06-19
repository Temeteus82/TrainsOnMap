# Tier-2 code-review findings (to tackle)

From the multi-angle `/code-review` of the Tier-2 branch
(`feat/track-accuracy-tier2-bake`, PR #21). Ranked most-severe first. One
candidate was refuted (the bake `virallinenSijainti` axis order is correctly
`[E, N]`, verified empirically) and is not listed.

Suggested fix-order cluster: **#1, #2** first (corrupt polylines, untested),
then **#3, #4, #6** (continuity + startup), then the rest.

> **Status — all 15 resolved** on branch `fix/tier2-review-findings`.
> - **#1–#4** RailGraph/TrainListModel: abort-on-gap routing, join-node first-track
>   orientation, chainage carried across Tier-1 fallbacks, windowed projection
>   trusted over global re-acquire. Three new fixture tests (gapped route,
>   reversed-digitisation L-junction, out-and-back parallel limb) — each verified
>   to fail pre-fix.
> - **#5–#8** TrackService: `precomputeRoutes` now stashes the latest set and is
>   re-driven from `geometryReady` and from the finished handler; eviction of
>   departed routes; sentinel negative-cache for unresolvable routes.
> - **#9–#11** UI/model: `selMatch` reset on selection change; empty codes skipped
>   in `routeStations()`; staleness guard hoisted ahead of map-matching.
> - **#12** `bake_rails.py`: `.get("tunniste")` with skip-if-missing.
> - **#13–#15** cleanup: one shared `tm35fin::projectToSegment` helper; Tier-1
>   matcher iterates the graph's plain coords (no per-vertex `QVariant` unbox);
>   dead `ratanumero`/`nodeA`/`nodeB`/`opOid` trimmed. **Deferred:** trimming the
>   blob-level `operatingPoints`/`rautatieliikennepaikat` payload needs a re-bake
>   (`scripts/bake_rails.py` regen against the infra-api) and is left as a
>   follow-up; `loadFromJson` already ignores them.

---

## A. Correctness — fix before relying on Tier-2

### [x] 1. routePath splices non-adjacent tracks across an unroutable middle leg
**Fixed:** `routePath` now aborts to an empty (unresolved) path on any unroutable
leg instead of stitching a chord; the train falls back to Tier-1 for the whole
route. Regression test `gappedRouteResolvesToNoPath` (4-station middle-gap fixture).
`src/RailGraph.cpp:260` (the `arrival >= 0 ? … : sets.at(k-1)` stitch).
- **Problem:** when a middle leg is unroutable, `dijkstra` has reset `arrival`
  to -1, so the next leg starts from the *previous station's* whole track set and
  is appended onto `full` with `j=1`, joining two tracks that aren't graph-adjacent.
- **Failure:** route S0..S3 with an S1→S2 gap → `full` = [S0→S1 path] + [S2→S3
  path minus first]; `buildPolyline` draws a multi-km chord across the gap and all
  chainage past it is inflated → corrupt projection/overlay for that train.
- **Fix idea:** when a leg fails, don't stitch across it — either start a fresh
  sub-polyline (and have projection handle multi-segment routes), or abort the
  route to an "unresolved" state and fall back to Tier-1 for the whole train.
- **Test:** add a fixture with an internal gap (3 stations, middle pair
  disconnected) and assert no spurious long segment / monotonic-ish chainage.

### [x] 2. buildPolyline orients the first track against the wrong reference
**Fixed:** the first track is now oriented by the *nearer* of track[1]'s two
endpoints (the shared join), so the join ends up last regardless of track[1]'s
digitisation direction. Regression test `buildPolylineOrientsReversedLJunction`
(reversed-digitisation L-junction fixture).
`src/RailGraph.cpp:292` (`const QGeoCoordinate ref = next.path.first();`).
- **Problem:** the first track is oriented using track[1]'s raw, not-yet-oriented
  *start* point instead of the shared join node. Track[1]'s digitisation direction
  is arbitrary, so on a non-colinear junction the first track can be reversed the
  wrong way; track[1] then self-orients ~a track-length away, leaving a big gap.
- **Failure:** leading segment runs backwards, chainage origin is at the wrong
  end, polyline zig-zags. The unit fixture passes only because its two tracks are
  co-linear and track[1] starts at the join.
- **Fix idea:** orient using the shared endpoint node (T1.nodeA/nodeB vs
  T2.nodeA/nodeB) so the shared node ends up last; or compare T1's ends to the
  *nearer* of T2's two endpoints.
- **Test:** L-shaped junction fixture with track[1] digitised end→start.

### [x] 3. Chainage is reset to -1 on every Tier-1 fallback fix
`src/TrainListModel.cpp:314` and `:331` (`row.chainage = newChainage;`,
`newChainage` defaults to -1 and is only set when the route match is accepted).
- **Problem:** a single transient Tier-1 fallback (fix briefly off-route, or
  route not yet resolved) wipes the carried chainage, so the next Tier-2 attempt
  does a full-route global search instead of a windowed continuation.
- **Failure:** on out-and-back / self-parallel routes the global re-acquire can
  snap to the wrong limb → marker jump (the failure the window was meant to
  prevent).
- **Fix idea:** keep `row.chainage` unchanged on a Tier-1 fallback (only update it
  when Tier-2 accepts), so continuity survives a transient miss.

### [x] 4. Route projection back-jumps via the window→global fallback
`src/RailGraph.cpp:359` (windowed result discarded when `offset >
kRouteAcceptMeters`, then a whole-route global search runs).
- **Problem:** a valid windowed match just over 120 m is thrown away for a global
  search that can pick a parallel/earlier limb (nearer in 2D, far in chainage).
- **Failure:** fix 130 m off the correct track near a parallel limb (~60 m in 2D,
  ~3 km in chainage) → global returns the parallel limb; `onTrack` (<150 m) is
  true so applyOne accepts it → marker jumps backward ~3 km.
- **Fix idea:** prefer the windowed hit on re-acquire (e.g. only go global if the
  windowed search found *nothing*, or bias the global result toward the prior
  chainage); raise the bar for accepting a global result that is far in chainage
  from `prevChainage`.

---

## B. Startup / scheduling robustness

### [x] 5. First route precompute is lost when categories arrive before the graph
`src/TrackService.cpp:204` (`if (!m_graph || m_graph->isEmpty() || …) return;`).
- **Problem:** at startup `refresh()` fetches /live-trains while geometry is still
  parsing on the worker thread, so `precomputeRoutes` early-returns and there is no
  `geometryReady → precompute` re-trigger.
- **Failure:** Tier-2 stays off (all Tier-1, no route overlay) for up to ~60 s
  until the next categories refresh lands after the graph is ready — longer if that
  refresh fails.
- **Fix idea:** stash the latest route sequences and re-run precompute from
  `geometryReady` (and/or have `precomputeRoutes` cache the request when the graph
  isn't ready yet).

### [x] 6. m_precomputing guard drops a whole batch instead of queuing it
`src/TrackService.cpp:204` (the `… || m_precomputing || …` guard).
- **Problem:** a batch arriving while a precompute is in flight is dropped, not
  queued; it only resolves if it reappears in a later refresh while idle.
- **Failure:** new trains appearing during the (slow first) precompute get no
  Tier-2 polyline until a future refresh coincides with an idle precomputer.
- **Fix idea:** queue pending route sequences and kick off a follow-up precompute
  in the `finished` handler if anything queued up.

### [x] 7. m_routePolys grows unbounded (no eviction)
`src/TrackService.cpp:229` (`m_routePolys.insert(...)` — never pruned).
- **Problem:** every route key ever seen is cached forever; route keys change as
  departureDates roll over, and each polyline holds hundreds of coordinates.
- **Failure:** slow unbounded memory growth over a long/overnight session (unlike
  the model's pruned trains).
- **Fix idea:** evict keys no longer present in the latest route set (or LRU /
  cap), e.g. prune in `precomputeRoutes` against the current `routeSequences`.

### [x] 8. Unresolvable routes are re-Dijkstra'd every refresh (no negative cache)
`src/TrackService.cpp:237` (only inserts when `poly.isValid()`).
- **Problem:** a route that doesn't resolve to a connected path never gets a cache
  entry, so `contains(key)` stays false and full multi-source Dijkstra +
  buildPolyline re-run for it on every 60 s refresh for the life of the run.
- **Fix idea:** cache a sentinel (empty/invalid polyline) for unresolved keys so
  they're skipped next time.

---

## C. UI / diagnostics

### [x] 9. selMatch shows the previous train for up to 750 ms after reselect
`qml/Main.qml:27` (`property var selMatch`) + the poll Timer at `:30`.
- **Problem:** the Timer's `running: trainDetails.hasSelection` stays true across a
  selection change, so `triggeredOnStart` doesn't re-fire; `selMatch` holds the
  old train's data until the next tick.
- **Failure:** the raw-fix ring, raw→snapped connector, and the detail-panel
  diagnostics render the previous train's position/tunniste for ≤750 ms after
  selecting a different train.
- **Fix idea:** reset `selMatch` to `({})` on selection change (e.g. a
  `Connections` on `trainDetails.selectionChanged`) and/or refresh it immediately
  there instead of waiting for the tick.

### [x] 10. routeStations vs precompute key can diverge → overlay silently missing
`src/TrainDetailsService.h:51` (`routeStations()`), vs the key built in
`DigitrafficClient::handleCategories`.
- **Problem:** `buildStops` keeps a stop for a row with an empty stationShortCode,
  while `DigitrafficClient` does `if (code.isEmpty()) continue;`. An empty code
  makes the '|'-joined key differ, so `routePolyline()`'s exact `constFind` misses.
- **Failure:** the resolved route is cached but the overlay never appears for that
  train (also any future reuse of `routeStations` for matching would use a wrong
  key).
- **Fix idea:** centralise route-code extraction (one helper used by both paths),
  or skip empty codes in `routeStations()` too.

### [x] 11. matchOnRoute runs full projection on stale fixes before the timestamp guard
`src/TrainListModel.cpp:248` (matching block runs before the `:306` staleness
`return`).
- **Problem:** out-of-order REST snapshots compute prevChainage/advance + route
  window projection (and platform snap) only to be dropped by the timestamp guard.
- **Failure:** avoidable per-fix CPU fleet-wide on every 60 s resync (correctness
  is fine — negative dt → advance 0, nothing stored).
- **Fix idea:** move the timestamp staleness check ahead of the matching block.

---

## D. Tooling

### [x] 12. bake_rails.py aborts on an op/part missing `tunniste`
`scripts/bake_rails.py:247` (and `op_by_oid`, `op_to_code` dict access).
- **Problem:** unconditional `entry["tunniste"]` / `p["tunniste"]` raises KeyError
  (uncaught here) if any matched feature lacks the key → no blob written.
- **Fix idea:** `.get("tunniste")` with a skip-if-missing guard.

---

## E. Cleanup / altitude

### [x] 13. Nearest-point-on-segment projection is triplicated
`src/RailGraph.cpp:337` (projectOntoRoute `search` lambda) + `nearestOnTrack`
(`:381`) + `src/TrackService.cpp:120` (matchToNetwork); `kMetresPerDegLat`
declared in both files.
- **Cost:** any change to the snap math must be made in three places that will
  silently diverge across Tier-1 / route projection / platform snap.
- **Fix idea:** one `projectPointToSegment(fix, A, B) → {snapped, dist, t}` helper.

### [x] 14. Track geometry stored twice; hot matcher uses the boxed copy
`src/TrackService.cpp:75` (loadNetwork copies each `RailGraph::Track::path` into a
`QVariantList` `Segment::path`) and `:120` (matchToNetwork unboxes a QVariant per
vertex per fix).
- **Cost:** every coordinate resident twice; per-fix QVariant unbox on a hot path.
  The boxing is only needed for the QML render binding in `loadForBounds`.
- **Fix idea:** iterate `m_graph->tracks()` (plain QGeoCoordinate + existing bbox)
  in matchToNetwork; keep the QVariantList only for rendering.

### [x] 15. Baked/parsed-but-unread payload
`src/RailGraph.h:30` (Track::ratanumero), `:34` (nodeA/nodeB), Station::opOid/name;
plus `operatingPoints` and per-track `rautatieliikennepaikat` in the blob.
- **Cost:** dead payload ships in the committed resource + decompresses each
  launch; `ratanumero`'s ratakmvalit parse runs per feature for no consumer;
  `loadFromJson` never reads `operatingPoints`/`rautatieliikennepaikat`. Doc
  comments still imply `seuraavatRaiteet` drives the graph (it's geometry-derived).
- **Fix idea:** trim baked fields to what `loadFromJson` consumes (or wire the
  unread ones in), make nodeA/nodeB load-locals, and update the comments.

---

### Also worth doing alongside
- ~~Add the two missing fixture tests called out in #1 and #2 (gapped route;
  reversed-digitisation L-junction)~~ — **done** (`gappedRouteResolvesToNoPath`,
  `buildPolylineOrientsReversedLJunction`); both verified to fail on the pre-fix code.
- The remaining live on-map eyeballing from the Tier-2 plan
  (`docs/track-accuracy-tier2-plan.md`, step 5) still stands.
