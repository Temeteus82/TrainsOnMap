# TrainsOnMap

A Qt6 desktop app that shows **live Finnish train positions** and **railway track
geometry** on an interactive map, using the open
[Digitraffic railway API](https://www.digitraffic.fi/rautatieliikenne/) from Fintraffic.

This repository is **scaffolding**: it compiles into a working app (live trains
appear out of the box), with clearly marked extension points.

## Features

- OpenStreetMap base layer via **Qt Location** (`osm` plugin).
- **Live train markers** streamed over **MQTT** (push, real-time), seeded by one
  REST snapshot at startup; coloured by moving/stopped and rotated to heading
  (derived from successive positions). A status dot shows the live connection.
- **Click a train** to open a timetable panel: stops, scheduled vs. estimated
  arrival/departure times, live delay, and track, with station codes resolved to
  names from the metadata API. While open, the panel **updates live** from the
  MQTT `trains/#` topic (scoped to the selected train).
- **Track geometry** drawn automatically for the current viewport (once zoomed
  in past the threshold), fetched from the Digitraffic infra-api GeoJSON and
  reprojected from EPSG:3067 to WGS84.
- Clean split between a **C++ networking/model backend** and a **QML map UI**.

## Architecture

```
main.cpp                    Bootstraps the QML engine, loads TrainsOnMap/Main.qml
│
├─ DigitrafficClient   (C++) One REST train-locations/latest snapshot → TrainListModel
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
- **CMake ≥ 3.21** and a C++17 compiler (MSVC 2019+ or MinGW on Windows).

> Qt Location is an optional component in the Qt installer — make sure
> **Qt Positioning** and **Qt Location** are ticked for your kit.
>
> The live feed uses MQTT over a *secure* WebSocket and needs no `QtMqtt` module
> (it's a small built-in client in [`src/MqttCodec.h`](src/MqttCodec.h) +
> `DigitrafficMqttClient`). If the `wss://` connection fails with a TLS error,
> ensure a Qt TLS backend is available (Schannel on Windows, or OpenSSL DLLs on
> the `PATH`); the app still shows the REST seed snapshot regardless.

## Build & run

### Command line (Windows, PowerShell)

```powershell
# Point CMake at your Qt kit (adjust path/compiler to your install)
cmake -S . -B build -G "Ninja" `
    -DCMAKE_PREFIX_PATH="C:/Qt/6.7.2/msvc2019_64"
cmake --build build
./build/TrainsOnMap.exe
```

### Qt Creator

Open `CMakeLists.txt` as a project, pick a Qt 6.5+ kit, and Run.

## Configuration knobs

- **Live stream:** `DigitrafficMqttClient { active: true }` in `Main.qml` — set
  `false` to disable MQTT and rely on the REST seed (or wire up polling).
- **Start region:** `Map { center; zoomLevel }` in `Main.qml`.
- **Track endpoint:** `TrackService { endpoint: "…" }` — point at a different
  infra-api version or a self-hosted GeoJSON.
- **Track auto-load zoom:** `trackZoomThreshold` in `Main.qml` — below this zoom
  the viewport covers too much of the network to fetch, so loading is skipped.

## Ideas for next steps

- Subscribe to the MQTT `trains/#` topic too, to push live timetable/delay
  updates into the open detail panel (no re-fetch on estimate changes).
- Smoothly animate marker movement between updates.
- Periodically re-seed from REST to prune trains that stopped reporting.
- Cache/throttle track loads; style tracks by line category.
- Highlight the selected train's route on the map from its timetable stops.
