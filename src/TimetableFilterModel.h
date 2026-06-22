#pragma once

#include <QSortFilterProxyModel>
#include <QtQmlIntegration>

/// View-side proxy over TimetableModel that drops passed-through (non-stopping)
/// timing points unless `showAll` is set.
///
/// Filtering in the model — rather than hiding rows with zero-height delegates —
/// is what lets the timetable ListView virtualise. A zero-height delegate adds
/// nothing to the view's filled height, so the view keeps instantiating rows
/// until the model is exhausted: a long route's hundred-plus timing points all
/// get built even when only the dozen booked stops are visible. With the passing
/// points filtered out of the row set, the view only ever materialises the stops
/// it actually shows. Roles pass straight through, so the delegate binds the same
/// role names (`stationName`, `stopping`, …) it would on the source model.
class TimetableFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    /// false (default) shows only booked stops; true shows every timing point,
    /// passed-through points included. Bound to the panel's "Show all timing
    /// points" toggle.
    Q_PROPERTY(bool showAll READ showAll WRITE setShowAll NOTIFY showAllChanged)

public:
    explicit TimetableFilterModel(QObject *parent = nullptr);

    bool showAll() const { return m_showAll; }
    void setShowAll(bool showAll);

    void setSourceModel(QAbstractItemModel *sourceModel) override;

signals:
    void showAllChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    bool m_showAll = false;
    int m_stoppingRole = -1;   ///< cached "stopping" role of the source model
};
