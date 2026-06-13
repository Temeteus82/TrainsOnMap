#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QGeoCoordinate>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>
#include <QtQmlIntegration>

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
    };

    explicit TrainListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_rows.size(); }

    /// Replace the whole set of trains (REST snapshot). Prunes trains that are
    /// no longer present.
    void updateTrains(const QVector<TrainPosition> &trains);

    /// Insert or update a single train in place (MQTT stream); only the changed
    /// row is touched, so existing markers don't flicker.
    void upsertTrain(const TrainPosition &train);

    /// Supply trainNumber -> type and -> category maps (e.g. from /live-trains).
    /// Markers are coloured by type/category; existing rows repaint on change.
    void setTrainMetadata(const QHash<int, QString> &types,
                          const QHash<int, QString> &categories);

signals:
    void countChanged();

private:
    struct Row {
        TrainPosition pos;
        double bearing = 0.0;
    };

    /// Bearing from the train's previous coordinate; also records the new one.
    double bearingFor(int trainNumber, const QGeoCoordinate &coordinate);

    QVector<Row> m_rows;
    QHash<int, int> m_indexByNumber;        ///< trainNumber -> row index
    QHash<int, QGeoCoordinate> m_previous;  ///< trainNumber -> last coord (for bearing)
    QHash<int, QString> m_categoryByNumber; ///< trainNumber -> category
    QHash<int, QString> m_typeByNumber;     ///< trainNumber -> train type
};
