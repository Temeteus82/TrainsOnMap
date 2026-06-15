# QML Profile Report — TrainsOnMap

## Profiling metadata

| | |
|---|---|
| Profile mode | `full` (all features recorded) |
| Trace file | `profiler/traces/qmlprofiler-trace-TrainsOnMap-2026-06-15-165422.qtd` |
| Approx. run duration | **~271 s** (`wall_ms_est` = 271,238 ms, from frame count × avg frame time) |
| Sum of captured range-event durations | **5,069.02 ms** — binding/JS/creating/signal/compiling time added together; **not** wall-clock time (events overlap and run across a 271 s session) |
| Total events | 638,661 |

---

## Event type summary

`ms_per_frame` is the honest per-frame CPU cost (total time amortised over the 18,098 rendered frames). Raw `count` scales with how long the app ran and how much you interacted with it — it is **not** a primary metric; treat it as "how often", not "how expensive".

| Type | Count | Total ms | ms/frame |
|---|---:|---:|---:|
| Creating | 18,178 | 2,152.86 | 0.119 |
| Javascript | 110,707 | 1,256.92 | 0.069 |
| HandlingSignal | 740 | 1,054.97 | 0.058 |
| Binding | 97,163 | 510.28 | 0.028 |
| Compiling | 18 | 93.99 | 0.005 |

Per-frame averages are all well under 1 ms, so on average the app is not CPU-bound in QML. The cost is **bursty** — concentrated in pan/zoom interaction and one-time startup — which the frame-time percentiles below make visible.

---

## Animation / frame-time summary

**How to read the percentiles**
- Frame time = wall-clock gap between successive frames; lower is smoother.
- p50 is the median; p95 / p99 mean 5% / 1% of frames were *worse* (slower) than that value; max is the single worst frame.
- 60 Hz vsync reference ≈ **16.67 ms/frame**. > 33 ms is visible stutter; > 50 ms is a stall.

For this run (`frame_count` = 18,098):
- **p95 = 17.24 ms** → ~905 frames were ≥ 17.24 ms (essentially vsync — not real jank).
- **p99 = 62.5 ms** → ~181 frames were ≥ 62.5 ms (clearly janky).

| Metric | Value |
|---|---:|
| Frame count | 18,098 |
| **frame_ms_p50** | **17.24 ms** |
| **frame_ms_p95** | **17.24 ms** |
| **frame_ms_p99** | **62.5 ms** |
| **frame_ms_max** | **200.0 ms** |
| **frames_over_25ms** | **381** |
| **frames_over_33ms** | **375** |
| **frames_over_50ms** | **299** |
| Avg framerate | 66.7 fps |
| Min framerate | 5 fps |
| Max framerate | 333 fps |

**Reading:** p50 and p95 are *identical* at 17.24 ms — meaning ~95% of frames render at vsync and the app feels smooth most of the time. The damage is entirely in the tail: **375 frames over 33 ms (user-visible jank) and 299 over 50 ms (severe stalls)**, with a worst frame of 200 ms (a 1/5-second freeze → the 5 fps minimum). Out of 18,098 frames that tail is ~2%, and it lines up with map panning and startup. This is a "smooth with periodic stalls during interaction" profile, not a "constantly slow" one.

---

## Memory summary

**Verdict: healthy — no leak signature.** Over the run the QML engine made 361,700 small-object allocations totalling ~37.0 MB and reclaimed **~93%** of those bytes; the live GC heap peaked at only ~4.4 MB and ended at ~4.3 MB. The steady allocation churn is expected for an app that re-evaluates marker bindings on every live position update; none of it accumulates.

`peak_live_bytes` below is the running-sum peak (largest simultaneous live total), not the biggest single allocation.

| Category | Allocations | Total allocated | Reclaimed | Peak live | Live at exit |
|---|---:|---:|---:|---:|---:|
| GC heap pages¹ | 69 | 4.82 MB | 506 KB | 4.39 MB | 4.32 MB |
| Small JS objects² | 361,700 | 36.98 MB | 34.54 MB | 4.30 MB | 2.44 MB |

¹ *GC heap pages* — memory pages the allocator hands to the QML/JS garbage-collected heap.
² *Small JS objects* — per-object GC allocations (the bulk of activity): binding results, JS values, temporary objects from expression evaluation.

*Large JS objects: none (all zero) — no oversized allocations spilled out of the small-item pool.*

---

## Top 30 hotspots

Sorted by total time. Source locations in the project are clickable; Qt-internal (`qrc:/qt-project.org/…`) entries are framework code and left as plain text.

| # | Total ms | Count | Avg ms | ms/frame | Type | Source | Details |
|--:|---:|---:|---:|---:|---|---|---|
| 1 | 1034.23 | 721 | 1.434 | 0.057 | HandlingSignal | [Main.qml:141](../../qml/Main.qml#L141) | DragHandler `onTranslationChanged` → `map.pan()` |
| 2 | 1029.58 | 721 | 1.428 | 0.057 | Javascript | [Main.qml:141](../../qml/Main.qml#L141) | (JS body of the pan handler above) |
| 3 | 573.66 | 2 | 286.831 | 0.032 | Creating | ToolButton.qml:9 | QtQuick.Templates/ToolButton |
| 4 | 573.37 | 2 | 286.684 | 0.032 | Creating | ToolButton.qml:23 | QtQuick.Controls.impl/IconLabel |
| 5 | 245.34 | 1 | 245.338 | 0.014 | Creating | [Main.qml:8](../../qml/Main.qml#L8) | ApplicationWindow (startup) |
| 6 | 244.62 | 1358 | 0.180 | 0.014 | Creating | Label.qml:9 | QtQuick.Templates/Label |
| 7 | 207.09 | 1 | 207.090 | 0.011 | Creating | [Main.qml:30](../../qml/Main.qml#L30) | TrainsOnMap/TrackService (geometry parse) |
| 8 | 144.49 | 4936 | 0.029 | 0.008 | Binding | [Main.qml:101](../../qml/Main.qml#L101) | MapPolyline `path: model.path` |
| 9 | 81.91 | 8758 | 0.009 | 0.005 | Binding | [TrainMarker.qml:147](../../qml/TrainMarker.qml#L147) | km/h `text` binding |
| 10 | 76.10 | 9873 | 0.008 | 0.004 | Creating | [Main.qml:97](../../qml/Main.qml#L97) | QtLocation/MapPolyline (one per track segment) |
| 11 | 71.23 | 10288 | 0.007 | 0.004 | Binding | [TrainMarker.qml:53](../../qml/TrainMarker.qml#L53) | `trainColor` binding |
| 12 | 55.62 | 10288 | 0.005 | 0.003 | Javascript | [TrainMarker.qml:53](../../qml/TrainMarker.qml#L53) | expression for `trainColor` |
| 13 | 51.32 | 2 | 25.661 | 0.003 | Creating | ApplicationWindow.qml:8 | QtQuick.Templates/ApplicationWindow |
| 14 | 51.19 | 2 | 25.594 | 0.003 | Compiling | [Main.qml](../../qml/Main.qml) | compile of Main.qml |
| 15 | 44.46 | 10288 | 0.004 | 0.002 | Binding | [TrainMarker.qml:59](../../qml/TrainMarker.qml#L59) | `stale` binding |
| 16 | 41.89 | 8758 | 0.005 | 0.002 | Binding | [TrainMarker.qml:74](../../qml/TrainMarker.qml#L74) | `coordinate: model.coordinate` |
| 17 | 41.34 | 10288 | 0.004 | 0.002 | Javascript | [TrainMarker.qml:59](../../qml/TrainMarker.qml#L59) | expression for `stale` |
| 18 | 28.00 | 1 | 28.001 | 0.002 | Creating | [Main.qml:19](../../qml/Main.qml#L19) | TrainsOnMap/DigitrafficClient |
| 19 | 20.36 | 10288 | 0.002 | 0.001 | Binding | [TrainMarker.qml:62](../../qml/TrainMarker.qml#L62) | `late` binding |
| 20 | 19.73 | 10288 | 0.002 | 0.001 | Binding | [TrainMarker.qml:63](../../qml/TrainMarker.qml#L63) | `ringColor` binding |
| 21 | 19.39 | 1 | 19.388 | 0.001 | Compiling | [TrainDetailPanel.qml](../../qml/TrainDetailPanel.qml) | compile of TrainDetailPanel.qml |
| 22 | 18.05 | 1663 | 0.011 | 0.001 | Binding | [TrainMarker.qml:54](../../qml/TrainMarker.qml#L54) | `badgeLabel` binding |
| 23 | 17.73 | 10259 | 0.002 | 0.001 | Javascript | [TrainMarker.qml:62](../../qml/TrainMarker.qml#L62) | expression for `late` |
| 24 | 16.23 | 8758 | 0.002 | 0.001 | Javascript | [TrainMarker.qml:74](../../qml/TrainMarker.qml#L74) | expression for `coordinate` |
| 25 | 16.00 | 2 | 8.001 | 0.001 | Creating | [Main.qml:45](../../qml/Main.qml#L45) | QtLocation/Plugin |
| 26 | 14.79 | 8755 | 0.002 | 0.001 | Javascript | [TrainMarker.qml:147](../../qml/TrainMarker.qml#L147) | expression for km/h `text` |
| 27 | 12.82 | 8758 | 0.001 | 0.001 | Binding | [TrainMarker.qml:116](../../qml/TrainMarker.qml#L116) | dot `rotation: model.bearing` |
| 28 | 12.24 | 266 | 0.046 | 0.001 | Creating | DefaultItemDelegate.qml:10 | QtQuick.Templates/ItemDelegate |
| 29 | 12.09 | 266 | 0.045 | 0.001 | Creating | [TrainDetailPanel.qml:83](../../qml/TrainDetailPanel.qml#L83) | QtQuick.Layouts/RowLayout |
| 30 | 10.89 | 134 | 0.081 | 0.001 | Creating | [TrainDetailPanel.qml:79](../../qml/TrainDetailPanel.qml#L79) | ItemDelegate |

---

## Detailed analysis — top 5 project hotspots

### 1. [Main.qml:141](../../qml/Main.qml#L141) — pan handler (HandlingSignal + Javascript, 1034 + 1030 ms)

```qml
DragHandler {
    id: drag
    target: null
    onTranslationChanged: (delta) => map.pan(-delta.x, -delta.y)   // line 141
}
```

**What it does:** every drag event during a pan calls `Map.pan()`. It fired 721 times for ~1034 ms of signal-handling time — by far the largest project cost, and it is the source of the frame-time tail (the 375 frames > 33 ms).

**Why it is expensive:** `map.pan()` itself is cheap, but it shifts the viewport, which forces the `Map` to re-transform and re-render **every** map item currently attached to it. The track layer alone holds **~9,873 `MapPolyline` items** (see hotspot #10) plus all live train markers — so each pan step reprojects ~10k geometries. That work lands on the render thread and produces the 50–200 ms stall frames. The QML-measured 1.43 ms/handler undercounts it because the heavy reprojection happens downstream in the scene graph.

**Suggested fix:** cut the number of live map items so each pan transforms far fewer geometries. The codebase already has the mechanism — `TrackService::loadForBounds(west, south, east, north)` filters segments to a viewport — but the QML never calls it (see hotspot #7). Wire pan/zoom end to a **debounced** `loadForBounds()` over `map.visibleRegion`'s bounding box (e.g. a 200–300 ms `Timer` restarted on `onCenterChanged`/`onZoomLevelChanged`, firing `loadForBounds` once the gesture settles). That replaces ~9,873 always-present polylines with only those in view, directly shrinking the per-pan cost.

### 2. [Main.qml:8](../../qml/Main.qml#L8) — ApplicationWindow creation (Creating, 245 ms, ×1)

```qml
ApplicationWindow {
    id: win
    visible: true
    ...
```

**What it does:** one-time construction of the root window and its entire child tree (map, both `MapItemView`s, overlay panels). Count = 1 — this is pure startup cost, not per-frame.

**Why it is "expensive":** it is the umbrella timing for building everything declared inside the window during startup, so it absorbs the cost of children created eagerly at load. It does not affect steady-state interaction.

**Suggested fix:** mostly leave it — 245 ms one-time is acceptable. If startup latency matters, defer non-essential overlay subtrees (e.g. `TrainDetailPanel`, which is only shown on selection) behind a `Loader` with `active: trainDetails.hasSelection` so their construction isn't paid up front. This also trims the `TrainDetailPanel` compile/create entries (hotspots #21, #29, #30).

### 3. [Main.qml:30](../../qml/Main.qml#L30) — TrackService construction (Creating, 207 ms, ×1)

```qml
TrackService {
    id: trackService
}
```

**What it does:** instantiating `TrackService` runs its C++ constructor, which calls `loadGeometry()` — decompress + parse the embedded GeoJSON and project the **entire national rail network** to WGS84. This is a single 207 ms synchronous block on the GUI thread at startup.

**Why it is expensive:** it parses thousands of features and runs the Transverse-Mercator inverse on every coordinate, all before the first frame. Count = 1, so it is a one-time startup stall, but it blocks the UI thread while it runs.

**Suggested fix:** if 207 ms of startup blocking is undesirable, move `loadGeometry()` off the constructor and onto a worker (e.g. `QtConcurrent::run`) that posts the finished `m_all` back to the GUI thread, or parse lazily on the first `load()`/`loadForBounds()` call. Combined with fix #1 (viewport culling), you would also only need to *project* segments as they enter view. Note this is the same one-time work the offline-baking design intends to pay once — the only question is whether it blocks the first frame.

### 4. [Main.qml:101](../../qml/Main.qml#L101) — MapPolyline `path` binding (Binding, 144 ms, ×4936) + [Main.qml:97](../../qml/Main.qml#L97) creation (×9873)

```qml
MapItemView {
    model: trackService.model
    delegate: MapPolyline {
        required property var model
        line.width: 2.2
        line.color: "#8c95a0"
        path: model.path            // line 101
    }
}
```

**What it does:** the track layer instantiates one `MapPolyline` per segment and binds its `path` to the model role. Creation fired **9,873** times (hotspot #10) and the `path` binding 4,936 times.

**Why it is expensive:** this is the root cause behind hotspot #1. Loading the whole network means ~9,873 live polyline delegates, each a real scene-graph node that must be reprojected on every pan/zoom. The `Component.onCompleted: { … trackService.load() }` at [Main.qml:92](../../qml/Main.qml#L92) deliberately loads **everything** ("there's no per-viewport fetch to debounce") — which is exactly what inflates the item count.

**Suggested fix:** same as #1 — drive the track model from `loadForBounds()` over the current viewport instead of `load()`, so the `MapItemView` only ever materialises the polylines actually on screen. The delegate itself is already lean (static width/colour, only `path` bound); the problem is *how many* of them exist, not the delegate's cost.

### 5. [TrainMarker.qml:147](../../qml/TrainMarker.qml#L147) — km/h text binding (Binding, 82 ms, ×8758)

```qml
Text {
    visible: marker.model.speed > 0
    text: Math.round(marker.model.speed) + " km/h"     // line 147
    ...
}
```

**What it does:** each train marker shows a live km/h sub-label; its `text` re-evaluates whenever that train's `speed` role changes (every MQTT/REST position update). 8,758 evaluations across the run. The sibling marker bindings — `trainColor` (#11/#12), `stale` (#15/#17), `coordinate` (#16), `late`/`ringColor` (#19/#20), `bearing` rotation (#27) — re-evaluate on the same cadence and together form the steady-state binding load.

**Why it is expensive (relatively):** individually trivial (0.009 ms each), but every live position update re-runs the *whole* cluster of bindings on a marker, and there are hundreds of markers. It is modest now (~82 ms total, 0.005 ms/frame) and not a current bottleneck — listed because it is the dominant *steady-state* (non-interaction) cost and will scale with train count.

**Suggested fix:** no action needed today. If marker churn ever shows up in the frame tail, the lever is to reduce per-update binding re-evaluation — e.g. compute the colour/label/ring once in C++ (the model already owns the data) and expose them as ready-made roles, so QML binds a value instead of re-running `colorFor()`/`labelFor()` switches on every tick. Keep this in reserve, not a priority.

---

## Next steps

In priority order:

1. **Viewport-cull the track layer (fixes #1 + #4).** Replace the startup `trackService.load()` ([Main.qml:92](../../qml/Main.qml#L92)) with a **debounced** `trackService.loadForBounds(...)` driven by `map.onCenterChanged`/`onZoomLevelChanged` (200–300 ms settle timer). This is the single highest-impact change: it attacks the ~9,873-item count that makes every `map.pan()` (the #1 hotspot, and the frame-time tail) expensive. The C++ `loadForBounds()` already exists and is currently unused for this purpose.
2. **Make `loadForBounds` resets cheap enough for repeated calls.** Once #1 is in, `setSegments()` will be called on each settled viewport change. With far fewer items per view its `beginResetModel` is fine; if you later want to avoid the full-layer rebuild, switch `TrackListModel` to incremental insert/remove. (This is the open **I-002** question from the earlier code review — the profile shows the reset is **not** currently per-frame because nothing calls `loadForBounds` yet, so the rewrite is *not* needed today; revisit only after wiring #1.)
3. **Optional: unblock startup (fix #3).** Move `TrackService::loadGeometry()` off the constructor/GUI thread (worker thread or lazy parse) to remove the 207 ms startup stall.
4. **Optional: lazy-load the detail panel (fix #2).** Wrap `TrainDetailPanel` in a `Loader` gated on `trainDetails.hasSelection` to defer its construction/compile off the startup path.
5. **Hold: marker binding cost (fix #5).** Not a current bottleneck; only revisit if train counts grow enough to push marker updates into the frame tail.

The actionable hotspots cluster in **two files** — [Main.qml](../../qml/Main.qml) and [TrainMarker.qml](../../qml/TrainMarker.qml). For a broader structural pass on those specific files, consider running `qt-qml-review` on them. Re-running this profiler after applying step 1 will show whether the frame-time tail (375 frames > 33 ms) shrinks.

---

> AI assistance has been used to create this output.
