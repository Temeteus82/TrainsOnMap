#pragma once

#include <QObject>
#include <QTimer>
#include <QtQmlIntegration>

#include "WeatherStationModel.h"

class QNetworkAccessManager;
class QNetworkReply;

/// Fetches FMI open-data weather observations (opendata.fmi.fi WFS,
/// `fmi::observations::weather::simple`) and exposes each station's air
/// temperature for an optional map overlay.
///
/// One request covers the whole country: the simple format returns flat
/// `BsWfsElement` records (coordinate + time + parameter + value), so no
/// separate station-metadata fetch is needed. Idle (no network traffic) until
/// `active` is set true by the sidebar toggle; observations refresh on a slow
/// timer (FMI stations report every 10 min).
class FmiWeatherClient : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(WeatherStationModel *model READ model CONSTANT)
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)

public:
    explicit FmiWeatherClient(QObject *parent = nullptr);

    WeatherStationModel *model() const { return m_model; }

    bool isActive() const { return m_active; }
    void setActive(bool active);

signals:
    void activeChanged();

private:
    void fetchData();   ///< latest air temperature, all stations in one request
    void handleData(QNetworkReply *reply);

    QNetworkAccessManager *m_net = nullptr;
    WeatherStationModel *m_model = nullptr;
    QTimer m_timer;
    bool m_active = false;
};
