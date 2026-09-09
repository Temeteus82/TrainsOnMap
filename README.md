# TrainsOnMap

A Qt6 desktop app that shows **live Finnish train positions** and **railway track
geometry** on an interactive map, using the open
[Digitraffic railway API](https://www.digitraffic.fi/rautatieliikenne/) from Fintraffic.

Live trains appear out of the box. The app supports light/dark/auto theming and a
keyboard-accessible UI, with a clean split between a C++ networking/model backend
and a QML map front-end, plus clearly marked extension points.

## Features

- **Muted base layer with light/dark theming** — Esri *Light Gray Canvas* /
  *Dark Gray Canvas* tiles via the Qt Location `osm` plugin, pointed at an
  embedded single-provider repository, so the coloured trains and rails stay the
  focus. No API key or account is needed.
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
├─ TrainFilterModel    (C++) QSortFilterProxyModel over TrainListModel: category
│                            toggles + free-text search, sorted by train number.
│                            Backs the train list (the keyboard/AT path to selection)
│
└─ qml/
    ├─ Main.qml             Map + MapItemView layers + InfoPanel + TrainDetailPanel
    ├─ TrainMarker.qml      Clickable MapQuickItem delegate for one train
    ├─ InfoPanel.qml        Floating status/controls card (incl. Appearance toggle)
    ├─ TrainListPanel.qml   Searchable, keyboard-operable list of the live fleet
    ├─ TrainDetailPanel.qml Slide-in timetable for the selected train
    ├─ StationBoardPanel.qml Departure board for a clicked station
    ├─ ToggleRow.qml        Shared labelled checkbox, focusable with a focus ring
    ├─ Theme.qml            Singleton: light/dark/auto palette (incl. the train
    │                       marker palette + its dark branch) + map basemap style
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

Every REST request — and the MQTT WebSocket handshake — sends a `Digitraffic-User`
header identifying this client, as
[Digitraffic asks](https://www.digitraffic.fi/ohjeet/). It comes from one shared
constant, `digitraffic::kUserAgent` in [`src/DigitrafficFormat.h`](src/DigitrafficFormat.h);
change it there to your own app id.

Digitraffic also asks for gzip, but **don't set `Accept-Encoding` by hand** — Qt 6
advertises it and inflates the response transparently, and a manual header turns
that off, so every reply then parses as raw gzip and fails silently. (That was a
real bug here: marker metadata never applied. See `CHANGES.md`.)

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
export CMAKE_PREFIX_PATH="$HOME/Qt/6.11.2/gcc_64"   # ABI-compatible with Clang
cmake --preset linux-clang
cmake --build --preset linux-clang
./build/linux-clang/bin/TrainsOnMap
```

### macOS (Clang)

The **`macos-clang-qt611`** preset is pinned to the stable Qt 6.11.2 kit and needs
no `CMAKE_PREFIX_PATH` export; `macos-clang-qt612` is the same but pinned to the
6.12 beta kit, for checking the app against it. Edit the pinned path in
[`CMakePresets.json`](CMakePresets.json) if your kit lives somewhere else.

```bash
cmake --preset macos-clang-qt611
cmake --build --preset macos-clang-qt611
open ./build/macos-clang-qt611/bin/TrainsOnMap.app
```

The plain **`macos-clang`** preset works with any Qt kit (official or a Homebrew
one) via `CMAKE_PREFIX_PATH`:

```bash
export CMAKE_PREFIX_PATH="$HOME/Qt/6.11.2/macos"   # or /opt/homebrew for a Homebrew Qt
cmake --preset macos-clang
cmake --build --preset macos-clang
open ./build/macos-clang/bin/TrainsOnMap.app
```

> The official Qt installer doesn't tick **Positioning**, **Location**,
> **WebSockets**, or **SerialPort** by default for the macOS kit. The first
> three are required directly; `SerialPort` is pulled in because macdeployqt
> bundles the Qt Positioning `nmea` plugin, which links against it — without it,
> the build still succeeds but the deployed `.app` is missing
> `QtSerialPort.framework` and the plugin's rpath can't resolve. Add all four
> via the Qt Maintenance Tool (or re-run the online installer and tick them) if
> configure fails on `Could NOT find Qt6Positioning` or similar.

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
> After macdeployqt, [`scripts/seal_bundle_rpaths.sh`](scripts/seal_bundle_rpaths.sh)
> strips any **absolute rpath** left on the bundled binaries and re-signs them.
> macdeployqt rewrites dependency references into the bundle but leaves the
> linker's rpath for the build machine's Qt (e.g. `/opt/homebrew/lib`) in place.
> Several Qt frameworks still name their siblings as bare
> `@rpath/QtCore.framework/…`, and dyld consults the *main executable's* rpaths to
> resolve those — so that leftover entry is a live route out of the bundle, and a
> second copy of QtCore/QtGui is loaded from the build machine's Qt:
>
> ```
> objc[…]: Class QT_ROOT_LEVEL_POOL__… is implemented in both
>   …/TrainsOnMap.app/Contents/Frameworks/QtCore.framework/…/QtCore and
>   /opt/homebrew/Cellar/qtbase/6.11.1/lib/QtCore.framework/…/QtCore.
>   This may cause spurious casting failures and mysterious crashes.
> ```
>
> This only reproduces on a machine that still has that Qt installed, which is
> what makes it easy to ship. The step must run **after** macdeployqt, which uses
> those same rpaths while resolving what to copy.
>
> macdeployqt ad-hoc signs the bundle, and the sealing step re-signs what it
> rewrites. For distribution to other Macs, sign with a Developer ID and notarize
> (`-codesign=<id>` on the macdeployqt call, then `xcrun notarytool`) — re-sign
> with the real identity *after* the sealing step; a Homebrew Qt may also need
> `-codesign` to satisfy strict Gatekeeper checks.

### Windows (MSVC 2022, PowerShell)

```powershell
$env:CMAKE_PREFIX_PATH = "C:/Qt/6.11.2/msvc2022_64"
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
$env:CMAKE_PREFIX_PATH = "C:/Qt/6.11.2/llvm-mingw_64"
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
  `Theme.mode`, which resolves `Theme.isDark`. `Theme.basemapRepo` (the embedded
  provider manifest) and every overlay colour follow it — all defined in
  `Theme.qml`. To change tile source, edit the `UrlTemplate` in
  `resources/basemap/{light,dark}/street`; for colours, edit the palette tokens.

  > The `osm` plugin's simpler `osm.mapping.custom.host` is deliberately **not**
  > used: Qt builds tile URLs by concatenating `%z/%x/%y.png` onto the host
  > string, which cannot express Esri's `z/y/x` order, its extensionless paths,
  > or any trailing query string (a `?key=` would land mid-path). The providers
  > repository takes a full `UrlTemplate` and has none of those limits. Its
  > manifest is embedded via `qrc:`, so no file is written at runtime and no
  > repository is fetched over the network.
- **Reduced motion:** `Theme.reducedMotion` (persisted via `QtCore.Settings`,
  category `Appearance`) — when `true`, non-essential animation (the LIVE pulse)
  is skipped. Type scale lives in `TypeScale.qml`: the plain roles
  (`caption`/`body`/…) size map-space text, and a `panel*` family at 0.75×
  sizes the overlay panels and train marker badges.
- **Train colours:** the type/category→colour map is in `TrainMarker.qml`
  (`colorFor()`); tweak the hex values or add train types there.
- **Marker labels:** `labelsVisible` on the `TrainMarker` delegate in `Main.qml`
  drops to dots-only below zoom 8, **and** whenever another train sits within
  ~30 px on screen (`nearestNeighborMeters` vs. `map.metersPerPixel()`) — the
  latter declutters a busy terminus like Helsinki even when zoomed in enough
  for labels elsewhere. Marker text renders via `Text.NativeRendering` (crisper
  than the GPU distance-field default at this small a size).
- **Start region:** the `mapLoader` `savedCenterLat` / `savedCenterLon` /
  `savedZoom` properties in `Main.qml` (the view is restored from these and
  preserved across a theme-driven map reload).
- **Rail geometry:** shipped pre-baked and offline in `resources/rails.geojson.qz`
  (no network fetch). Re-generate with `python3 scripts/bake_rails.py` when the
  rail topology changes; `TrackService` filters it to the viewport in memory.

## Ideas for next steps

The open work is **UI/accessibility**, not features. The round-2 audit
([`docs/ui-audit-round2.md`](docs/ui-audit-round2.md)) is the source of truth and
carries the measured contrast ratios, the WCAG references, and a suggested order.
**Both Criticals are now closed**, along with 9 of the other findings:

- **U2-C1 / U2-O1** — the train list. Selecting a train is what this app is
  *for*, and on the map it is pointer-only: an unlabelled `MapQuickItem` is
  nothing at all to a screen reader (WCAG 2.1.1 / 4.1.2, Level A). The sidebar
  now carries a searchable, keyboard-operable list of the live fleet — Tab to
  the search box, arrows through the rows, Enter to open — with each row exposed
  as a button named like the marker reads ("IC 967, 120 km/h, 5 minutes late").
  Search matches number, type or line letter, so what you read off a marker is
  what you can type.
- **U2-C4 / U2-W8** — the marker palette got a dark-mode branch, and every
  hardcoded colour moved into `Theme`. Cargo navy measured **1.04:1** on the
  then-current dark basemap — invisible at the zoom levels where the capsule is
  a bare dot.
  The dark values are hand-tuned rather than luminance-lifted, because lifting
  each hue independently collapses `S` onto `HL` and `T` onto `PYO`; the light
  branch keeps juliadata.fi parity untouched.

**Still open:** U2-W3/W4 (focus management — now the cheapest next step, since
the list gives the sidebar a real focus chain), U2-W2 (type scale), U2-W6
(localisation), U2-W9/W10 (first-run view and sidebar density — W10 got *worse*
with the list added), and U2-O2/O4.

**Features** are a different story — nothing is queued. Favourite/pinned trains
and "nearest trains to me" were both considered and declined; a `/trains/{date}`
summary cache is deferred only because `/live-trains` already covers the same
need. Recently shipped: a **station departure board** (click a station dot),
**punctuality stats** (on-time % per category, aggregated from `/live-trains`),
an optional **weather overlay** (FMI open-data observations), and a round of UI
polish — OS-scalable panel text, density-aware marker-label declutter near busy
termini, keyboard map panning, and fade transitions on the side panels.

`CHANGES.md`'s **Known issues / follow-ups** has the rationale for the declined
and deferred items; `docs/track-accuracy-tier2-plan.md` step 5 still wants a
human pass over parallel-track/junction/platform matching on the live map.
