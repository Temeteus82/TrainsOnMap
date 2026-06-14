#include "TrainListModel.h"

#include <QSet>

namespace {
// A train missing from a REST snapshot is pruned only once its last position is
// older than this, so a just-arrived MQTT-only train isn't deleted before REST
// catches up. Matches the Swift app's staleTrainGrace.
constexpr qint64 kStaleGraceSecs = 120;

// Status-ring thresholds.
constexpr qint64 kStalePositionSecs = 300;   // position older than this reads as stale
constexpr int kVeryLateMinutes = 5;          // delay above this is "very late" (red)
}

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
    case CategoryRole:    return m_categoryByNumber.value(row.pos.trainNumber);
    case TrainTypeRole:   return m_typeByNumber.value(row.pos.trainNumber);
    case CommuterLineRole: return m_lineByNumber.value(row.pos.trainNumber);
    case RingStateRole:   return ringStateFor(row);
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
        { CategoryRole,    "category" },
        { TrainTypeRole,   "trainType" },
        { CommuterLineRole, "commuterLine" },
        { RingStateRole,   "ringState" },
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
    QSet<int> active;
    active.reserve(trains.size());
    for (const TrainPosition &tp : trains)
        active.insert(tp.trainNumber);

    // Prune trains the snapshot no longer reports, but only once they've aged
    // past the grace window — MQTT may know about a train before REST does, and
    // deleting it early would flicker the marker. Walk back-to-front so indices
    // stay valid as rows are removed.
    const QDateTime now = QDateTime::currentDateTimeUtc();
    bool removed = false;
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        const TrainPosition &pos = m_rows.at(i).pos;
        if (active.contains(pos.trainNumber))
            continue;
        if (pos.timestamp.isValid() && pos.timestamp.secsTo(now) < kStaleGraceSecs)
            continue;
        beginRemoveRows(QModelIndex(), i, i);
        m_rows.remove(i);
        endRemoveRows();
        removed = true;
    }
    if (removed)
        reindex();

    // Merge every fetched row through the single, timestamp-guarded funnel.
    for (const TrainPosition &tp : trains)
        applyOne(tp);

    // Garbage-collect bearing history down to currently-live trains so it can't
    // grow unbounded over a long session.
    for (auto it = m_previous.begin(); it != m_previous.end();)
        it = m_indexByNumber.contains(it.key()) ? std::next(it) : m_previous.erase(it);

    if (removed)
        emit countChanged();
}

void TrainListModel::upsertTrain(const TrainPosition &train)
{
    applyOne(train);
}

void TrainListModel::applyOne(const TrainPosition &train)
{
    const auto it = m_indexByNumber.constFind(train.trainNumber);
    if (it != m_indexByNumber.constEnd()) {
        const int rowIndex = it.value();
        Row &row = m_rows[rowIndex];
        // Timestamp guard: the 60 s REST resync lags the MQTT firehose, so a
        // stale snapshot must never overwrite a fresher position — that would
        // record a ~180°-reversed bearing. Drop it.
        if (train.timestamp.isValid() && row.pos.timestamp.isValid()
            && train.timestamp < row.pos.timestamp)
            return;
        row.bearing = bearingFor(train.trainNumber, train.coordinate);
        row.pos = train;
        const QModelIndex idx = index(rowIndex);
        emit dataChanged(idx, idx,
                         { CoordinateRole, SpeedRole, BearingRole, TimestampRole,
                           DepartureDateRole, RingStateRole });
        return;
    }

    const int newRow = m_rows.size();
    beginInsertRows(QModelIndex(), newRow, newRow);
    Row row;
    row.bearing = bearingFor(train.trainNumber, train.coordinate);
    row.pos = train;
    m_rows.push_back(row);
    m_indexByNumber.insert(train.trainNumber, newRow);
    endInsertRows();
    emit countChanged();
}

void TrainListModel::reindex()
{
    m_indexByNumber.clear();
    m_indexByNumber.reserve(m_rows.size());
    for (int i = 0; i < m_rows.size(); ++i)
        m_indexByNumber.insert(m_rows.at(i).pos.trainNumber, i);
}

void TrainListModel::setTrainMetadata(const QHash<int, QString> &types,
                                      const QHash<int, QString> &categories,
                                      const QHash<int, QString> &commuterLines)
{
    m_typeByNumber = types;
    m_categoryByNumber = categories;
    m_lineByNumber = commuterLines;
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1),
                         { CategoryRole, TrainTypeRole, CommuterLineRole });
}

void TrainListModel::setTrainStatuses(const QHash<int, TrainStatus> &statuses)
{
    m_statusByNumber = statuses;
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1), { RingStateRole });
}

QString TrainListModel::ringStateFor(const Row &row) const
{
    const TrainStatus st = m_statusByNumber.value(row.pos.trainNumber);
    const bool flaggedRunning = st.known && st.running && !st.cancelled;

    // Stale / not running: greyed, no ring.
    if (st.known && (st.cancelled || !st.running))
        return QStringLiteral("stale");
    if (row.pos.timestamp.isValid()
        && row.pos.timestamp.secsTo(QDateTime::currentDateTimeUtc()) > kStalePositionSecs
        && !flaggedRunning)
        return QStringLiteral("stale");

    // No status yet (cache not loaded, or train absent from /live-trains): no ring.
    if (!st.known)
        return QStringLiteral("none");

    // Lateness takes precedence over the green ready ring.
    if (st.delayMinutes > kVeryLateMinutes)
        return QStringLiteral("red");
    if (st.delayMinutes >= 1)
        return QStringLiteral("amber");

    // On time: green only while waiting (stopped); a running on-time train gets
    // no ring, so the map isn't a wash of green.
    if (row.pos.speed == 0.0)
        return QStringLiteral("green");
    return QStringLiteral("none");
}
