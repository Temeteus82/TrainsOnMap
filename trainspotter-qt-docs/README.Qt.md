# TrainSpotter — Qt 6.11 port docs

Index for building a cross-platform (Windows / Linux / macOS) **Qt 6.11 + Qt Quick**
TrainSpotter that reuses the data logic of the shipping Swift/Apple app. Each document
below owns one concern; this page points you at the right one and shows how they fit
together.

> ## ⚠️ Historical — the port was built, and it diverged
>
> **These are the pre-build design notes. The app they planned is this repo,
> TrainsOnMap, and it is shipping.** Read these for the Swift→Qt reasoning and the
> Qt Location tile research; do **not** treat them as instructions for the current
> code. Where they disagree with `src/`, `src/` wins.
>
> For how the app is actually built: **[`src/doc/index.md`](../src/doc/index.md)**
> (one reference page per C++ class) and the repo [`README.md`](../README.md).
>
> ### Where the shipping app deliberately diverged
>
> | These docs prescribe | TrainsOnMap ships | Why |
> |---|---|---|
> | `QMqttClient` (Qt MQTT module) | Hand-rolled MQTT 3.1.1 in [`src/MqttCodec.h`](../src/MqttCodec.h) over a plain `QWebSocket` | Qt MQTT is a separate installable component, absent from a default Qt install. The framing/reassembly these docs call "gone" is [`DigitrafficMqttClient`](../src/doc/DigitrafficMqttClient.md)'s job again. |
> | `/trains/{date}` summary cache **+** `/live-trains` fallback | `/live-trains` only, polled with `?version=` deltas | One source covers currently-running trains regardless of departure date, so the cross-midnight fallback these docs build has nothing to fall back *from*. The summary cache was declined as redundant. |
> | Rings: amber 1–5 min, red > 5 min | Amber ≥ 5, red ≥ 15, **scheduled passenger trains only** | Refined against live data — the original thresholds lit up most of the map. |
> | `fillColorRole` / `badgeLabelRole` computed in C++ | `colorFor()` / `labelFor()` in [`qml/TrainMarker.qml`](../qml/TrainMarker.qml) | Two profiler runs measured the marker bindings at ~0.005 ms/frame and explicitly declined moving them to C++, which would couple the model to the UI palette for no gain. |
> | `QSortFilterProxyModel` over the train model for category filters | Delegate binds `visible` to `InfoPanel.categoryVisible()` | The one proxy in the tree, `TimetableFilterModel`, filters *timetable rows*, not trains. |
> | Upsert key `"{trainNumber}-{departureDate}"` string | A `TrainKey` struct (+ `qHash`) | Same identity, no string formatting on a hot path. |
> | `RailGeometry.bin` (`TSRG`) from `build_rail_geometry.py` | `resources/rails.geojson.qz` (`qCompress`) from [`scripts/bake_rails.py`](../scripts/bake_rails.py) | Schema v2 carries track identity, topology and a station crosswalk for route-constrained matching — see [`docs/track-accuracy-tier2-plan.md`](../docs/track-accuracy-tier2-plan.md). |
> | A descriptive `User-Agent` | A `Digitraffic-User` header from one shared constant, and **never** a hand-set `Accept-Encoding` | Setting `Accept-Encoding` by hand disables Qt's transparent gzip inflation; every reply then parses as raw gzip and fails silently. |
>
> What ported unchanged: the REST-bootstrap-then-MQTT-deltas shape, the single
> `apply()` funnel with its timestamp guard, merge-don't-replace with the 120 s
> prune grace, `QGeoCoordinate::azimuthTo()` for bearing, `QAbstractListModel` as
> the diffing layer, QML delegates instead of rasterised annotations, and the
> juliadata.fi palette (including the `#30B0C7` commuter fallback these docs
> picked).

## The documents

| Read this | For | Depends on |
|---|---|---|
| **[DataFlow.Qt.md](DataFlow.Qt.md)** | The full architecture: every layer translated Swift→Qt (modules, `QObject`/`Q_PROPERTY`, `QMqttClient`, Qt Location map, threading), plus a side-by-side mapping table. Start here for the big picture. | — |
| **[TrainDataWiring.Qt.md](TrainDataWiring.Qt.md)** | A focused how-to for wiring the **live position feed** (REST bootstrap + MQTT deltas + 60 s resync) into a `QAbstractListModel`. The `apply()`/merge/prune rules that must match Swift exactly. | DataFlow.Qt.md |
| **[MarkerColors.Qt.md](MarkerColors.Qt.md)** | The **marker colour & label** scheme: the juliadata.fi palette + `(trainType, category, speed)` resolver, the badge-label format rules (line letter / `"TYPE NUMBER"`), where `trainType` comes from (and the cross-midnight fallback), status rings, and how to expose `fillColorRole` / `badgeLabelRole`. | TrainDataWiring.Qt.md |

### Canonical Swift references (source of truth for the *Swift* app)

These describe the separate shipping Swift/Apple app the port copied from — not
this repo. They remain accurate about *that* codebase, so consult them when a Qt
doc says "same as Swift" or for detail not repeated on the Qt side; just don't read
them as describing TrainsOnMap (its status rings, for one, use different
thresholds — see the divergence table above):

| Document | For |
|---|---|
| [DataFlow.md](DataFlow.md) | The Swift app's data flow — the original of `DataFlow.Qt.md`. |
| [MarkerColors.md](MarkerColors.md) | Canonical palette **provenance** + the "refresh the hex from the juliadata.fi DOM" procedure. `MarkerColors.Qt.md` deliberately doesn't repeat these. |

## Suggested reading order

1. **[DataFlow.Qt.md](DataFlow.Qt.md)** — orient on the whole architecture and the
   Swift→Qt mapping.
2. **[TrainDataWiring.Qt.md](TrainDataWiring.Qt.md)** — wire positions into the model
   (the minimum to see trains on the map).
3. **[MarkerColors.Qt.md](MarkerColors.Qt.md)** — add type-coloured, labelled badges
   (this is what pulls in the `/trains/{date}` + `/live-trains` endpoints).

## Data sources at a glance

| Source | Feeds | Covered in |
|---|---|---|
| `GET /train-locations/latest/` | Position bootstrap + 60 s resync/prune | TrainDataWiring.Qt.md |
| MQTT `train-locations/#` (`wss://rata.digitraffic.fi/mqtt`) | Live position deltas | TrainDataWiring.Qt.md |
| `GET /trains/{date}` | Type/category cache (badge colour + label) | MarkerColors.Qt.md |
| `GET /live-trains` | Type fallback (cross-midnight) + delay/status rings | MarkerColors.Qt.md |

All free, no API key. Base REST URL `https://rata.digitraffic.fi/api/v1`.

## The "same-as-Swift" contract

Wherever the Qt code reproduces Swift logic, these invariants are what make the two
behave identically — keep them intact (details in TrainDataWiring.Qt.md § 5):

1. REST bootstrap **before** relying on MQTT (the firehose is deltas-only).
2. One `apply()` funnel for every update, REST or MQTT.
3. Timestamp guard on upsert (stops the lagging resync reversing bearings).
4. Merge-don't-replace on resync; 120 s prune grace.
5. Colour resolves `trainType` as `summary ?? liveStatus` so cross-midnight cargo and
   night trains aren't mis-coloured.

---

*Required Qt modules, CMake, and the per-layer Swift→Qt mapping table live at the top
of [DataFlow.Qt.md](DataFlow.Qt.md).*
