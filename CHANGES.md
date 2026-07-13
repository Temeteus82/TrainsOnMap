# Changes

Work log for the live-train pipeline, organised by category. Tick items as they
land; keep open items under **Known issues / follow-ups**. Each push should
leave this file describing what that push contains.

Legend: ✨ feature · 🐛 bug fix · ♻️ change/refactor · ✅ verification · 📋 follow-up

---

## Weather overlay switched to FMI open data

The road-weather proxy is replaced with real weather observations.

### ♻️ FMI observations replace the Fintraffic road-weather proxy
- [x] New `FmiWeatherClient` fetches `opendata.fmi.fi` WFS
      (`fmi::observations::weather::simple`, `parameters=temperature`, whole-country
      bbox, last-30-min window) and keeps each station's latest reading. The simple
      format is flat XML (`BsWfsElement`: coordinate + time + value inline), parsed
      with `QXmlStreamReader` — one request, so the old two-stage
      station-metadata + data fetch collapses into one. `NaN` values (missing
      sensor) are skipped. Poll every 10 min (FMI's reporting cadence).
- [x] `RoadWeatherClient` deleted; `WeatherPoint` drops the unused `name` role
      (the map chips never showed it and FMI simple has no station names).
      Sidebar toggle relabelled "Weather" (`showWeather`) — no proxy disclaimer
      needed now that it's actual weather-station data.

### ✅ Verification
- [x] Clean `windows-llvm` build; `ctest` 3/3. The client's exact query verified
      against live FMI data (~190 stations return temperature records).
      Chip rendering on the running map not yet eyeballed (same delegate as
      before, only the model source changed).

---

## Panel/marker type scale + crisper small text

### 🐛 Side-panel and marker text too large
- [x] `TypeScale` gained a `panel*` role family (`panelCaption`/`panelBody`/
      `panelSubhead`/`panelTitle` + matching icon sizes) at a fixed 0.75×
      `panelScale`, alongside the existing plain roles. `InfoPanel`,
      `TrainDetailPanel`, `StationBoardPanel`, `ToggleRow`, and the train
      marker badge text (`TrainMarker.qml`) now reference the `panel*` roles;
      the FMI weather map chip still uses the plain scale (untouched — not
      flagged).

### 🐛 Blurry small text on train markers
- [x] Shrinking the marker text to `panelCaption` (8.25 pt) exposed the
      softness of Qt Quick's default `Text.QtRendering` (GPU distance-field
      glyphs) at small sizes. Set `renderType: Text.NativeRendering` on the
      three `TrainMarker.qml` labels (badge, speed, delay) — platform-hinted
      rasterisation, crisp at small sizes, and fine here since this text is
      never transformed/scaled (NativeRendering's one real limitation).

### ✅ Verification
- [x] Clean `linux-release` build after each step; `ctest` 3/3; app launched
      (persistent, not just a smoke-timeout) and eyeballed live by the user,
      who confirmed the result looks good.

---

## Marker declutter near busy termini (e.g. Helsinki)

### ✨ Density-aware label suppression
- [x] Qt Location has no built-in marker clustering (`MapItemView` is a plain
      model→delegate repeater — checked against the Qt 6.11 docs), so this is
      app logic. `TrainListModel` now precomputes `nearestNeighborMeters` for
      every row once per REST snapshot (O(n²) over the live fleet, but only
      every 60 s, so trivially cheap); MQTT single-train upserts don't
      recompute it since a terminus is essentially stationary between polls.
- [x] `Main.qml`'s train delegate combines that with a zoom/latitude-derived
      ground resolution (`map.metersPerPixel`, standard spherical-Mercator
      formula) so `labelsVisible` drops to a bare dot whenever another train
      is closer than ~30 px on screen — on top of the existing zoom < 8
      country-scale collapse. A stationary cluster at a terminus platform now
      reads as dots instead of overlapping text badges.

### ✅ Verification
- [x] Clean `linux-release` build; app launches with no QML warnings; `ctest`
      3/3. Not yet eyeballed against a live cluster of trains at Helsinki
      (needs the app running against live Digitraffic data at the right time
      of day).

---

## Feature batch — station board, punctuality stats, road-weather overlay

Three of the deferred feature ideas, built API-first (endpoints verified against
live Digitraffic before coding).

### ✨ #1 Station departure board
- [x] Passenger stations render as clickable dots (`StationListModel`, populated
      from the existing /metadata/stations fetch — passenger stations only, shown
      at zoom ≥ 9). Clicking one opens a board panel.
- [x] `StationBoardService` fetches `/live-trains/station/{code}` (same train-object
      shape the app already parses) and turns each calling train into a board row
      (`StationBoardModel`): time + live estimate, destination, track, delay,
      arriving/departing. `StationBoardPanel.qml` lists them, sorted by time.
- [x] Shares the right-side slot with the train detail panel — selecting a station
      clears any train selection and vice versa, so they never overlap.

### ✨ #5 Punctuality stats
- [x] No stats endpoint exists, so it's aggregated **client-side** from the
      `/live-trains` delay data already polled each cycle (`DigitrafficClient::`
      `recomputePunctuality`): % on time (≤5 min) per broad category, shown as a
      sidebar caption line. No extra request.

### ✨ #4 Road-weather overlay (labelled as road, not rail)
- [x] The rail API publishes **no** weather; this uses Fintraffic **road** weather
      (`tie.digitraffic.fi`) as a nearby-conditions proxy, clearly labelled as such.
      `RoadWeatherClient` fetches station coords once + air temperature (`ILMA`
      sensor) on a slow timer; `WeatherStationModel` feeds a map layer of
      temperature chips. Off by default (idle, no traffic) behind a sidebar
      "Road weather" toggle.

### ✅ Verification
- [x] Clean `linux-release` build; app launches with no QML warnings; `ctest` 3/3.
      Endpoints (`/live-trains/station`, road weather stations + data) verified
      against live data before implementation. Interactive click/toggle paths not
      yet eyeballed on the running map.

---

## UI-audit accessibility fixes — marker contrast, type scale, a11y, panel overflow

Four `qt-ui-design` audit findings, applied to the QML surface (no C++). PR #41.

### 🐛 Marker label contrast (WCAG)
- [x] `TrainMarker` capsule labels hardcoded white ink, which fell below 4.5:1 on the
      light train hues (cyan/orange/pink/stale-grey). New `inkFor()` picks black or
      white by true sRGB relative luminance — per train type, mirroring
      `Theme.accentText` — and `labelInk` drives the badge, km/h and `+N min` labels.

### ♻️ Type scale + accessibility polish
- [x] Capsule labels routed through `TypeScale.caption` (were raw 9/11 px; the 9 px
      sub-labels sat below the scale's documented 11 px floor).
- [x] Carriage cells expose `Accessible.name`/`description` (type · power · amenities)
      so that detail is reachable without the hover tooltip (`TrainDetailPanel.qml`).
- [x] `InfoPanel` wraps its content in a `Flickable` and caps its height to the space
      below the top margin, so a short window scrolls the card (themed scrollbar)
      instead of clipping the lower sections off-screen (`InfoPanel.qml`).

### ✅ Verification
- [x] Clean `linux-release` build (qmlcachegen validates the QML); smoke-launched with
      an empty log (no QML warnings). Deferred: keyboard-reachable markers and sidebar
      cursor consistency (audit #3/#6) — see **Still open** below.

---

## Track-category styling + themed timetable scrollbar

PR #38. Running lines vs sidings now read distinctly, and the timetable scrollbar
matches the dark panel.

### ✨ Track categories by `paaraide`
- [x] `RailGraph`/`TrackService` carry each segment's `paaraide` main-track flag
      through to a `mainTrack` role on `TrackListModel`; the `Main.qml` track delegate
      draws running lines bolder/opaque (`Theme.railColor`, width 2.2) and sidings
      thinner/dimmer (`Theme.railSidingColor`, width 1.3). Sidings hide via the legend
      toggle.

### ✨ Themed timetable scrollbar
- [x] The timetable `ScrollBar` gets a thin muted-ink handle that fades in on
      hover/press, reading correctly on the dark card (`TrainDetailPanel.qml`).

---

## Map/legend polish — filters, cause text, breadcrumb trail

Four small features, each reusing an existing service/pattern rather than
adding new architecture.

### ✨ Track-category legend + siding toggle
- [x] `InfoPanel` legend swatches (`Theme.railColor` / `railSidingColor`) plus a
      "Show sidings" toggle (`showSidings`); the track `MapPolyline` delegate in
      `Main.qml` hides non-`mainTrack` segments when it's off.

### ✨ Train-type filter
- [x] `InfoPanel` gained `showCommuter` / `showLongDistance` / `showCargo`
      toggles + `categoryVisible(category)`; the `TrainMarker` delegate binds
      `visible` to it. An unrecognised/empty category (metadata not loaded yet)
      always shows, so trains never vanish at startup.
- [x] New shared `ToggleRow.qml` (labelled checkbox, keyboard-focusable with a
      focus ring, mirrors the Appearance segment's accessibility pattern) —
      4 call sites (siding + 3 category toggles) justified factoring it out.

### ✨ Delay-cause text on the timetable
- [x] `DigitrafficClient` fetches `/metadata/cause-category-codes` once
      (mirrors the existing one-shot station-names fetch) and exposes
      `causeCategoryNames()` (categoryCode -> Finnish name).
- [x] `TrainDetailsService::buildStops()` captures each stop's top-level cause
      `categoryCode` (departure preferred over arrival, like `delayMinutes`);
      `rebuildStops()` resolves it to `TimetableStop::causeText`, shown as an
      italic caption line under the track/passing label in
      `TrainDetailPanel.qml`. Only the top-level code is resolved (not
      `detailedCategoryCode`/`thirdCategoryCode`) — coarser, but one fetch.

### ✨ Breadcrumb trail for the selected train
- [x] `Main.qml` appends the selected train's snapped position (from the
      existing `matchInfoFor()` 750 ms poll) to `win.trailPoints` (capped at 8),
      reset on selection change, rendered as a fading `MapPolyline`. No new
      C++/model state — reuses the same diagnostics feed as the route/connector
      overlay.

---

## Track accuracy — Tier 2 (topology-routed map matching)

Route-constrained map matching: markers now follow each train's *scheduled* path
through double-track and junctions, and a stopped train pins to its booked
platform track. Plan + findings in `docs/track-accuracy-tier2-plan.md`.

### ✨ Re-bake with identity + topology + station crosswalk
- [x] **Schema-v2 blob** (`scripts/bake_rails.py`): keeps per-track `tunniste`,
      `paaraide`, `kaupallinenNumero`, `seuraavatRaiteet`, `viereisetRaiteet`,
      `rautatieliikennepaikat`, `ratakmvalit` (was geometry-only), and bakes a
      station crosswalk (`stations`, `operatingPoints`). `schemaVersion: 2` lets
      the loader reject old blobs. 4936 tracks, 557 stations → **2.6 MB (+9 %)**.
- [x] **Crosswalk join verified** — `uicKoodi` over the union of
      `rautatieliikennepaikat` + `liikennepaikanosat` (parts), matched to
      `/metadata/stations.stationUICCode`: **214/215 passenger stations resolve**
      (only the parts layer makes hubs like Pasila resolvable; parent-op fallback
      for tracks). `lyhenne`/coarse-op `uicKoodi` do **not** join.

### ✨ Derived-graph routing + route-constrained matching
- [x] **`RailGraph`** (new pure, unit-testable core): parses the v2 blob, builds a
      routing graph from **geometry endpoints + `viereisetRaiteet`** (because
      `seuraavatRaiteet` is empty in the live data — 38 edges nationally), and
      provides Dijkstra route resolution, chainage-windowed projection, and
      platform snapping. `TrackService` owns it and derives render segments from it.
- [x] **Off-thread route precompute** (`TrackService::precomputeRoutes`): resolves
      each train's station sequence to a chainage-parameterised polyline in a
      `QtConcurrent` task, deduped + memoised; a 60 s refresh only adds new routes.
- [x] **Route-constrained match** (`TrainListModel::applyOne`): snaps to the
      nearest point on the train's route within a `speed·Δt` window (1-D chainage
      carried across fixes for continuity), platform-snaps a stopped train to its
      `commercialTrack`, and falls back to the Tier-1 nearest+heading matcher when
      no route is resolved or the fix is off the booked route.

### ✅ Validation
- [x] **Unit tests** (`tests/tst_railgraph.cpp`, QtTest + CTest): fixture parse,
      endpoint-graph route stitching, chainage projection, platform snapping, plus
      a real-blob smoke test (HKI→PSL→TPE resolves; on-route point projects <1 m).
      **9/9 pass.**
- [x] **Debug overlay** (`Main.qml`): the selected train's resolved route polyline,
      a raw→snapped connector, a ring at the raw fix; the detail panel shows
      `on route / nearest track · N m off · <tunniste>`.
- [x] Builds clean (`windows-llvm`); app runs with live data (REST+MQTT+routing) —
      no crashes/warnings.
- [ ] 📋 Live on-map eyeballing of parallel-track/junction/platform behaviour
      (inherently visual; not fully judgeable from unit tests).

---

## Dark mode, map panning & UI-audit polish

A map-interaction fix, full light/dark/auto theming, three dark-mode/launch bug
fixes, and the remaining `qt-ui-design` audit items.

### 🐛 Bug fixes
- [x] **Horizontal panning was throttled** — `Map.center` was two-way bound to the
      loader's `savedCenter*` (the binding read center *from* them while
      `onCenterChanged` wrote them *back*), so every `map.pan()` was re-asserted by
      the binding and horizontal drags barely moved. Center/zoom are now restored
      imperatively once in `Component.onCompleted`; the handlers only write back for
      theme-reload persistence (`Main.qml`).
- [x] **No console window on launch** — the target is a GUI-subsystem app in
      Release (`WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>`); Debug keeps the console
      attached for `qDebug`/logs (`CMakeLists.txt`). Verified PE subsystem = GUI.
- [x] **Timetable unreadable in dark mode** — the native-styled `ItemDelegate`
      painted a white system-palette background, hiding the theme-coloured
      (near-white) text. Its background is now driven by `Theme`
      (`TrainDetailPanel.qml`).
- [x] **Delay badges hidden under the scrollbar** — the timetable row reserves the
      vertical `ScrollBar` width on the right, and a conflicting `verticalCenter`
      anchor was removed (`TrainDetailPanel.qml`).

### ✨ / ♻️ UI audit — Criticals (C1–C3) + Warnings/Opportunities (W1–W4, O1–O3)
- [x] **C1** Theme-aware markers: dark hues lifted for legibility on the dark
      basemap, light dot stroke in dark mode, theme-aware label/km-h colours.
- [x] **C2** `Theme.accentText` is dark ink on the light dark-mode accent (white
      on it was ~2.8:1 → now ~6.4:1).
- [x] **C3** marker text labels gate on `zoomLevel >= 8.0` (dots only at country
      scale) — fixes the overlap clutter.
- [x] **W1** km/h marker sub-label 8 px → 11 px.
- [x] **W2** new `TypeScale` singleton (caption/body/subhead/title) replaces ~6
      ad-hoc font sizes across the panels (sizes are px — OS font-scale TODO below).
- [x] **W3** reduced-motion opt-out (`Theme.reducedMotion`, persisted via
      `QtCore.Settings`); the LIVE pulse gates on it.
- [x] **W4** Appearance segments, the detail-panel close button, and the show-all
      checkbox are keyboard-focusable (Tab + Space/Enter) with focus rings and
      `Accessible` roles.
- [x] **O1** marker labels sit on a semi-opaque themed pill (was a 1 px outline).
- [x] **O2** selection is a haloed accent double-ring that reads on blue/navy dots.
- [x] **O3** new `AppIcon` (Canvas line-art, theme-recolourable) replaces the
      emoji glyphs (train / close / check). New singletons registered in CMake:
      `TypeScale.qml`, `AppIcon.qml`.

### ✅ Verification
- [x] Clean `windows-llvm` Release build; no QML errors on startup. Screenshot-
      verified: horizontal + vertical pan, dark-mode markers and timetable, delay
      badges clear of the scrollbar, and a console-free GUI launch.

---

## Accessibility — UI audit critical fixes

From the `qt-ui-design` audit. The three **Critical** (WCAG / core-law) findings:

### 🐛 / ♻️ Fixes
- [x] **Keyboard focus invisible** — the custom-styled sidebar buttons dropped
      the style's focus ring. Added `focusPolicy: Qt.StrongFocus` and a
      `visualFocus`-driven focus ring (accent outline) on *Refresh* and
      *Load tracks* (`InfoPanel.qml`).
- [x] **Lateness was colour-only** — the marker late ring (amber/red) is now
      paired with a textual `+N min` badge so the state is legible without colour
      perception (colour-blind safety). Added a `delayMinutes` model role
      (`TrainListModel`) and the badge (`TrainMarker.qml`).
- [x] **Sub-4.5:1 text contrast** — raised failing greys to ≥ 4.5:1: sidebar
      attribution `#9aa0a6`→`#6b7280`; detail-panel status `#888888`, track
      sub-label and neutral delay `#999999` → `#5f6671`
      (`InfoPanel.qml`, `TrainDetailPanel.qml`).

### ✅ Verification
- [x] Builds clean; app loads with no QML errors. Screenshot-verified an amber
      late train rendering both the ring **and** a matching "+5 min" badge.

---

## Map framing & sidebar polish

### ✨ Features
- [x] Sidebar card (`InfoPanel`) redesigned: rounded 12 px card with a soft drop
      shadow, icon badge + title/subtitle header, hairline dividers, a pulsing
      green **LIVE** indicator paired with the live-train count, and flat
      primary/outlined action buttons (custom-styled, `QtQuick.Controls.Basic`).

### 🐛 Bug fixes
- [x] **Map was not interactive** — Qt 6 removed `MapGestureArea`, so the `Map`
      had no pan/zoom at all (the old "enabled by default" comment was wrong).
      Added explicit `DragHandler` (pan), `WheelHandler` (wheel/trackpad zoom),
      `PinchHandler` (pinch zoom, north-up — no rotation), and `StandardKey`
      zoom-in/out shortcuts (`Main.qml`).

### ♻️ Changes
- [x] Map now opens framed on **southern Finland** (the Helsinki–Turku–Tampere
      rail triangle, centre 61.0 °N, 24.5 °E, zoom 7.0) instead of the capital
      area; clamped to `minimumZoomLevel` 4.0 (zoom out for the whole country) /
      `maximumZoomLevel` 18.0 (`Main.qml`).
- [x] The whole pre-baked rail network (4936 segments) is now rendered: loaded
      once at startup via `trackService.load()` and held as a static model, so
      panning/zooming no longer rebuilds polyline delegates. Dropped the
      per-viewport `loadForBounds` fetch, the `trackZoomThreshold` gate, and the
      debounce timer (`Main.qml`). Cost: ~+110 MB RSS over the filtered view.
- [x] Rail stroke bumped to width 2.2, colour `#8c95a0` (was 1.4 / `#b4b8bf`) so
      the network reads clearly at the overview zoom (`Main.qml`).

### ✅ Verification
- [x] Visual: southern Finland in view, full rail network drawn as legible grey
      lines with trains on top; redesigned sidebar shows live count, LIVE pulse,
      restyled buttons, and "4936 track segments". Wheel-zoom confirmed working.
- [x] Measured: ~400 MB RSS, ~44 % of a core idle (the idle CPU is dominated by
      the live train markers, not the static rail polylines).

---

## Status-ring rules (refinement)

### ♻️ Changes
- [x] Late rings now have a clear two-tier threshold: **amber at 5–14 min late,
      red at 15+ min** (nothing under 5 min). Constants `kLateMinutes`/
      `kVeryLateMinutes` (`TrainListModel`).
- [x] Delay rings apply to **scheduled passenger trains only** — Cargo,
      Locomotive, Shunting and On-track-machines carry no ring. Green "ready" ring
      additionally requires a genuinely on-time (`delay <= 0`), stopped train.

### ✅ Verification
- [x] Visual: green rings only on stopped trains, red rings on very-late ICs.
- [x] Data cross-check vs live `/live-trains`: amber tier populated, all
      non-passenger categories excluded.

---

## Live-train pipeline (REST + MQTT → map)

### ✨ Features
- [x] Marker colours: full juliadata.fi palette keyed on `trainType`, with a
      category/speed fallback (`TrainMarker.qml`).
- [x] Badge labels: commuter line letter → `"TYPE NUMBER"` → bare number, plus a
      `km/h` sub-line while moving (`TrainMarker.qml`).
- [x] `commuterLine` model role, sourced from `/live-trains`
      (`TrainListModel`, `DigitrafficClient`).
- [x] Status rings: green (ready/stopped on time) · amber (1–5 min late) · red
      (>5 min) · grey-dimmed (stale / cancelled / not running). Computed in
      `TrainListModel::ringStateFor()` from live status + position age/speed.
- [x] `TrainStatus` (delay / cancelled / running) parsed from `/live-trains`
      (`DigitrafficClient::handleCategories`).

### 🐛 Bug fixes
- [x] **REST metadata never applied** — manual `Accept-Encoding: gzip` disabled
      Qt's transparent decompression, so every REST reply parsed as raw gzip and
      failed silently (markers stayed orange/bare-number, no rings). Removed the
      manual header in `DigitrafficClient`, `TrainDetailsService`,
      `TrackService`; Qt now negotiates + inflates automatically.
- [x] **Rail tracks failed to load** — infra-api rejected fractional bbox
      coordinates (`400 "Coordinate must be an integer number"`). Now floor/ceil
      to whole metres (`TrackService::loadForBounds`). 529 segments load.

### ♻️ Changes / refactors
- [x] `TrainListModel` now has a single timestamp-guarded `applyOne()` funnel for
      both REST and MQTT updates (was two separate paths).
- [x] REST snapshot **merges + prunes** (120 s grace) instead of
      `beginResetModel()` — no marker flicker, MQTT positions not clobbered.
- [x] Bearing history (`previousPositions`) garbage-collected to live trains.
- [x] REST poll cadence 5 s → 60 s resync (`DigitrafficClient`); MQTT carries
      live deltas in between.
- [x] Resync actually enabled — `DigitrafficClient.active: true` in `Main.qml`
      (was `false`, so nothing pruned after the bootstrap).
- [x] Rail lines restyled to thin light grey (`#b4b8bf`, width 1.4) so trains
      read as the focal layer on the light base (`Main.qml`).

### ✅ Verification
- [x] Builds and links (via `-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=build/bin`, see
      follow-up below).
- [x] Ran the app and screenshot-verified: type colours, labels, km/h, status
      rings, live MQTT, and 529 rail segments on the CARTO light base.

---

## Build & tooling

### ✨ Features
- [x] `CMakePresets.json` pins the toolchain per OS — **Clang** on Linux/macOS,
      **MSVC 2022** (VS 17 generator, x64) on Windows — each building into its own
      `build/<preset>/` folder, with the executable under `bin/` for clarity.
- [x] Windows: `windeployqt` post-build step bundles the Qt DLLs and required
      plugins (windows platform, TLS backend for `wss://`, QML / QtLocation
      geoservices / QtPositioning) next to the `.exe`, plus `--compiler-runtime`,
      so a fresh build runs without Qt on `PATH` (`CMakeLists.txt`).

### ♻️ Changes
- [x] **CMake exe/module name collision** (fixed) — default `cmake --build build`
      used to fail at link (`cannot open output file TrainsOnMap: Is a directory`)
      because the extension-less executable collided with the `TrainsOnMap/` QML
      module directory on Linux/macOS. Fixed by emitting the executable to a
      `bin/` subfolder via `RUNTIME_OUTPUT_DIRECTORY` (`CMakeLists.txt`).
- [x] README **Build & run** rewritten around the presets, with per-OS paths
      (`build/<preset>/bin/...`).

### ✅ Verification
- [x] `cmake --preset linux-clang` + `cmake --build --preset linux-clang`
      configures with Clang 22 and builds; the Clang binary launches and renders
      trains, rings, and rail tracks.
- [x] Windows build verified on Windows 11 + Qt 6.11.1 (2026-06-15): clean
      from-scratch configure + Release build with the **MSVC** preset (VS 2022
      Build Tools v17.14, `Visual Studio 17 2022` generator) and with a **MinGW**
      kit (Qt's bundled GCC 13 + Ninja), both exit 0 and emit `TrainsOnMap.exe`.
      The `windeployqt` post-build step runs and bundles the Qt DLLs/plugins.
      Launching the deployed `.exe` standalone not yet confirmed.

---

## Rail geometry (offline)

### ✨ Features
- [x] Rail network is **pre-baked and shipped in the repo** instead of fetched on
      every launch. `scripts/bake_rails.py` tiles the national network from the
      infra-api, de-dupes by `tunniste`, strips to geometry-only, and writes a
      compressed snapshot (`resources/rails.geojson.qz`).
- [x] The snapshot is embedded as a Qt resource and loaded once at startup;
      `TrackService` now projects it to WGS84 in memory and `loadForBounds()`
      filters to the viewport — no network calls for tracks (`TrackService`,
      `CMakeLists.txt`).

### ♻️ Changes
- [x] `TrackService` no longer uses `QNetworkAccessManager` / the infra-api
      endpoint; the live-fetch + bbox-request path is removed.

### 📝 Notes
- Container is Qt's `qCompress` (zlib) format, not a literal `.gz`, so it loads
  with pure Qt Core (`qUncompress`) — no `find_package(ZLIB)` or build-time
  gunzip, keeping the MSVC 2022 build dependency-free. Re-bake with
  `python3 scripts/bake_rails.py` when the rail topology changes.

---

## Simplification pass (repo-wide over-engineering audit)

### ♻️ Changes
- [x] **`TrackMatcher` interface folded away** — it had exactly one implementation
      (`TrackService`), and `DigitrafficClient` already coupled to `TrackService*`
      directly. The `TrackMatch` / `RouteMatchRequest` value types moved into
      `TrackService.h`; `TrainListModel` now holds a forward-declared
      `const TrackService *` (`TrackMatcher.h` deleted).
- [x] **`/metadata/stations` fetched once, not twice** — `DigitrafficClient` now
      also parses station names from its one-shot stations fetch and exposes them
      via `stationNames()`; `TrainDetailsService` consumes them through its new
      `fleet` property (wired in `Main.qml`) instead of issuing its own request.
- [x] **Dead code deleted** — unused `tm35fin::fromWgs84()` (forward projection,
      no callers) and `TrainListModel::nearestRouteStation()` (line-for-line
      duplicate of `nearestRouteStationCode()`; the one call site now looks the
      code up in `m_stationCoords`).
- [x] **Baked blob slimmed** — `ratakmvalit` (~0.5 MB of linear referencing) and
      the crosswalk diagnostics (`uic`/`opOid`/`match`/`distM`) are no longer
      baked; `RailGraph::loadFromJson` never read them. Raw JSON 8.3 → 7.7 MB
      (crosswalk resolution stats are still printed at bake time).

---

## 📋 Known issues / follow-ups
- [ ] `/trains/{date}` summary cache not added — `/live-trains` already supplies
      type/category/line for currently-running trains (covers cross-midnight), so
      it's unnecessary for now. Revisit only if a type source independent of
      "currently running" is needed.

### Feature ideas
- [x] **Station departure board** — done: clickable station layer +
      `StationBoardService`/`StationBoardModel` + `StationBoardPanel` over
      `/live-trains/station/{code}` (see the feature batch section above).
- [x] ~~**Favourite/pinned trains**~~ — declined, not appealing.
- [x] ~~**"Nearest trains to me"**~~ — declined, not appealing.
- [x] **Rail-weather overlay** — done: first as a Fintraffic **road**-weather
      proxy (the rail API publishes no weather), since replaced by real FMI
      open-data observations (`FmiWeatherClient`, see the FMI section above).
- [x] **On-time / punctuality stats** — done: there is no statistics endpoint, so
      it's aggregated client-side from the `/live-trains` delay data
      (`DigitrafficClient::recomputePunctuality`), no new fetch.

### UI design audit (qt-ui-design)
All audit findings — Criticals **and** the Warnings/Opportunities below — are now
implemented (see "Dark mode, map panning & UI-audit polish" above).

**✅ Done**
- [x] `TypeScale` singleton (modular scale) replaced the hardcoded font sizes.
- [x] `Theme` singleton of role-based colour tokens — drives light/dark/auto and
      reconciles the previously-divergent semantic colours.
- [x] Marker label declutter — text hidden below zoom 8 (dots only).
- [x] Reduced-motion opt-out for the LIVE pulse (`Theme.reducedMotion`).
- [x] Dark theme for overlays — basemap + all overlay colours follow `Theme`.

**📋 Still open**
- [x] ~~Type sizes are px, not `pointSize`.~~ Done — every `TypeScale`-driven
      label now sets `font.pointSize` instead of `pixelSize`, so text follows
      the OS "Large text" / accessibility DPI setting.
- [x] ~~Sidebar redundant affordances.~~ Done — dropped the "Load tracks"
      button (auto-refresh on pan/zoom already covered it, so it was a no-op)
      and stopped `TrackService::loadForBounds` from restating the viewport
      count into `status`, which duplicated the dedicated track-count label.
- [x] ~~Map markers are click-only; no keyboard pan.~~ Partially done — the map
      itself now pans with the arrow keys (existing Ctrl+/- already zoomed).
      Individual train markers still aren't Tab-reachable one-by-one; doing
      that properly needs a train list view, which is out of scope now that
      the list-dependent feature ideas below were declined.
- [x] ~~Action buttons are 32 px tall.~~ Done — Refresh is now 44 px
      (the only action button left after "Load tracks" was removed).
- [x] ~~`TrainDetailPanel` only toggles `visible`.~~ Done — both right-side
      panels (train detail + station board) now fade over 220 ms; the loaded
      item is kept around (hidden) after first use instead of being torn down,
      so the closing fade has something to animate.
- [x] ~~Timetable `ScrollBar` styling needs refinement to match the dark panel.~~
      Done — themed handle (PR #38).
- [x] ~~Dark basemap tile cache: stale `light_all` tiles linger after switching
      theme.~~ Done — `Main.qml` gives each basemap style its own cache directory
      (`cacheDirFor`).
