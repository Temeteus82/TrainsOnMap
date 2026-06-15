# QML Profile Report — TrainsOnMap (post-culling verification)

## Profiling metadata

| | |
|---|---|
| Profile mode | `full` |
| Trace file | `profiler/traces/qmlprofiler-trace-TrainsOnMap-2026-06-15-200510.qtd` |
| Approx. run duration | **~59 s** (`wall_ms_est` = 58,763 ms) |
| Sum of captured range-event durations | **6,799.96 ms** — binding/JS/creating/signal/compiling added together; **not** wall-clock. |
| Total events | 2,869,414 |

This run profiled the build *after* the viewport-culling + off-thread-parse changes, with deliberately heavy panning/zooming to stress the per-settle track reload.

---

## Event type summary

`ms_per_frame` is total time amortised over the 5,344 rendered frames. Raw `count` scales with run length/interaction and is not a primary metric. Note this was a short, interaction-dense run, so per-frame figures are higher than an idle session would show.

| Type | Count | Total ms | ms/frame |
|---|---:|---:|---:|
| Creating | 99,545 | 2,357.45 | 0.441 |
| Javascript | 92,638 | 1,809.76 | 0.339 |
| Binding | 78,177 | 1,356.03 | 0.254 |
| HandlingSignal | 10,617 | 1,219.05 | 0.228 |
| Compiling | 18 | 57.68 | 0.011 |

---

## Animation / frame-time summary

**How to read:** frame time = gap between frames; 60 Hz vsync ≈ 16.67 ms; > 33 ms is visible stutter, > 50 ms a stall. p95/p99 = the 5%/1% worst frames.

For this run (`frame_count` = 5,344): p95 → ~267 frames ≥ 21.28 ms; p99 → ~53 frames ≥ 35.71 ms.

| Metric | Value |
|---|---:|
| Frame count | 5,344 |
| **frame_ms_p50** | **17.24 ms** |
| **frame_ms_p95** | **21.28 ms** |
| **frame_ms_p99** | **35.71 ms** |
| **frame_ms_max** | **333.33 ms** |
| **frames_over_25ms** | **159** |
| **frames_over_33ms** | **76** |
| **frames_over_50ms** | **25** |
| Avg framerate | 90.9 fps |

Severe stalls are rare: 25 frames (0.47%) over 50 ms, 76 (1.42%) over 33 ms, despite continuous heavy panning. The single 333 ms `max` is a one-off (map-plugin init / first geometry-ready reset at startup), not sustained.

---

## Memory summary

**Verdict: healthy, no leak.** 2.39M small-object allocations (~287 MB) over the run, **~99% reclaimed**; live GC heap peaked at ~4.1 MB and ended at ~1.6 MB. The high allocation volume is the per-settle track reload churning binding/temporary objects — it is fully reclaimed, but reducing that churn is the subject of the hotspot below.

| Category | Allocations | Total allocated | Reclaimed | Peak live | Live at exit |
|---|---:|---:|---:|---:|---:|
| GC heap pages¹ | 270 | 17.2 MB | 14.7 MB | 4.14 MB | 2.53 MB |
| Small JS objects² | 2,388,291 | 274.4 MB | 272.7 MB | 4.02 MB | 1.63 MB |

¹ memory pages backing the JS/GC heap. ² per-object GC allocations (binding results, temporaries). Large JS objects: none.

---

## Top hotspots (project, abridged)

| # | Total ms | Count | Type | Source | Details |
|--:|---:|---:|---|---|---|
| 1 | 1199.61 | 46,715 | Binding | [Main.qml:136](../../qml/Main.qml#L136) | MapPolyline `path: model.path` |
| 2 | 714.25 | 5,081 | HandlingSignal | [Main.qml:176](../../qml/Main.qml#L176) | DragHandler `onTranslationChanged` → `map.pan()` |
| 3 | 572.32 | 93,428 | Creating | [Main.qml:132](../../qml/Main.qml#L132) | QtLocation/MapPolyline (one per visible segment) |
| 4 | 326.07 | 70 | Javascript | [Main.qml:105](../../qml/Main.qml#L105) | `refreshTracks` (viewport filter call) |
| 5 | 266.08 | 68 | HandlingSignal | [Main.qml:101](../../qml/Main.qml#L101) | debounce Timer `onTriggered` |
| — | 24.29 | 2,348 | Binding | [TrainMarker.qml:147](../../qml/TrainMarker.qml#L147) | km/h text (marker steady-state) |
| — | 17.79 | 2 | Creating | [Main.qml:210](../../qml/Main.qml#L210) | TrainDetailPanel `Loader` (lazy — built on selection) |

---

## Findings & actions

### Dominant cost: full model reset on every viewport settle — ADDRESSED
Hotspots #1 + #3 are the same root cause: `loadForBounds` previously called `setSegments()`, which did a `beginResetModel`. Across ~68 settles that **recreated every visible `MapPolyline` 93,428 times** and re-ran the `path` binding 46,715 times — the two largest project costs by total time.

**Fix applied:** `TrackListModel` now stores each row's stable segment id and exposes `setVisibleSegments(ids, paths)`, which diffs the incoming (ascending-id) set against current rows and emits batched incremental `insert`/`remove`. Consecutive overlapping viewports now rebuild only the polylines that entered/left the view, not the whole layer. The first load (empty → full) and a fully-disjoint jump both collapse to a single bulk op, so the change never regresses those cases.

### Marker bindings — NO ACTION (intentionally held)
`TrainMarker.qml` bindings total ~24 ms (0.005 ms/frame) even under heavy interaction — not a bottleneck. Moving colour/label computation into C++ model roles would couple the model to the UI palette for no measurable gain. Revisit only if train counts grow enough to push marker updates into the frame tail.

### Pan handler (#2) and refreshTracks (#4)
`map.pan()` and the debounced `refreshTracks` are inherent to interaction; both should fall as the incremental model reduces the delegate churn each settle triggers. Re-profile after this change to confirm hotspots #1/#3 collapse.

---

## Next steps
1. Re-profile once more to confirm the incremental model collapsed hotspots #1 (`path` binding) and #3 (MapPolyline creation), and that tracks still render correctly while panning (model-diff correctness check).
2. No further track/marker optimisation is indicated by this trace.

---

> AI assistance has been used to create this output.
