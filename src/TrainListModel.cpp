#include "TrainListModel.h"

#include "TrackService.h"

#include <QSet>

#include <algorithm>
#include <limits>

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

// When a fix falls back to Tier-1 while the train was last on its route (a
// transient miss or a tunnel GPS blackout), dead-reckon the carried chainage
// forward by speed·Δt instead of freezing it, so the next fix's route window
// stays centred on where the train should be rather than at the tunnel mouth.
// Capped so a garbage Δt can't fling the window across the country; over-shooting
// is self-correcting (the forward re-acquire in projectOntoRoute catches up).
constexpr double kMaxGapAdvanceMeters = 20000.0;

// Teleport guard. A single feed glitch — a stale/duplicate coordinate, or a
// record momentarily mislabelled onto this train's (date, number) — can place a
// train hundreds of km away for one fix, then snap back ("jumps to Tampere and
// back"). Reject a fix only when the jump from the last one is both large *and*
// implies a speed no train can reach: a real GPS-blackout re-emergence is spread
// over a long Δt, so its implied speed stays plausible and passes; ordinary GPS
// jitter never clears the distance floor. After this many consecutive impossible
// fixes we trust the feed again — our own anchor may have been the stale one.
constexpr double kMaxPlausibleSpeedKmh = 350.0;   // well above any real train
constexpr double kMinTeleportMeters = 2000.0;     // ignore sub-2 km jitter
constexpr int kMaxOutlierStreak = 2;
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
    case NearestNeighborRole: return row.nearestNeighborMeters;
    default:              return {};
    }
}

QHash<int, QByteArray> TrainListModel::roleNames() const
{
    // Built once: the table is constant, and returning a copy of it is a COW
    // refcount bump rather than a rebuild of 14 entries. (Function-local static
    // init is thread-safe, and nothing ever mutates this.)
    static const QHash<int, QByteArray> roles = {
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
        { NearestNeighborRole, "nearestNeighborMeters" },
    };
    return roles;
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

QString TrainListModel::nearestRouteStationCode(const TrainKey &key,
                                                const QGeoCoordinate &fix) const
{
    const auto route = m_routeByKey.constFind(key);
    if (route == m_routeByKey.constEnd() || m_stationCoords.isEmpty())
        return {};

    QString best;
    double bestDist = kStationSnapMeters;
    for (const QString &code : route->codes) {
        const auto it = m_stationCoords.constFind(code);
        if (it == m_stationCoords.constEnd() || !it->isValid())
            continue;
        const double d = fix.distanceTo(*it);
        if (d < bestDist) {
            bestDist = d;
            best = code;
        }
    }
    return best;
}

QVariantMap TrainListModel::matchInfoFor(int trainNumber, const QString &departureDate) const
{
    QVariantMap info;
    const auto it = m_indexByKey.constFind(TrainKey{departureDate, trainNumber});
    if (it == m_indexByKey.constEnd())
        return info;
    const Row &row = m_rows.at(it.value());
    info.insert(QStringLiteral("offset"), row.trackOffsetMeters);
    info.insert(QStringLiteral("tunniste"), row.matchedTunniste);
    info.insert(QStringLiteral("onRoute"), row.onRoute);
    info.insert(QStringLiteral("accuracy"), row.pos.accuracy);   // GPS radius (m), -1 if none
    if (row.rawCoordinate.isValid()) {
        info.insert(QStringLiteral("rawLat"), row.rawCoordinate.latitude());
        info.insert(QStringLiteral("rawLon"), row.rawCoordinate.longitude());
    }
    if (row.pos.coordinate.isValid()) {
        info.insert(QStringLiteral("snapLat"), row.pos.coordinate.latitude());
        info.insert(QStringLiteral("snapLon"), row.pos.coordinate.longitude());
    }
    return info;
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

    recomputeNearestNeighbors();

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

    // Timestamp staleness guard, ahead of map-matching: the 60 s REST resync lags
    // the MQTT firehose, so an out-of-order snapshot must never overwrite a fresher
    // position (it would record a ~180°-reversed bearing). Drop it here, before the
    // route projection / platform snap, so that work isn't burned fleet-wide on
    // every resync only to be discarded below (#11).
    const auto existing = m_indexByKey.constFind(key);
    if (existing != m_indexByKey.constEnd() && train.timestamp.isValid()
        && m_rows.at(existing.value()).pos.timestamp.isValid()
        && train.timestamp < m_rows.at(existing.value()).pos.timestamp)
        return;

    // Teleport guard: drop a physically impossible jump from the last fix (a feed
    // glitch plotting the train hundreds of km away for one frame) before it ever
    // reaches the model. Both gates must trip — a large jump *and* an impossible
    // implied speed — so a slow re-emergence after a long blackout still passes
    // and ordinary jitter is ignored. The streak cap re-acquires if our anchor
    // turns out to be the stale one. See kMaxPlausibleSpeedKmh.
    if (existing != m_indexByKey.constEnd() && train.coordinate.isValid()) {
        Row &r = m_rows[existing.value()];
        if (r.rawCoordinate.isValid() && r.outlierStreak < kMaxOutlierStreak) {
            const double jump = r.rawCoordinate.distanceTo(train.coordinate);
            const qint64 dt = (r.pos.timestamp.isValid() && train.timestamp.isValid())
                                  ? r.pos.timestamp.secsTo(train.timestamp) : 0;
            const double impliedKmh = dt > 0 ? jump / dt * 3.6
                                             : std::numeric_limits<double>::infinity();
            if (jump > kMinTeleportMeters && impliedKmh > kMaxPlausibleSpeedKmh) {
                ++r.outlierStreak;
                return;
            }
        }
        r.outlierStreak = 0;
    }

    // Map-match the raw fix to the rail network: snap an on-track fix onto the
    // rail (so markers ride the rails instead of jittering beside them), and
    // record how far off the network it was so QML can flag a suspect position.
    // Off-track fixes are kept raw — the network may simply have a gap there.
    TrainPosition matched = train;
    const QGeoCoordinate raw = train.coordinate;
    double offset = -1.0;
    QString matchedTunniste;
    bool onRoute = false;

    // Carry the per-train route chainage forward by default. It's only
    // overwritten when a Tier-2 match is accepted below; a transient miss must
    // not wipe it to -1, which would force the next Tier-2 attempt into a
    // full-route global search instead of a windowed continuation (a marker jump
    // on self-parallel routes, #3). This is hoisted out of the raw.isValid()
    // block on purpose (R1): an invalid fix skips matching entirely, so leaving
    // the carry-forward inside it would reset chainage to -1 and reopen exactly
    // the global re-acquire this is meant to prevent on the next valid fix.
    //
    // Scope (R4): this only bridges *transient* Tier-1 fallbacks (a single bad
    // fix, a brief off-route blip, or a not-yet-resolved route). Across a
    // sustained off-route stretch the frozen chainage lags the train, the window
    // eventually misses, and projectOntoRoute falls back to a global re-acquire.
    // That's an accepted limitation: dead-reckoning the chainage instead would
    // mis-advance a genuinely diverted train that isn't on the booked route.
    double prevChainage = -1.0;
    bool wasOnRoute = false;
    const auto rit = m_indexByKey.constFind(key);
    if (rit != m_indexByKey.constEnd()) {
        prevChainage = m_rows.at(rit.value()).chainage;
        wasOnRoute = m_rows.at(rit.value()).onRoute;
    }
    double newChainage = prevChainage;

    if (m_matcher && raw.isValid()) {
        const auto prev = m_previous.constFind(key);
        const bool havePrev = prev != m_previous.constEnd() && prev->isValid();
        const bool stopped = train.speed < kStoppedSpeedKmh;

        // Estimate the direction of travel from the last stored position so the
        // Tier-1 matcher can prefer the track the train runs along; skipped when
        // stopped or barely moved (the azimuth would be noise).
        double heading = -1.0;
        if (havePrev && train.speed >= kHeadingMinSpeedKmh
            && prev->distanceTo(raw) > kHeadingMinMoveMeters)
            heading = prev->azimuthTo(raw);

        // Estimate the progress since the last fix (speed·Δt) so the route matcher
        // can window its search around the carried chainage.
        qint64 dtSecs = 0;
        if (rit != m_indexByKey.constEnd()) {
            const Row &r = m_rows.at(rit.value());
            if (r.pos.timestamp.isValid() && train.timestamp.isValid())
                dtSecs = r.pos.timestamp.secsTo(train.timestamp);
        }
        const double advance = dtSecs > 0 ? (train.speed / 3.6) * double(dtSecs) : 0.0;

        QGeoCoordinate snapped = raw;
        bool accepted = false;

        // Tier-2: constrain the match to the train's scheduled route, and snap to
        // its booked platform track when stopped at a station.
        const auto route = m_routeByKey.constFind(key);
        if (route != m_routeByKey.constEnd() && route->codes.size() >= 2) {
            RouteMatchRequest req;
            req.stationCodes = route->codes;
            req.prevChainage = prevChainage;
            req.advanceMeters = advance;
            if (stopped) {
                const QString stCode = nearestRouteStationCode(key, raw);
                if (!stCode.isEmpty()) {
                    req.platformStation = stCode;
                    req.platformTrack = route->commercialTrack.value(stCode);
                }
            }
            const TrackMatch rm = m_matcher->matchOnRoute(raw, req);
            if (rm.onRoute && rm.onTrack && rm.snapped.isValid()) {
                snapped = rm.snapped;
                offset = rm.distanceMeters;
                matchedTunniste = rm.tunniste;
                newChainage = rm.chainageMeters;
                onRoute = true;
                accepted = true;
            }
        }

        // Tier-1 fallback: nearest track + heading, with stopped-train continuity
        // (#2) and the off-network station pin (#5) — used until the route is
        // resolved, or when the fix is too far from the booked route (diversion).
        if (!accepted) {
            // Bridge a transient gap: if the train was on its route last fix (so
            // this is a blip / tunnel blackout, not a sustained diversion),
            // dead-reckon the carried chainage forward instead of freezing it at
            // the tunnel mouth, so the next fix's window re-engages Tier-2 where
            // the train actually is. onRoute is stored false on this row, so the
            // very next fix won't dead-reckon again — it's a one-shot bridge that
            // can't run away on a genuine off-route stretch.
            if (route != m_routeByKey.constEnd() && route->codes.size() >= 2
                && wasOnRoute && prevChainage >= 0.0 && advance > 0.0)
                newChainage = prevChainage + std::min(advance, kMaxGapAdvanceMeters);

            const TrackMatch m = m_matcher->matchToNetwork(raw, heading);
            offset = m.distanceMeters;
            snapped = (m.onTrack && m.snapped.isValid()) ? m.snapped : raw;
            if (stopped) {
                if (havePrev && prev->distanceTo(raw) < kStoppedHoldMeters) {
                    snapped = *prev;
                } else if (!m.onTrack) {
                    // Pin to the nearest scheduled station, if one is in range
                    // (value() of a missing/"" code is an invalid coordinate).
                    const QGeoCoordinate st =
                        m_stationCoords.value(nearestRouteStationCode(key, raw));
                    if (st.isValid())
                        snapped = st;
                }
            }
        }
        matched.coordinate = snapped;
    }

    const auto it = m_indexByKey.constFind(key);
    if (it != m_indexByKey.constEnd()) {
        const int rowIndex = it.value();
        Row &row = m_rows[rowIndex];
        // Staleness was already rejected at the top of applyOne (#11).
        row.bearing = bearingFor(key, matched.coordinate);
        row.pos = matched;
        row.trackOffsetMeters = offset;
        row.rawCoordinate = raw;
        row.matchedTunniste = matchedTunniste;
        row.chainage = newChainage;
        row.onRoute = onRoute;
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
    row.rawCoordinate = raw;
    row.matchedTunniste = matchedTunniste;
    row.chainage = newChainage;
    row.onRoute = onRoute;
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

void TrainListModel::recomputeNearestNeighbors()
{
    for (int i = 0; i < m_rows.size(); ++i) {
        const QGeoCoordinate &a = m_rows.at(i).pos.coordinate;
        double best = -1.0;
        if (a.isValid()) {
            for (int j = 0; j < m_rows.size(); ++j) {
                if (j == i)
                    continue;
                const QGeoCoordinate &b = m_rows.at(j).pos.coordinate;
                if (!b.isValid())
                    continue;
                const double d = a.distanceTo(b);
                if (best < 0.0 || d < best)
                    best = d;
            }
        }
        m_rows[i].nearestNeighborMeters = best;
    }
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1), { NearestNeighborRole });
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

    // The position feed (train-locations) is far fresher than the bulk
    // /live-trains poll that sets runningCurrently, and that flag lags actual
    // departures: a train that has clearly pulled out can still be reported as
    // not-running for a poll or two. A recent fix that is actually moving is
    // positive proof the train is running, so treat it as authoritative over the
    // !running flag — otherwise a visibly-moving train (speed on the badge, fix
    // gliding along the rail) is greyed, which reads as a bug. Cancelled stays
    // authoritative; a cancelled train should not be on the live feed at all.
    const bool positionFresh = row.pos.timestamp.isValid()
        && row.pos.timestamp.secsTo(QDateTime::currentDateTimeUtc()) <= kStalePositionSecs;
    const bool demonstrablyRunning = positionFresh && row.pos.speed >= kStoppedSpeedKmh;

    // Stale / not running: greyed, no ring.
    if (st.known && st.cancelled)
        return QStringLiteral("stale");
    if (st.known && !st.running && !demonstrablyRunning)
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
