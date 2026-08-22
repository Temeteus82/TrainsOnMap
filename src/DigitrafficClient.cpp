#include "DigitrafficClient.h"

#include "NetworkDiagnostics.h"

#include "RailGraph.h"

#include <QGeoCoordinate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include "DigitrafficFormat.h"

#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <chrono>

namespace {
constexpr auto kLatestUrl = "https://rata.digitraffic.fi/api/v1/train-locations/latest/";
// Currently-running trains, carrying trainCategory for marker colouring.
constexpr auto kLiveTrainsUrl = "https://rata.digitraffic.fi/api/v1/live-trains";
// Station metadata (short code -> name + coordinate); fetched once at startup.
constexpr auto kStationsUrl = "https://rata.digitraffic.fi/api/v1/metadata/stations";
// Delay-cause category codes (e.g. "A" -> "Aikataulu ja liikennöinti"); fetched
// once at startup for the timetable's delay-cause line.
constexpr auto kCauseCategoriesUrl = "https://rata.digitraffic.fi/api/v1/metadata/cause-category-codes";
// Detailed cause category codes (e.g. "S2" -> "Sähköratavika"); the top-level
// category alone (e.g. "Sähkörata") is too coarse to be a useful delay reason.
constexpr auto kDetailedCauseCategoriesUrl
    = "https://rata.digitraffic.fi/api/v1/metadata/detailed-cause-category-codes";
// REST is the bootstrap + prune path; MQTT carries live deltas in between, so
// the snapshot only needs to run slowly. Matches the Swift app's 60 s resync.
constexpr int kResyncIntervalMs = 60 * 1000;
// /live-trains is polled with ?version= deltas in between, but a full snapshot is
// re-pulled at least this often to GC accumulated state and let the route cache
// evict trains that quietly left the fleet (a finished train may never appear in
// a delta). At the 60 s resync that's one full pull per ~5 deltas.
constexpr qint64 kFullCategoriesIntervalMs = 5 * 60 * 1000;
// Abort a stalled request rather than leaving the status stuck on "Fetching…".
constexpr auto kRequestTimeout = std::chrono::seconds{15};
}

DigitrafficClient::DigitrafficClient(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_model(new TrainListModel(this))
    , m_stations(new StationListModel(this))
{
    m_net->setTransferTimeout(kRequestTimeout);
    netdiag::logSslErrors(m_net, "DigitrafficClient");

    m_timer.setInterval(kResyncIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &DigitrafficClient::refresh);
    // Categories piggyback on refresh() (every kResyncIntervalMs while active),
    // so they need no separate poll — and stay quiet when polling is stopped.

    fetchStations();   // one-shot: station coordinates for parked-train pinning
    fetchCauseCategories();   // one-shot: delay-cause code -> name
    fetchDetailedCauseCategories();   // one-shot: detailed delay-cause code -> name
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
    // Hand the matcher to the model so it can snap/flag every fix (REST and the
    // shared MQTT path both funnel through it).
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
    req.setRawHeader("Digitraffic-User", digitraffic::kUserAgent);
    // Don't set Accept-Encoding by hand: Qt 6 advertises it and inflates gzip
    // transparently. Setting it ourselves disables that, so readAll() would
    // return raw compressed bytes and JSON parsing would fail.

    QNetworkReply *reply = m_net->get(req);
    adjustPending(+1);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleReply(reply); });
    setStatus(QStringLiteral("Fetching train positions…"));

    refreshCategories();   // seed marker colours alongside the position snapshot
}

void DigitrafficClient::refreshCategories()
{
    // Decide between a full snapshot and an incremental version-delta. Full when
    // we have no baseline yet (first call, or a prior full was cleared to 0) or
    // the periodic resync window has elapsed; otherwise ask only for trains
    // modified after the highest version we've already merged.
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const bool full = m_liveVersion == 0 || !m_lastFullCategories.isValid()
                      || m_lastFullCategories.msecsTo(now) >= kFullCategoriesIntervalMs;

    QUrl url(QString::fromLatin1(kLiveTrainsUrl));
    if (full) {
        m_lastFullCategories = now;
    } else {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("version"), QString::number(m_liveVersion));
        url.setQuery(query);
    }

    QNetworkRequest req{url};
    req.setRawHeader("Digitraffic-User", digitraffic::kUserAgent);

    QNetworkReply *reply = m_net->get(req);
    adjustPending(+1);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, full] { handleCategories(reply, full); });
}

void DigitrafficClient::handleCategories(QNetworkReply *reply, bool full)
{
    reply->deleteLater();
    adjustPending(-1);
    if (reply->error() != QNetworkReply::NoError)
        return;   // markers keep their last colours until the next refresh

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray())
        return;

    // A full snapshot is authoritative: drop the accumulated state (and the
    // version baseline) first, so trains that have left the fleet fall out and the
    // route cache can evict them. A delta merges onto what we already have.
    if (full) {
        m_accTypes.clear();
        m_accCategories.clear();
        m_accLines.clear();
        m_accStatuses.clear();
        m_accRoutes.clear();
        m_liveVersion = 0;
    }

    const QJsonArray arr = doc.array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        // Key on (departureDate, trainNumber): the number alone is reused daily
        // and can be in motion under two dates at once. See TrainKey.
        const TrainKey key{o.value("departureDate").toString(), o.value("trainNumber").toInt()};
        // Advance the delta baseline to the newest version we've merged. Each
        // train object carries the full state (a delta is not a patch), so a plain
        // upsert keeps the accumulated maps correct.
        m_liveVersion = std::max(m_liveVersion, o.value("version").toInteger(0));

        m_accTypes.insert(key, o.value("trainType").toString());
        m_accCategories.insert(key, o.value("trainCategory").toString());
        // commuterLineID is "" for non-commuter trains; kept as-is so the badge
        // label can test for emptiness.
        m_accLines.insert(key, o.value("commuterLineID").toString());

        TrainStatus st;
        st.known = true;
        st.cancelled = o.value("cancelled").toBool();
        st.running = o.value("runningCurrently").toBool();
        // Current delay = differenceInMinutes of the most recently passed stop
        // (the last timetable row that already has an actualTime). In the same
        // pass, collect the route's station codes plus the booked commercialTrack
        // per stop, for route-constrained matching + platform snapping.
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

        m_accStatuses.insert(key, st);
        if (!route.codes.isEmpty())
            m_accRoutes.insert(key, std::move(route));
        else
            m_accRoutes.remove(key);   // schedule disappeared on this update
    }

    // Push the full accumulated state to the model (the setters replace wholesale)
    // and re-drive route precompute/eviction against the whole known fleet.
    m_model->setTrainMetadata(m_accTypes, m_accCategories, m_accLines);
    m_model->setTrainStatuses(m_accStatuses);
    m_model->setTrainRoutes(m_accRoutes);
    recomputePunctuality();

    if (m_matcher) {
        QVector<QVector<QString>> routeSequences;
        routeSequences.reserve(m_accRoutes.size());
        for (auto it = m_accRoutes.constBegin(); it != m_accRoutes.constEnd(); ++it)
            routeSequences.push_back(it.value().codes);
        m_matcher->precomputeRoutes(routeSequences);
    }
}

void DigitrafficClient::fetchStations()
{
    QNetworkRequest req{QUrl(QString::fromLatin1(kStationsUrl))};
    req.setRawHeader("Digitraffic-User", digitraffic::kUserAgent);
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
    m_stationNames.clear();
    m_stationNames.reserve(arr.size());
    QVector<StationPoint> passengerStations;   // for the clickable map layer
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        const QString code = o.value("stationShortCode").toString();
        if (code.isEmpty())
            continue;
        const QGeoCoordinate coord(o.value("latitude").toDouble(),
                                   o.value("longitude").toDouble());
        const QString name = o.value("stationName").toString();
        coords.insert(code, coord);
        m_stationNames.insert(code, name);
        // Only passenger stations get a map dot (freight/junction points would
        // just clutter the board layer, and their station board is uninteresting).
        if (o.value("passengerTraffic").toBool() && coord.isValid())
            passengerStations.push_back({code, name, coord});
    }
    m_model->setStationCoords(coords);
    m_stations->setStations(passengerStations);
    emit stationNamesChanged();
}

void DigitrafficClient::fetchCauseCategories()
{
    QNetworkRequest req{QUrl(QString::fromLatin1(kCauseCategoriesUrl))};
    req.setRawHeader("Digitraffic-User", digitraffic::kUserAgent);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleCauseCategories(reply); });
}

void DigitrafficClient::handleCauseCategories(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
        return;   // the delay-cause line just stays blank

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray())
        return;

    const QJsonArray arr = doc.array();
    m_causeCategoryNames.clear();
    m_causeCategoryNames.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        const QString code = o.value("categoryCode").toString();
        if (!code.isEmpty())
            m_causeCategoryNames.insert(code, o.value("categoryName").toString());
    }
    emit causeCategoryNamesChanged();
}

void DigitrafficClient::fetchDetailedCauseCategories()
{
    QNetworkRequest req{QUrl(QString::fromLatin1(kDetailedCauseCategoriesUrl))};
    req.setRawHeader("Digitraffic-User", digitraffic::kUserAgent);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { handleDetailedCauseCategories(reply); });
}

void DigitrafficClient::handleDetailedCauseCategories(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
        return;   // the delay-cause line just stays at the coarser top-level category

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray())
        return;

    const QJsonArray arr = doc.array();
    m_detailedCauseCategoryNames.clear();
    m_detailedCauseCategoryNames.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        const QString code = o.value("detailedCategoryCode").toString();
        if (!code.isEmpty())
            m_detailedCauseCategoryNames.insert(code, o.value("detailedCategoryName").toString());
    }
    emit causeCategoryNamesChanged();
}

void DigitrafficClient::handleReply(QNetworkReply *reply)
{
    reply->deleteLater();
    adjustPending(-1);

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

void DigitrafficClient::adjustPending(int delta)
{
    const bool wasLoading = m_pendingRequests > 0;
    m_pendingRequests += delta;
    if ((m_pendingRequests > 0) != wasLoading)
        emit loadingChanged();
}

void DigitrafficClient::recomputePunctuality()
{
    // Aggregate on-time performance from the running fleet: a train is punctual
    // when it's running, not cancelled, and ≤5 min behind schedule at its last
    // passed stop. Grouped by the broad categories the sidebar filters on. Reuses
    // the /live-trains data already polled each cycle — no extra request.
    struct Tally { int onTime = 0; int total = 0; };
    QHash<QString, Tally> byCat;
    for (auto it = m_accStatuses.constBegin(); it != m_accStatuses.constEnd(); ++it) {
        const TrainStatus &st = it.value();
        if (!st.known || !st.running || st.cancelled)
            continue;
        const QString cat = m_accCategories.value(it.key());
        if (cat != "Long-distance" && cat != "Commuter" && cat != "Cargo")
            continue;
        Tally &t = byCat[cat];
        ++t.total;
        if (st.delayMinutes <= 5)
            ++t.onTime;
    }

    // Fixed order (matches the sidebar), abbreviated labels.
    struct Row { const char *cat; const char *label; };
    static const Row order[] = {
        {"Long-distance", "LD"}, {"Commuter", "Cmtr"}, {"Cargo", "Cargo"}};
    QStringList parts;
    for (const Row &r : order) {
        const Tally t = byCat.value(QString::fromLatin1(r.cat));
        if (t.total == 0)
            continue;
        parts << QStringLiteral("%1 %2%").arg(QString::fromLatin1(r.label))
                                         .arg(t.onTime * 100 / t.total);
    }
    const QString s = parts.isEmpty()
        ? QString()
        : QStringLiteral("On time (≤5m): ") + parts.join(QStringLiteral(" · "));
    if (m_punctuality != s) {
        m_punctuality = s;
        emit punctualityChanged();
    }
}
