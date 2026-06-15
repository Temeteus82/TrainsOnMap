# TrainsOnMap

A Qt6 desktop app that shows **live Finnish train positions** and **railway track
geometry** on an interactive map, using the open
[Digitraffic railway API](https://www.digitraffic.fi/rautatieliikenne/) from Fintraffic.

This repository is **scaffolding**: it compiles into a working app (live trains
appear out of the box), with clearly marked extension points.

## Features

- **Light OpenStreetMap base layer** — CARTO *Positron* tiles (OSM data, light
  minimal cartography like juliadata.fi's map) via the Qt Location `osm` plugin's
  custom-host mechanism, so the coloured trains and rails are the focus. One-line
  restyle (`dark_all`, Voyager, …) via the `basemapStyle` property in `Main.qml`.
- **Live train markers** streamed over **MQTT** (push, real-time), seeded by one
  REST snapshot at startup; **coloured by train type** like juliadata.fi
  (S/Pendolino = green, IC = red, PYO/night = navy, commuter = green,
  cargo = navy, other = grey) and rotated to heading. A status dot shows the
  live connection.
- **Click a train** to open a timetable panel: stops, scheduled vs. estimated
  arrival/departure times, live delay, and track, with station codes resolved to
  names from the metadata API. While open, the panel **updates live** from the
  MQTT `trains/#` topic (scoped to the selected train).
- **Track geometry** drawn automatically for the current viewport (once zoomed
  in past the threshold), fetched from the Digitraffic infra-api GeoJSON and
  reprojected from EPSG:3067 to WGS84.
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
├─ TrackService        (C++) Fetches raiteet.geojson (whole net or bbox) → TrackListModel
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
    ├─ InfoPanel.qml        Floating status/controls card
    └─ TrainDetailPanel.qml Slide-in timetable for the selected train
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
| Track geometry | `GET https://rata.digitraffic.fi/infra-api/latest/raiteet.geojson?bbox=…` | GeoJSON FeatureCollection of LineString/MultiLineString in **EPSG:3067**. The full network is 10+ MB, so `loadForBounds()` sends a `bbox` (in EPSG:3067) for the viewport and features are reprojected to WGS84. |

All requests send a `Digitraffic-User` header (set it to your own app id in
`DigitrafficClient.cpp` / `TrackService.cpp`) and `Accept-Encoding: gzip` as
[recommended by Digitraffic](https://www.digitraffic.fi/ohjeet/).

### Coordinate reference system

The infra-api returns track geometry in **EPSG:3067 (ETRS-TM35FIN)** — projected
metres, *not* WGS84 — and interprets the `bbox` parameter in that same CRS. So
the app:

- forward-projects the WGS84 viewport corners to EPSG:3067 to build the `bbox`, and
- inverse-projects every returned coordinate back to WGS84 for the map.

Both transforms live in [`src/Projection.h`](src/Projection.h) (Snyder
Transverse Mercator, sub-metre accurate; ETRS89 ≈ WGS84 here). Train positions,
by contrast, already come as WGS84 GeoJSON Points and need no transform.

## Prerequisites

- **Qt 6.5 or newer** with the `Quick`, `Qml`, `Network`, `Positioning`,
  `Location`, and `WebSockets` modules (Qt Location ships the `osm` map plugin).
- **CMake ≥ 3.21**, **Ninja**, and a C++17 compiler (MSVC 2019+ or MinGW on
  Windows; GCC or Clang on Linux/macOS).

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

### Linux (Clang)

```bash
export CMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/gcc_64"   # ABI-compatible with Clang
cmake --preset linux-clang
cmake --build --preset linux-clang
./build/linux-clang/bin/TrainsOnMap
```

### macOS (Clang)

```bash
export CMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/macos"
cmake --preset macos-clang
cmake --build --preset macos-clang
./build/macos-clang/bin/TrainsOnMap
```

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
> to the `.exe`, so it runs without a Qt install on `PATH`.

### Qt Creator

Open `CMakeLists.txt` (or the presets) as a project, pick a Qt 6.5+ kit with the
matching compiler, and Run.

## Configuration knobs

- **Live stream:** `DigitrafficMqttClient { active: true }` in `Main.qml` — set
  `false` to disable MQTT and rely on the REST seed (or wire up polling).
- **Basemap style:** `basemapStyle` in `Main.qml` — `"light_all"` (default),
  `"dark_all"`, `"light_nolabels"`, or `"rastertiles/voyager"` (CARTO styles).
- **Train colours:** the type/category→colour map is in `TrainMarker.qml`
  (`colorFor()`); tweak the hex values or add train types there.
- **Start region:** `Map { center; zoomLevel }` in `Main.qml`.
- **Track endpoint:** `TrackService { endpoint: "…" }` — point at a different
  infra-api version or a self-hosted GeoJSON.
- **Track auto-load zoom:** `trackZoomThreshold` in `Main.qml` — below this zoom
  the viewport covers too much of the network to fetch, so loading is skipped.

## Ideas for next steps

- Smoothly animate marker movement between updates.
- Periodically re-seed from REST to prune trains that stopped reporting.
- Cache/throttle track loads; style tracks by line category.
- Highlight the selected train's route on the map from its timetable stops.
- Declutter overlapping number labels at low zoom (hide labels below a zoom, or
  cluster nearby trains).
