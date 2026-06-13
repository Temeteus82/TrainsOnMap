#include "TrackListModel.h"

TrackListModel::TrackListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int TrackListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_segments.size();
}

QVariant TrackListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_segments.size())
        return {};
    if (role == PathRole)
        return m_segments.at(index.row());
    return {};
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    return { { PathRole, "path" } };
}

void TrackListModel::setSegments(const QVector<QVariantList> &segments)
{
    beginResetModel();
    m_segments = segments;
    endResetModel();
    emit countChanged();
}
