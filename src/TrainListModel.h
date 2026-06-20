#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QGeoCoordinate>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>
#include <QVector>
#include <QtQmlIntegration>

#include "TrackMatcher.h"

/// Live running status for a train, from the bulk /live-trains poll. Combined
/// with each train's position age + speed to pick a marker status ring.
struct TrainStatus {
    int delayMinutes = 0;   ///< delay at the most recently passed stop
    bool cancelled = false;
    bool running = false;   ///< runningCurrently
    bool known = false;     ///< false until /live-trains has reported this train
};

/// A single decoded train position from the Digitraffic train-locations feed.
struct TrainPosition {
    int trainNumber = 0;
    QString departureDate;       ///< "YYYY-MM-DD"; identifies the run for timetable lookups
    QGeoCoordinate coordinate;   ///< WGS84 (lat, lon)
    double speed = 0.0;          ///< km/h
    QDateTime timestamp;         ///< UTC report time
    int accuracy = -1;           ///< GPS uncertainty radius in metres; -1 if unreported
};

/// A train's scheduled path, from /live-trains' timeTableRows: the ordered
/// station short codes (used to resolve the route polyline + nearest stop) plus
/// the booked commercialTrack per stopping station (used for platform snapping).
struct TrainRoute {
    QVector<QString> codes;
    QHash<QString, QString> commercialTrack;
};

/// Digitraffic identifies a train run by the pair (departureDate, trainNumber),
/// NOT by trainNumber alone — numbers are reused every day, and the same number
/// can run under two departure dates at once (e.g. an overnight service still in
/// motion past midnight alongside today's instance). Every per-train map is keyed
/// on this composite so the two runs never collide.
struct TrainKey {
    QString departureDate;
    int trainNumber = 0;

    bool operator==(const TrainKey &other) const noexcept
    {
        return trainNumber == other.trainNumber && departureDate == other.departureDate;
    }
};

inline size_t qHash(const TrainKey &key, size_t seed = 0) noexcept
{
    return qHashMulti(seed, key.departureDate, key.trainNumber);
}

inline TrainKey keyOf(const TrainPosition &pos)
{
    return { pos.departureDate, pos.trainNumber };
}

/// Parse one train-locations object (same JSON shape over REST and MQTT).
inline TrainPosition parseTrainLocation(const QJsonObject &o)
{
    TrainPosition tp;
    tp.trainNumber = o.value(QStringLiteral("trainNumber")).toInt();
    tp.departureDate = o.value(QStringLiteral("departureDate")).toString();
    const QJsonArray c = o.value(QStringLiteral("location")).toObject()
                             .value(QStringLiteral("coordinates")).toArray();
    if (c.size() >= 2) {
        // GeoJSON order is [longitude, latitude]; QGeoCoordinate takes (lat, lon).
        tp.coordinate = QGeoCoordinate(c.at(1).toDouble(), c.at(0).toDouble());
    }
    tp.speed = o.value(QStringLiteral("speed")).toDouble();
    tp.timestamp = QDateTime::fromString(o.value(QStringLiteral("timestamp")).toString(),
                                         Qt::ISODateWithMs);
    // accuracy is the GPS uncertainty radius (m); absent on some fixes → -1.
    tp.accuracy = o.value(QStringLiteral("accuracy")).toInt(-1);
    return tp;
}

/// List model of live trains, consumed by a QML MapItemView.
///
/// Supports both a bulk replace (REST snapshot) and per-train upserts (MQTT
/// stream). Instances are owned by DigitrafficClient and exposed via its
/// `model` property; QML cannot construct one directly.
class TrainListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Obtain via DigitrafficClient.model")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        TrainNumberRole = Qt::UserRole + 1,
        DepartureDateRole, ///< "YYYY-MM-DD", needed for the timetable endpoint
        CoordinateRole,   ///< QGeoCoordinate, bind directly to MapQuickItem.coordinate
        SpeedRole,        ///< km/h
        BearingRole,      ///< degrees, derived from successive positions
        TimestampRole,
        CategoryRole,     ///< "Commuter" / "Long-distance" / "Cargo" / … (broad class)
        TrainTypeRole,    ///< "IC" / "S" / "PYO" / "HL" / "T" / … (drives marker colour)
        CommuterLineRole, ///< "R" / "Z" / "U" / … commuter line letter, "" if none
        RingStateRole,    ///< "green"/"amber"/"red"/"stale"/"none" — marker status ring
        DelayMinutesRole, ///< live delay at the last passed stop (pairs a number with the ring colour)
        AccuracyRole,     ///< GPS uncertainty radius in metres; -1 if unreported
        TrackOffsetRole,  ///< metres from the nearest rail (map-matched); -1 if not matched yet
    };

    explicit TrainListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_rows.size(); }

    /// Merge a REST snapshot: upsert every fetched train, then prune trains
    /// missing from it once they've aged past a grace window. Merges rather than
    /// replaces so concurrent MQTT updates aren't clobbered and markers don't
    /// flicker.
    void updateTrains(const QVector<TrainPosition> &trains);

    /// Insert or update a single train in place (MQTT stream); only the changed
    /// row is touched, so existing markers don't flicker.
    void upsertTrain(const TrainPosition &train);

    /// Supply (date,number) -> type, -> category and -> commuter-line maps (e.g.
    /// from /live-trains). Markers are coloured by type/category and labelled by
    /// line/type; existing rows repaint on change.
    void setTrainMetadata(const QHash<TrainKey, QString> &types,
                          const QHash<TrainKey, QString> &categories,
                          const QHash<TrainKey, QString> &commuterLines);

    /// Supply (date,number) -> live running status (delay / cancelled / running),
    /// e.g. from /live-trains. Drives the marker status ring; existing rows
    /// repaint on change.
    void setTrainStatuses(const QHash<TrainKey, TrainStatus> &statuses);

    /// Set the rail-network map-matcher used to snap/flag incoming GPS fixes.
    /// Optional: with none set, positions are stored raw. Not owned.
    void setMatcher(const TrackMatcher *matcher) { m_matcher = matcher; }

    /// Supply station short-code -> WGS84 coordinate (from /metadata/stations).
    /// Used to pin a stopped, off-network train to the station it's booked at.
    void setStationCoords(const QHash<QString, QGeoCoordinate> &coords) { m_stationCoords = coords; }

    /// Supply (date,number) -> scheduled route (station codes + per-stop platform)
    /// from /live-trains' timeTableRows. Drives Tier-2 route-constrained matching
    /// and platform snapping, plus the Tier-1 nearest-station fallback.
    void setTrainRoutes(const QHash<TrainKey, TrainRoute> &routes) { m_routeByKey = routes; }

    /// Diagnostics for the route-matched position of one train (for the debug
    /// overlay / detail panel): { rawLat, rawLon, snapLat, snapLon, offset,
    /// tunniste, onRoute, accuracy }. Empty map if the train isn't present.
    Q_INVOKABLE QVariantMap matchInfoFor(int trainNumber, const QString &departureDate) const;

signals:
    void countChanged();

private:
    struct Row {
        TrainPosition pos;
        double bearing = 0.0;
        double trackOffsetMeters = -1.0;  ///< raw fix's distance to nearest rail; -1 if unmatched
        QGeoCoordinate rawCoordinate;     ///< unsnapped fix (for the debug overlay)
        QString matchedTunniste;          ///< track OID the fix matched, "" if none
        double chainage = -1.0;           ///< 1-D route position carried across fixes
        bool onRoute = false;             ///< last fix matched the scheduled route
        int outlierStreak = 0;            ///< consecutive rejected teleport fixes
    };

    /// The single upsert funnel for every position update, REST or MQTT. Drops
    /// a stale update (older timestamp than the row it would overwrite), else
    /// inserts or updates the row in place.
    void applyOne(const TrainPosition &train);

    /// Rebuild trainNumber -> row index after rows are removed.
    void reindex();

    /// Bearing from the train's previous coordinate; also records the new one.
    double bearingFor(const TrainKey &key, const QGeoCoordinate &coordinate);

    /// Nearest scheduled-route station to `fix` within the station-snap radius, or
    /// an invalid coordinate if the train has no known route, no station coords are
    /// loaded yet, or none is close enough.
    QGeoCoordinate nearestRouteStation(const TrainKey &key, const QGeoCoordinate &fix) const;

    /// Short code of the nearest scheduled station to `fix` within the snap radius
    /// (for platform snapping), or "" when none qualifies.
    QString nearestRouteStationCode(const TrainKey &key, const QGeoCoordinate &fix) const;

    QVector<Row> m_rows;
    QHash<TrainKey, int> m_indexByKey;        ///< (date,number) -> row index
    QHash<TrainKey, QGeoCoordinate> m_previous;  ///< (date,number) -> last coord (for bearing)
    QHash<TrainKey, QString> m_categoryByNumber; ///< (date,number) -> category
    QHash<TrainKey, QString> m_typeByNumber;     ///< (date,number) -> train type
    QHash<TrainKey, QString> m_lineByNumber;     ///< (date,number) -> commuter line letter
    QHash<TrainKey, TrainStatus> m_statusByNumber; ///< (date,number) -> live running status
    QHash<QString, QGeoCoordinate> m_stationCoords; ///< station short code -> coord
    QHash<TrainKey, TrainRoute> m_routeByKey;       ///< (date,number) -> scheduled route

    const TrackMatcher *m_matcher = nullptr;   ///< snaps/flags GPS fixes; not owned

    /// Resolve the status-ring state for a row from its position + cached status.
    QString ringStateFor(const Row &row) const;
};
