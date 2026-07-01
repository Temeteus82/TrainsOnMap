#include "TrackListModel.h"

#include <QSet>

TrackListModel::TrackListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int TrackListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_paths.size();
}

QVariant TrackListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_paths.size())
        return {};
    if (role == PathRole)
        return m_paths.at(index.row());
    if (role == MainTrackRole)
        return m_main.at(index.row());
    return {};
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    return { { PathRole, "path" }, { MainTrackRole, "mainTrack" } };
}

void TrackListModel::setVisibleSegments(const QVector<int> &ids, const QVector<QVariantList> &paths,
                                        const QVector<bool> &mains)
{
    const int oldCount = m_paths.size();

    // Phase 1 — remove rows whose id is no longer in the new set, batching
    // contiguous runs into a single begin/endRemoveRows.
    const QSet<int> wanted(ids.begin(), ids.end());
    int row = 0;
    while (row < m_ids.size()) {
        if (wanted.contains(m_ids.at(row))) {
            ++row;
            continue;
        }
        int end = row;
        while (end < m_ids.size() && !wanted.contains(m_ids.at(end)))
            ++end;
        beginRemoveRows(QModelIndex(), row, end - 1);
        m_ids.remove(row, end - row);
        m_paths.remove(row, end - row);
        m_main.remove(row, end - row);
        endRemoveRows();
        // row now indexes the element that shifted down into this slot.
    }

    // Phase 2 — insert new ids. After phase 1, m_ids is a subsequence of `ids`
    // (both ascending), so walk them together and insert the missing runs.
    int cur = 0;  // index into the live model (m_ids)
    int k = 0;    // index into the incoming ids
    while (k < ids.size()) {
        if (cur < m_ids.size() && m_ids.at(cur) == ids.at(k)) {
            ++cur;
            ++k;
            continue;
        }
        // ids[k] is missing; gather the run that all belongs before m_ids[cur].
        int runEnd = k;
        while (runEnd < ids.size()
               && (cur >= m_ids.size() || ids.at(runEnd) < m_ids.at(cur)))
            ++runEnd;
        const int n = runEnd - k;
        beginInsertRows(QModelIndex(), cur, cur + n - 1);
        for (int j = runEnd - 1; j >= k; --j) {  // reverse-insert keeps ascending order
            m_ids.insert(cur, ids.at(j));
            m_paths.insert(cur, paths.at(j));
            m_main.insert(cur, mains.at(j));
        }
        endInsertRows();
        cur += n;
        k = runEnd;
    }

    if (m_paths.size() != oldCount)
        emit countChanged();
}
