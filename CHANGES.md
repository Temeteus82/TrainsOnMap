# Changes

Work log for the live-train pipeline, organised by category. Tick items as they
land; keep open items under **Known issues / follow-ups**. Each push should
leave this file describing what that push contains.

Legend: ✨ feature · 🐛 bug fix · ♻️ change/refactor · ✅ verification · 📋 follow-up

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
- [ ] Windows `windeployqt` deployment not testable on this Linux host — verify on
      a Windows/MSVC machine.

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

## 📋 Known issues / follow-ups
- [ ] `/trains/{date}` summary cache not added — `/live-trains` already supplies
      type/category/line for currently-running trains (covers cross-midnight), so
      it's unnecessary for now. Revisit only if a type source independent of
      "currently running" is needed.
