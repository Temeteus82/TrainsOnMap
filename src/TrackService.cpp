#include "TrackService.h"

#include "Projection.h"

#include <QGeoCoordinate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr auto kUserAgent = "TrainsOnMap/0.1 (Qt6 scaffolding)";

// Append a single GeoJSON coordinate ring as a polyline segment.
// The infra-api returns EPSG:3067 (ETRS-TM35FIN) easting/northing in metres,
// which we reproject to WGS84 for the map.
void appendLine(const QJsonArray &coords, QVector<QVariantList> &out)
{
    QVariantList path;
    path.reserve(coords.size());
    for (const QJsonValue &cv : coords) {
        const QJsonArray p = cv.toArray();
        if (p.size() < 2)
            continue;
        const QGeoCoordinate c = tm35fin::toWgs84(p.at(0).toDouble(), p.at(1).toDouble());
        if (c.isValid())
            path.append(QVariant::fromValue(c));
    }
    if (path.size() >= 2)
        out.push_back(path);
}
}

TrackService::TrackService(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_model(new TrackListModel(this))
{
}

void TrackService::setEndpoint(const QString &endpoint)
{
    if (m_endpoint == endpoint)
        return;
    m_endpoint = endpoint;
    emit endpointChanged();
}

void TrackService::load()
{
    fetch(QUrl(m_endpoint));
}

void TrackService::loadForBounds(double west, double south, double east, double north)
{
    // The infra-api interprets bbox in its native CRS (EPSG:3067), so project the
    // viewport corners and take their min/max box — the grid is not perfectly
    // axis-aligned with lon/lat, so all four corners matter.
    double minE = std::numeric_limits<double>::max();
    double minN = std::numeric_limits<double>::max();
    double maxE = std::numeric_limits<double>::lowest();
    double maxN = std::numeric_limits<double>::lowest();
    for (const double lat : {south, north}) {
        for (const double lon : {west, east}) {
            double e = 0.0, n = 0.0;
            tm35fin::fromWgs84(lat, lon, e, n);
            minE = std::min(minE, e);
            maxE = std::max(maxE, e);
            minN = std::min(minN, n);
            maxN = std::max(maxN, n);
        }
    }

    QUrl url(m_endpoint);
    QUrlQuery query(url);
    // The infra-api rejects fractional coordinates ("Coordinate must be an
    // integer number"), so floor/ceil to whole metres without shrinking the box.
    query.addQueryItem(QStringLiteral("bbox"),
                       QStringLiteral("%1,%2,%3,%4")
                           .arg(qint64(std::floor(minE)))
                           .arg(qint64(std::floor(minN)))
                           .arg(qint64(std::ceil(maxE)))
                           .arg(qint64(std::ceil(maxN))));
    url.setQuery(query);
    fetch(url);
}

void TrackService::fetch(const QUrl &url)
{
    // Supersede any in-flight request so panning/zooming feels responsive.
    if (m_inflight) {
        m_inflight->abort();
        m_inflight = nullptr;
    }
    setLoading(true);
    setStatus(QStringLiteral("Loading track geometry…"));

    QNetworkRequest req{url};
    req.setRawHeader("Digitraffic-User", kUserAgent);

    QNetworkReply *reply = m_net->get(req);
    m_inflight = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleReply(reply); });
}

void TrackService::handleReply(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply == m_inflight)
        m_inflight = nullptr;

    if (reply->error() == QNetworkReply::OperationCanceledError)
        return;   // superseded by a newer viewport request

    setLoading(false);

    if (reply->error() != QNetworkReply::NoError) {
        setStatus(QStringLiteral("Track load error: %1").arg(reply->errorString()));
        return;
    }

    const QByteArray body = reply->readAll();
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        setStatus(QStringLiteral("Track parse error (%1)").arg(perr.errorString()));
        return;
    }

    const QJsonArray features = doc.object().value(QStringLiteral("features")).toArray();
    QVector<QVariantList> segments;
    segments.reserve(features.size());

    for (const QJsonValue &fv : features) {
        const QJsonObject geom = fv.toObject().value(QStringLiteral("geometry")).toObject();
        const QString type = geom.value(QStringLiteral("type")).toString();
        const QJsonArray coords = geom.value(QStringLiteral("coordinates")).toArray();

        if (type == QLatin1String("LineString")) {
            appendLine(coords, segments);
        } else if (type == QLatin1String("MultiLineString")) {
            for (const QJsonValue &part : coords)
                appendLine(part.toArray(), segments);
        }
    }

    m_model->setSegments(segments);
    setStatus(QStringLiteral("%1 track segments loaded").arg(segments.size()));
}

void TrackService::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void TrackService::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}
