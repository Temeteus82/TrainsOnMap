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

## 📋 Known issues / follow-ups
- [ ] **CMake exe/module name collision** — default `cmake --build build` fails
      at link (`cannot open output file TrainsOnMap: Is a directory`) because the
      executable target and the QML module URI share the name `TrainsOnMap`.
      Workaround: `-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=build/bin`. Proper fix: give
      the executable a distinct `OUTPUT_NAME` or output dir.
- [ ] `/trains/{date}` summary cache not added — `/live-trains` already supplies
      type/category/line for currently-running trains (covers cross-midnight), so
      it's unnecessary for now. Revisit only if a type source independent of
      "currently running" is needed.
