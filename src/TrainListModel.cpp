#include "TrainListModel.h"

#include <QSet>

namespace {
// A train missing from a REST snapshot is pruned only once its last position is
// older than this, so a just-arrived MQTT-only train isn't deleted before REST
// catches up. Matches the Swift app's staleTrainGrace.
constexpr qint64 kStaleGraceSecs = 120;

// Status-ring thresholds.
constexpr qint64 kStalePositionSecs = 300;   // position older than this reads as stale
constexpr int kLateMinutes = 5;              // amber "late" ring at this delay or more
constexpr int kVeryLateMinutes = 15;         // red "very late" ring at this delay or more
constexpr double kStoppedSpeedKmh = 0.5;     // below this a train counts as stopped/waiting

// Below this speed the heading from successive fixes is GPS noise, not travel
// direction, so it's not fed to the (direction-aware) map-matcher; likewise a
// move shorter than this between two fixes yields an unreliable azimuth.
constexpr double kHeadingMinSpeedKmh = 5.0;
constexpr double kHeadingMinMoveMeters = 8.0;

// Stopped-train handling (tier-1 continuity, parts #2/#5). A parked train's
// fixes jitter by tens of metres and can re-snap onto a neighbouring track each
// poll; once it's snapped, hold that point while successive fixes stay within
// this radius. If instead the parked fix is off the network entirely and a
// scheduled station sits within the station-snap radius, pin it there (the train
// is sitting in a station whose throat the GPS has drifted out of).
constexpr double kStoppedHoldMeters = 40.0;
constexpr double kStationSnapMeters = 250.0;
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
    case CategoryRole:    return m_categoryByNumber.value(keyOf(row.pos));
    case TrainTypeRole:   return m_typeByNumber.value(keyOf(row.pos));
    case CommuterLineRole: return m_lineByNumber.value(keyOf(row.pos));
    case RingStateRole:   return ringStateFor(row);
    case DelayMinutesRole: return m_statusByNumber.value(keyOf(row.pos)).delayMinutes;
    case AccuracyRole:    return row.pos.accuracy;
    case TrackOffsetRole: return row.trackOffsetMeters;
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
        { DelayMinutesRole, "delayMinutes" },
        { AccuracyRole,    "accuracy" },
        { TrackOffsetRole, "trackOffsetMeters" },
    };
}

double TrainListModel::bearingFor(const TrainKey &key, const QGeoCoordinate &coordinate)
{
    double bearing = 0.0;
    const auto prev = m_previous.constFind(key);
    if (prev != m_previous.constEnd() && prev->isValid() && coordinate.isValid()
        && prev->distanceTo(coordinate) > 1.0) {
        bearing = prev->azimuthTo(coordinate);
    }
    m_previous.insert(key, coordinate);
    return bearing;
}

QGeoCoordinate TrainListModel::nearestRouteStation(const TrainKey &key,
                                                   const QGeoCoordinate &fix) const
{
    const auto route = m_routeByKey.constFind(key);
    if (route == m_routeByKey.constEnd() || m_stationCoords.isEmpty())
        return {};

    QGeoCoordinate best;
    double bestDist = kStationSnapMeters;
    for (const QString &code : *route) {
        const auto it = m_stationCoords.constFind(code);
        if (it == m_stationCoords.constEnd() || !it->isValid())
            continue;
        const double d = fix.distanceTo(*it);
        if (d < bestDist) {
            bestDist = d;
            best = *it;
        }
    }
    return best;
}

void TrainListModel::updateTrains(const QVector<TrainPosition> &trains)
{
    QSet<TrainKey> active;
    active.reserve(trains.size());
    for (const TrainPosition &tp : trains)
        active.insert(keyOf(tp));

    // Prune trains the snapshot no longer reports, but only once they've aged
    // past the grace window — MQTT may know about a train before REST does, and
    // deleting it early would flicker the marker. Walk back-to-front so indices
    // stay valid as rows are removed.
    const QDateTime now = QDateTime::currentDateTimeUtc();
    bool removed = false;
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        const TrainPosition &pos = m_rows.at(i).pos;
        if (active.contains(keyOf(pos)))
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
        it = m_indexByKey.contains(it.key()) ? std::next(it) : m_previous.erase(it);

    if (removed)
        emit countChanged();
}

void TrainListModel::upsertTrain(const TrainPosition &train)
{
    applyOne(train);
}

void TrainListModel::applyOne(const TrainPosition &train)
{
    const TrainKey key = keyOf(train);

    // Map-match the raw fix to the rail network: snap an on-track fix onto the
    // rail (so markers ride the rails instead of jittering beside them), and
    // record how far off the network it was so QML can flag a suspect position.
    // Off-track fixes are kept raw — the network may simply have a gap there.
    TrainPosition matched = train;
    double offset = -1.0;
    if (m_matcher && matched.coordinate.isValid()) {
        const auto prev = m_previous.constFind(key);
        const bool havePrev = prev != m_previous.constEnd() && prev->isValid();
        const bool stopped = train.speed < kStoppedSpeedKmh;

        // Estimate the direction of travel from the last stored position so the
        // matcher can prefer the track the train runs along (vs. one it crosses).
        // Skipped when stopped or barely moved — the azimuth would be noise.
        double heading = -1.0;
        if (havePrev && train.speed >= kHeadingMinSpeedKmh
            && prev->distanceTo(matched.coordinate) > kHeadingMinMoveMeters)
            heading = prev->azimuthTo(matched.coordinate);

        const TrackMatch m = m_matcher->matchToNetwork(matched.coordinate, heading);
        offset = m.distanceMeters;
        QGeoCoordinate snapped = (m.onTrack && m.snapped.isValid()) ? m.snapped
                                                                    : matched.coordinate;

        if (stopped) {
            // #2 Continuity: a parked train's jittering fixes shouldn't drag the
            // marker around (or hop it to a neighbouring track). Once we have a
            // snapped point and the new fix is still nearby, keep that point.
            if (havePrev && prev->distanceTo(matched.coordinate) < kStoppedHoldMeters) {
                snapped = *prev;
            } else if (!m.onTrack) {
                // #5 Stopped and off the network: pin to the nearest scheduled
                // station if one is close — the train is in a station the GPS has
                // drifted out of. (Skipped while running: a moving off-track fix is
                // a genuine geometry gap, not a station stop.)
                const QGeoCoordinate st = nearestRouteStation(key, matched.coordinate);
                if (st.isValid())
                    snapped = st;
            }
        }
        matched.coordinate = snapped;
    }

    const auto it = m_indexByKey.constFind(key);
    if (it != m_indexByKey.constEnd()) {
        const int rowIndex = it.value();
        Row &row = m_rows[rowIndex];
        // Timestamp guard: the 60 s REST resync lags the MQTT firehose, so a
        // stale snapshot must never overwrite a fresher position — that would
        // record a ~180°-reversed bearing. Drop it.
        if (matched.timestamp.isValid() && row.pos.timestamp.isValid()
            && matched.timestamp < row.pos.timestamp)
            return;
        row.bearing = bearingFor(key, matched.coordinate);
        row.pos = matched;
        row.trackOffsetMeters = offset;
        const QModelIndex idx = index(rowIndex);
        emit dataChanged(idx, idx,
                         { CoordinateRole, SpeedRole, BearingRole, TimestampRole,
                           DepartureDateRole, RingStateRole, AccuracyRole, TrackOffsetRole });
        return;
    }

    const int newRow = m_rows.size();
    beginInsertRows(QModelIndex(), newRow, newRow);
    Row row;
    row.bearing = bearingFor(key, matched.coordinate);
    row.pos = matched;
    row.trackOffsetMeters = offset;
    m_rows.push_back(row);
    m_indexByKey.insert(key, newRow);
    endInsertRows();
    emit countChanged();
}

void TrainListModel::reindex()
{
    m_indexByKey.clear();
    m_indexByKey.reserve(m_rows.size());
    for (int i = 0; i < m_rows.size(); ++i)
        m_indexByKey.insert(keyOf(m_rows.at(i).pos), i);
}

void TrainListModel::setTrainMetadata(const QHash<TrainKey, QString> &types,
                                      const QHash<TrainKey, QString> &categories,
                                      const QHash<TrainKey, QString> &commuterLines)
{
    m_typeByNumber = types;
    m_categoryByNumber = categories;
    m_lineByNumber = commuterLines;
    if (!m_rows.isEmpty())
        // RingStateRole too: ringStateFor() gates the delay ring on category,
        // so a category change can change the ring even if status is unchanged.
        emit dataChanged(index(0), index(m_rows.size() - 1),
                         { CategoryRole, TrainTypeRole, CommuterLineRole, RingStateRole });
}

void TrainListModel::setTrainStatuses(const QHash<TrainKey, TrainStatus> &statuses)
{
    m_statusByNumber = statuses;
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1),
                         { RingStateRole, DelayMinutesRole });
}

QString TrainListModel::ringStateFor(const Row &row) const
{
    const TrainStatus st = m_statusByNumber.value(keyOf(row.pos));
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

    // Delay/readiness rings are only meaningful for scheduled passenger trains.
    // Cargo and special movements (locomotive, shunting, on-track machines) carry
    // no delay indication.
    const QString category = m_categoryByNumber.value(keyOf(row.pos));
    if (category != QLatin1String("Long-distance") && category != QLatin1String("Commuter"))
        return QStringLiteral("none");

    // Lateness takes precedence over the green ready ring: red at 15+ min late,
    // amber at 5–14 min, nothing under 5 min.
    if (st.delayMinutes >= kVeryLateMinutes)
        return QStringLiteral("red");
    if (st.delayMinutes >= kLateMinutes)
        return QStringLiteral("amber");

    // Green "ready" ring only for a genuinely on-time, stopped (waiting) train; a
    // running on-time train gets no ring, so the map isn't a wash of green.
    // "Stopped" is a small threshold, not exact 0, so a creeping feed value
    // (e.g. 0.3 km/h) still reads as waiting.
    if (row.pos.speed < kStoppedSpeedKmh && st.delayMinutes <= 0)
        return QStringLiteral("green");
    return QStringLiteral("none");
}
