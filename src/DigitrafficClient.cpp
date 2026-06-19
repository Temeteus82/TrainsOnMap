#include "DigitrafficClient.h"

#include "RailGraph.h"

#include <QGeoCoordinate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <chrono>

namespace {
constexpr auto kLatestUrl = "https://rata.digitraffic.fi/api/v1/train-locations/latest/";
// Currently-running trains, carrying trainCategory for marker colouring.
constexpr auto kLiveTrainsUrl = "https://rata.digitraffic.fi/api/v1/live-trains";
// Station metadata (short code -> name + coordinate); fetched once at startup.
constexpr auto kStationsUrl = "https://rata.digitraffic.fi/api/v1/metadata/stations";
// Digitraffic asks every client to identify itself. Replace with your own app id.
constexpr auto kUserAgent = "TrainsOnMap/0.1 (Qt6 scaffolding)";
// REST is the bootstrap + prune path; MQTT carries live deltas in between, so
// the snapshot only needs to run slowly. Matches the Swift app's 60 s resync.
constexpr int kResyncIntervalMs = 60 * 1000;
// Abort a stalled request rather than leaving the status stuck on "Fetching…".
constexpr auto kRequestTimeout = std::chrono::seconds{15};
}

DigitrafficClient::DigitrafficClient(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_model(new TrainListModel(this))
{
    m_net->setTransferTimeout(kRequestTimeout);

    m_timer.setInterval(kResyncIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &DigitrafficClient::refresh);
    // Categories piggyback on refresh() (every kResyncIntervalMs while active),
    // so they need no separate poll — and stay quiet when polling is stopped.

    fetchStations();   // one-shot: station coordinates for parked-train pinning
}

void DigitrafficClient::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (m_active) {
        refresh();          // fetch right away, then on the timer
        m_timer.start();
    } else {
        m_timer.stop();
    }
    emit activeChanged();
}

void DigitrafficClient::setMatcher(TrackService *matcher)
{
    if (m_matcher == matcher)
        return;
    m_matcher = matcher;
    // TrackService implements TrackMatcher; hand the model the interface so it can
    // snap/flag every fix (REST and the shared MQTT path both funnel through it).
    m_model->setMatcher(matcher);
    emit matcherChanged();
}

void DigitrafficClient::setPollIntervalMs(int ms)
{
    ms = qMax(1000, ms);    // be a good API citizen
    if (m_timer.interval() == ms)
        return;
    m_timer.setInterval(ms);
    emit pollIntervalMsChanged();
}

void DigitrafficClient::refresh()
{
    QNetworkRequest req{QUrl(QString::fromLatin1(kLatestUrl))};
    req.setRawHeader("Digitraffic-User", kUserAgent);
    // Don't set Accept-Encoding by hand: Qt 6 advertises it and inflates gzip
    // transparently. Setting it ourselves disables that, so readAll() would
    // return raw compressed bytes and JSON parsing would fail.

    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleReply(reply); });
    setStatus(QStringLiteral("Fetching train positions…"));

    refreshCategories();   // seed marker colours alongside the position snapshot
}

void DigitrafficClient::refreshCategories()
{
    QNetworkRequest req{QUrl(QString::fromLatin1(kLiveTrainsUrl))};
    req.setRawHeader("Digitraffic-User", kUserAgent);

    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleCategories(reply); });
}

void DigitrafficClient::handleCategories(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
        return;   // markers just stay neutral until the next refresh

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray())
        return;

    const QJsonArray arr = doc.array();
    QHash<TrainKey, QString> types;
    QHash<TrainKey, QString> categories;
    QHash<TrainKey, QString> commuterLines;
    QHash<TrainKey, TrainStatus> statuses;
    QHash<TrainKey, TrainRoute> routes;
    QVector<QVector<QString>> routeSequences;   // for off-thread route precompute
    types.reserve(arr.size());
    categories.reserve(arr.size());
    commuterLines.reserve(arr.size());
    statuses.reserve(arr.size());
    routes.reserve(arr.size());
    routeSequences.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        // Key on (departureDate, trainNumber): the number alone is reused daily
        // and can be in motion under two dates at once. See TrainKey.
        const TrainKey key{o.value("departureDate").toString(), o.value("trainNumber").toInt()};
        types.insert(key, o.value("trainType").toString());
        categories.insert(key, o.value("trainCategory").toString());
        // commuterLineID is "" for non-commuter trains; kept as-is so the badge
        // label can test for emptiness.
        commuterLines.insert(key, o.value("commuterLineID").toString());

        TrainStatus st;
        st.known = true;
        st.cancelled = o.value("cancelled").toBool();
        st.running = o.value("runningCurrently").toBool();
        // Current delay = differenceInMinutes of the most recently passed stop
        // (the last timetable row that already has an actualTime). In the same
        // pass, collect the route's station codes (ARRIVAL+DEPARTURE rows repeat a
        // code, so de-dupe consecutively) plus the booked commercialTrack per
        // stop, for route-constrained matching + platform snapping.
        const QJsonArray rows = o.value("timeTableRows").toArray();
        TrainRoute route;
        QStringList rawCodes;
        rawCodes.reserve(rows.size());
        for (const QJsonValue &rv : rows) {
            const QJsonObject row = rv.toObject();
            if (!row.value("actualTime").toString().isEmpty())
                st.delayMinutes = row.value("differenceInMinutes").toInt();
            const QString code = row.value("stationShortCode").toString();
            if (code.isEmpty())
                continue;
            rawCodes.push_back(code);
            const QString track = row.value("commercialTrack").toString();
            if (!track.isEmpty() && row.value("trainStopping").toBool())
                route.commercialTrack.insert(code, track);
        }
        // Skip empties + collapse consecutive duplicates via the shared helper, so
        // this key matches TrainDetailsService::routeStations exactly (R8).
        route.codes = RailGraph::canonicalRouteCodes(rawCodes);
        statuses.insert(key, st);
        if (!route.codes.isEmpty()) {
            routeSequences.push_back(route.codes);
            routes.insert(key, std::move(route));
        }
    }
    m_model->setTrainMetadata(types, categories, commuterLines);
    m_model->setTrainStatuses(statuses);
    m_model->setTrainRoutes(routes);
    if (m_matcher)
        m_matcher->precomputeRoutes(routeSequences);
}

void DigitrafficClient::fetchStations()
{
    QNetworkRequest req{QUrl(QString::fromLatin1(kStationsUrl))};
    req.setRawHeader("Digitraffic-User", kUserAgent);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleStations(reply); });
}

void DigitrafficClient::handleStations(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
        return;   // parked-train station snapping just stays disabled

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray())
        return;

    const QJsonArray arr = doc.array();
    QHash<QString, QGeoCoordinate> coords;
    coords.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        const QString code = o.value("stationShortCode").toString();
        const double lat = o.value("latitude").toDouble();
        const double lon = o.value("longitude").toDouble();
        if (!code.isEmpty())
            coords.insert(code, QGeoCoordinate(lat, lon));
    }
    m_model->setStationCoords(coords);
}

void DigitrafficClient::handleReply(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        setStatus(QStringLiteral("Network error: %1").arg(reply->errorString()));
        return;
    }

    const QByteArray body = reply->readAll();
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isArray()) {
        setStatus(QStringLiteral("Unexpected response (%1)").arg(perr.errorString()));
        return;
    }

    const QJsonArray arr = doc.array();
    QVector<TrainPosition> trains;
    trains.reserve(arr.size());

    for (const QJsonValue &v : arr) {
        const TrainPosition tp = parseTrainLocation(v.toObject());
        if (tp.coordinate.isValid())
            trains.push_back(tp);
    }

    m_model->updateTrains(trains);
    setStatus(QStringLiteral("%1 trains • updated %2")
                  .arg(trains.size())
                  .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
}

void DigitrafficClient::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}
