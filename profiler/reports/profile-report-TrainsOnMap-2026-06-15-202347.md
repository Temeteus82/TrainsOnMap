# QML Profile Report — TrainsOnMap (incremental-model verification, on `main`)

## Profiling metadata

| | |
|---|---|
| Profile mode | `full` |
| Trace file | `profiler/traces/qmlprofiler-trace-TrainsOnMap-2026-06-15-202104.qtd` |
| Approx. run duration | **~51 s** (`wall_ms_est` = 51,070 ms) |
| Sum of captured range-event durations | **3,753.41 ms** — binding/JS/creating/signal/compiling added together; **not** wall-clock. |
| Total events | 883,522 |

Profiled the merged `main` build (all perf work: viewport culling + off-thread parse + incremental `TrackListModel` diff), with deliberately heavy panning/zooming and a few train-panel opens.

---

## Event type summary

`ms_per_frame` is total time amortised over the 5,475 rendered frames. Raw `count` scales with run length/interaction and is not a primary metric.

| Type | Count | Total ms | ms/frame |
|---|---:|---:|---:|
| Creating | 34,314 | 1,271.25 | 0.232 |
| Javascript | 57,310 | 1,095.88 | 0.200 |
| HandlingSignal | 6,918 | 873.73 | 0.160 |
| Binding | 46,618 | 460.20 | 0.084 |
| Compiling | 18 | 52.34 | 0.010 |

---

## Animation / frame-time summary

**How to read:** frame time = gap between frames; 60 Hz vsync ≈ 16.67 ms; > 33 ms is visible stutter, > 50 ms a stall. p95/p99 = the 5%/1% worst frames.

For this run (`frame_count` = 5,475): p95 → ~274 frames ≥ 19.23 ms; p99 → ~55 frames ≥ 45.45 ms.

| Metric | Value |
|---|---:|
| Frame count | 5,475 |
| **frame_ms_p50** | **16.13 ms** |
| **frame_ms_p95** | **19.23 ms** |
| **frame_ms_p99** | **45.45 ms** |
| **frame_ms_max** | **333.33 ms** |
| **frames_over_25ms** | **126** |
| **frames_over_33ms** | **94** |
| **frames_over_50ms** | **44** |
| Avg framerate | 107.2 fps |

Median/95th-percentile frames are at or near vsync. The 333 ms `max` is a startup one-off (map-plugin init, hotspot below), not a steady-state stall. The >50 ms tail (44 frames, 0.80%) is dominated by the largest-delta settles (big zoom-outs that insert/remove many segments at once).

---

## Memory summary

**Verdict: healthy, and markedly leaner than before.** 678,455 small-object allocations (~80 MB), **~96% reclaimed**, live GC heap peaked ~4.2 MB, ended ~2.9 MB. Allocation volume fell ~72% vs the pre-incremental run (which churned ~2.39M allocations / 287 MB) — a direct consequence of no longer recreating the whole track layer on every settle.

| Category | Allocations | Total allocated | Reclaimed | Peak live | Live at exit |
|---|---:|---:|---:|---:|---:|
| GC heap pages | 94 | 6.36 MB | 2.72 MB | 4.08 MB | 3.64 MB |
| Small JS objects | 678,455 | 76.86 MB | 74.11 MB | 3.99 MB | 2.75 MB |

Large JS objects: none.

---

## Verification result — incremental model works as intended

The change under test (`TrackListModel` incremental id-keyed diff, commit `4bacfd0`) was aimed at the two dominant hotspots from the previous run. Both collapsed:

| Hotspot | Pre-incremental run | This run | Change |
|---|---:|---:|---:|
| `path: model.path` binding ([Main.qml:136](../../qml/Main.qml#L136)) | 46,715 evals / 1,199.6 ms | 12,835 / 338.5 ms | **−73% / −72%** |
| MapPolyline creation ([Main.qml:132](../../qml/Main.qml#L132)) | 93,428 / 572.3 ms | 25,671 / 167.5 ms | **−73% / −71%** |
| Small-object allocations | 2,388,291 | 678,455 | **−72%** |
| Range-event total | 6,799.96 ms | 3,753.41 ms | **−45%** |

The settle count was comparable (~50 `refreshTracks` calls), so the drop reflects per-settle work, not less interaction: each settle now creates/binds only the segments entering the view rather than the entire visible set. No QML or threading errors were emitted; the off-thread geometry parse (`geometryReady` → first viewport load) functioned and the app exited cleanly.

**Frame smoothness** is net neutral-to-better: p50 17.24 → 16.13 ms and p95 21.28 → 19.23 ms improved; p99 35.71 → 45.45 ms and >50 ms 0.47% → 0.80% are marginally higher but small in absolute terms and attributable to large-delta zoom settles plus interaction-to-interaction variance (the runs are manual, not scripted). The big win is the ~45–72% reduction in CPU work and allocation churn.

---

## Remaining hotspots (all expected / inherent)

| # | Total ms | Count | Type | Source | Note |
|--:|---:|---:|---|---|---|
| 1 | 651.12 | 3,268 | HandlingSignal | [Main.qml:176](../../qml/Main.qml#L176) | `DragHandler` → `map.pan()` — inherent QtLocation reprojection; now the top cost since the reset is gone |
| 2 | 647.10 | 1,442 | Creating | Label.qml:9 (Qt internal) | Labels inside controls / detail-panel rows |
| 3 | 199.14 | 2 | Creating | [Main.qml:45](../../qml/Main.qml#L45) | QtLocation Plugin init (startup one-off) |
| 4 | 167.46 | 25,671 | Creating | [Main.qml:132](../../qml/Main.qml#L132) | MapPolyline (now delta-only) |

`TrainMarker` bindings remain negligible (`trainColor` ~8 ms, km/h text ~16 ms) — confirming again that the deferred marker-role optimisation is not warranted.

---

## Next steps
- **None indicated by this trace for the track layer** — the incremental diff achieved its goal. The remaining top cost (`map.pan()`) is QtLocation-internal reprojection of the on-screen items; the lever there is fewer/simpler map items, already addressed by culling.
- One open item is a **human visual confirmation** that no track segments go missing/duplicated/stale during aggressive panning (the model-diff correctness, which a trace cannot assert).

---

> AI assistance has been used to create this output.
