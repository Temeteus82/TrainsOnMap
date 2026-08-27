#include "TrainFilterModel.h"

#include <QAbstractItemModel>

TrainFilterModel::TrainFilterModel(QObject *parent)
    : QSortFilterProxyModel{parent}
{
    setDynamicSortFilter(true);
    // Keep the list stable to read: rows are inserted and pruned continuously as
    // the fleet turns over, and an unsorted proxy would reorder under the cursor.
    setSortRole(Qt::DisplayRole);   // replaced with the real role in setSourceModel
    connect(this, &QAbstractItemModel::rowsInserted, this, &TrainFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &TrainFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &TrainFilterModel::countChanged);
}

void TrainFilterModel::setSourceModel(QAbstractItemModel *sourceModel)
{
    // Resolve the role numbers BEFORE delegating: the base emits modelReset from
    // inside its own endResetModel(), and QSortFilterProxyModel builds its row
    // mapping lazily on the first query after that. A client already attached to
    // the proxy — here the ListView, since sourceModel is a QML binding —
    // queries during that reset, and the mapping built then is kept. Resolving
    // the roles afterwards would let that pass run with every role at -1, which
    // fails open (see filterAcceptsRow) and leaves the filter inert until
    // something else invalidates it. This is the exact ordering bug
    // TimetableFilterModel shipped with once; don't reintroduce it here.
    if (sourceModel) {
        const QHash<int, QByteArray> roles = sourceModel->roleNames();
        // Reverse lookup is unambiguous: role names are unique per metaobject.
        m_numberRole = roles.key("trainNumber", -1);
        m_typeRole = roles.key("trainType", -1);
        m_lineRole = roles.key("commuterLine", -1);
        m_categoryRole = roles.key("category", -1);
        if (m_numberRole >= 0)
            setSortRole(m_numberRole);
    }

    QSortFilterProxyModel::setSourceModel(sourceModel);

    if (m_numberRole >= 0)
        sort(0, Qt::AscendingOrder);
}

void TrainFilterModel::setSearchText(const QString &text)
{
    if (m_searchText == text)
        return;
    m_searchText = text;
    refilter();
    emit searchTextChanged();
}

void TrainFilterModel::setShowCommuter(bool show)
{
    if (m_showCommuter == show)
        return;
    m_showCommuter = show;
    refilter();
    emit filtersChanged();
}

void TrainFilterModel::setShowLongDistance(bool show)
{
    if (m_showLongDistance == show)
        return;
    m_showLongDistance = show;
    refilter();
    emit filtersChanged();
}

void TrainFilterModel::setShowCargo(bool show)
{
    if (m_showCargo == show)
        return;
    m_showCargo = show;
    refilter();
    emit filtersChanged();
}

void TrainFilterModel::refilter()
{
    // Row acceptance is all that changed, so invalidate rows only — a full reset
    // would discard the ListView's instantiated delegates and its scroll
    // position, which is the whole reason this is a proxy and not a rebuild.
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

bool TrainFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QAbstractItemModel *src = sourceModel();
    if (!src)
        return true;
    const QModelIndex idx = src->index(sourceRow, 0, sourceParent);
    if (!idx.isValid())
        return false;

    // Category toggles. An unknown/empty category always shows: /live-trains
    // metadata lands after the position snapshot, so filtering on a
    // not-yet-known category would make trains vanish for the first few seconds
    // — the same fail-open the map delegate uses.
    if (m_categoryRole >= 0) {
        const QString category = src->data(idx, m_categoryRole).toString();
        if (category == QLatin1String("Commuter") && !m_showCommuter)
            return false;
        if (category == QLatin1String("Long-distance") && !m_showLongDistance)
            return false;
        if (category == QLatin1String("Cargo") && !m_showCargo)
            return false;
    }

    if (m_searchText.isEmpty())
        return true;

    // Match what the map badge shows: the number, the type, the line letter.
    if (m_numberRole >= 0
        && QString::number(src->data(idx, m_numberRole).toInt()).contains(m_searchText))
        return true;
    if (m_typeRole >= 0
        && src->data(idx, m_typeRole).toString().contains(m_searchText, Qt::CaseInsensitive))
        return true;
    if (m_lineRole >= 0) {
        const QString line = src->data(idx, m_lineRole).toString();
        if (!line.isEmpty() && line.contains(m_searchText, Qt::CaseInsensitive))
            return true;
    }
    return false;
}
