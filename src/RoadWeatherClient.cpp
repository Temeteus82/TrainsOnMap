#include "RoadWeatherClient.h"

#include <QGeoCoordinate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <utility>

namespace {
constexpr const char *kUserAgent = "TrainsOnMap/0.1 (Qt6 scaffolding)";
constexpr const char *kStationsUrl = "https://tie.digitraffic.fi/api/weather/v1/stations";
constexpr const char *kDataUrl = "https://tie.digitraffic.fi/api/weather/v1/stations/data";
constexpr int kPollMs = 5 * 60 * 1000;   // road weather drifts slowly
}   // namespace

RoadWeatherClient::RoadWeatherClient(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_model(new WeatherStationModel(this))
{
    m_net->setTransferTimeout(20000);
    m_timer.setInterval(kPollMs);
    connect(&m_timer, &QTimer::timeout, this, &RoadWeatherClient::fetchData);
}

void RoadWeatherClient::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (m_active) {
        if (m_stationsLoaded)
            fetchData();
        else
            fetchStations();   // fetchData() runs once the coords land
        m_timer.start();
    } else {
        m_timer.stop();
        m_model->clear();
    }
    emit activeChanged();
}

void RoadWeatherClient::fetchStations()
{
    QNetworkRequest req{QUrl(QString::fromLatin1(kStationsUrl))};
    req.setRawHeader("Digitraffic-User", kUserAgent);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleStations(reply); });
}

void RoadWeatherClient::handleStations(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
        return;

    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    const QJsonArray feats = root.value("features").toArray();
    m_coords.clear();
    m_names.clear();
    for (const QJsonValue &v : feats) {
        const QJsonObject f = v.toObject();
        const QJsonObject props = f.value("properties").toObject();
        const int id = props.value("id").toInt();
        // GeoJSON Point: coordinates are [lon, lat, (alt)].
        const QJsonArray c = f.value("geometry").toObject().value("coordinates").toArray();
        if (id == 0 || c.size() < 2)
            continue;
        m_coords.insert(id, QGeoCoordinate(c.at(1).toDouble(), c.at(0).toDouble()));
        m_names.insert(id, props.value("name").toString());
    }
    m_stationsLoaded = !m_coords.isEmpty();
    if (m_active && m_stationsLoaded)
        fetchData();
}

void RoadWeatherClient::fetchData()
{
    if (!m_stationsLoaded) {
        fetchStations();
        return;
    }
    QNetworkRequest req{QUrl(QString::fromLatin1(kDataUrl))};
    req.setRawHeader("Digitraffic-User", kUserAgent);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleData(reply); });
}

void RoadWeatherClient::handleData(QNetworkReply *reply)
{
    reply->deleteLater();
    if (!m_active || reply->error() != QNetworkReply::NoError)
        return;

    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    const QJsonArray stations = root.value("stations").toArray();
    QVector<WeatherPoint> points;
    points.reserve(stations.size());
    for (const QJsonValue &v : stations) {
        const QJsonObject s = v.toObject();
        const QGeoCoordinate coord = m_coords.value(s.value("id").toInt());
        if (!coord.isValid())
            continue;
        // Air temperature sensor "ILMA".
        double temp = 0.0;
        bool hasTemp = false;
        const QJsonArray sensors = s.value("sensorValues").toArray();
        for (const QJsonValue &sv : sensors) {
            const QJsonObject so = sv.toObject();
            if (so.value("name").toString() == QLatin1String("ILMA")) {
                temp = so.value("value").toDouble();
                hasTemp = true;
                break;
            }
        }
        if (!hasTemp)
            continue;
        WeatherPoint p;
        p.coordinate = coord;
        p.tempC = temp;
        p.tempText = QString::number(qRound(temp)) + QStringLiteral("°");
        p.name = m_names.value(s.value("id").toInt());
        points.push_back(std::move(p));
    }
    m_model->setPoints(points);
}
