#include "TimetableFilterModel.h"

TimetableFilterModel::TimetableFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
}

void TimetableFilterModel::setShowAll(bool showAll)
{
    if (m_showAll == showAll)
        return;
    // Row-only filter change; the begin/endFilterChange pair is the non-deprecated
    // replacement for invalidateFilter() (Qt 6.9+/6.10+).
    beginFilterChange();
    m_showAll = showAll;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    emit showAllChanged();
}

void TimetableFilterModel::setSourceModel(QAbstractItemModel *sourceModel)
{
    QSortFilterProxyModel::setSourceModel(sourceModel);
    // QRangeModel names each role after the backing gadget's Q_PROPERTY, so
    // TimetableStop::stopping is exposed as the "stopping" role. Cache its number
    // here instead of rebuilding roleNames() on every filter test. A source reset
    // (setStops) keeps the same schema, so the cached role stays valid.
    m_stoppingRole = sourceModel ? sourceModel->roleNames().key("stopping", -1) : -1;
}

bool TimetableFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    // Show everything when expanded, or if the source lacks a "stopping" role
    // (schema changed) — never silently hide rows in that case.
    if (m_showAll || m_stoppingRole < 0)
        return true;
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    return idx.data(m_stoppingRole).toBool();
}
