# TrackListModel

## 1. Class Overview

`TrackListModel` is the render model for the railway track layer. Each row is one
track segment — a polyline expressed as a `QVariantList` of `QGeoCoordinate` —
ready to bind to a QML `MapPolyline.path` inside a `MapItemView`.

The whole national network is far larger than any single viewport, so
`TrackService` filters it to the visible area and hands the result to this model.
The key design point: each row carries the segment's **stable id** (its index in
the full network), so `setVisibleSegments()` can diff one viewport against the
next and emit only the incremental insert/remove. The `MapItemView` then rebuilds
just the polylines that entered or left the view, not the entire layer on every
pan — which is what keeps panning smooth over a ~10k-segment network.

## 2. Project Structure and Dependencies

- **Owned by `TrackService`** and exposed via its `model` property; bound to the
  track `MapItemView` in `Main.qml`. QML cannot construct one
  (`QML_UNCREATABLE`).
- **Qt modules:** Qt6::Core (`QAbstractListModel`, `QVariantList`, `QVector`,
  `QSet`), Qt6::Qml (`QtQmlIntegration`). The path elements are `QGeoCoordinate`
  (Qt6::Positioning) boxed into `QVariant`.

## 3. Class Hierarchy and Role

`TrackListModel : public QAbstractListModel`. From `QAbstractListModel` it
inherits the model/view contract (overrides `rowCount`/`data`/`roleNames`) and the
incremental-update signals. Its role is narrow and presentation-only: hold the
currently-visible segments and diff updates so the map view changes minimally.

## 4. Q_PROPERTY Declarations

| Property | Type | READ | WRITE | NOTIFY | Description |
|----------|------|------|-------|--------|-------------|
| `count` | `int` | `count` | — | `countChanged` | Number of visible track segments. Read-only; bound by the InfoPanel. |

## 5. Enumerations

`TrackListModel::Role`:

| Value | Integer | Description |
|-------|---------|-------------|
| `PathRole` | `Qt::UserRole + 1` | The segment's polyline as a `QVariantList<QGeoCoordinate>` (QML role name `path`). The only data role. |

## 6. Public Member Variables

None public. (Private: `m_ids` — ascending segment ids — and `m_paths`, parallel.)

## 7. Signals

#### void countChanged()

Emitted when the number of visible segments changes after a `setVisibleSegments`
diff.

## 8. Public Slots and Q_INVOKABLE Methods

None.

## 9. Public Methods

#### int rowCount(const QModelIndex &parent = QModelIndex()) const [override]

See section 10.

#### QVariant data(const QModelIndex &index, int role) const [override]

See section 10.

#### QHash<int, QByteArray> roleNames() const [override]

See section 10.

#### int count() const

Visible-segment count (the `count` property getter).

#### void setVisibleSegments(const QVector<int> &ids, const QVector<QVariantList> &paths)

Replaces the visible set. `ids` and `paths` are parallel and **must both be sorted
ascending by id** (`TrackService` emits them in network order, which is
ascending). The method diffs against the current rows in two phases:

1. **Remove** rows whose id is no longer wanted, batching contiguous runs into a
   single `begin/endRemoveRows`.
2. **Insert** the new ids: after phase 1 the live ids are a subsequence of the
   incoming ids (both ascending), so it walks them together and inserts the
   missing runs (reverse-inserting within a run to preserve ascending order).

Only the segments that actually changed between viewports are touched. Emits
`countChanged` only if the row count changed.

## 10. Protected Virtual Methods / Event Handlers

#### int rowCount(const QModelIndex &parent) const [override]

From `QAbstractListModel`. Number of visible segments (0 for any valid parent —
flat list).

#### QVariant data(const QModelIndex &index, int role) const [override]

From `QAbstractItemModel`. Returns the row's path for `PathRole`; an invalid
`QVariant` for an out-of-range index or any other role.

#### QHash<int, QByteArray> roleNames() const [override]

From `QAbstractItemModel`. Maps `PathRole` → `"path"`.

## 11. Ownership and Lifecycle

Parent-owned `QObject`: constructed by `TrackService` with the service as parent,
destroyed with it. All state is owned by value. `QML_ELEMENT` but
`QML_UNCREATABLE` — QML reaches the single instance through `TrackService.model`.

## 12. Thread Safety

**GUI-thread only.** It is a view-driving model; `setVisibleSegments` and the
model-change signals run on the GUI thread. (The segments themselves are *prepared*
off-thread inside `TrackService`, but they are handed to this model on the GUI
thread.)

## 13. QML Exposure

Registered with `QML_ELEMENT` and `QML_UNCREATABLE("Obtain via
TrackService.model")` (module `TrainsOnMap` 1.0). The track `MapItemView`'s
delegate binds the `path` role to a `MapPolyline.path`.

## 14. Inter-Class Interactions

- **`TrackService`** owns it and is the sole writer, calling `setVisibleSegments`
  from `loadForBounds` as the viewport changes.
- **`Main.qml`** binds the model to a `MapItemView` and reads `count` in the
  InfoPanel.

## 15. External Communication

None. It holds geometry handed to it in-process; no I/O.
