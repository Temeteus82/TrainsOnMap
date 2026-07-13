#pragma once

#include <QGeoCoordinate>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQmlIntegration>

#include "WeatherStationModel.h"

class QNetworkAccessManager;
class QNetworkReply;

/// Fetches Fintraffic **road** weather (tie.digitraffic.fi) and exposes each
/// station's air temperature for an optional map overlay.
///
/// NOTE: this is deliberately *road* weather — the Digitraffic rail API publishes
/// no weather at all. It's shown as a nearby-conditions proxy along the network and
/// is labelled as road weather in the UI. Idle (no network traffic) until `active`
/// is set true by the sidebar toggle; station metadata is fetched once, then air
/// temperatures refresh on a slow timer (weather drifts gradually).
class RoadWeatherClient : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(WeatherStationModel *model READ model CONSTANT)
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)

public:
    explicit RoadWeatherClient(QObject *parent = nullptr);

    WeatherStationModel *model() const { return m_model; }

    bool isActive() const { return m_active; }
    void setActive(bool active);

signals:
    void activeChanged();

private:
    void fetchStations();                        ///< one-shot: id -> coord/name
    void handleStations(QNetworkReply *reply);
    void fetchData();                            ///< latest air temperature per station
    void handleData(QNetworkReply *reply);

    QNetworkAccessManager *m_net = nullptr;
    WeatherStationModel *m_model = nullptr;
    QTimer m_timer;
    bool m_active = false;
    bool m_stationsLoaded = false;
    QHash<int, QGeoCoordinate> m_coords;   ///< station id -> position
    QHash<int, QString> m_names;           ///< station id -> name
};
