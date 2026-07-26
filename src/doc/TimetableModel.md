# TimetableModel

## 1. Class Overview

`TimetableModel` is the list model behind the train detail panel's timetable: one
row per station on the selected train's journey, with the ARRIVAL and DEPARTURE
timetable rows for a station folded into a single entry. It also computes
**journey progress** — how far along its booked stops the train has demonstrably
travelled — exposed as model properties the panel uses to draw a progress marker.

It is built on **`QRangeModel`** (Qt 6.10+), the reflective range-adapting model.
The row type `TimetableStop` is a `Q_GADGET` whose `Q_PROPERTY` declarations *are*
the model's schema: `QRangeModel` reflects each property into a QML role of the
same name, so there is no hand-written `data()`/`roleNames()`/role enum. The class
itself only adds a type-safe replace/clear API and a few convenience properties.

## 2. Project Structure and Dependencies

- **Owned by `TrainDetailsService`** and exposed via its `model` property; QML
  cannot construct one (`QML_UNCREATABLE`). Often viewed through a
  `TimetableFilterModel` proxy.
- **Unit-tested** directly by `tests/tst_timetablemodel.cpp`, which pins the
  reflected role table and the `setStops`/`clear`/`count` contract.
- **Qt modules:** Qt6::Core, Qt6::Qml — `QRangeModel` lives in QtQml, and the
  test target links `Qt6::Qml` for the `QtQmlIntegration` header. Requires Qt 6.10+
  (`QRangeModel`) and the multi-role row representation (Qt 6.11+).

## 3. Class Hierarchy and Role

`TimetableModel : public QRangeModel`. `QRangeModel` is a concrete
`QAbstractItemModel` that adapts a C++ range (here a `QVector<TimetableStop>`) to
the model/view contract, deriving `rowCount`/`data`/`roleNames` from the element
type's metaobject. `TimetableModel` passes a pointer to its own member container
to the base constructor (the encapsulation pattern from the `QRangeModel` docs),
so structural edits go through the `QAbstractItemModel` API. Its role is to own the
stop data, derive progress, and provide a small typed surface over the generic
base.

## 4. Q_PROPERTY Declarations

On the model:

| Property | Type | READ | WRITE | NOTIFY | Description |
|----------|------|------|-------|--------|-------------|
| `count` | `int` | `count` | — | `countChanged` | Number of timing points (rows). Read-only. |
| `passedStops` | `int` | `passedStops` | — | `progressChanged` | Booked (stopping) stops the train has already left. Read-only. |
| `totalStops` | `int` | `totalStops` | — | `progressChanged` | Total booked stops on the route. Read-only. |
| `nextStopRow` | `int` | `nextStopRow` | — | `progressChanged` | Source row of the NEXT booked stop; −1 when none/complete. Read-only. |

On the `TimetableStop` gadget — each becomes a QML **role** of the same name
(`MEMBER`-backed):

| Property (role) | Type | Description |
|-----------------|------|-------------|
| `stationName` | `QString` | Resolved station name; falls back to the short code. |
| `stationShortCode` | `QString` | Station short code. |
| `scheduledArrival` | `QString` | Local "HH:mm"; empty for the origin. |
| `estimatedArrival` | `QString` | Local "HH:mm"; empty when same as scheduled / unknown. |
| `scheduledDeparture` | `QString` | Local "HH:mm"; empty for the destination. |
| `estimatedDeparture` | `QString` | Local "HH:mm". |
| `delayMinutes` | `int` | Latest known difference (departure preferred). |
| `track` | `QString` | Commercial track; may be empty. |
| `cancelled` | `bool` | Stop cancelled. |
| `stopping` | `bool` | True = booked/commercial stop; false = passed through. |
| `passed` | `bool` | The train has already departed/arrived here (journey progress). |
| `isNext` | `bool` | First booked stop the train hasn't reached yet. |

## 5. Enumerations

None. The role table is derived from `TimetableStop`'s properties, not a
hand-written enum.

## 6. Public Member Variables

None on the model. `TimetableStop` is a public `Q_GADGET` value type whose data
members back the roles above; it additionally carries three **build-time
accumulators deliberately left without `Q_PROPERTY`** (so they stay invisible to
the model and QML): `sawCommercial`, `sawTrainStopping`, and `sawActual` — used by
`TrainDetailsService`/`setStops` to resolve `stopping` and `passed`.

The header also specialises `QRangeModel::RowOptions<TimetableStop>` to
`RowCategory::MultiRoleItem`, opting the gadget into multi-role-per-item
representation (Qt 6.11+) so its properties map to **roles on one item** rather
than to columns — exactly what the QML list delegate needs.

## 7. Signals

#### void countChanged()

Emitted when the number of rows changes (on `setStops`/`clear`).

#### void progressChanged()

Emitted when the derived journey progress changes (`passedStops`/`totalStops`/
`nextStopRow`), including on every live rebuild so the progress marker advances.

## 8. Public Slots and Q_INVOKABLE Methods

None.

## 9. Public Methods

#### int count() const / int passedStops() const / int totalStops() const / int nextStopRow() const

Getters for the four model properties.

#### void setStops(QVector<TimetableStop> stops)

Replaces the stop list. Takes its argument **by value** and moves it into the
backing container: this is rebuilt on every live MQTT update for the selected
train, so `TrainDetailsService::rebuildStops()` `std::move()`s its resolved
vector in rather than paying a second copy. Before the swap it derives journey
progress from each stop's `sawActual` flag: the last point that recorded an
`actualTime` is the furthest the train has demonstrably reached, so every point
up to and including it is marked `passed`, and the first booked stop after it is
marked `isNext`. Counts
`totalStops`/`passedStops` over the booked stops and records `nextStopRow`.
Recomputed on every (re)build, so live MQTT updates advance the marker. Uses a full
`beginResetModel`/`endResetModel` (timetables are small and arrive wholesale) and
emits `countChanged` + `progressChanged`.

#### void clear()

Empties the model (reset) and zeroes the progress counters (`nextStopRow` = −1).
No-op when already empty. Emits `countChanged` + `progressChanged`.

## 10. Protected Virtual Methods / Event Handlers

None overridden. `rowCount`, `data`, `roleNames`, `index`, `parent` etc. are all
provided by `QRangeModel` from the backing container and `TimetableStop`'s
metaobject. `setStops`/`clear` drive structural change through the base
`begin/endResetModel` API.

## 11. Ownership and Lifecycle

Parent-owned `QObject`: constructed by `TrainDetailsService` with the service as
parent, destroyed with it. The backing `QVector<TimetableStop> m_stops` is a value
member. **Construction-order note:** the `QRangeModel` base is constructed (with
`&m_stops`) before `m_stops` itself — safe because `QRangeModel` only introspects
`TimetableStop`'s metaobject during construction (a type-level operation), never
dereferencing the container's data. `QML_ELEMENT` but `QML_UNCREATABLE`.

## 12. Thread Safety

**GUI-thread only.** A view-driving model updated and read on the GUI thread.

## 13. QML Exposure

Registered with `QML_ELEMENT` and `QML_UNCREATABLE("Obtain via
TrainDetailsService.model")` (module `TrainsOnMap` 1.0). The detail panel binds the
gadget-derived role names (`stationName`, `scheduledDeparture`, `stopping`, …) in
its delegate and reads `passedStops`/`totalStops`/`nextStopRow` for the progress
indicator — typically through a `TimetableFilterModel` proxy.

## 14. Inter-Class Interactions

- **`TrainDetailsService`** owns it and is the sole writer (`setStops` after a
  timetable fetch or live MQTT update; `clear` on deselect).
- **`TimetableFilterModel`** proxies it to hide non-stopping timing points, and
  caches its `"stopping"` role number from `roleNames()`.

## 15. External Communication

None. It holds already-parsed stops; the Digitraffic fetch and JSON decode live in
`TrainDetailsService`.

## 16. Usage Example

```cpp
#include "TimetableModel.h"

auto *model = new TimetableModel(this);

QVector<TimetableStop> stops;
TimetableStop origin;
origin.stationShortCode = QStringLiteral("HKI");
origin.stationName      = QStringLiteral("Helsinki");
origin.scheduledDeparture = QStringLiteral("14:09");
origin.stopping = true;
origin.sawActual = true;            // train has departed here
stops.push_back(origin);
// … more stops …

model->setStops(stops);             // resets the model; derives passed/isNext
// model->nextStopRow() now points at the first not-yet-reached booked stop.
```
