# QML Profile Report — TrainsOnMap

## Header

| Field | Value |
|---|---|
| Profile mode | `full` (all features recorded) |
| Trace file | `profiler/traces/qmlprofiler-trace-TrainsOnMap-2026-06-22-171010.qtd` (220 MB) |
| Run duration (wall-clock, est.) | **~155.8 s** (≈ 2 min 36 s), derived from frame count × average frame interval |
| Sum of captured range-event durations | **8383.31 ms** — this is the total of all binding / JavaScript / signal / creating / compiling event durations added together; range events **nest and overlap** (a signal handler's body is also counted as a JavaScript event, a binding's expression likewise), so this figure **overcounts** and is **not** wall-clock time |
| Total events captured | **3,145,022** |

A note on counts throughout this report: a raw `count` scales with how long the
app ran and how much it was exercised (here: heavy panning, several detail-panel
opens, ~2.5 min of live data refreshing). Treat `count` as "how often this ran",
not as a severity score. The honest per-frame CPU metric is **`ms_per_frame`**.

## Event type summary

| Type | Count | Total ms | ms/frame |
|---|---:|---:|---:|
| Javascript | 371,867 | 3210.39 | 0.262 |
| Binding | 352,999 | 2060.66 | 0.168 |
| HandlingSignal | 10,887 | 1745.55 | 0.142 |
| Creating | 93,373 | 1307.18 | 0.107 |
| Compiling | 22 | 59.53 | 0.005 |

The headline per-frame cost is the `ms/frame` column, not `count`. Even summed,
these categories total ≈ 0.68 ms/frame of QML-engine work — comfortably inside a
16.67 ms (60 Hz) budget. The performance story here is therefore **not** a steady
per-frame tax; it is **bursts** — specific interactions (panning, viewport track
reloads, dense marker glides) that concentrate work into individual frames and
produce the jank visible in the frame-time section below.

## Animation / frame-time summary

**How to read the percentiles**
- Frame time = wall-clock gap between successive rendered frames; lower is smoother.
- p50 is the median; p95 / p99 mean 5 % / 1 % of frames were *worse* than that value; max is the single worst frame.
- 60 Hz vsync reference ≈ **16.67 ms/frame**. > 33 ms is user-visible stutter; > 50 ms is a stall.

Translating this run's percentiles into concrete frame counts (frame_count = 12,254):
- **p95 = 18.18 ms → ~613 frames** were ≥ 18.18 ms.
- **p99 = 35.71 ms → ~123 frames** were ≥ 35.71 ms.

| Metric | Value |
|---|---:|
| Frame count | 12,254 |
| **frame_ms_p50** | **17.24 ms** |
| **frame_ms_p95** | **18.18 ms** |
| **frame_ms_p99** | **35.71 ms** |
| **frame_ms_max** | **333.33 ms** |
| **frames_over_25ms** | **367** |
| **frames_over_33ms** | **272** |
| **frames_over_50ms** | **37** |
| avg_framerate | 78.7 |
| min_framerate | 3 |
| max_framerate | 1000 |

Reading: the median frame sits at ~17 ms (essentially 60 Hz vsync — smooth most
of the time). The tail is where the trouble is: **272 frames exceeded 33 ms**
(user-visible jank) and **37 frames exceeded 50 ms** (severe stalls), with a worst
frame of **333 ms**. These are bursts, not a constant drag — they line up with the
hotspots below: viewport track reloads on pan-settle (Main.qml track layer) and
the per-animation-step distance check in TrainMarker.

## Memory summary

**Verdict: healthy — no leak, GC keeps up. The notable signal is allocation
*churn*, not retention.**

Over the run, QML allocated **≈ 242.8 MB** across **2,139,490 allocations** and
reclaimed **≈ 97 %** of it (combined small + large items: 235.5 MB freed of
242.8 MB). Peak live GC heap was only **~10.7 MB** and live-at-exit **~7.3 MB**,
so nothing is accumulating. But 2.1 M allocations in ~156 s (≈ 13,700/s) is a high
turnover rate, and it is dominated by short-lived small JS objects — the by-product
of the high-frequency binding re-evaluations and the destroy/recreate cycle of map
delegates discussed below. Cutting that churn (hotspots #2, #3, #5) reduces GC
pressure, which is itself a contributor to the frame-time tail.

`peak_live_bytes` below is the running-sum peak (largest *live* total at any
instant), not the largest single allocation.

| Category | Allocations | Total allocated | Reclaimed | Peak live | Live at exit |
|---|---:|---:|---:|---:|---:|
| Small JS objects | 2,139,475 | 239.8 MB | 232.5 MB | 10.6 MB | 7.3 MB |
| GC heap pages | 403 | 25.4 MB | 18.0 MB | 10.7 MB | 7.5 MB |
| Large JS objects | 15 | 3.0 MB | 3.0 MB | 768 KB | 0 B |

Legend:
- **Small JS objects** — per-object GC allocations (the bulk of all events): QML value types, JS closures, short-lived arrays/objects from binding and signal evaluation.
- **GC heap pages** — heap pages the allocator grabs/releases from the OS to back the above.
- **Large JS objects** — allocations too big for the small-item pool (here: fully reclaimed, none live at exit).

*(No pixmap-cache events were captured in this trace, so there is no image-loading section.)*

## Top 30 hotspots

Sorted by `total_ms`. Range events nest, so the same source line can appear once
as a `HandlingSignal`/`Binding` (the outer event) and once as `Javascript` (its
inner expression) — their durations overlap rather than add.

| # | Total ms | Count | Avg ms | ms/frame | Type | Source | Details |
|--:|--:|--:|--:|--:|---|---|---|
| 1 | 1021.95 | 3,366 | 0.304 | 0.083 | HandlingSignal | [Main.qml:340](../../qml/Main.qml#L340) | (DragHandler onTranslationChanged) |
| 2 | 1010.90 | 3,366 | 0.300 | 0.082 | Javascript | [Main.qml:340](../../qml/Main.qml#L340) | |
| 3 | 741.84 | 135,616 | 0.005 | 0.061 | Binding | [TrainMarker.qml:126](../../qml/TrainMarker.qml#L126) | (Behavior `enabled`) |
| 4 | 697.29 | 26,672 | 0.026 | 0.057 | Binding | [Main.qml:246](../../qml/Main.qml#L246) | (MapPolyline `path`) |
| 5 | 637.13 | 135,616 | 0.005 | 0.052 | Javascript | [TrainMarker.qml:126](../../qml/TrainMarker.qml#L126) | expression for enabled |
| 6 | 487.59 | 84 | 5.805 | 0.040 | Javascript | [Main.qml:209](../../qml/Main.qml#L209) | refreshTracks |
| 7 | 483.01 | 82 | 5.890 | 0.039 | HandlingSignal | [Main.qml:205](../../qml/Main.qml#L205) | (trackReloadTimer onTriggered) |
| 8 | 482.90 | 82 | 5.889 | 0.039 | Javascript | [Main.qml:205](../../qml/Main.qml#L205) | expression for onTriggered |
| 9 | 342.89 | 53,344 | 0.006 | 0.028 | Creating | [Main.qml:242](../../qml/Main.qml#L242) | QtLocation/MapPolyline |
| 10 | 302.39 | 10,118 | 0.030 | 0.025 | Creating | Label.qml:9 *(Qt internal)* | QtQuick.Templates/Label |
| 11 | 114.59 | 41 | 2.795 | 0.009 | Binding | [Main.qml:265](../../qml/Main.qml#L265) | (routeOverlay `path`) |
| 12 | 110.38 | 2 | 55.192 | 0.009 | Creating | ApplicationWindow.qml:8 *(Qt internal)* | QtQuick.Templates/ApplicationWindow |
| 13 | 83.24 | 41 | 2.030 | 0.007 | Javascript | [Main.qml:265](../../qml/Main.qml#L265) | expression for path |
| 14 | 66.90 | 1,634 | 0.041 | 0.005 | Creating | [TrainDetailPanel.qml:380](../../qml/TrainDetailPanel.qml#L380) | QtQuick.Layouts/RowLayout |
| 15 | 66.06 | 12 | 5.505 | 0.005 | HandlingSignal | [TrainMarker.qml:279](../../qml/TrainMarker.qml#L279) | (marker onClicked) |
| 16 | 65.95 | 12 | 5.496 | 0.005 | Javascript | [TrainMarker.qml:279](../../qml/TrainMarker.qml#L279) | expression for onClicked |
| 17 | 65.79 | 12 | 5.482 | 0.005 | HandlingSignal | [Main.qml:309](../../qml/Main.qml#L309) | (TrainMarker onClicked → show) |
| 18 | 65.77 | 12 | 5.481 | 0.005 | Javascript | [Main.qml:309](../../qml/Main.qml#L309) | |
| 19 | 64.85 | 1,634 | 0.040 | 0.005 | Creating | DefaultItemDelegate.qml:10 *(Qt internal)* | QtQuick.Templates/ItemDelegate |
| 20 | 61.85 | 821 | 0.075 | 0.005 | Creating | [TrainDetailPanel.qml:347](../../qml/TrainDetailPanel.qml#L347) | ItemDelegate |
| 21 | 54.28 | 5,877 | 0.009 | 0.004 | Binding | [TrainMarker.qml:109](../../qml/TrainMarker.qml#L109) | (`coordinate: model.coordinate`) |
| 22 | 53.32 | 5,877 | 0.009 | 0.004 | Binding | [TrainMarker.qml:258](../../qml/TrainMarker.qml#L258) | (speed text) |
| 23 | 50.33 | 26,672 | 0.002 | 0.004 | Javascript | [Main.qml:246](../../qml/Main.qml#L246) | expression for path |
| 24 | 38.37 | 2 | 19.186 | 0.003 | Creating | [Main.qml:108](../../qml/Main.qml#L108) | QtQuick/Loader |
| 25 | 36.80 | 2 | 18.400 | 0.003 | Creating | [Main.qml:133](../../qml/Main.qml#L133) | QtLocation/Plugin |
| 26 | 36.45 | 2 | 18.227 | 0.003 | Compiling | [Main.qml](../../qml/Main.qml) | qrc:/qt/qml/TrainsOnMap/qml/Main.qml |
| 27 | 36.04 | 26,672 | 0.001 | 0.003 | Binding | [Main.qml:245](../../qml/Main.qml#L245) | (`line.color`) |
| 28 | 33.82 | 1,634 | 0.021 | 0.003 | Creating | [TrainDetailPanel.qml:413](../../qml/TrainDetailPanel.qml#L413) | QtQuick.Layouts/ColumnLayout |
| 29 | 31.13 | 1,634 | 0.019 | 0.003 | Creating | [TrainDetailPanel.qml:390](../../qml/TrainDetailPanel.qml#L390) | QtQuick.Layouts/ColumnLayout |
| 30 | 28.37 | 3,563 | 0.008 | 0.002 | HandlingSignal | [Main.qml:200](../../qml/Main.qml#L200) | (onVisibleRegionChanged) |

## Detailed analysis — top 5 project hotspots

### 1. [Main.qml:340](../../qml/Main.qml#L340) — DragHandler pan (HandlingSignal + JavaScript, ~1022 ms, 3,366 calls)

```qml
DragHandler {
    id: drag
    target: null
    onTranslationChanged: (delta) => map.pan(-delta.x, -delta.y)
}
```

**What it does.** Every drag-translation delta during a pan gesture scrolls the
`Map` by the pixel delta. Entries #1 (the signal handler) and #2 (its JS body) are
the *same* code measured at two nesting levels — the real wall cost is ~1022 ms,
not the sum.

**Why it shows up #1.** It is #1 by `total_ms` purely because the session involved
a lot of panning (3,366 fires). Its per-call cost is small (0.30 ms) and its
**per-frame cost is only 0.083 ms** — negligible. The 0.30 ms/call is dominated by
the *native* `Map.pan()` reprojecting visible map content, not by the QML/JS in the
handler, which is already minimal. Each pan also fires `onVisibleRegionChanged`
(line 200, entry #30) which merely `restart()`s a debounce timer — cheap and
correct.

**Suggested fix.** No change to this handler is warranted — it is already minimal
and correct (`delta` is the per-event increment, the right thing to feed `pan()`).
The lever for smoother panning is **what each pan triggers downstream** (the
viewport track reload — hotspots #3–#5), not the handler itself. Flag it here only
so it isn't mistaken for a code smell: a high `total_ms` with a tiny `ms/frame` is
"the user did this a lot", not "this is slow".

### 2. [TrainMarker.qml:126](../../qml/TrainMarker.qml#L126) — Behavior `enabled` re-evaluated every animation step (Binding + JS, ~742 ms, **135,616 evaluations**)

```qml
readonly property real maxGlideMeters: 4000
Behavior on coordinate {
    enabled: !Theme.reducedMotion
             && marker.coordinate.distanceTo(marker.model.coordinate) < marker.maxGlideMeters
    CoordinateAnimation {
        duration: 1000
        easing.type: Easing.Linear
    }
}
```

**What it does.** The `Behavior` decides, per coordinate change, whether to glide
(animate) or snap. The `enabled` guard disables the glide when the jump exceeds
`maxGlideMeters` (a GPS re-acquire), so the dot doesn't drag across open terrain.

**Why it is expensive — this is the clearest win in the trace.** The `enabled`
expression references **`marker.coordinate`** — the very property the
`CoordinateAnimation` is mutating. So while a marker glides (a 1000 ms animation),
`marker.coordinate` changes every frame, which re-triggers the `enabled` binding,
which recomputes `distanceTo()` — a great-circle (trig) calculation — **on every
animation step, for every animating marker.** That is why this single expression
was evaluated **135,616 times** (≈ 23× the 5,877 actual position fixes at
[line 109](../../qml/TrainMarker.qml#L109)). It is pure overhead: the glide-vs-snap
decision only depends on the distance between the *previous* fix and the *incoming*
fix, which is known **once**, when a new `model.coordinate` arrives.

**Suggested fix.** Break the dependency on the live animated `coordinate`. Compute
the jump distance only when `model.coordinate` changes and cache the boolean:

```qml
property bool glideEnabled: false
property var _lastFix: model.coordinate

Connections {
    target: marker          // or use onModelCoordinateChanged if model is a context property
    function onCoordinateChanged() {
        marker.glideEnabled = !Theme.reducedMotion
            && marker._lastFix.distanceTo(model.coordinate) < marker.maxGlideMeters
        marker._lastFix = model.coordinate
    }
}

Behavior on coordinate {
    enabled: marker.glideEnabled
    CoordinateAnimation { duration: 1000; easing.type: Easing.Linear }
}
```

This evaluates `distanceTo()` ~5,877 times (once per fix) instead of 135,616 —
roughly a 23× cut on this path — and removes a per-frame trig call from every
gliding marker, directly easing the frame-time tail and the small-object churn in
the memory section.

### 3. [Main.qml:246](../../qml/Main.qml#L246) — MapPolyline `path` binding rebuilt on every viewport reload (Binding, 697 ms, 26,672 evaluations)

```qml
MapItemView {
    model: trackService.model
    delegate: MapPolyline {
        required property var model
        line.width: 2.2
        line.color: Theme.railColor
        path: model.path
    }
}
```

**What it does.** Each track-segment polyline binds its `path` to the model row's
coordinate array.

**Why it is expensive.** The `Binding` cost (697 ms) dwarfs the JS expression cost
(50 ms, entry #23) because assigning `path` to a `MapPolyline` triggers a *native*
geometry rebuild (tessellation of the line). The 26,672 evaluations are not
re-evaluations of a stable delegate — they are *fresh assignments on delegate
creation*. Every pan-settle reloads the viewport track set; if the model emits a
**reset**, `MapItemView` destroys and recreates all delegates, so every visible
segment re-runs this binding (and re-tessellates) even if it was already on screen
a moment earlier. This is the same root cause as hotspot #5 (53,344 MapPolyline
*creations*).

**Suggested fix.** Make `trackService.model` update **incrementally** — emit
`beginInsertRows`/`beginRemoveRows` (or `dataChanged`) for only the segments that
entered/left the padded viewport, rather than `beginResetModel()`. `MapItemView`
then reuses the existing `MapPolyline` delegates for unchanged in-view segments,
so neither this `path` binding nor the polyline geometry is rebuilt for them. That
collapses both #3 and #5 to the actual churn at the viewport edges. (If the model
already diffs, verify it is not falling back to a full reset on each
`loadForBounds` — the 53,344 creations / 82 reloads ≈ 650 new polylines per reload
suggests near-full recreation.)

### 4. [Main.qml:209](../../qml/Main.qml#L209) / [Main.qml:205](../../qml/Main.qml#L205) — `refreshTracks()` viewport filter (JS + HandlingSignal, ~487 ms, 84 calls, **5.8 ms each**)

```qml
Timer {
    id: trackReloadTimer
    interval: 250        // settle delay: fire once the gesture stops
    onTriggered: map.refreshTracks()
}
...
function refreshTracks() {
    if (!map.mapReady || map.width <= 0 || map.height <= 0) return
    const nw = map.toCoordinate(Qt.point(0, 0), false)
    const se = map.toCoordinate(Qt.point(map.width, map.height), false)
    if (!nw.isValid || !se.isValid) return
    const latPad = Math.abs(nw.latitude - se.latitude) * 0.25
    const lonPad = Math.abs(se.longitude - nw.longitude) * 0.25
    trackService.loadForBounds(nw.longitude - lonPad, se.latitude - latPad,
                               se.longitude + lonPad, nw.latitude + latPad)
}
```

**What it does.** On each pan-settle (250 ms debounce), plus on `geometryReady`,
`onRequestTrackReload`, and `mapReady`, it computes the padded viewport bounds and
asks `trackService` to filter the ~10k-segment network down to what's visible.

**Why it is expensive.** At **5.8 ms per call** this is the single most expensive
*per-invocation* project hotspot, and it runs **synchronously on the UI thread**.
The `toCoordinate` calls are cheap; the cost is `trackService.loadForBounds`
filtering the full network. A 5.8 ms synchronous call lands entirely within one
frame, so each reload can push that frame over the 16.67 ms budget — a direct
contributor to the 272 frames > 33 ms (each pan-settle reload is one such spike).
The 250 ms debounce correctly caps the *frequency* (~4/s) but not the *per-call*
cost.

**Suggested fix.** Back the track geometry with a **spatial index** (a uniform
grid or R-tree) so `loadForBounds` returns the in-view segments in time
proportional to the *result* size instead of scanning all ~10k segments. With a
grid keyed on a coarse lat/lon cell, a viewport query touches only the overlapping
cells. That turns the 5.8 ms scan into a sub-millisecond lookup and removes the
per-pan frame spike. (This is the profile-gated "match/segment spatial index"
follow-up — the trace now justifies it: it is the most expensive single UI-thread
call.)

### 5. [Main.qml:242](../../qml/Main.qml#L242) — MapPolyline delegate creation churn (Creating, 343 ms, **53,344 instantiations**)

```qml
delegate: MapPolyline {
    required property var model
    line.width: 2.2
    line.color: Theme.railColor
    path: model.path
}
```

**What it does.** Instantiates one `MapPolyline` per visible track segment.

**Why it is expensive.** **53,344** `MapPolyline` objects were created over the
run — far more than the number of distinct segments, because each viewport reload
that resets the model destroys and recreates the whole visible delegate set. This
is the creation-side twin of hotspot #3, and it is the largest single source of
the small-object allocation churn reported in the memory section (each delegate +
its bindings + the `path` array is transient). 343 ms of constructor time plus the
GC cost of collecting all those throwaway objects.

**Suggested fix.** Same as #3 — incremental model updates so `MapItemView` reuses
delegates for segments that stay in view; only segments crossing the padded
viewport boundary are created/destroyed. This is the highest-leverage structural
change in the trace because it simultaneously cuts #3 (path rebinding), #5 (this
creation cost), entry #27 (`line.color` rebinding, 26,672×), and a large share of
the 2.1 M small-object allocations.

## Next steps

In priority order (highest leverage first):

1. **Cache the glide decision in [TrainMarker.qml:126](../../qml/TrainMarker.qml#L126)** so the `Behavior.enabled` guard no longer recomputes `distanceTo()` on the live animated `coordinate`. Lowest-effort, highest-confidence win: ~135,616 → ~5,877 evaluations, removes a per-frame trig call per gliding marker. *(Hotspot #2.)*
2. **Make `trackService.model` update incrementally** (granular row insert/remove instead of `beginResetModel`) so `MapItemView` reuses unchanged in-view `MapPolyline` delegates. One change that collapses hotspots #3, #5, entry #27, and the bulk of small-object churn. *(Hotspots #3 & #5.)*
3. **Add a spatial index to `trackService.loadForBounds`** (grid or R-tree) to turn the 5.8 ms full-network scan into a sub-millisecond viewport query, removing the per-pan-settle frame spike. *(Hotspot #4.)*
4. Leave **[Main.qml:340](../../qml/Main.qml#L340)** (DragHandler pan) as-is — it is #1 by total time only because of heavy panning; its per-frame cost is negligible and the handler is already minimal.

The actionable hotspots cluster in exactly two project files —
**[qml/Main.qml](../../qml/Main.qml)** (track layer: 246, 242, 209, 205, 265, 245)
and **[qml/TrainMarker.qml](../../qml/TrainMarker.qml)** (126, 109, 258). For a
broader structural pass on those two files beyond these specific lines, run
`qt-qml-review` scoped to them. After applying any of the fixes above, re-run this
profiler to get a fresh standalone diagnosis.

---

> AI assistance has been used to create this output.
