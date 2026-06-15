#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QGeoCoordinate>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>
#include <QtQmlIntegration>

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
};

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

    /// Supply trainNumber -> type, -> category and -> commuter-line maps (e.g.
    /// from /live-trains). Markers are coloured by type/category and labelled by
    /// line/type; existing rows repaint on change.
    void setTrainMetadata(const QHash<int, QString> &types,
                          const QHash<int, QString> &categories,
                          const QHash<int, QString> &commuterLines);

    /// Supply trainNumber -> live running status (delay / cancelled / running),
    /// e.g. from /live-trains. Drives the marker status ring; existing rows
    /// repaint on change.
    void setTrainStatuses(const QHash<int, TrainStatus> &statuses);

signals:
    void countChanged();

private:
    struct Row {
        TrainPosition pos;
        double bearing = 0.0;
    };

    /// The single upsert funnel for every position update, REST or MQTT. Drops
    /// a stale update (older timestamp than the row it would overwrite), else
    /// inserts or updates the row in place.
    void applyOne(const TrainPosition &train);

    /// Rebuild trainNumber -> row index after rows are removed.
    void reindex();

    /// Bearing from the train's previous coordinate; also records the new one.
    double bearingFor(int trainNumber, const QGeoCoordinate &coordinate);

    QVector<Row> m_rows;
    QHash<int, int> m_indexByNumber;        ///< trainNumber -> row index
    QHash<int, QGeoCoordinate> m_previous;  ///< trainNumber -> last coord (for bearing)
    QHash<int, QString> m_categoryByNumber; ///< trainNumber -> category
    QHash<int, QString> m_typeByNumber;     ///< trainNumber -> train type
    QHash<int, QString> m_lineByNumber;     ///< trainNumber -> commuter line letter
    QHash<int, TrainStatus> m_statusByNumber; ///< trainNumber -> live running status

    /// Resolve the status-ring state for a row from its position + cached status.
    QString ringStateFor(const Row &row) const;
};
