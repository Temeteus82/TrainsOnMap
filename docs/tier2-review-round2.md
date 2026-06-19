# Tier-2 fix review — round 2 findings

From an extra-high-effort multi-angle `/code-review` of commit `56f64d0`
("resolve all 15 Tier-2 routing code-review findings") on branch
`fix/tier2-review-findings`. 10 finder angles + verification + sweep. The diff
itself verified clean — no crashes, races, or broken call sites. These are the
residual issues, ranked most-severe first.

> **Resolution (2026-06-19, branch `fix/round2-and-followups`).** R1, R2, R3, R5,
> R7, R8 fixed; R4 documented in code; R6 accepted as a documented tradeoff (no
> code change). Build clean (`windows-llvm`); `tst_railgraph` 13/13 (added
> `reacquireRespectsHysteresisBand` for R5). See per-item notes below.

Two candidates were **refuted** and are not listed:
- `buildPolyline` empty-path deref — `loadFromJson` guarantees `path.size() >= 2`.
- An in-flight worker re-inserting a departed route — self-corrects: the finished
  handler's own `kickPrecompute()` re-entry evicts it in the same turn.

One claim was **corrected down in severity**: the `routeStations()` key divergence
affects only the debug overlay, not Tier-2 matching — matching uses `route->codes`
from `m_routeByKey` (`TrainListModel.cpp:276`), the canonical DigitrafficClient
codes, not `routeStations()`.

---

## A. Worth fixing (cheap, real)

### [x] R1. Invalid-coordinate fix wipes the carried route chainage
**Fixed.** `prevChainage` + `newChainage = prevChainage` hoisted out of the
`raw.isValid()` block in `applyOne`, so an invalid fix preserves the carried
chainage. (Both feeds currently pre-filter invalid coords, so this removes a
latent fragility rather than a live bug.)
`src/TrainListModel.cpp:266` (`newChainage = prevChainage;`).
- **Problem:** the carry-forward is inside `if (m_matcher && raw.isValid())`. An
  invalid raw coordinate skips the whole block, so `newChainage` stays at its -1
  default and `row.chainage` is reset to -1 at `:327` (also stores an invalid pos).
- **Failure:** one fix with an invalid coordinate → next valid fix has
  `prevChainage = -1` → `projectOntoRoute` does an unconstrained global search →
  possible re-acquire onto a parallel/earlier limb. The exact continuity loss the
  #3 carry-forward was meant to prevent, reopened for the invalid-fix path.
- **Fix idea:** hoist `prevChainage` and `newChainage = prevChainage` out of the
  `raw.isValid()` block (or skip the row update entirely when `raw` is invalid).

### [x] R2. Eviction runs before the `m_precomputing` early-return
**Fixed.** Eviction pass moved below `if (m_precomputing) return;` in
`kickPrecompute`; a resync arriving mid-precompute no longer rescans the cache —
the finished handler's re-entry runs eviction on completion.
`src/TrackService.cpp:206` (eviction loop) vs `:217` (`if (m_precomputing) return;`).
- **Problem:** every `precomputeRoutes()` call (one per 60 s resync) rebuilds the
  full `live` QSet and rescans all of `m_routePolys` on the GUI thread even when a
  precompute is already in flight and nothing can be launched — work the in-flight
  finished-handler will redo on completion anyway.
- **Cost:** O(cache) wasted main-thread work per resync on a busy fleet. Not a
  correctness bug.
- **Fix idea:** move the eviction pass below the `m_precomputing` early-return, or
  only evict on the path that actually launches a batch.

### [x] R3. Stale comment: `m_all` "(and Tier-1 matcher)"
**Fixed.** Comment now reads "render segments (viewport cull only)".
`src/TrackService.h:93`.
- **Problem:** `matchToNetwork` was rewritten (#14) to scan `m_graph->tracks()`;
  `m_all` is now used only by `loadForBounds` for rendering. The annotation
  "render segments (and Tier-1 matcher)" is now wrong.
- **Cost:** misleads the next maintainer into thinking `m_all` is still on the hot
  match path (and into preserving it for that reason).
- **Fix idea:** drop "(and Tier-1 matcher)" from the comment.

---

## B. Known design tradeoffs (document, not necessarily change)

### [x] R4. Sustained Tier-1 fallback freezes chainage → stale window → global re-acquire
**Documented (no behaviour change).** The carry-forward comment in `applyOne` now
states the transient-only scope and why dead-reckoning is deliberately not done
(it would mis-advance a genuinely diverted train). Global re-acquire on a
sustained off-route stretch is the accepted graceful degradation.
`src/TrainListModel.cpp:266` + `src/RailGraph.cpp` `projectOntoRoute`.
- **Problem:** the #3 carry-forward freezes `row.chainage` across a Tier-1 stretch.
  It helps a single-fix miss, but over several off-route fixes the frozen
  `prevChainage` lags the train; when Tier-2 re-engages the window misses and
  `projectOntoRoute` falls through to the global search.
- **Failure:** ~2 km off-route stretch → on re-entry the window
  `[prevChainage-150, centre+400]` excludes the true chainage → global search →
  on a self-parallel route, the parallel-limb jump #4 aimed to prevent.
- **Fix idea (optional):** dead-reckon the chainage during fallback
  (`newChainage = prevChainage + advance`) instead of freezing — but that has its
  own failure mode on a genuine diversion (advancing chainage the train isn't on).
  At minimum, add a comment acknowledging the transient-only scope.

### [x] R5. `kRouteReacquireMeters = 300` is a magic threshold; (150, 300] hits drop to Tier-1
**Fixed.** Constant now documented as 2× the 150 m snap-accept (a continuity
hysteresis margin, explicitly *not* the on-track gate), and the (150, 300] band
tradeoff is spelled out. New `reacquireRespectsHysteresisBand` test pins both
sides of the 300 m boundary (windowed hit trusted ≤300; global re-acquire >300).
`src/RailGraph.cpp:34` (constant) + `:363` (gate).
- **Problem:** the gate decouples "trust the windowed hit" (≤300) from the 150 m
  snap/onTrack gate. A windowed offset in (150, 300] is returned but rejected by
  `onTrack`, dropping the train to Tier-1 where the old code's global search might
  have found a genuine <150 m on-route hit.
- **Failure:** train on-route but the fix is ~180 m from the nearest in-window
  vertex (sparse polyline) while a global search would find ~40 m just outside the
  window → old stayed Tier-2, new drops to Tier-1.
- **Fix idea:** no test pins the (150, 300] band — add a `projectOntoRoute`
  re-acquire test case, and/or tie the constant to `kSnapAcceptMeters` with a
  documented multiplier instead of a bare 300.

### [x] R6. routePath aborts the whole route on a single unroutable leg
**Accepted as-is (documented tradeoff, no code change).** This is one of the two
options finding #1 explicitly sanctioned, and the abort path is already
thoroughly commented in `routePath` (a chord across the gap would inflate all
downstream chainage). Keeping the routable prefix is a larger change deferred
until a real route is observed losing Tier-2 to a single mid-route gap.
`src/RailGraph.cpp:268`.
- **Problem:** one unroutable consecutive station-pair anywhere now returns `{}`
  for the entire route (previously the bad leg was skipped and the rest kept).
- **Failure:** a long route with a single bad mid-way pair loses Tier-2 for its
  whole length; every train on it falls back to Tier-1 even for the kilometres
  that route cleanly.
- **Note:** this is one of the two options the original finding #1 sanctioned
  (the other being multi-segment polylines). Keeping the routable prefix would
  degrade more gracefully but is a larger change.

---

## C. Edge cases (low value)

### [x] R7. Eviction can drop a still-selected train's route polyline
**Fixed.** `TrackService::pinRoute(QStringList)` keeps the selected train's route
in the cache (and resolves it) even after the train leaves the live fleet;
`kickPrecompute` protects the pinned key from eviction and includes it in the
todo. Wired from `Main.qml` on `selectionChanged` / `routeStationsChanged`
(empty list unpins on deselect).
`src/TrackService.cpp:206`.
- **Problem:** the old cache was append-only; the new eviction removes any key not
  in the current `/live-trains` fleet. A selected train that leaves the next fleet
  refresh (stopped running / filtered) but stays selected loses its cached route.
- **Failure:** the route overlay disappears and matching falls to Tier-1 for a
  selected-but-no-longer-live train, whereas before it persisted.

### [x] R8. routeStations() #10 fix is incomplete (debug overlay only)
**Fixed.** Factored `RailGraph::canonicalRouteCodes` (skip-empty +
dedup-consecutive) and routed both `DigitrafficClient::handleCategories` and
`TrainDetailsService::routeStations` through it, so the precompute key and the
overlay key are now built identically and can't diverge.
`src/TrainDetailsService.h:54`.
- **Problem:** it skips empty codes but does not collapse consecutive duplicates
  the way `DigitrafficClient::handleCategories` does, so an empty `stationShortCode`
  adjacent to a duplicate code still diverges the overlay key.
- **Failure:** timetable rows `[A, (empty), A]` → `routeStations()` = `A|A` vs
  precompute key `A` → `routePolyline()` misses → the debug overlay silently never
  draws for that train. Rare input; **matching is unaffected** (it uses the
  canonical codes).
- **Fix idea:** factor one `canonicalize(skip-empty + dedup-consecutive)` helper
  used by both paths — the fully-robust form of the #10 fix.
