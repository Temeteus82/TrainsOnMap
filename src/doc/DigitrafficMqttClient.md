# DigitrafficMqttClient

## 1. Class Overview

`DigitrafficMqttClient` is the **live-stream** half of TrainsOnMap's position
pipeline (the REST half is `DigitrafficClient`). It connects to the Digitraffic
MQTT broker over a secure WebSocket and streams train-position deltas, upserting
each one into a `TrainListModel` so markers move between the slow 60 s REST
resyncs.

It speaks MQTT 3.1.1 itself using the byte-level helpers in `MqttCodec.h` (no
external broker library), so the app depends only on the LGPL Qt WebSockets
module. Beyond the firehose subscription, it can also subscribe to a **single
selected train's** topic so the detail panel receives that train's live timetable
updates, re-emitted as the `trainMessage` signal.

## 2. Project Structure and Dependencies

- **Declared in QML** (`Main.qml`) as `DigitrafficMqttClient { id: trainStream;
  model: trainClient.model; active: true }` — note it shares
  `DigitrafficClient`'s model rather than owning one.
- **Wired to `TrainDetailsService`** via `stream: trainStream`, which connects to
  `trainMessage` and drives `subscribeTrain` / `unsubscribeTrain`.
- **Qt modules:** Qt6::Core (`QObject`, `QTimer`, `QByteArray`), Qt6::WebSockets
  (`QWebSocket`, `QWebSocketHandshakeOptions`), Qt6::Network (`QNetworkRequest`),
  Qt6::Qml (`QML_ELEMENT`). `QRandomGenerator` supplies a unique client id.
- **Project-internal dependencies:** `MqttCodec.h` (`mqttwire` wire helpers),
  `TrainListModel` (the upsert target and `parseTrainLocation`/`TrainPosition`).

## 3. Class Hierarchy and Role

`DigitrafficMqttClient : public QObject`. From `QObject` it gets signals/slots,
properties and parent ownership. Its role is a self-contained MQTT-over-WebSocket
transport and session manager: connect, authenticate (CONNECT/CONNACK),
subscribe, keep-alive ping, re-frame the byte stream into MQTT packets, dispatch
PUBLISHes, and reconnect on drop.

## 4. Q_PROPERTY Declarations

| Property | Type | READ | WRITE | NOTIFY | Description |
|----------|------|------|-------|--------|-------------|
| `model` | `TrainListModel *` | `model` | `setModel` | `modelChanged` | The model to upsert positions into. Set in QML to `DigitrafficClient.model` so REST and MQTT share rows. |
| `active` | `bool` | `isActive` | `setActive` | `activeChanged` | True opens the connection (and auto-reconnects on drop); false closes it cleanly. |
| `connected` | `bool` | `isConnected` | — | `connectedChanged` | True once the MQTT session is established (CONNACK accepted). Read-only. |
| `status` | `QString` | `status` | — | `statusChanged` | Human-readable connection status ("Connecting…", "Live (MQTT)", "Reconnecting…", error text). Read-only. |

## 5. Enumerations

None of its own (it uses the `mqttwire::PacketType` enum from `MqttCodec.h`).

## 6. Public Member Variables

None public.

## 7. Signals

#### void modelChanged()

Emitted when the `model` is (re)assigned.

#### void activeChanged()

Emitted when `active` flips.

#### void connectedChanged()

Emitted when the MQTT session connects or disconnects.

#### void statusChanged()

Emitted when the `status` string changes.

#### void trainMessage(const QByteArray &payload)

Emitted for each PUBLISH on a `trains/#` topic — the full running-train JSON for
the per-train subscription. `TrainDetailsService::onStreamTrainMessage` connects
to this to apply live timetable/estimate updates for the selected train. (PUBLISHes
on `train-locations/#` are *not* re-emitted; they go straight to the model.)

## 8. Public Slots and Q_INVOKABLE Methods

#### void subscribeTrain(const QString &departureDate, int trainNumber)

Subscribes to live updates for one train (topic `trains/<date>/<number>/#`),
replacing any previous per-train subscription. No-op if the topic is unchanged or
`departureDate` is empty. Used by the detail panel when a train is selected. If
not yet connected, the subscription is remembered and (re)sent on CONNACK.

#### void unsubscribeTrain()

Drops the current per-train subscription (sends UNSUBSCRIBE if connected) and
clears the remembered topic.

## 9. Public Methods

#### TrainListModel *model() const / void setModel(TrainListModel *model)

Getter/setter for the shared model.

#### bool isActive() const / void setActive(bool active)

Getter/setter for `active`. The setter opens or closes the connection.

#### bool isConnected() const / QString status() const

Read-only getters for `connected` and `status`.

## 10. Protected Virtual Methods / Event Handlers

None overridden. Behaviour is implemented in private handlers connected to
`QWebSocket` signals: `onSocketConnected` (send CONNECT), `onSocketDisconnected`
(stop ping, schedule reconnect while active), `onBinaryMessage` (re-frame and
parse packets), and `dispatchPacket` (handle CONNACK/SUBACK/PUBLISH/PINGRESP).

## 11. Ownership and Lifecycle

Parent-owned `QObject` (owned by the QML engine in `Main.qml`). The `QWebSocket`
is constructed with `this` as parent. The two `QTimer`s (keep-alive ping and
reconnect) are value members. The **destructor** sends an MQTT DISCONNECT and
closes the socket if still connected, for a clean broker teardown. The `model` is
**not owned** — it belongs to `DigitrafficClient`.

## 12. Thread Safety

**GUI-thread only.** `QWebSocket`, the timers, and the model upserts all run on
the GUI-thread event loop. There is no internal locking and none is required for
the intended single-threaded use.

## 13. QML Exposure

Registered with `QML_ELEMENT` (module `TrainsOnMap` 1.0). QML instantiates
`DigitrafficMqttClient {}`, binds `model`/`active`, reads `connected`/`status`
(InfoPanel), and — through `TrainDetailsService.stream` — drives the per-train
subscription.

## 14. Inter-Class Interactions

- **`TrainListModel`** (shared, not owned): each decoded `train-locations`
  PUBLISH becomes a `parseTrainLocation` → `upsertTrain` call, touching only the
  changed row so markers don't flicker.
- **`DigitrafficClient`** provides the shared model; the two clients cooperate on
  the same rows (REST snapshot/prune + MQTT deltas).
- **`TrainDetailsService`** sets itself as the stream consumer: it connects to
  `trainMessage` and calls `subscribeTrain`/`unsubscribeTrain` as the selection
  changes.

## 15. External Communication

**Bidirectional MQTT 3.1.1 over secure WebSocket (wss), GUI-thread async.**

- **Endpoint:** `wss://rata.digitraffic.fi:443/mqtt`, opened with the `mqtt`
  WebSocket subprotocol and an `Origin: https://www.digitraffic.fi` header. No
  credentials (clean session). TLS is handled by Qt's WebSockets/TLS backend.
- **Subscriptions:** the firehose `train-locations/#` (always, on connect) and an
  optional per-train `trains/<date>/<number>/#`.
- **Data format:** PUBLISH payloads are JSON. `train-locations/*` payloads are
  single train-location objects fed to the model; `trains/*` payloads are full
  running-train objects re-emitted via `trainMessage`.
- **Keep-alive:** a PINGREQ is sent every 30 s (half the 60 s keep-alive).
- **Re-framing:** WebSocket frame boundaries are independent of MQTT packet
  boundaries, so incoming bytes are buffered and parsed into as many complete
  packets as are available. A frame implausibly larger than 1 MiB is treated as a
  corrupt/out-of-sync stream: the buffer is dropped and the socket closed to force
  a clean reconnect rather than buffering toward the protocol ceiling.
- **Reconnection:** while `active`, a drop schedules a reconnect after 5 s; a
  selected-train subscription is restored automatically on the next CONNACK. All
  socket callbacks fire on the GUI thread.
