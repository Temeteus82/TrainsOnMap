# TrainListModel

## 1. Class Overview

`TrainListModel` is the list model of live trains that a QML `MapItemView` binds
to in order to draw one marker per train. It is the convergence point of both
position data paths: a **bulk replace** from the REST snapshot
(`DigitrafficClient`) and **per-train upserts** from the MQTT stream
(`DigitrafficMqttClient`), merged through a single timestamp-guarded funnel so the
two never clobber each other and markers don't flicker.

Beyond storing positions, the model is where most of the app's per-fix
intelligence lives: it derives bearing from successive fixes, **map-matches** each
fix against the rail network through an optional `TrackService` (Tier-2
route-constrained or Tier-1 nearest-track), pins parked off-network trains to
their booked station, rejects physically-impossible "teleport" fixes, and
computes a status-ring colour per train from its delay/running state and position
age.

The header also defines the value types shared across the position pipeline
(`TrainPosition`, `TrainStatus`, `TrainRoute`, `TrainKey`) and the
`parseTrainLocation` JSON helper used by both clients.

## 2. Project Structure and Dependencies

- **Owned by `DigitrafficClient`** and exposed via its `model` property;
  `DigitrafficMqttClient` is given the *same* instance via QML (`model:
  trainClient.model`). QML cannot construct one (`QML_UNCREATABLE`).
- **Qt modules:** Qt6::Core (`QAbstractListModel`, `QHash`, `QVector`,
  `QDateTime`, `QJsonObject/Array`, `QVariantMap`), Qt6::Positioning
  (`QGeoCoordinate`), Qt6::Qml (`QtQmlIntegration`).
- **Project-internal dependency:** `TrackService` (forward-declared in the
  header; the .cpp includes `TrackService.h` for the matcher calls and the
  `TrackMatch`/`RouteMatchRequest` value types).

## 3. Class Hierarchy and Role

`TrainListModel : public QAbstractListModel`. From `QAbstractListModel` it
inherits the model/view contract — it must (and does) override `rowCount`, `data`
and `roleNames`, and it emits `beginInsertRows`/`endInsertRows`,
`beginRemoveRows`/`endRemoveRows` and `dataChanged` so the attached `MapItemView`
updates incrementally rather than rebuilding the whole layer. Its role is the
single source of truth for live-train marker state.

## 4. Q_PROPERTY Declarations

| Property | Type | READ | WRITE | NOTIFY | Description |
|----------|------|------|-------|--------|-------------|
| `count` | `int` | `count` | — | `countChanged` | Number of rows (trains currently held). Read-only. |

## 5. Enumerations

`TrainListModel::Role` — the custom item-data roles (each maps to the QML role
name in parentheses). Used as the `role` argument to `data()` and surfaced via
`roleNames()`:

| Value | Integer | Description |
|-------|---------|-------------|
| `TrainNumberRole` | `Qt::UserRole + 1` | Train number (`trainNumber`). |
| `DepartureDateRole` | +2 | Run's departure date "YYYY-MM-DD" (`departureDate`); needed for the timetable endpoint. |
| `CoordinateRole` | +3 | `QGeoCoordinate` (`coordinate`); bind directly to `MapQuickItem.coordinate`. The snapped position when matching is active. |
| `SpeedRole` | +4 | Speed in km/h (`speed`). |
| `BearingRole` | +5 | Heading in degrees, derived from successive positions (`bearing`). |
| `TimestampRole` | +6 | UTC report time (`timestamp`). |
| `CategoryRole` | +7 | Broad class — "Commuter"/"Long-distance"/"Cargo"/… (`category`). |
| `TrainTypeRole` | +8 | Train type "IC"/"S"/… (`trainType`); drives marker colour. |
| `CommuterLineRole` | +9 | Commuter line letter "R"/"Z"/… (`commuterLine`), "" if none. |
| `RingStateRole` | +10 | Status-ring state "green"/"amber"/"red"/"stale"/"none" (`ringState`). |
| `DelayMinutesRole` | +11 | Live delay at the last passed stop (`delayMinutes`). |
| `AccuracyRole` | +12 | GPS uncertainty radius in metres (`accuracy`); −1 if unreported. |
| `TrackOffsetRole` | +13 | Metres from the nearest rail after map-matching (`trackOffsetMeters`); −1 if not matched. |
| `NearestNeighborRole` | +14 | Metres to the nearest other live train (`nearestNeighborMeters`); −1 if none. Drives label declutter near a busy terminus. Recomputed once per REST snapshot, not per MQTT upsert. |

## 6. Public Member Variables

None public. The header does define several value structs used at the API
boundary:

`TrainPosition` — one decoded position: `trainNumber` (int), `departureDate`
(QString), `coordinate` (QGeoCoordinate, WGS84), `speed` (double km/h),
`timestamp` (QDateTime UTC), `accuracy` (int m, −1 if unreported).

`TrainStatus` — live running status: `delayMinutes` (int), `cancelled` (bool),
`running` (bool, `runningCurrently`), `known` (bool, false until `/live-trains`
has reported the train).

`TrainRoute` — scheduled path: `codes` (`QVector<QString>` ordered station codes)
and `commercialTrack` (`QHash<QString,QString>` per-stop platform).

`TrainKey` — the composite identity `(departureDate, trainNumber)`. Train numbers
are reused daily and the same number can run under two dates at once, so every
per-train map is keyed on this pair. Provides `operator==` and a `qHash`
overload.

## 7. Signals

#### void countChanged()

Emitted when the row count changes (a train inserted or pruned).

## 8. Public Slots and Q_INVOKABLE Methods

#### Q_INVOKABLE QVariantMap matchInfoFor(int trainNumber, const QString &departureDate) const

Returns map-matching diagnostics for one train, for the QML debug overlay /
detail panel: `{ rawLat, rawLon, snapLat, snapLon, offset, tunniste, onRoute,
accuracy }`. Returns an empty map if the train isn't present. Callable from QML
(it is polled on a timer in `Main.qml`, since the diagnostics aren't a notifying
role).

## 9. Public Methods

#### int rowCount(const QModelIndex &parent = QModelIndex()) const [override]

See section 10.

#### QVariant data(const QModelIndex &index, int role) const [override]

See section 10.

#### QHash<int, QByteArray> roleNames() const [override]

See section 10.

#### int count() const

Row count (the `count` property getter).

#### void updateTrains(const QVector<TrainPosition> &trains)

Merges a REST snapshot: upserts every fetched train through `applyOne`, then
prunes trains missing from the snapshot **only once they've aged past a 120 s
grace window** (so a just-arrived MQTT-only train isn't deleted before REST catches
up). Merges rather than replaces so concurrent MQTT updates aren't clobbered and
markers don't flicker. Also recomputes `nearestNeighborMeters` for every row
(`recomputeNearestNeighbors`, O(n²) over the live fleet — cheap at the 60 s REST
cadence) and garbage-collects bearing history down to live trains.

#### void upsertTrain(const TrainPosition &train)

Inserts or updates a single train in place (the MQTT path) via `applyOne`; only
the changed row is touched.

#### void setTrainMetadata(const QHash<TrainKey, QString> &types, const QHash<TrainKey, QString> &categories, const QHash<TrainKey, QString> &commuterLines)

Supplies the type/category/commuter-line maps (from `/live-trains`). Markers are
coloured by type/category and labelled by line/type; existing rows repaint
(`dataChanged` over `CategoryRole`, `TrainTypeRole`, `CommuterLineRole`,
`RingStateRole`).

#### void setTrainStatuses(const QHash<TrainKey, TrainStatus> &statuses)

Supplies the running-status map (delay/cancelled/running). Drives the marker
status ring; existing rows repaint (`RingStateRole`, `DelayMinutesRole`).

#### void setMatcher(const TrackService *matcher)

Sets the (optional, **not owned**) rail-network matcher used to snap/flag incoming
fixes. With none set, positions are stored raw.

#### void setStationCoords(const QHash<QString, QGeoCoordinate> &coords)

Supplies station short-code → coordinate (from `/metadata/stations`), used to pin
a stopped, off-network train to the station it's booked at.

#### void setTrainRoutes(const QHash<TrainKey, TrainRoute> &routes)

Supplies the per-train scheduled routes (codes + per-stop platform), driving
Tier-2 route-constrained matching, platform snapping, and the Tier-1
nearest-station fallback.

## 10. Protected Virtual Methods / Event Handlers

#### int rowCount(const QModelIndex &parent) const [override]

From `QAbstractListModel`. Returns the number of train rows (0 for any valid
parent — this is a flat list).

#### QVariant data(const QModelIndex &index, int role) const [override]

From `QAbstractItemModel`. Returns the value for the given row and `Role`. Bounds-
checks the index and returns an invalid `QVariant` for out-of-range indices or
unknown roles. `RingStateRole` is computed on read via `ringStateFor()`; metadata
roles are looked up by `TrainKey`.

#### QHash<int, QByteArray> roleNames() const [override]

From `QAbstractItemModel`. Maps each `Role` to the QML name the delegate binds
(`trainNumber`, `coordinate`, `bearing`, `ringState`, …).

## 11. Ownership and Lifecycle

Parent-owned `QObject`: constructed by `DigitrafficClient` with the client as
parent, so it is destroyed with the client. The model holds a **non-owning** `const
TrackService *m_matcher`. All other state
(rows, index/bearing/metadata/route hashes, station coords) is owned by value.
Although declared `QML_ELEMENT`, it is `QML_UNCREATABLE` — QML obtains the single
instance via `DigitrafficClient.model`, never by constructing it.

## 12. Thread Safety

**GUI-thread only.** It is a `QAbstractListModel` driving a view, and emits
model-change signals that the view consumes on the GUI thread. The matcher it
calls (`TrackService`) is likewise read on the GUI thread. No internal locking.

## 13. QML Exposure

Registered with `QML_ELEMENT` **and** `QML_UNCREATABLE("Obtain via
DigitrafficClient.model")` (module `TrainsOnMap` 1.0). QML uses the role names from
`roleNames()` in delegates and may call the `Q_INVOKABLE matchInfoFor`, but cannot
instantiate the type.

## 14. Inter-Class Interactions

- **`DigitrafficClient`** owns it and calls `updateTrains`, `setTrainMetadata`,
  `setTrainStatuses`, `setTrainRoutes`, `setStationCoords`, `setMatcher`.
- **`DigitrafficMqttClient`** shares it and calls `upsertTrain`.
- **`TrackService`** is consulted per fix (`matchOnRoute` then
  `matchToNetwork`).
- **`Main.qml`** binds the role names in the train `MapItemView` delegate
  (`TrainMarker`) and polls `matchInfoFor` for the Tier-2 debug overlay.

## 15. External Communication

None directly. The model receives already-decoded `TrainPosition` values; the
network I/O lives in `DigitrafficClient`/`DigitrafficMqttClient`. The inline
`parseTrainLocation` helper in the header converts one Digitraffic JSON object
(the same shape over REST and MQTT) into a `TrainPosition`, handling the GeoJSON
`[lon, lat]` → `QGeoCoordinate(lat, lon)` axis order.
