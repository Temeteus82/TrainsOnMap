#include "TimetableModel.h"

TimetableModel::TimetableModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int TimetableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_stops.size();
}

QVariant TimetableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_stops.size())
        return {};

    const TimetableStop &s = m_stops.at(index.row());
    switch (role) {
    case StationNameRole:        return s.stationName;
    case StationShortCodeRole:   return s.stationShortCode;
    case ScheduledArrivalRole:   return s.scheduledArrival;
    case EstimatedArrivalRole:   return s.estimatedArrival;
    case ScheduledDepartureRole: return s.scheduledDeparture;
    case EstimatedDepartureRole: return s.estimatedDeparture;
    case DelayMinutesRole:       return s.delayMinutes;
    case TrackRole:              return s.track;
    case CancelledRole:          return s.cancelled;
    default:                     return {};
    }
}

QHash<int, QByteArray> TimetableModel::roleNames() const
{
    return {
        { StationNameRole,        "stationName" },
        { StationShortCodeRole,   "stationShortCode" },
        { ScheduledArrivalRole,   "scheduledArrival" },
        { EstimatedArrivalRole,   "estimatedArrival" },
        { ScheduledDepartureRole, "scheduledDeparture" },
        { EstimatedDepartureRole, "estimatedDeparture" },
        { DelayMinutesRole,       "delayMinutes" },
        { TrackRole,              "track" },
        { CancelledRole,          "cancelled" },
    };
}

void TimetableModel::setStops(const QVector<TimetableStop> &stops)
{
    beginResetModel();
    m_stops = stops;
    endResetModel();
    emit countChanged();
}

void TimetableModel::clear()
{
    if (m_stops.isEmpty())
        return;
    beginResetModel();
    m_stops.clear();
    endResetModel();
    emit countChanged();
}
