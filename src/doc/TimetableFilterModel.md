# TimetableFilterModel

## 1. Class Overview

`TimetableFilterModel` is a thin view-side proxy over `TimetableModel`. By default
it drops passed-through (non-stopping) timing points, showing only the booked
stops; a `showAll` flag reveals every timing point.

The reason it filters *in the model* rather than hiding rows with zero-height
delegates is performance: a zero-height delegate still counts as instantiated, so
a `ListView` keeps building rows until the source is exhausted — a long route's
hundred-plus timing points all get constructed even though only a dozen booked
stops are visible. By removing the passing points from the row set, the view only
ever materialises the rows it actually shows, so the timetable `ListView`
virtualises properly. Roles pass straight through, so the delegate binds the same
role names (`stationName`, `stopping`, …) it would on the source model.

## 2. Project Structure and Dependencies

- **Used by the detail panel** (`TrainDetailPanel.qml`) wrapping
  `TrainDetailsService.model`; QML *can* construct this one (it is a plain
  `QML_ELEMENT`, not uncreatable).
- **Qt modules:** Qt6::Core (`QSortFilterProxyModel`), Qt6::Qml
  (`QtQmlIntegration`).
- **Source model:** `TimetableModel` (a `QRangeModel`) — the proxy reads its
  `"stopping"` role.

## 3. Class Hierarchy and Role

`TimetableFilterModel : public QSortFilterProxyModel`. From
`QSortFilterProxyModel` it inherits transparent row/column mapping between a source
model and a filtered/sorted view; this subclass overrides only the row-acceptance
predicate (and customises `setSourceModel` to cache a role number). Its role is
purely presentational filtering — no sorting is configured.

## 4. Q_PROPERTY Declarations

| Property | Type | READ | WRITE | NOTIFY | Description |
|----------|------|------|-------|--------|-------------|
| `showAll` | `bool` | `showAll` | `setShowAll` | `showAllChanged` | `false` (default) shows only booked stops; `true` shows every timing point, passed-through points included. Bound to the panel's "Show all timing points" toggle. |

## 5. Enumerations

None.

## 6. Public Member Variables

None public. (Private: `m_showAll`, and `m_stoppingRole` — the cached `"stopping"`
role number of the source model, −1 if unavailable.)

## 7. Signals

#### void showAllChanged()

Emitted when the `showAll` flag flips.

## 8. Public Slots and Q_INVOKABLE Methods

#### Q_INVOKABLE int proxyRowForSource(int sourceRow) const

Maps a source-model row to its row in this filtered view, or −1 if the row is
currently hidden or out of range. This lets the panel scroll the (proxied)
`ListView` to a stop identified by its **source** index — specifically the NEXT
stop reported by `TimetableModel::nextStopRow`.

## 9. Public Methods

#### bool showAll() const / void setShowAll(bool showAll)

Getter/setter for the `showAll` property. The setter wraps the change in the
non-deprecated `beginFilterChange()` / `endFilterChange(Direction::Rows)` pair
(Qt 6.9+/6.10+ replacement for `invalidateFilter()`), since only row acceptance
changes, then emits `showAllChanged`.

#### void setSourceModel(QAbstractItemModel *sourceModel) [override]

From `QSortFilterProxyModel`. Caches the `"stopping"` role number from the source's
`roleNames()` — so the per-row filter test needn't rebuild `roleNames()` each call —
and then delegates to the base. A source reset (`setStops`) keeps the same schema,
so the cached role stays valid.

The order matters: the base implementation emits `modelReset` from inside its own
`endResetModel()`, and `QSortFilterProxyModel` builds its row mapping lazily on the
first query after that. A client attached to the proxy before the source is set —
in the app the `ListView`, since `sourceModel` is a QML binding — queries during
that reset, and the mapping built then is kept. Caching the role after the base
call let that pass run fail-open, leaving every passing point visible until the
next invalidation.

## 10. Protected Virtual Methods / Event Handlers

#### bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const [override]

From `QSortFilterProxyModel`. Returns `true` for every row when `showAll` is set or
when the source lacks a `"stopping"` role (schema changed — never silently hide
rows in that case); otherwise returns the row's `"stopping"` value, so only booked
stops are accepted.

## 11. Ownership and Lifecycle

Parent-owned `QObject`. When declared in QML it is owned by the QML engine; it does
**not** own its source model (`TimetableModel`, owned by `TrainDetailsService`) —
the proxy must not outlive a source it points at, but in practice both live for the
panel's lifetime.

## 12. Thread Safety

**GUI-thread only**, like any view-side proxy model.

## 13. QML Exposure

Registered with `QML_ELEMENT` (module `TrainsOnMap` 1.0) and **creatable** from
QML. The panel constructs a `TimetableFilterModel { sourceModel:
trainDetails.model }`, binds `showAll` to the toggle, and calls
`proxyRowForSource` to scroll to the next stop.

## 14. Inter-Class Interactions

- **`TimetableModel`** is the source; the proxy reads its `"stopping"` role and
  maps its `nextStopRow` source index through `proxyRowForSource`.
- **`TrainDetailPanel.qml`** owns the proxy, binds `showAll`, and uses the proxied
  view for virtualised scrolling.

## 15. External Communication

None.

## 16. Usage Example

```cpp
#include "TimetableFilterModel.h"

auto *proxy = new TimetableFilterModel(this);
proxy->setSourceModel(timetableModel);   // a TimetableModel*
proxy->setShowAll(false);                // booked stops only

// Scroll a ListView to the next booked stop (identified by its source row).
const int proxyRow = proxy->proxyRowForSource(timetableModel->nextStopRow());
if (proxyRow >= 0)
    listView->positionViewAtIndex(proxyRow, ...);
```
