#include "TrainListModel.h"

TrainListModel::TrainListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int TrainListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

QVariant TrainListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Row &row = m_rows.at(index.row());
    switch (role) {
    case TrainNumberRole:   return row.pos.trainNumber;
    case DepartureDateRole: return row.pos.departureDate;
    case CoordinateRole:  return QVariant::fromValue(row.pos.coordinate);
    case SpeedRole:       return row.pos.speed;
    case BearingRole:     return row.bearing;
    case TimestampRole:   return row.pos.timestamp;
    default:              return {};
    }
}

QHash<int, QByteArray> TrainListModel::roleNames() const
{
    return {
        { TrainNumberRole, "trainNumber" },
        { DepartureDateRole, "departureDate" },
        { CoordinateRole,  "coordinate" },
        { SpeedRole,       "speed" },
        { BearingRole,     "bearing" },
        { TimestampRole,   "timestamp" },
    };
}

double TrainListModel::bearingFor(int trainNumber, const QGeoCoordinate &coordinate)
{
    double bearing = 0.0;
    const auto prev = m_previous.constFind(trainNumber);
    if (prev != m_previous.constEnd() && prev->isValid() && coordinate.isValid()
        && prev->distanceTo(coordinate) > 1.0) {
        bearing = prev->azimuthTo(coordinate);
    }
    m_previous.insert(trainNumber, coordinate);
    return bearing;
}

void TrainListModel::updateTrains(const QVector<TrainPosition> &trains)
{
    // Authoritative snapshot: rebuild the whole list (and index).
    beginResetModel();

    m_rows.clear();
    m_rows.reserve(trains.size());
    m_indexByNumber.clear();

    for (const TrainPosition &tp : trains) {
        Row row;
        row.pos = tp;
        row.bearing = bearingFor(tp.trainNumber, tp.coordinate);
        m_indexByNumber.insert(tp.trainNumber, m_rows.size());
        m_rows.push_back(row);
    }

    endResetModel();
    emit countChanged();
}

void TrainListModel::upsertTrain(const TrainPosition &train)
{
    const double bearing = bearingFor(train.trainNumber, train.coordinate);

    const auto it = m_indexByNumber.constFind(train.trainNumber);
    if (it != m_indexByNumber.constEnd()) {
        const int rowIndex = it.value();
        Row &row = m_rows[rowIndex];
        row.pos = train;
        row.bearing = bearing;
        const QModelIndex idx = index(rowIndex);
        emit dataChanged(idx, idx,
                         { CoordinateRole, SpeedRole, BearingRole, TimestampRole, DepartureDateRole });
        return;
    }

    const int newRow = m_rows.size();
    beginInsertRows(QModelIndex(), newRow, newRow);
    Row row;
    row.pos = train;
    row.bearing = bearing;
    m_rows.push_back(row);
    m_indexByNumber.insert(train.trainNumber, newRow);
    endInsertRows();
    emit countChanged();
}
