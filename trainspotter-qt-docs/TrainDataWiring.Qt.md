# Wiring the train-location feed into a Qt 6.11 `QAbstractListModel`

A focused how-to for pulling **live train positions** from the Fintraffic Digitraffic
API into a Qt Quick app, using the **same fetch logic** as the shipping Swift app
(REST bootstrap → MQTT live deltas → 60 s resync/prune) and exposing the result as a
`QAbstractListModel` that a `Map`/`MapItemView` can bind to.

This is a **wiring guide**, not source. It tells you which pieces connect to which,
in what order, and the rules that keep the data correct. Code fragments are short
*illustrative wiring sketches*, not drop-in implementations. For the broader
architecture and the Swift→Qt mapping, see [`DataFlow.Qt.md`](DataFlow.Qt.md).

> ## ⚠️ Historical — the wiring shipped, the transport didn't
>
> This planned the port; the port is this repo, TrainsOnMap. **§5 is the part that
> survived intact** — the `apply()` funnel, the timestamp guard, merge-don't-replace
> and the 120 s prune grace are all in
> [`TrainListModel`](../src/doc/TrainListModel.md) as described, and they are still
> the rules that keep the feed correct.
>
> **§4b is not what shipped.** There is no `QMqttClient`: MQTT 3.1.1 is hand-rolled
> in [`src/MqttCodec.h`](../src/MqttCodec.h) over a plain `QWebSocket`, so the
> keepalive, frame reassembly and length-varint decoding that §4b says "are none of
> your concern" are all implemented in
> [`DigitrafficMqttClient`](../src/doc/DigitrafficMqttClient.md). §7's two-endpoint
> type lookup collapsed to `/live-trains` alone. Full list:
> [`README.Qt.md`](README.Qt.md). Current code: [`src/doc/`](../src/doc/index.md).

---

## 1. The shape of the pipeline

Three independent sources feed one model. Nothing here needs a background thread —
all of it runs on the GUI thread (Qt's event loop is the analogue of the Swift app's
`@MainActor`).

```
  REST snapshot ────┐                          ┌── once at startup (bootstrap)
  (QNetworkAccessMgr)│                          └── every 60 s (resync / prune)
                     │
  MQTT deltas ───────┼──►  apply(TrainLocation) ──►  TrainListModel
  (QMqttClient/WSS)  │      (upsert + bearing)        (QAbstractListModel)
                     │                                       │
  60 s QTimer ───────┘                                       ▼
                                                    Map { MapItemView → MapQuickItem }
```

- **REST** `/train-locations/latest/` — the *full* current set. Used once to fill the
  map immediately, then every 60 s to **prune** trains the broker stopped reporting.
- **MQTT** `train-locations/#` — the live firehose, **deltas only**. A fresh
  subscriber sees nothing until a train moves, which is exactly why the REST bootstrap
  is mandatory.
- **`apply()`** — the single funnel every update (REST row or MQTT message) flows
  through. All the correctness rules live here (§5).

> **Same-as-Swift contract.** Keep these invariants and the Qt app behaves like the
> Swift one: (1) REST bootstrap before relying on MQTT, (2) one `apply()` funnel,
> (3) timestamp guard, (4) merge-don't-replace on resync, (5) 120 s prune grace.

---

## 2. Components to create

| Component | Base | Responsibility |
|---|---|---|
| `TrainLocation` | plain value struct | One position snapshot + `fromJson()` + `id()` |
| `DigitrafficService` | `QObject` | REST calls via one shared `QNetworkAccessManager`; emits parsed results |
| `MqttClient` | `QObject` | Wraps `QMqttClient` + `QWebSocket`; emits raw payloads; owns reconnect backoff |
| `TrainListModel` | `QAbstractListModel` | Holds the rows, runs `apply()`/prune, exposes roles to QML |

You can fold the orchestration (timers, wiring the three sources together) **into**
`TrainListModel`, or keep a thin `TrainStore` that owns the model — either works. The
guide below assumes the model also orchestrates, to keep the wiring in one place.

---

## 3. The data model

### `TrainLocation` fields (decode from each JSON object)

| Field | Type | Source JSON | Notes |
|---|---|---|---|
| `trainNumber` | `int` | `trainNumber` | |
| `departureDate` | `QString` | `departureDate` | `"yyyy-MM-dd"` |
| `timestamp` | `QDateTime` | `timestamp` | ISO8601 + fractional seconds (§7) |
| `coordinate` | `QGeoCoordinate` | `location.coordinates` | GeoJSON is **`[lon, lat]`** — flip to `QGeoCoordinate(lat, lon)` |
| `speed` | `int` | `speed` | default `0` if absent |

- **`id()`** → `QStringLiteral("%1-%2").arg(trainNumber).arg(departureDate)`. This is the
  upsert key, identical to the Swift `"{trainNumber}-{departureDate}"`. A train's run
  is unique per (number, departure date), **not** number alone.
- The MQTT payload is the *same* single-object shape as one REST array element, so one
  `TrainLocation::fromJson(QJsonObject)` decodes both paths. Reuse it.

### `TrainListModel` roles

Expose at least these via `roleNames()` so QML delegates can bind:

| Role | From |
|---|---|
| `coordinateRole` | `coordinate` (a `QGeoCoordinate`, usable directly by `MapQuickItem.coordinate`) |
| `trainNumberRole` / `displayNameRole` | `trainNumber` |
| `speedRole` | `speed` |
| `bearingRole` | computed (§6) — degrees, or an invalid/`-1` sentinel when none |
| `hasBearingRole` | `speed > 0 && previousPositions.contains(id)` |
| `badgeLabelRole` | computed — line letter / `"TYPE NUMBER"` / bare number ([colours & labels](MarkerColors.Qt.md)) |
| `fillColorRole` | computed — type colour ([colours & labels](MarkerColors.Qt.md)) |

The last two — the badge **colour** and **label text** — depend on the train *type*,
which the position feed does **not** carry; [`MarkerColors.Qt.md`](MarkerColors.Qt.md)
covers how they're resolved (and the extra endpoints that requires). Optionally add a
status-ring role too.

---

## 4. Wiring the three sources

### 4a. REST (bootstrap + resync) — `DigitrafficService`

- Construct **one** `QNetworkAccessManager`, parented to the service, reused for every
  request. (One-NAM-per-request is the classic Qt leak/perf mistake.)
- `fetchTrainLocations()` issues `nam->get(QNetworkRequest{url})` and connects the
  reply's `finished` signal to a lambda that: checks `reply->error()`, parses the JSON
  array into `QList<TrainLocation>`, emits a typed signal, and calls
  `reply->deleteLater()`.

```cpp
// wiring sketch — service side
auto *reply = nam_->get(QNetworkRequest{baseUrl_.resolved(QUrl("train-locations/latest/"))});
connect(reply, &QNetworkReply::finished, this, [this, reply] {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) { emit requestFailed(reply->errorString()); return; }
    emit trainLocationsReady(parseLocations(reply->readAll()));  // QList<TrainLocation>
});
```

```cpp
// wiring sketch — model side
connect(service_, &DigitrafficService::trainLocationsReady,
        this, &TrainListModel::onSnapshot);          // → merge + prune (§5)
connect(service_, &DigitrafficService::requestFailed,
        this, &TrainListModel::onRequestFailed);      // → set error string
```

The **resync** is just a `QTimer` (60 000 ms, repeating) whose `timeout` calls
`service_->fetchTrainLocations()` again. The *same* `onSnapshot` slot handles both the
bootstrap and every resync — the merge/prune logic (§5) makes them interchangeable.

### 4b. MQTT (live deltas) — `MqttClient`

The broker is `wss://rata.digitraffic.fi/mqtt` and **requires the `mqtt` WebSocket
subprotocol**. In Qt 6.11 you do **not** hand-roll MQTT — `QMqttClient` frames it, and
since 6.10 it speaks WebSockets natively. Setup sequence:

1. Create a `QMqttClient`; set `setProtocolVersion(QMqttClient::MQTT_3_1_1)`.
2. Create a `QWebSocket` you own (must outlive the client). Configure the subprotocol:
   ```cpp
   QWebSocketHandshakeOptions opts;
   opts.setSubprotocols({ QStringLiteral("mqtt") });   // broker requirement
   ```
   Point it at the broker URL and hand it to the client's secure-WebSocket connect:
   ```cpp
   // wiring sketch — see the official "WebSockets MQTT Subscription" example for the
   // exact QWebSocket open/handshake sequence (native path is new in 6.10).
   mqtt_->connectToHostWebSocketEncrypted(webSocket_);  // SecureWebSocket transport
   ```
3. On `QMqttClient::connected`, **subscribe** to the topic filter `train-locations/#`
   at **QoS 0** (the firehose; don't await SUBACK):
   ```cpp
   connect(mqtt_, &QMqttClient::connected, this, [this] {
       mqtt_->subscribe(QMqttTopicFilter{QStringLiteral("train-locations/#")}, 0);
   });
   ```
4. On `QMqttClient::messageReceived(payload, topic)`, emit the raw `payload` upward;
   the model decodes one `TrainLocation` from it and calls `apply()`:
   ```cpp
   connect(mqtt_, &QMqttClient::messageReceived, this,
           [this](const QByteArray &payload, const QMqttTopicName &) { emit messageReceived(payload); });
   ```

```cpp
// wiring sketch — model side
connect(mqtt_, &MqttClient::messageReceived, this, [this](const QByteArray &p) {
    const auto obj = QJsonDocument::fromJson(p).object();
    if (!obj.isEmpty()) apply(TrainLocation::fromJson(obj));
});
```

**Keepalive** is automatic (`QMqttClient::autoKeepAlive` defaults to `true`). Qt also
handles the partial/coalesced-frame reassembly and MQTT length-varint decoding that
the Swift app does by hand — none of that is your concern here.

> **⚠️ It became your concern.** TrainsOnMap has no Qt MQTT dependency, so all
> three came back: a 30 s PINGREQ timer, the re-framing loop (WebSocket frame
> boundaries don't align to MQTT packet boundaries), and
> `mqttwire::decodeRemainingLength`. See [`MqttCodec.md`](../src/doc/MqttCodec.md)
> for the wire helpers and [`DigitrafficMqttClient.md`](../src/doc/DigitrafficMqttClient.md)
> for the session/reconnect handling. A frame implausibly larger than 1 MiB is
> treated as a desynced stream and forces a clean reconnect — a guard this section,
> written against a library that framed for you, had no reason to mention.

**Auto-reconnect is NOT automatic.** Reproduce the Swift backoff: on
`QMqttClient::disconnected` or `errorChanged`, restart the connection from a `QTimer`
with exponential backoff (1 s → 30 s), reset to 1 s after a session that actually
received messages. The 60 s REST resync keeps the map fresh while disconnected, so a
network blip self-heals without a relaunch.

### 4c. Putting the sources together (startup)

`startRefreshing()` does, in order:

1. `service_->fetchTrainLocations()` — the bootstrap snapshot (so stationary trains
   appear at once).
2. `mqtt_->start()` — connects, subscribes on `connected`, reconnects on drop.
3. Start the 60 s resync `QTimer`.

`stopRefreshing()` stops the timer and calls `mqtt_->disconnectFromHost()`.

---

## 5. The `apply()` / merge / prune rules (the core "same logic")

This is the part that must match the Swift app exactly. Three operations:

### Upsert — `apply(const TrainLocation& loc)`
1. Look up the row by `loc.id()`.
2. **If it exists:**
   - **Timestamp guard:** if `loc.timestamp < existing.timestamp`, **drop it** and
     return. (The 60 s resync lags the MQTT firehose; without this, a stale snapshot
     can overwrite a fresher MQTT position and produce a ~180°-reversed bearing.)
   - Save `previousPositions[id] = existing.coordinate` **before** overwriting — this
     is the "from" point for the next bearing.
   - Replace the row's data, then emit `dataChanged(index, index, { changedRoles })`
     so only that one `MapQuickItem` moves.
3. **If it doesn't exist:** `beginInsertRows()` → append → `endInsertRows()`.
4. Update `lastUpdated`; clear any error string.

### Merge + prune — `onSnapshot(const QList<TrainLocation>& fetched)`
The REST snapshot **merges**, it does not replace (so concurrent MQTT updates aren't
clobbered and the map doesn't flicker):
1. Build `activeIds` = set of `fetched[i].id()`.
2. Remove a row **only if** its id is absent from `activeIds` **and** its `timestamp`
   is older than a **120 s grace window**. (The grace stops a just-arrived MQTT-only
   train from being deleted before REST catches up; a genuinely gone train stops
   updating and ages out.) Use `beginRemoveRows`/`endRemoveRows`.
3. Call `apply()` on every fetched row (re-using the timestamp guard).
4. Garbage-collect `previousPositions` down to currently-live ids so it doesn't grow
   unbounded over a long session.

### Error path
If a REST request fails, set an `errorMessage` property (NOTIFY) for the QML
empty-state overlay; an `isLoadingList` flag is `true` only while the model is empty.

---

## 6. Bearing (direction arrow)

Don't bake the bearing into the row from the feed — compute it:

- `bearing(id)` = `previousPositions[id].azimuthTo(currentCoordinate)` →
  `QGeoCoordinate::azimuthTo()` returns degrees clockwise from north.
- Return "no bearing" (hide the arrow) when `speed == 0` **or** there's no previous
  position (the first message for that train).
- In the QML delegate, bind the arrow's `rotation` to the `bearingRole` and animate
  with `Behavior on rotation { RotationAnimation { duration: 400; direction: RotationAnimation.Shortest } }`.
  `Shortest` gives the 350°→10° = +20° turn for free — no manual delta math.

---

## 7. Marker colours & labels

Each badge gets a **colour** (the juliadata.fi palette) and a **label** (line letter,
or `"TYPE NUMBER"`, e.g. `IC 967`) — both pure functions of the train's
`type / category / line / speed`. Because they need the train *type*, which the
position feed doesn't carry, they pull in two extra endpoints (`/trains/{date}` + a
`/live-trains` fallback that also handles cross-midnight trains).

The full palette, the `QColor` resolver, the **badge-label** format rules, the type
lookup with its cross-midnight fallback, the optional status rings, and how to wire the
`fillColorRole` / `badgeLabelRole` into the model all live in
**[`MarkerColors.Qt.md`](MarkerColors.Qt.md)**.

---

## 8. Gotchas (carry-overs that bite in Qt)

- **Dates.** Parse `timestamp` as `QDateTime::fromString(s, Qt::ISODateWithMs)`, and if
  invalid fall back to `Qt::ISODate` — the same two-format strategy as the Swift custom
  decoder. Store/compare in UTC.
- **GeoJSON axis order.** `coordinates` is `[longitude, latitude]`. Flipping the order
  is the single most common porting bug — `QGeoCoordinate(lat, lon)`.
- **Object lifetime.** No ARC. Parent every `QObject` (timers, `QMqttClient`,
  `QWebSocket`, service) so they're destroyed with the model; `deleteLater()` each
  `QNetworkReply` in its `finished` handler; guard lambdas with a context object
  (the 3-arg `connect`) so a late reply can't call into a destroyed model.
- **WebSocket ownership.** The `QWebSocket` you pass to
  `connectToHostWebSocketEncrypted()` is owned by *you* and must stay valid for the
  client's whole lifetime — parent it to the `MqttClient`.
- **gzip.** `QNetworkAccessManager` decompresses gzip transparently; you read plain
  JSON from `reply->readAll()`. No manual inflate.
- **Threading.** Keep it single-threaded (GUI thread). Only move JSON parsing of large
  payloads off-thread if profiling shows a hitch — then `QtConcurrent::run` + a queued
  signal back. Don't pre-emptively thread it; the Swift app does all this on one actor.

---

## 9. Endpoints used here

| Purpose | Endpoint | When |
|---|---|---|
| Position snapshot | `GET /api/v1/train-locations/latest/` | bootstrap + every 60 s |
| Live positions | MQTT topic `train-locations/#` on `wss://rata.digitraffic.fi/mqtt` | continuous |
| Type/category cache (colour) | `GET /api/v1/trains/{yyyy-MM-dd}` | startup + midnight rollover ([colours](MarkerColors.Qt.md)) |
| Type fallback + status rings | `GET /api/v1/live-trains` | every 120 s ([colours](MarkerColors.Qt.md)) |

Base REST URL: `https://rata.digitraffic.fi/api/v1`. No API key. Send a descriptive
`User-Agent` on REST requests (and the WSS handshake) per Digitraffic etiquette.

> The first two rows are the **position** pipeline (§1–§5). The last two are pulled in
> only because **marker colour needs the train type** — wire each as a `QHash` cache
> refreshed on a `QTimer`, exactly like the 60 s resync. See
> [`MarkerColors.Qt.md`](MarkerColors.Qt.md) and [`DataFlow.Qt.md`](DataFlow.Qt.md) § Layers.

---

*Companion to [`DataFlow.Qt.md`](DataFlow.Qt.md) (full Qt port reference) and
[`DataFlow.md`](DataFlow.md) (the shipping Swift app, the source of this logic).*
