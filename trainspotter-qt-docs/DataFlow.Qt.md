# TrainSpotter (Qt) — Data Fetching & Handling

> ## ⚠️ Historical design note — superseded by the built app
>
> This planned the port. **The port is built: it is this repo, TrainsOnMap.**
> Several prescriptions below were deliberately not followed — most importantly
> the MQTT transport (§2 assumes the Qt MQTT module; the app hand-rolls MQTT in
> [`src/MqttCodec.h`](../src/MqttCodec.h)) and the type-cache design (§ Startup
> assumes `/trains/{date}` + a `/live-trains` fallback; the app uses `/live-trains`
> alone). **[`README.Qt.md`](README.Qt.md) has the full divergence table**; the
> current code is documented in [`src/doc/index.md`](../src/doc/index.md).
>
> Kept for the Swift→Qt mapping table, the Qt Location tile-plugin research, and
> the reasoning behind the `apply()`/merge/prune rules — all of which did ship.
>
> **Original scope note.** A port reference for a planned *separate* cross-platform
> (Windows + Linux, also macOS) TrainSpotter built on **Qt 6.11 / Qt Quick**. It
> mirrors [`DataFlow.md`](DataFlow.md) (the shipping Swift/Apple app) but translates
> every layer to Qt idioms. Design guidance, not a description of existing code —
> file/class names below are *proposed*.

How live train positions (and supporting metadata) move from the Fintraffic
Digitraffic API into the app's state and onto the map. All data comes from the free
[Digitraffic API](https://www.digitraffic.fi/rautatieliikenne/) — no API key. The
goal is the same **"only Qt modules, no third-party packages"** discipline the Swift
app keeps with Apple frameworks: everything here ships in Qt 6.11.

## Required Qt modules

```cmake
find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Qml Quick           # app + QML UI
    Network                      # REST (QNetworkAccessManager)
    WebSockets                   # MQTT-over-WSS transport
    Mqtt                         # QMqttClient (Qt MQTT)
    Positioning                  # device location
    Location                     # Map, MapPolyline, MapQuickItem (Qt Location)
)
target_link_libraries(trainspotter PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
    Qt6::Network Qt6::WebSockets Qt6::Mqtt
    Qt6::Positioning Qt6::Location)
```

> Qt MQTT and Qt Location are **separate installable components** in the Qt online
> installer (not part of the default Qt install). Qt Location was reintroduced in
> 6.5 and is fully supported in 6.11; Qt MQTT gained **native WebSocket support in
> 6.10** — both are central below.

> **⚠️ Not what shipped — don't copy this block.** `Qt6::Mqtt` is exactly the
> dependency TrainsOnMap avoided (see §2). Its real list is
> `find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Qml Quick Network Positioning
> Location WebSockets Concurrent Test)` — no `Mqtt`; `Concurrent` for the
> off-thread rail-geometry parse and route precompute, `Test` for the CTest
> targets. See [`CMakeLists.txt`](../CMakeLists.txt).

## How it maps from the Swift app

| Swift / Apple | Qt 6.11 | Notes |
|---|---|---|
| `@Observable @MainActor final class TrainStore` | `QObject` subclass on the GUI thread with `Q_PROPERTY(... NOTIFY ...)`; trains as a `QAbstractListModel` | QML binds to properties/model; signals replace `@Observable` change tracking |
| `@MainActor` isolation | Qt main (GUI) thread + event loop | Network/MQTT callbacks fire on the main thread → no locking, same as `@MainActor` |
| `async throws` service methods | `QNetworkAccessManager::get()` → `QNetworkReply::finished` (signals/slots) | Optionally C++20 coroutines, but signals/slots keep it dependency-free |
| Hand-rolled `MQTTClient.swift` (~250 lines) | **`QMqttClient`** (Qt MQTT) | **Deleted entirely** — Qt frames MQTT 3.1.1 for you, incl. WSS |
| `URLSessionWebSocketTask` (`mqtt` subprotocol) | `QWebSocket` + `QWebSocketHandshakeOptions::setSubprotocols({"mqtt"})` | Handed to `QMqttClient::connectToHostWebSocketEncrypted()` |
| `MKMapView` + `CachedTileOverlay` (CARTO) | Qt Location `Map` + `osm` plugin custom tiles (or a custom geoservices plugin) | See [§ Map & tiles](#5-map--tiles) — retina/subdomains need care |
| `MKMultiPolyline` rail overlay | `MapPolyline` items via `MapItemView` | One delegate per line; same pre-baked `RailGeometry.bin` |
| `TrainMarkerView` rasterised via `ImageRenderer` → `MKAnnotationView.image` | `MapQuickItem` with a QML `sourceItem` delegate | **No rasterisation** — QML draws the badge live |
| Arrow `CAShapeLayer` + shortest-arc `CABasicAnimation` | QML arrow + `Behavior on rotation { RotationAnimation { direction: RotationAnimation.Shortest } }` | Shortest-arc is built in |
| `TrainLocation.bearing()` (haversine) | `QGeoCoordinate::azimuthTo()` | Built-in; 0 = north, clockwise |
| `CLLocationManager` / `LocationService` | `QGeoPositionInfoSource` (C++) or `PositionSource` (QML) | Qt Positioning |
| `JSONDecoder` + custom ISO8601 strategy | `QJsonDocument` + `QDateTime::fromString(s, Qt::ISODateWithMs)` w/ fallback | Same two-format fallback |
| `Codable` structs | plain C++ value structs + a `fromJson(QJsonObject)` factory | |

## At a glance

```
            ┌────────────────────── DigitrafficService (QObject) ──────────────────────┐
            │  REST via QNetworkAccessManager:                                          │
            │  /train-locations/latest/   /trains/{date}   /metadata/*   /live-trains   │
            └───────────────────────────────────────────────────────────────────────-──┘
                     ▲                                   ▲
     one snapshot +  │ finished() signals               │ on-demand (train tapped)
     60 s resync     │                                   │
            ┌────────┴────────────┐            ┌─────────┴──────────┐
 MQTT msgs  │     TrainStore      │            │  TrainDetails /    │
 ┌─────────▶│  QObject (GUI thr.) │            │  Composition       │
 │ apply()  │  Q_PROPERTY + NOTIFY │            └────────────────────┘
 │          │  TrainListModel     │  ── filtered model ──▶  Map { MapItemView → MapQuickItem }
┌┴─────────┐│  (QAbstractListModel)│  ── azimuthTo()    ──▶  arrow rotation (QML Behavior)
│QMqttClient││  summaries/stations │
│ + QWebSock││  statuses           │
└──────────┘└─────────────────────┘
 wss://rata.digitraffic.fi/mqtt   topic filter: train-locations/#
```

Two transports feed positions (unchanged from the Swift app):

- **REST** (`/train-locations/latest/`) — used once to bootstrap, then every 60 s to
  prune. Returns the full current set of active trains.
- **MQTT** (`train-locations/#`) — the live firehose, **deltas only**. A
  freshly-connected subscriber sees nothing until trains report, which is exactly why
  the REST bootstrap is required.

## Layers

### 1. `DigitrafficService` — the network layer

A `QObject` (or a namespace of free functions) wrapping `QNetworkAccessManager`. One
`QNetworkAccessManager` instance is reused for the app's lifetime (it pools
connections; creating one per request is an anti-pattern). Endpoints used:

| Method (proposed) | Endpoint | Purpose |
|---|---|---|
| `fetchTrainLocations()` | `/train-locations/latest/` | All active train positions (REST snapshot) |
| `fetchTrainSummaries(date)` | `/trains/{yyyy-MM-dd}` | trainNumber → type / category / commuter line cache |
| `fetchStations()` | `/metadata/stations` | Station short-code → name catalogue |
| `fetchCauseCategories()` | `/metadata/cause-category-codes` | Delay-cause id → human name |
| `fetchRunningTrainStatuses()` | `/live-trains` | Bulk trainNumber → delay/running flags **+ type/category/line** (rings + cross-midnight fallback; every 120 s) |
| `fetchComposition(...)` | `/compositions/{date}/{n}` | Wagon composition (per-train, on demand) |
| `fetchTrainDetails(...)` | `/live-trains/{n}` | Full timetable + live delays (per-train, on demand) |

**Async style.** Each call issues `nam->get(QNetworkRequest{url})` and connects to
the reply's `finished` signal with a lambda that parses and emits a typed result
signal (e.g. `trainLocationsReady(QList<TrainLocation>)`) or `requestFailed(QString)`.
This is the dependency-free path. If you'd rather write linear code, C++20
coroutines work with a thin `QNetworkReply`-awaiter or `QtFuture::connect(reply, ...)`
— but that's optional polish, not required for parity.

```cpp
void DigitrafficService::fetchTrainLocations() {
    auto *reply = nam_->get(request(QStringLiteral("/train-locations/latest/")));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(reply->errorString());
            return;
        }
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        QList<TrainLocation> out;
        for (const auto v : doc.array())
            out.append(TrainLocation::fromJson(v.toObject()));
        emit trainLocationsReady(out);
    });
}
```

**Date decoding.** Digitraffic emits ISO8601 with fractional seconds. Mirror the
Swift two-step fallback:

```cpp
QDateTime parseTs(const QString &s) {
    QDateTime dt = QDateTime::fromString(s, Qt::ISODateWithMs);   // fractional first
    if (!dt.isValid()) dt = QDateTime::fromString(s, Qt::ISODate); // plain fallback
    return dt;
}
```

`QDateTime::fromString` with `Qt::ISODateWithMs`/`Qt::ISODate` handles the trailing
`Z` (UTC) correctly; store as UTC and convert for display.

**gzip.** `QNetworkAccessManager` advertises and decodes gzip transparently, so the
~6 MB `/live-trains` payload arrives decompressed — same as `URLSession`.

**Response validation.** Check `reply->error()`, and the HTTP status via
`reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)`. Treat a `404` from
`/compositions/...` as "no composition" (return empty), not an error — same special
case as the Swift `fetchComposition`.

### 2. `MqttClient` — the live transport (now mostly Qt's job)

> **⚠️ Not what shipped.** TrainsOnMap does **not** use Qt MQTT. Qt MQTT is a
> separate installable component that a default Qt install lacks, which would have
> made the app un-buildable against a stock kit — so the hand-rolled client came
> across from Swift instead, as the `mqttwire` helpers in
> [`src/MqttCodec.h`](../src/MqttCodec.h) driving a plain `QWebSocket`. That means
> the CONNECT/SUBSCRIBE/PINGREQ encoding, the partial/coalesced-frame reassembly
> and the Variable-Byte-Integer length decoding this section says are "all gone"
> are all still implemented, in
> [`DigitrafficMqttClient`](../src/doc/DigitrafficMqttClient.md). The one piece of
> advice below that *did* survive verbatim: the broker requires the `mqtt`
> WebSocket subprotocol via `QWebSocketHandshakeOptions`, and the reconnect
> backoff is still the app's job.

The Swift app hand-rolls MQTT 3.1.1 over a WebSocket because `URLSession` gives no
MQTT framing. **Qt MQTT removes that entirely.** Wrap a `QMqttClient` in a small
`QObject` that owns the WebSocket, sets up the subscription, and re-exposes a clean
`messageReceived(QByteArray)` plus a reconnect policy.

The broker `wss://rata.digitraffic.fi/mqtt` requires the **`mqtt` WebSocket
subprotocol**. Qt MQTT's `connectToHostWebSocketEncrypted()` (since 6.10) will open
its own socket if you pass `nullptr`, but then you can't set the subprotocol — so
**construct your own `QWebSocket`** with `QWebSocketHandshakeOptions` and hand it in:

```cpp
mqtt_ = new QMqttClient(this);
mqtt_->setProtocolVersion(QMqttClient::MQTT_3_1_1);   // matches the Swift client
// clientId left empty → QMqttClient generates a unique one.

ws_ = new QWebSocket();                                // owned by us; must outlive mqtt_
QWebSocketHandshakeOptions opts;
opts.setSubprotocols({QStringLiteral("mqtt")});        // broker requirement
ws_->setHandshakeOptions(opts);
ws_->setRequestHeader("User-Agent", "TrainSpotter-Qt/1.0 (https://www.digitraffic.fi)");

connect(mqtt_, &QMqttClient::connected, this, [this] {
    // QoS 0 firehose — like the Swift client we don't await SUBACK.
    mqtt_->subscribe(QMqttTopicFilter{QStringLiteral("train-locations/#")}, /*qos*/ 0);
});
connect(mqtt_, &QMqttClient::messageReceived, this,
        [this](const QByteArray &payload, const QMqttTopicName &) {
            emit messageReceived(payload);            // TrainStore decodes + apply()
        });
connect(mqtt_, &QMqttClient::disconnected, this, &MqttClient::scheduleReconnect);
connect(mqtt_, &QMqttClient::errorChanged, this, &MqttClient::scheduleReconnect);

mqtt_->connectToHostWebSocketEncrypted(ws_);           // SecureWebSocket transport
```

> The exact `QWebSocket` open/handshake sequence for the native path is new in 6.10 —
> cross-check against Qt's official **"WebSockets MQTT Subscription"** example, which
> shows the `QWebSocketHandshakeOptions::setSubprotocols({"mqtt"})` + `open(url, opts)`
> setup verbatim.

What Qt now handles that the Swift code did by hand: CONNECT/CONNACK, SUBSCRIBE,
PUBLISH receive, **PINGREQ keepalive** (`autoKeepAlive` defaults to `true`; tune
`keepAlive` seconds), DISCONNECT, **and** the partial/coalesced-frame reassembly +
MQTT Variable-Byte-Integer length decoding that `MQTTClient.swift` implements
manually. All gone.

**Auto-reconnect.** Qt MQTT does **not** auto-reconnect, so keep the Swift app's
policy: on `disconnected`/`errorChanged`, restart via a `QTimer` with exponential
backoff (1 s → 30 s), reset to 1 s after a productive session. The 60 s REST resync
keeps data fresh while disconnected, so a blip self-heals — identical reasoning to
`DataFlow.md`.

### 3. `TrainStore` — state & orchestration

A single `QObject` on the GUI thread, the source of truth. Views are QML and bind to
it. There is no `@Observable`; you get the same effect from `Q_PROPERTY` + `NOTIFY`
signals, and from a model for the train list:

```cpp
class TrainStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(TrainListModel *trains READ trains CONSTANT)
    Q_PROPERTY(bool   isLoadingList READ isLoadingList NOTIFY isLoadingListChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage  NOTIFY errorMessageChanged)
    Q_PROPERTY(QDateTime lastUpdated READ lastUpdated   NOTIFY lastUpdatedChanged)
    // ... selectedTrain, filter state (activeCategories, proximityFilterKm) ...
};
```

Key state (proposed):

- **`TrainListModel : QAbstractListModel`** — the live set rendered on the map. A
  model (not a bare `QList`) so `MapItemView`/`Repeater` get incremental row
  add/remove/change instead of rebuilding every annotation. Roles: coordinate,
  trainNumber, displayName, speed, bearing, badgeLabel, fillColor, ringState,
  isSelected. This is the structural difference from the Swift `[TrainLocation]`
  array + manual `syncAnnotations` diffing — `QAbstractListModel` *is* the diffing
  layer.
- `previousPositions : QHash<QString, QGeoCoordinate>` — each train's prior coordinate
  for `azimuthTo()`.
- `trainSummaries : QHash<int, TrainSummary>`, `stations`, `causeCategoryNames`,
  `trainStatuses : QHash<int, TrainRunningStatus>` — the metadata caches.
- `selectedTrain` / `selectedTrainDetails` / `selectedTrainComposition` — the tapped
  train's on-demand detail.

Expose to QML via `qmlRegisterSingletonInstance("TrainSpotter", 1, 0, "Store", store)`
or a context property.

## The position lifecycle

### Startup — `startRefreshing()`

Same three concurrent jobs as the Swift `streamTask`/`resyncTask`/`statusTask`, but
expressed with `QTimer`s and signal chains instead of detached `Task`s:

**Live path:**
1. Kick off metadata fetches in parallel: `fetchTrainSummaries(today)`,
   `fetchStations()`, `fetchCauseCategories()` (three independent `QNetworkReply`s —
   they already run concurrently).
2. `fetchTrainLocations()` once for the bootstrap snapshot so stationary trains
   appear immediately.
3. Start the `MqttClient` (which subscribes on `connected` and reconnects on drop
   with backoff, per § 2).

> Unlike the Swift `async let (...)` which *awaits* the metadata before the first
> snapshot, the Qt version need not block. But keep the **ordering guarantee that
> matters**: the marker colour depends on type data, so wire `apply()` to recolour
> when `trainSummaries`/`trainStatuses` land (the model emits `dataChanged` for the
> affected rows). See [§ cross-midnight fallback](#derived-data-the-ui-consumes).

**Prune path — a 60 s `QTimer`:**
- On timeout: if the operating day rolled past midnight (compare `QDate::currentDate()`
  to the date the summaries were loaded for), re-run `fetchTrainSummaries(today)` so
  new-day trains don't all decode as `Other` and get filtered out; then
  `fetchTrainLocations()` (which merges + prunes — below).

**Status path — a 120 s `QTimer` (feeds rings + the type fallback):**
- Once on startup, then every 120 s: `fetchRunningTrainStatuses()` hits the bulk
  `/live-trains` endpoint (one request, ~6 MB) and decodes a lean
  `TrainRunningStatus` per train — current delay, `cancelled`, `runningCurrently`,
  **plus `trainType` / `trainCategory` / `commuterLineID`** — into `trainStatuses`.
  Slower cadence than the position resync because delays drift gradually and the
  payload is large. It's bulk (not per-train), so it respects the "no eager per-train
  details" rule.

`stopRefreshing()` stops both timers and disconnects the MQTT client.

> **Qt lifetime caveat.** Parent every `QObject` (timers, the `QMqttClient`, the
> service) to the store, and `deleteLater()` each `QNetworkReply` in its `finished`
> handler. There is no ARC; leaked replies and dangling lambda captures are the most
> common Qt-port bug. Capture `this`/`reply` only, and guard against the reply
> outliving the store with `QPointer` or a `connect`-context object.

### Upsert — `apply(const TrainLocation&)`

The single funnel for **every** position update, REST or MQTT — identical logic to
the Swift `apply(_:)`:

1. If a train with the same `id` (`"{trainNumber}-{departureDate}"`) exists:
   - **Guard on timestamp:** drop if `loc.timestamp < existing.timestamp`. The 60 s
     resync lags the MQTT firehose; without this a stale snapshot could overwrite a
     fresher position and record a ~180°-reversed bearing.
   - Snapshot the existing coordinate into `previousPositions[id]` **before**
     overwriting — the "from" point for the next `azimuthTo()`.
   - Update the model row in place → emits `dataChanged(index, index, {roles})`, which
     moves just that `MapQuickItem` (no full rebuild).
2. Otherwise `beginInsertRows`/append/`endInsertRows`.
3. Update `lastUpdated`; clear `errorMessage` if set.

### Prune & merge — on each REST snapshot

Merges rather than replaces, so concurrent MQTT updates aren't clobbered and the map
doesn't flicker:

1. Compute the set of `id`s in the fetched snapshot.
2. Remove a model row only if it's absent from the snapshot **and** its timestamp is
   older than a 120 s grace window — keeps a just-arrived MQTT-only train from being
   deleted before REST catches up. Use `beginRemoveRows`/`endRemoveRows`.
3. `apply()` every fetched row (re-using the timestamp guard).
4. Garbage-collect `previousPositions` down to currently-live ids.

On failure set `errorMessage` (surfaced by the QML empty-state overlay on first load).
`isLoadingList` is only `true` while the model is empty.

## Derived data the UI consumes

- **Filtered model** — apply the category toggles + optional proximity radius. In Qt,
  layer a `QSortFilterProxyModel` over `TrainListModel` (override
  `filterAcceptsRow`), and bind the `Map`'s `MapItemView.model` to the proxy. Filter
  by category **via `category(for:)`** so cross-day trains use their real category
  (see below), and skip category filtering until `trainSummaries` is populated —
  same gate as the Swift app.
- **Bearing** — `previousPositions[id].azimuthTo(current)` gives degrees clockwise
  from north; return "no bearing" (hide the arrow) when `speed == 0` or there's no
  prior position (first message). Drives the QML arrow's `rotation`, animated with a
  shortest-arc `RotationAnimation`.
- **`trainType(for) / category(for) / commuterLineID(for)`** — look-ups into
  `trainSummaries`, **falling back to `trainStatuses`** (the bulk `/live-trains` poll)
  when the summary cache misses. This is the **cross-midnight fix** carried over
  verbatim: `trainSummaries` is built from `/trains/{today}` and keyed by
  `trainNumber`, so a train running across midnight — cross-midnight cargo (irregular
  numbers) and night trains (`PYO`) that departed *yesterday* — isn't in today's
  schedule and would otherwise render with the wrong (heuristic orange/gray) colour.
  The `/live-trains` payload lists currently-running trains with their real
  type/category/line regardless of departure date, at zero extra request. See
  [`MarkerColors.Qt.md`](MarkerColors.Qt.md); the resolver logic is pure and ports
  unchanged.

## Map & tiles {#5-map--tiles}

The Swift app stacks a muted **CARTO Positron** raster base (with `{s}` subdomain
rotation + `{r}` retina, a 256 MB disk cache, and an offline cache-fallback) under a
vector rail overlay. Qt Location gives you two routes:

**A. `osm` plugin with a custom host (quick path).** Set the OSM plugin's
`osm.mapping.custom.host` to a CARTO tile URL; the plugin appends `%z/%x/%y.png`
(since 6.5 it skips the postfix if the URL already ends in `.png`), and you select the
custom map type (`Map.supportedMapTypes[last]`, type `MapType.CustomMap`). The plugin
ships a **built-in disk tile cache** (`osm.mapping.cache.directory` /
`osm.mapping.cache.disk.size`, default 50 MiB — bump it) and an **offline tile
directory** (`osm.mapping.offline.directory`) — together these replace the Swift
`CachedTileOverlay` URLCache + offline fallback. Set `osm.useragent` (the OSM tile
policy requires it).

```qml
Plugin {
    id: cartoPlugin
    name: "osm"
    PluginParameter { name: "osm.useragent";            value: "TrainSpotter-Qt/1.0" }
    PluginParameter { name: "osm.mapping.custom.host";  value: "https://a.basemaps.cartocdn.com/light_all/" }
    PluginParameter { name: "osm.mapping.custom.mapcopyright";  value: "© OpenStreetMap · CARTO" }
    PluginParameter { name: "osm.mapping.cache.disk.size";     value: 268435456 } // 256 MiB
    PluginParameter { name: "osm.mapping.highdpi_tiles";       value: true }
}
```

> **Caveat — not 1:1 with the Swift overlay.** `osm.mapping.custom.host` is a *single*
> host and only substitutes `%z/%x/%y`. It does **not** do the `{s}` subdomain
> rotation or the CARTO `{r}`/`@2x` retina suffix that the Swift `CachedTileOverlay`
> resolves by hand. `osm.mapping.highdpi_tiles` works via provider `-hires` files, not
> a custom host, so a custom CARTO host effectively serves 1× tiles. For evaluation
> this is fine; for production parity use route B.

**B. Custom geoservices plugin (full parity).** Subclass
`QGeoTiledMappingManagerEngine` + `QGeoTileFetcher` (or `QGeoTiledMapReply`) to build
the tile URL yourself — there you can rotate `a/b/c/d` subdomains, add the `@2x`
suffix on high-DPI screens, set the `User-Agent`, and plug in your own
`QNetworkDiskCache` with the offline `returnCacheDataDontLoad`-style fallback. This is
the true analogue of `CachedTileOverlay` and the only way to match it exactly. More
work; do it only if the 1× custom-host tiles aren't good enough.

**Rail overlay.** Keep the pre-baked `RailGeometry.bin` (the `TSRG` blob from
`build_rail_geometry.py`) — it's plain little-endian floats, language-agnostic. Load
and parse it in C++ (`QFile` + `QDataStream`, or memory-map via `QFile::map`), expose
the polylines to QML, and render with `MapItemView` over a polyline model whose
delegate is a `MapPolyline` (`line.color`, `line.width`, `path: list<coordinate>`).
Match the Swift stroke: warm dark grey, ~1.1 px, round joins.

> **Performance.** The national network is thousands of segments. One `MapPolyline`
> per segment through a `MapItemView` is the straightforward approach and is GPU-drawn,
> but profile it — if it's heavy, coalesce contiguous segments into fewer, longer
> `MapPolyline` paths at bake time, or drop to a custom `QQuickItem`/scene-graph node.
> The Swift app sidesteps this with a single `MKMultiPolyline`; QML has no
> multi-polyline equivalent.

**Markers.** Each train is a `MapQuickItem` (placed via `MapItemView` over the
filtered model) whose `sourceItem` is the badge QML — coloured `Rectangle`/`Label`
with the `"TYPE NUMBER"` / line-letter text, a status-ring border, and a child arrow.
This **replaces `ImageRenderer`** outright: no rasterisation, no sizing races, no
`refresh()` on each update — QML re-evaluates bindings when the model row changes. Set
`anchorPoint` so the badge's anchor sits on the coordinate. The arrow:

```qml
Image {                     // or a Canvas/Shape triangle
    rotation: model.bearing            // degrees, from azimuthTo()
    visible: model.hasBearing
    Behavior on rotation {
        RotationAnimation { duration: 400; direction: RotationAnimation.Shortest }
    }
}
```

`RotationAnimation.Shortest` gives the 350°→10° = +20° shortest-arc turn for free —
the hand-written delta math in the Swift `updateArrow(...)` isn't needed.

Colour resolution (palette, the `(trainType, category, speed)` resolver, status rings)
lives in **[`MarkerColors.Qt.md`](MarkerColors.Qt.md)**.

## On-demand detail (when a train is tapped)

`select(train)` sets `selectedTrain` and fires two independent fetches:

- `fetchTrainDetails(...)` → `/live-trains/{n}` (full timetable + live delays).
- `fetchComposition(...)` → `/compositions/{date}/{n}` (wagon makeup; absent for many).

Guard each reply's handler with `selectedTrain.id == train.id` before writing results,
so a slow response for a now-deselected train can't clobber the panel if the user taps
a different marker mid-flight — same race guard as the Swift `fetchDetails`. Details
are fetched **on demand only**, never eagerly for all trains.

## Why it's built this way

Mostly unchanged from `DataFlow.md`; the Qt-specific notes:

- **Qt MQTT instead of a hand-rolled client:** the single biggest simplification —
  ~250 lines of framing/reassembly become a configured `QMqttClient`. The only thing
  to re-add is the reconnect-with-backoff loop, which Qt MQTT deliberately leaves to
  the app.
- **`QAbstractListModel` instead of array + manual diff:** the model *is* the
  annotation-diffing layer that `syncAnnotations` does by hand in the Swift app —
  `MapItemView` adds/removes/moves only the changed `MapQuickItem`s.
- **QML delegates instead of `ImageRenderer`:** live vector badges; no snapshot, no
  reuse races, no `prepareForReuse` arrow-clearing.
- **GUI-thread `QObject`s instead of `@MainActor`:** all mutation on the main thread;
  network/MQTT callbacks land there too, so no Combine, no locks — same guarantee.
- **`QGeoCoordinate::azimuthTo()` / `RotationAnimation.Shortest`:** Qt provides the
  bearing math and shortest-arc animation the Swift app implements manually.

## Threading model

Everything described lives on the **main (GUI) thread**, mirroring `@MainActor`.
`QNetworkAccessManager`, `QMqttClient`, and `QTimer` are all asynchronous-but-single-
threaded: their signals fire on the thread they live on, so no mutexes are needed. Only
move work off-thread if profiling shows JSON parsing of the ~6 MB `/live-trains`
payload janks the UI — then parse in a `QtConcurrent::run` / `QThreadPool` and marshal
the result back with a queued signal. Don't reach for threads pre-emptively; the Swift
app does all of this on the main actor without issue.

## Source map (proposed)

| File | Responsibility |
|---|---|
| `models/TrainLocation.{h,cpp}` | Position struct, GeoJSON→`QGeoCoordinate`, `fromJson()` |
| `models/TrainModels.{h,cpp}` | `TrainSummary`, `TrainRunningStatus`, `TrainDetails`, `TimeTableRow`, enums |
| `models/TrainListModel.{h,cpp}` | `QAbstractListModel` of live trains + roles |
| `services/DigitrafficService.{h,cpp}` | All REST calls (`QNetworkAccessManager`) + JSON/date parsing |
| `services/MqttClient.{h,cpp}` | `QMqttClient` + `QWebSocket` transport + reconnect backoff |
| `services/RailNetwork.{h,cpp}` | Loads `RailGeometry.bin` → polyline list for QML |
| `store/TrainStore.{h,cpp}` | State, fetch orchestration, `apply()` / prune / bearing, type/category lookups |
| `qml/Main.qml`, `qml/MapView.qml`, `qml/TrainMarker.qml`, `qml/TrainDetailPanel.qml`, `qml/FilterPanel.qml` | Qt Quick UI |
| `Resources/RailGeometry.bin` | Pre-baked rail network (shared with the Swift app, unchanged) |

---

*Index: [`README.Qt.md`](README.Qt.md). Fetch wiring: [`TrainDataWiring.Qt.md`](TrainDataWiring.Qt.md).
Marker colours: [`MarkerColors.Qt.md`](MarkerColors.Qt.md). Swift original:
[`DataFlow.md`](DataFlow.md).*
