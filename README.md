# TrainsOnMap

A Qt6 desktop app that shows **live Finnish train positions** and **railway track
geometry** on an interactive map, using the open
[Digitraffic railway API](https://www.digitraffic.fi/rautatieliikenne/) from Fintraffic.

Live trains appear out of the box. The app supports light/dark/auto theming and a
keyboard-accessible UI, with a clean split between a C++ networking/model backend
and a QML map front-end, plus clearly marked extension points.

## Features

- **OpenStreetMap base layer with light/dark theming** — CARTO *Positron*
  (`light_all`) / *Dark Matter* (`dark_all`) tiles via the Qt Location `osm`
  plugin's custom-host mechanism, so the coloured trains and rails stay the focus.
  An **Appearance** toggle (Auto / Light / Dark) reskins the whole app — basemap,
  overlays, and markers — through the `Theme` singleton; *Auto* follows the
  desktop colour scheme.
- **Live train markers** streamed over **MQTT** (push, real-time), seeded by one
  REST snapshot at startup; **coloured by train type** like juliadata.fi
  (S/Pendolino = green, IC = red, PYO/night = navy, commuter = green,
  cargo = navy, other = grey) and rotated to heading. Type/number/km-h labels sit
  on a contrast pill and appear once zoomed in (dots only at country scale); the
  selected train gets a highlight ring. A status dot shows the live connection.
- **Accessible, themeable UI** — role-based colour tokens and a shared type scale
  (`Theme` / `TypeScale` singletons); keyboard-focusable controls with visible
  focus rings; colour-blind-safe train state (colour **and** ring **and** text);
  a reduced-motion opt-out; and crisp, theme-recolourable icons (`AppIcon`).
- **Click a train** to open a timetable panel: stops, scheduled vs. estimated
  arrival/departure times, live delay (with its **cause category**, e.g.
  "Onnettomuus", resolved from `/metadata/cause-category-codes`), and track,
  with station codes resolved to names from the metadata API. While open, the
  panel **updates live** from the MQTT `trains/#` topic (scoped to the selected
  train). Selecting a train also draws a short fading **breadcrumb trail** of
  its recent matched positions.
- **Track geometry** drawn automatically for the current viewport (once zoomed
  in past the threshold), served from a **pre-baked national snapshot embedded in
  the binary** (`scripts/bake_rails.py` → `:/data/rails.geojson.qz`) — no infra-api
  fetch at launch. `loadForBounds()` just culls the resident geometry to the
  viewport via a spatial grid. A sidebar **legend + toggle** can hide sidings/yards
  (running lines always show); a **train-type filter** (Commuter / Long-distance /
  Cargo) can hide whole marker categories.
- Clean split between a **C++ networking/model backend** and a **QML map UI**.

## Architecture

```text
main.cpp                    Bootstraps the QML engine, loads TrainsOnMap/Main.qml
│
├─ DigitrafficClient   (C++) REST train-locations/latest snapshot + periodic
│                            live-trains category map → TrainListModel
│   └─ TrainListModel  (C++) QAbstractListModel of live trains (roles: coordinate,
│                            trainNumber, speed, bearing, timestamp); bulk replace
│                            for the snapshot, per-train upsert for the MQTT stream
│
├─ DigitrafficMqttClient (C++) MQTT 3.1.1 over WebSocket (MqttCodec.h); subscribes
│                            to train-locations/# (upserts into TrainListModel) and,
│                            per selection, trains/<date>/<n>/# (emits trainMessage)
│
├─ TrackService        (C++) Loads the baked rails.geojson.qz blob into a RailGraph
│                            once at startup; loadForBounds() culls to the viewport → TrackListModel.
│                            Also Tier-2 route matching (Dijkstra over reconstructed topology)
│   └─ TrackListModel  (C++) QAbstractListModel of polyline segments (role: path)
│
├─ TrainDetailsService (C++) On click, fetches /trains/{date}/{number}, resolves
│   │                        station codes via /metadata/stations → TimetableModel;
│   │                        then applies live trainMessage updates for that train
│   └─ TimetableModel  (C++) QAbstractListModel of merged stops (arr/dep, delay, track)
│
└─ qml/
    ├─ Main.qml             Map + MapItemView layers + InfoPanel + TrainDetailPanel
    ├─ TrainMarker.qml      Clickable MapQuickItem delegate for one train
    ├─ InfoPanel.qml        Floating status/controls card (incl. Appearance toggle)
    ├─ TrainDetailPanel.qml Slide-in timetable for the selected train
    ├─ Theme.qml            Singleton: light/dark/auto palette + map basemap style
    ├─ TypeScale.qml        Singleton: modular type scale (caption/body/subhead/title)
    └─ AppIcon.qml          Canvas line-art icons (train / close / check), theme-tinted
```

C++ types are registered to QML via `QML_ELEMENT` under the `TrainsOnMap` module,
so `DigitrafficClient` and `TrackService` are instantiated declaratively in QML.

## Data sources & endpoints

| Data | Endpoint | Notes |
|------|----------|-------|
| Train positions (seed) | `GET https://rata.digitraffic.fi/api/v1/train-locations/latest/` | JSON array; `location` is a GeoJSON Point `[lon, lat]`, plus `speed`, `trainNumber`, `departureDate`, `timestamp`. Fetched once at startup. |
| Train positions (live) | `wss://rata.digitraffic.fi:443/mqtt`, topic `train-locations/#` | MQTT 3.1.1 over WebSocket (subprotocol `mqtt`, no credentials). Each PUBLISH payload is the same JSON shape as one REST element. |
| Train type/category | `GET https://rata.digitraffic.fi/api/v1/live-trains` | `trainNumber` → `trainType` (`IC` / `S` / `PYO` / …) and `trainCategory` for marker colour; seeded at startup, refreshed every 5 min. |
| Timetable (initial) | `GET https://rata.digitraffic.fi/api/v1/trains/{departureDate}/{trainNumber}` | Single-element array; `timeTableRows` are ARRIVAL/DEPARTURE entries with `scheduledTime`, `liveEstimateTime`, `actualTime`, `differenceInMinutes`, `commercialStop`, `commercialTrack`. |
| Timetable (live) | `wss://rata.digitraffic.fi:443/mqtt`, topic `trains/<date>/<number>/#` | Subscribed only while a train is selected; each PUBLISH is the full running-train object, re-applied to the open panel. |
| Station names | `GET https://rata.digitraffic.fi/api/v1/metadata/stations` | Maps `stationShortCode` → `stationName`; fetched once and cached. |
| Track geometry (baked) | `GET https://rata.digitraffic.fi/infra-api/latest/raiteet.geojson?bbox=…` — **build time only** | GeoJSON FeatureCollection of LineString/MultiLineString in **EPSG:3067**. The full network is 10+ MB, so `scripts/bake_rails.py` tiles the national extent in EPSG:3067, merges/de-dupes features by `tunniste`, and commits the compressed `rails.geojson.qz` blob the app embeds. **Not fetched at runtime** — loaded once at startup and culled to the viewport by `loadForBounds()`. |

All requests send a `Digitraffic-User` header (set it to your own app id in
`DigitrafficClient.cpp` / `TrackService.cpp`) and `Accept-Encoding: gzip` as
[recommended by Digitraffic](https://www.digitraffic.fi/ohjeet/).

### Coordinate reference system

The infra-api returns track geometry in **EPSG:3067 (ETRS-TM35FIN)** — projected
metres, *not* WGS84 — and interprets the `bbox` parameter in that same CRS. The
bbox tiling and forward projection now happen offline in `scripts/bake_rails.py`,
and the baked blob keeps its coordinates in EPSG:3067. So at runtime the app only:

- inverse-projects every baked coordinate back to WGS84 for the map, once at
  startup while parsing the blob ([`RailGraph::loadFromJson`](src/RailGraph.cpp)).

The transform lives in [`src/Projection.h`](src/Projection.h) (Snyder Transverse
Mercator, sub-metre accurate; ETRS89 ≈ WGS84 here). Train positions, by contrast,
already come as WGS84 GeoJSON Points and need no transform, and live map-matching
works in a local metric frame (no full EPSG:3067 round-trip per fix).

## Prerequisites

- **Qt 6.5 or newer** with the `Quick`, `Qml`, `QuickControls2`, `Network`,
  `Positioning`, `Location`, `WebSockets`, and `Concurrent` modules (Qt Location
  ships the `osm` map plugin; the *Auto* theme follows the desktop colour scheme,
  and the reduced-motion setting persists via `QtCore`'s `Settings`).
- **CMake ≥ 3.21**, **Ninja**, and a C++17 compiler (MSVC 2022 or the bundled
  llvm-mingw / MinGW on Windows; GCC or Clang on Linux/macOS).

> Qt Location is an optional component in the Qt installer — make sure
> **Qt Positioning** and **Qt Location** are ticked for your kit.
>
> The live feed uses MQTT over a *secure* WebSocket and needs no `QtMqtt` module
> (it's a small built-in client in [`src/MqttCodec.h`](src/MqttCodec.h) +
> `DigitrafficMqttClient`). If the `wss://` connection fails with a TLS error,
> ensure a Qt TLS backend is available (Schannel on Windows, or OpenSSL DLLs on
> the `PATH`); the app still shows the REST seed snapshot regardless.

## Build & run

The toolchain per platform is pinned in [`CMakePresets.json`](CMakePresets.json)
— **Clang** on Linux/macOS, and **MSVC 2022** or **LLVM/Clang** on Windows — and
each preset builds into its own folder under `build/` with the executable in a
`bin/` subfolder.
Point Qt at your kit by exporting `CMAKE_PREFIX_PATH` once (a system Qt is found
automatically and needs no export).

On Windows the **`windows-llvm`** preset (Qt's bundled llvm-mingw Clang + `lld`,
Release) is the recommended default; `windows-msvc` (VS 2022) is also supported.
The `windows-llvm` and `windows-msvc` presets build **Release**; the Linux/macOS
presets build **Debug** (override with `-DCMAKE_BUILD_TYPE=…` if needed).

### Linux (Clang)

```bash
export CMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/gcc_64"   # ABI-compatible with Clang
cmake --preset linux-clang
cmake --build --preset linux-clang
./build/linux-clang/bin/TrainsOnMap
```

### macOS (Clang)

```bash
export CMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/macos"   # or /opt/homebrew for a Homebrew Qt
cmake --preset macos-clang
cmake --build --preset macos-clang
open ./build/macos-clang/bin/TrainsOnMap.app
```

> The build produces a relocatable **`TrainsOnMap.app`** and runs **macdeployqt**
> automatically, copying the Qt frameworks and the needed plugins (the QML imports,
> the QtLocation geoservices / QtPositioning plugins, and the TLS backend for the
> `wss://` MQTT feed) into the bundle, so it runs without a Qt install. The icon
> (`resources/icon/appicon.icns`, regenerated from the SVG master with
> [`scripts/make_icns.sh`](scripts/make_icns.sh)) and `Info.plist` metadata are set
> from `CMakeLists.txt`.
>
> To package a distributable disk image, build the **`dmg`** target — it wraps the
> deployed bundle into `TrainsOnMap-<version>.dmg` with the drag-to-`/Applications`
> layout:
>
> ```bash
> cmake --build --preset macos-clang --target dmg
> # -> build/macos-clang/bin/TrainsOnMap-0.1.0.dmg
> ```
>
> macdeployqt ad-hoc signs the bundle. For distribution to other Macs, sign with a
> Developer ID and notarize (`-codesign=<id>` on the macdeployqt call, then
> `xcrun notarytool`); a Homebrew Qt may also need `-codesign` to satisfy strict
> Gatekeeper checks.

### Windows (MSVC 2022, PowerShell)

```powershell
$env:CMAKE_PREFIX_PATH = "C:/Qt/6.11.1/msvc2022_64"
cmake --preset windows-msvc
cmake --build --preset windows-msvc        # Release
./build/windows-msvc/bin/Release/TrainsOnMap.exe
```

### Windows (LLVM/Clang, PowerShell)

Uses Qt's bundled **llvm-mingw** toolchain (Clang + `lld`). Put its `bin` on
`PATH` and point `CMAKE_PREFIX_PATH` at the ABI-matching `llvm-mingw_64` kit —
**not** the `msvc2022_64` kit.

```powershell
$env:PATH = "C:/Qt/Tools/llvm-mingw1706_64/bin;$env:PATH"
$env:CMAKE_PREFIX_PATH = "C:/Qt/6.11.1/llvm-mingw_64"
cmake --preset windows-llvm
cmake --build --preset windows-llvm        # Release
./build/windows-llvm/bin/TrainsOnMap.exe
```

> ✅ Verified on Windows 11 with Qt 6.11.1 (2026-06-15): the MSVC preset
> (VS 2022 Build Tools v17.14), the LLVM/Clang preset (llvm-mingw Clang 17 +
> `lld` + Ninja), and a MinGW kit (Qt's bundled GCC 13 + Ninja) all configure
> and build clean.

> The build runs **windeployqt** automatically, copying the Qt DLLs and the
> needed plugins (the `windows` platform plugin, the TLS backend for the `wss://`
> MQTT feed, and the QML / QtLocation geoservices / QtPositioning plugins) next
> to the `.exe`, so it runs without a Qt install on `PATH`. (Plugins land in
> subfolders; the core `Qt6*.dll` sit beside the `.exe` because the Windows loader
> resolves implicitly-linked DLLs from the application directory.)
>
> **Release builds are GUI apps** — no console window on launch. **Debug** builds
> keep a console attached for `qDebug` / log output (`WIN32_EXECUTABLE` is enabled
> only for non-Debug configurations).

### Qt Creator

Open `CMakeLists.txt` (or the presets) as a project, pick a Qt 6.5+ kit with the
matching compiler, and Run.

## Configuration knobs

- **Live stream:** `DigitrafficMqttClient { active: true }` in `Main.qml` — set
  `false` to disable MQTT and rely on the REST seed (or wire up polling).
- **Appearance & basemap:** the **Auto / Light / Dark** toggle in the sidebar sets
  `Theme.mode`, which resolves `Theme.isDark`. `Theme.basemapStyle`
  (`light_all` / `dark_all`) and every overlay colour follow it — all defined in
  `Theme.qml`; edit the CARTO style strings or palette tokens there.
- **Reduced motion:** `Theme.reducedMotion` (persisted via `QtCore.Settings`,
  category `Appearance`) — when `true`, non-essential animation (the LIVE pulse)
  is skipped. Type scale lives in `TypeScale.qml`.
- **Train colours:** the type/category→colour map is in `TrainMarker.qml`
  (`colorFor()`); tweak the hex values or add train types there.
- **Marker labels:** `labelsVisible: map.zoomLevel >= 8.0` on the `TrainMarker`
  delegate in `Main.qml` — below this zoom only dots are drawn, to avoid clutter.
- **Start region:** the `mapLoader` `savedCenterLat` / `savedCenterLon` /
  `savedZoom` properties in `Main.qml` (the view is restored from these and
  preserved across a theme-driven map reload).
- **Rail geometry:** shipped pre-baked and offline in `resources/rails.geojson.qz`
  (no network fetch). Re-generate with `python3 scripts/bake_rails.py` when the
  rail topology changes; `TrackService` filters it to the viewport in memory.

## Ideas for next steps

- Station departure board (click a station, not just a train, to see everything
  passing through it).
- Favourite/pinned trains, persisted via `QtCore.Settings`.
- "Nearest trains to me" using the already-linked `QtPositioning` module.
- Rail-weather overlay (Digitraffic also publishes track condition data).
- On-time / punctuality stats badge per train type.

See `CHANGES.md`'s **Known issues / follow-ups** for the full rationale on
why each of these is deferred rather than done.
