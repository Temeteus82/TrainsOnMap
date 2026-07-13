#pragma once

#include <QGeoCoordinate>
#include <QRangeModel>
#include <QString>
#include <QVector>
#include <QtQmlIntegration>

/// One passenger station: its short code, display name and WGS84 coordinate.
/// Rendered as a clickable dot on the map's station layer; clicking one opens the
/// station departure board.
///
/// Like TimetableStop/CompositionVehicle, this is a Q_GADGET whose Q_PROPERTYs are
/// the model's row schema — QRangeModel (Qt 6.10+) reflects each into a QML role of
/// the same name, so there is no hand-written data()/roleNames().
struct StationPoint {
    Q_GADGET
    Q_PROPERTY(QString code MEMBER code)
    Q_PROPERTY(QString name MEMBER name)
    Q_PROPERTY(QGeoCoordinate coordinate MEMBER coordinate)
public:
    QString code;                 ///< station short code, e.g. "TPE"
    QString name;                 ///< display name, e.g. "Tampere"
    QGeoCoordinate coordinate;    ///< WGS84 position
};

// A QML list needs the gadget's properties as named roles on one item, so opt the
// row type into the multi-role representation (Qt 6.11+) — as the other models do.
template <>
struct QRangeModel::RowOptions<StationPoint> {
    static constexpr auto rowCategory = QRangeModel::RowCategory::MultiRoleItem;
};

/// List model of passenger stations, bound to the map's clickable station layer.
/// Owned by DigitrafficClient and exposed via its `stations` property; populated
/// once from the /metadata/stations fetch (passenger stations only). Backed by
/// QRangeModel over an internal QVector<StationPoint>.
class StationListModel : public QRangeModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Obtain via DigitrafficClient.stations")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit StationListModel(QObject *parent = nullptr);

    int count() const { return m_stations.size(); }

    void setStations(const QVector<StationPoint> &stations);

signals:
    void countChanged();

private:
    QVector<StationPoint> m_stations;
};
