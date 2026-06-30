# CompositionModel

## 1. Class Overview

`CompositionModel` is the list model behind the detail panel's **carriage strip** —
one row per vehicle in a train's physical makeup (a locomotive or a passenger
wagon), ordered front-to-back as a passenger would walk past them.

Like `TimetableModel`, it is built on **`QRangeModel`** (Qt 6.10+) over a
`Q_GADGET` row type. The row type `CompositionVehicle` declares its schema purely
through `Q_PROPERTY`s, which `QRangeModel` reflects into QML roles of the same
name — so there is no hand-written `data()`/`roleNames()`. The class adds only a
type-safe replace/clear API and a `count` convenience property.

## 2. Project Structure and Dependencies

- **Owned by `TrainDetailsService`** and exposed via its `composition` property;
  QML cannot construct one (`QML_UNCREATABLE`).
- **Unit-tested** by `tests/tst_compositionmodel.cpp`, which pins the reflected
  role table (including the `QStringList amenities` role) and the
  `setVehicles`/`clear`/`count` contract.
- **Qt modules:** Qt6::Core, Qt6::Qml (`QRangeModel`, `QtQmlIntegration`).
  Requires Qt 6.10+ and the multi-role row representation (Qt 6.11+).

## 3. Class Hierarchy and Role

`CompositionModel : public QRangeModel`. `QRangeModel` adapts the member
`QVector<CompositionVehicle>` to the model/view contract, deriving every role from
`CompositionVehicle`'s metaobject. The base is constructed with a pointer to the
member container so structural edits go through the `QAbstractItemModel` API. Its
role is to own the consist and present it as a list.

## 4. Q_PROPERTY Declarations

On the model:

| Property | Type | READ | WRITE | NOTIFY | Description |
|----------|------|------|-------|--------|-------------|
| `count` | `int` | `count` | — | `countChanged` | Number of vehicles in the consist. Read-only. |

On the `CompositionVehicle` gadget — each becomes a QML **role** of the same name
(`MEMBER`-backed):

| Property (role) | Type | Description |
|-----------------|------|-------------|
| `position` | `int` | Physical order in the consist (API `location`); used to sort. |
| `locomotive` | `bool` | True = locomotive, false = passenger wagon. |
| `label` | `QString` | Passenger-facing car number (wagon); empty for a loco. |
| `vehicleType` | `QString` | `wagonType` ("Ed") or `locomotiveType` ("Sr2"). |
| `powerType` | `QString` | Loco only, e.g. "Electric" / "Diesel". |
| `amenities` | `QStringList` | Human-readable list, e.g. ["Catering", "Accessible"]. |

## 5. Enumerations

None — the role table is derived from `CompositionVehicle`'s properties.

## 6. Public Member Variables

None on the model. `CompositionVehicle` is a public `Q_GADGET` value type whose
members back the roles above (all exposed; unlike `TimetableStop` it has no hidden
accumulators).

The header specialises `QRangeModel::RowOptions<CompositionVehicle>` to
`RowCategory::MultiRoleItem`, opting the gadget into multi-role-per-item
representation (Qt 6.11+) so its properties map to roles on one item rather than
columns — exactly as `TimetableModel` does for `TimetableStop`.

## 7. Signals

#### void countChanged()

Emitted when the number of vehicles changes (on `setVehicles`/`clear`).

## 8. Public Slots and Q_INVOKABLE Methods

None.

## 9. Public Methods

#### int count() const

Vehicle count (the `count` property getter).

#### void setVehicles(const QVector<CompositionVehicle> &vehicles)

Replaces the consist wholesale (a consist is small and arrives as a unit) via
`beginResetModel`/`endResetModel`; emits `countChanged`.

#### void clear()

Empties the model (reset). No-op when already empty; emits `countChanged`.

## 10. Protected Virtual Methods / Event Handlers

None overridden. `rowCount`/`data`/`roleNames`/`index`/`parent` come from
`QRangeModel`; `setVehicles`/`clear` drive structural change through the base
reset API.

## 11. Ownership and Lifecycle

Parent-owned `QObject`: constructed by `TrainDetailsService` with the service as
parent, destroyed with it. The backing `QVector<CompositionVehicle> m_vehicles` is
a value member. **Construction-order note:** the `QRangeModel` base is constructed
(with `&m_vehicles`) before `m_vehicles` — safe because `QRangeModel` only
introspects `CompositionVehicle`'s metaobject during construction, never the data.
`QML_ELEMENT` but `QML_UNCREATABLE`.

## 12. Thread Safety

**GUI-thread only.** A view-driving model updated and read on the GUI thread.

## 13. QML Exposure

Registered with `QML_ELEMENT` and `QML_UNCREATABLE("Obtain via
TrainDetailsService.composition")` (module `TrainsOnMap` 1.0). The detail panel's
carriage delegate binds the gadget-derived role names (`locomotive`, `label`,
`vehicleType`, `amenities`, …).

## 14. Inter-Class Interactions

- **`TrainDetailsService`** owns it and is the sole writer: it flattens a
  composition API `journeySection` into ordered `CompositionVehicle`s
  (locomotives first, then wagons, sorted by `position`) and calls `setVehicles`;
  `clear` on deselect or when a run has no stock data.

## 15. External Communication

None. It holds already-parsed vehicles; the `/compositions/{date}/{number}` fetch
and JSON decode live in `TrainDetailsService`.

## 16. Usage Example

```cpp
#include "CompositionModel.h"

auto *model = new CompositionModel(this);

QVector<CompositionVehicle> consist;
CompositionVehicle loco;
loco.position = 0;
loco.locomotive = true;
loco.vehicleType = QStringLiteral("Sr2");
loco.powerType = QStringLiteral("Electric");
consist.push_back(loco);

CompositionVehicle car;
car.position = 1;
car.label = QStringLiteral("24");
car.vehicleType = QStringLiteral("Ed");
car.amenities = { QStringLiteral("Catering") };
consist.push_back(car);

model->setVehicles(consist);   // resets the model; count() == 2
```
