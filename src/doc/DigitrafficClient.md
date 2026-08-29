# DigitrafficClient

## 1. Class Overview

TrainsOnMap shows live Finnish train positions on a map. There are two data
paths for those positions: a REST snapshot that bootstraps and periodically
re-syncs the full fleet, and an MQTT stream that carries live deltas in between.
`DigitrafficClient` is the **REST** path.

It polls the Digitraffic `train-locations/latest` endpoint, decodes the
positions, and feeds them into a `TrainListModel` (which it owns and exposes via
its `model` property). On the same cycle it refreshes per-train *metadata* from
`/live-trains` — train type/category/line (for marker colour and labels), running
status (for the marker status ring) and scheduled routes (for Tier-2 matching) —
and, once at startup, loads station coordinates so a parked off-network train can
be pinned to the station it's booked at.

REST is the bootstrap-and-prune authority; it runs slowly (a 60 s resync) because
MQTT keeps positions fresh between cycles.

## 2. Project Structure and Dependencies

- **Declared in QML** (`Main.qml`) as `DigitrafficClient { id: trainClient;
  active: true; matcher: trackService }`. Its `model` is shared with
  `DigitrafficMqttClient` and bound to a train `MapItemView`.
- **Owns:** a `QNetworkAccessManager`, a `TrainListModel` (child), and a
  `QTimer` resync clock.
- **Holds (not owned):** a `TrackService *matcher`.
- **Qt modules:** Qt6::Core (`QObject`, `QTimer`, `QDateTime`, `QHash`),
  Qt6::Network (`QNetworkAccessManager`/`QNetworkReply`/`QNetworkRequest`),
  Qt6::Qml (`QML_ELEMENT`).
- **Project-internal dependencies:** `TrainListModel` (the model it fills, plus
  the `TrainPosition`/`TrainKey`/`TrainStatus`/`TrainRoute` value types and the
  `parseTrainLocation` helper), `TrackService` (the matcher), `RailGraph`
  (`canonicalRouteCodes`).

## 3. Class Hierarchy and Role

`DigitrafficClient : public QObject`. From `QObject` it gets the meta-object
system (signals/slots, properties) and parent-based ownership. Its role is the
REST polling controller: it owns the position model and orchestrates the three
Digitraffic REST calls (positions, live-trains metadata, station metadata),
mapping their JSON onto the model and the matcher.

## 4. Q_PROPERTY Declarations

| Property | Type | READ | WRITE | NOTIFY | Description |
|----------|------|------|-------|--------|-------------|
| `model` | `TrainListModel *` | `model` | — | — (`CONSTANT`) | The live-train list model, owned by the client. Read-only. |
| `active` | `bool` | `isActive` | `setActive` | `activeChanged` | When set true, fetches immediately and starts the resync timer; false stops polling. |
| `status` | `QString` | `status` | — | `statusChanged` | Human-readable status (e.g. "Fetching train positions…", "123 trains • updated 14:05:09"). Read-only. |
| `matcher` | `TrackService *` | `matcher` | `setMatcher` | `matcherChanged` | Optional rail-network matcher; when set, the model snaps and flags incoming GPS fixes against the track geometry. |

## 5. Enumerations

None.

## 6. Public Member Variables

None public.

## 7. Signals

#### void activeChanged()

Emitted when the `active` property flips.

#### void statusChanged()

Emitted when the `status` string changes.

#### void matcherChanged()

Emitted when the `matcher` is (re)assigned.

## 8. Public Slots and Q_INVOKABLE Methods

#### void refresh()

Fetches the latest positions once, immediately (issues the `train-locations/latest`
GET and sets the status to "Fetching…"). Also calls `refreshCategories()` so
marker colours/statuses/routes are seeded alongside the position snapshot. Bound
to the resync timer and to the InfoPanel's "Refresh" button.

#### void refreshCategories()

Refreshes marker metadata (category/type/line), running status and scheduled
routes from `/live-trains`. Most cycles it pulls only version-deltas
(`?version=<max seen>`); a full snapshot is re-pulled at least every ~5 minutes to
bound memory and let the route cache evict departed trains. Called by `refresh()`.

## 9. Public Methods

#### TrainListModel *model() const

The model getter (also the `model` property).

#### bool isActive() const / void setActive(bool active)

Getter/setter for `active`. `setActive(true)` calls `refresh()` then starts the
timer; `setActive(false)` stops it.

#### QString status() const

The status getter.

#### TrackService *matcher() const / void setMatcher(TrackService *matcher)

Getter/setter for the optional matcher. The setter also forwards the matcher to
the model (`TrainListModel::setMatcher`) so both the REST and shared MQTT paths
snap through it.

## 10. Protected Virtual Methods / Event Handlers

None overridden. Private helpers do the JSON work: `handleReply` (positions),
`handleCategories` (metadata/status/routes, with a `full` flag), `fetchStations`
/ `handleStations` (one-shot station coordinates for the model plus the
`stationNames()` map shared with `TrainDetailsService`; `stationNamesChanged`
fires when it lands), and `setStatus`.

## 11. Ownership and Lifecycle

Parent-owned `QObject` (owned by the QML engine when declared in `Main.qml`). The
`QNetworkAccessManager` and `TrainListModel` are constructed with `this` as
parent, so Qt destroys them with the client. The `QTimer m_timer` is a value
member. Each `QNetworkReply` is `deleteLater()`-d in its finished handler. The
`TrackService *m_matcher` is **not owned** — it is a reference to a sibling object
declared in QML.

## 12. Thread Safety

**GUI-thread only.** All network I/O uses the GUI-thread event loop (Qt Network's
asynchronous model); the reply handlers run on the GUI thread and touch the model
directly. There is no internal synchronisation and none is needed for the intended
single-threaded use.

## 13. QML Exposure

Registered with `QML_ELEMENT` (module `TrainsOnMap` 1.0), so QML can write
`DigitrafficClient {}`. From QML the app binds `active`, `matcher` and reads
`model`/`status`, and calls `refresh()`.

## 14. Inter-Class Interactions

- **`TrainListModel`** (owned): receives `updateTrains` (REST snapshot merge),
  `setTrainMetadata`, `setTrainStatuses`, `setTrainRoutes`, `setStationCoords`,
  and `setMatcher`.
- **`DigitrafficMqttClient`** shares this client's `model` (set in QML via
  `model: trainClient.model`), so REST and MQTT funnel into the same rows.
- **`TrackService`** (the matcher): receives `precomputeRoutes()` with the whole
  fleet's route sequences each metadata cycle.
- **`RailGraph::canonicalRouteCodes`** is used to canonicalise each route's
  station codes so the precompute key matches the overlay key.

## 15. External Communication

**Outbound HTTPS (REST), GUI-thread async.** Three Digitraffic endpoints
(`https://rata.digitraffic.fi`):

- `GET /api/v1/train-locations/latest/` — the full position snapshot, parsed as a
  JSON array of train-location objects.
- `GET /api/v1/live-trains[?version=N]` — per-train metadata, running status and
  scheduled `timeTableRows`; polled as version-deltas with periodic full resyncs.
- `GET /api/v1/metadata/stations` — station short-code → coordinate (for the
  model) and → name (exposed via `stationNames()` and consumed by
  `TrainDetailsService`), fetched once at startup.

Every request sends a `Digitraffic-User` identification header
(`TrainsOnMap/0.1 (Qt6 scaffolding)` — replace with your own app id). The transfer
timeout is 15 s so a stalled request aborts rather than leaving the status stuck.
`Accept-Encoding` is deliberately **not** set by hand so Qt 6 can transparently
inflate gzip. Network/parse errors set an error status but otherwise leave the
last good data in place (markers keep their colours, parked-train snapping just
stays disabled). All `finished` callbacks fire on the GUI thread.
