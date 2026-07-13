#pragma once

#include <QGeoCoordinate>
#include <QRangeModel>
#include <QString>
#include <QVector>
#include <QtQmlIntegration>

/// One road weather station's latest air temperature, for the map overlay.
///
/// NOTE: this is Fintraffic **road** weather (tie.digitraffic.fi), not rail — the
/// rail API publishes no weather. It's shown as a nearby-conditions proxy and is
/// labelled as road weather in the UI.
///
/// Q_GADGET whose Q_PROPERTYs are the model's row schema (QRangeModel reflects each
/// into a QML role of the same name).
struct WeatherPoint {
    Q_GADGET
    Q_PROPERTY(QGeoCoordinate coordinate MEMBER coordinate)
    Q_PROPERTY(QString tempText MEMBER tempText)   ///< e.g. "19°"
    Q_PROPERTY(double tempC MEMBER tempC)          ///< air temperature, °C
    Q_PROPERTY(QString name MEMBER name)           ///< station name
public:
    QGeoCoordinate coordinate;
    QString tempText;
    double tempC = 0.0;
    QString name;
};

// Named roles on one item (Qt 6.11+), as the other QRangeModel gadgets do.
template <>
struct QRangeModel::RowOptions<WeatherPoint> {
    static constexpr auto rowCategory = QRangeModel::RowCategory::MultiRoleItem;
};

/// List model of road weather stations (air temperature), bound to the map's
/// optional weather layer. Owned by RoadWeatherClient and exposed via its `model`
/// property. Backed by QRangeModel over an internal QVector<WeatherPoint>.
class WeatherStationModel : public QRangeModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Obtain via RoadWeatherClient.model")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit WeatherStationModel(QObject *parent = nullptr);

    int count() const { return m_points.size(); }

    void setPoints(const QVector<WeatherPoint> &points);
    void clear();

signals:
    void countChanged();

private:
    QVector<WeatherPoint> m_points;
};
