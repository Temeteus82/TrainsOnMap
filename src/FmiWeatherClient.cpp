#include "FmiWeatherClient.h"

#include "DigitrafficFormat.h"
#include "NetworkDiagnostics.h"

#include <QDateTime>
#include <QDebug>
#include <QGeoCoordinate>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>

#include <chrono>

namespace {
constexpr const char *kWfsUrl = "https://opendata.fmi.fi/wfs";
constexpr int kPollMs = 10 * 60 * 1000;   // FMI stations report every 10 min
constexpr int kWindowSecs = 30 * 60;      // ask for the last half hour, keep the latest
// Deliberately longer than digitraffic::kRequestTimeout: a different provider,
// and this one is a WFS query that assembles a half hour of observations.
constexpr auto kRequestTimeout = std::chrono::seconds{20};

QUrl buildQueryUrl()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QUrlQuery q;
    q.addQueryItem("service", "WFS");
    q.addQueryItem("version", "2.0.0");
    q.addQueryItem("request", "getFeature");
    q.addQueryItem("storedquery_id", "fmi::observations::weather::simple");
    q.addQueryItem("parameters", "temperature");
    q.addQueryItem("bbox", "19,59,32,71");   // all of Finland (lon/lat)
    q.addQueryItem("starttime", now.addSecs(-kWindowSecs).toString(Qt::ISODate));
    q.addQueryItem("endtime", now.toString(Qt::ISODate));
    QUrl url{QString::fromLatin1(kWfsUrl)};
    url.setQuery(q);
    return url;
}
}   // namespace

FmiWeatherClient::FmiWeatherClient(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_model(new WeatherStationModel(this))
{
    m_net->setTransferTimeout(kRequestTimeout);
    netdiag::logSslErrors(m_net, "FmiWeatherClient");
    m_timer.setInterval(kPollMs);
    connect(&m_timer, &QTimer::timeout, this, &FmiWeatherClient::fetchData);
}

void FmiWeatherClient::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (m_active) {
        fetchData();
        m_timer.start();
    } else {
        m_timer.stop();
        m_model->clear();
    }
    emit activeChanged();
}

void FmiWeatherClient::fetchData()
{
    QNetworkReply *reply = m_net->get(QNetworkRequest{buildQueryUrl()});
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleData(reply); });
}

void FmiWeatherClient::handleData(QNetworkReply *reply)
{
    reply->deleteLater();
    if (!m_active || reply->error() != QNetworkReply::NoError)
        return;

    // Flat repeating records; the window holds several timestamps per station,
    // so key on the coordinate and keep the most recent valid reading.
    struct Reading { QDateTime time; WeatherPoint point; };
    QHash<QString, Reading> latest;

    QXmlStreamReader xml(reply->readAll());
    QGeoCoordinate coord;
    QDateTime time;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;
        const auto name = xml.name();
        if (name == QLatin1String("BsWfsElement")) {
            coord = QGeoCoordinate();
            time = QDateTime();
        } else if (name == QLatin1String("pos")) {
            // "lat lon" (EPSG:4258)
            const QStringList parts = xml.readElementText().simplified().split(QLatin1Char(' '));
            if (parts.size() >= 2) {
                // toDouble() yields 0.0 on failure and (0,0) is a valid
                // coordinate, so check the ok flags — like the ParameterValue
                // parse below — and reject anything outside the bbox the query
                // pins, or every malformed record collapses onto one "0,0" key.
                bool okLat = false, okLon = false;
                const double lat = parts.at(0).toDouble(&okLat);
                const double lon = parts.at(1).toDouble(&okLon);
                if (okLat && okLon && digitraffic::inFinlandBox(lat, lon))
                    coord = QGeoCoordinate(lat, lon);
            }
        } else if (name == QLatin1String("Time")) {
            time = QDateTime::fromString(xml.readElementText(), Qt::ISODate);
        } else if (name == QLatin1String("ParameterValue")) {
            bool ok = false;
            const double temp = xml.readElementText().toDouble(&ok);
            if (!ok || qIsNaN(temp) || !coord.isValid() || !time.isValid())
                continue;   // FMI reports missing values as "NaN"
            const QString key = QString::number(coord.latitude()) + QLatin1Char(',')
                                + QString::number(coord.longitude());
            Reading &r = latest[key];
            if (r.time.isValid() && r.time >= time)
                continue;
            r.time = time;
            r.point.coordinate = coord;
            r.point.tempC = temp;
            r.point.tempText = QString::number(qRound(temp)) + QStringLiteral("°");
        }
    }

    // A truncated or malformed response leaves the loop with only the records
    // parsed so far. Keep the previous (complete) overlay rather than replacing
    // it with a partial one — the poll timer retries in a few minutes.
    if (xml.hasError()) {
        qWarning("FmiWeatherClient: observation parse failed at line %lld: %ls",
                 xml.lineNumber(), qUtf16Printable(xml.errorString()));
        return;
    }

    QVector<WeatherPoint> points;
    points.reserve(latest.size());
    for (const Reading &r : std::as_const(latest))
        points.push_back(r.point);
    m_model->setPoints(points);
}
