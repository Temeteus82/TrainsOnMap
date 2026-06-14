#include "DigitrafficClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {
constexpr auto kLatestUrl = "https://rata.digitraffic.fi/api/v1/train-locations/latest/";
// Currently-running trains, carrying trainCategory for marker colouring.
constexpr auto kLiveTrainsUrl = "https://rata.digitraffic.fi/api/v1/live-trains";
// Digitraffic asks every client to identify itself. Replace with your own app id.
constexpr auto kUserAgent = "TrainsOnMap/0.1 (Qt6 scaffolding)";
constexpr int kCategoryRefreshMs = 5 * 60 * 1000;
// REST is the bootstrap + prune path; MQTT carries live deltas in between, so
// the snapshot only needs to run slowly. Matches the Swift app's 60 s resync.
constexpr int kResyncIntervalMs = 60 * 1000;
}

DigitrafficClient::DigitrafficClient(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_model(new TrainListModel(this))
{
    m_timer.setInterval(kResyncIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &DigitrafficClient::refresh);

    // Categories change slowly; refresh them on their own, longer cadence.
    m_categoryTimer.setInterval(kCategoryRefreshMs);
    connect(&m_categoryTimer, &QTimer::timeout, this, &DigitrafficClient::refreshCategories);
    m_categoryTimer.start();
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
    QHash<int, QString> types;
    QHash<int, QString> categories;
    QHash<int, QString> commuterLines;
    QHash<int, TrainStatus> statuses;
    types.reserve(arr.size());
    categories.reserve(arr.size());
    commuterLines.reserve(arr.size());
    statuses.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        const int number = o.value("trainNumber").toInt();
        types.insert(number, o.value("trainType").toString());
        categories.insert(number, o.value("trainCategory").toString());
        // commuterLineID is "" for non-commuter trains; kept as-is so the badge
        // label can test for emptiness.
        commuterLines.insert(number, o.value("commuterLineID").toString());

        TrainStatus st;
        st.known = true;
        st.cancelled = o.value("cancelled").toBool();
        st.running = o.value("runningCurrently").toBool();
        // Current delay = differenceInMinutes of the most recently passed stop
        // (the last timetable row that already has an actualTime).
        const QJsonArray rows = o.value("timeTableRows").toArray();
        for (const QJsonValue &rv : rows) {
            const QJsonObject row = rv.toObject();
            if (!row.value("actualTime").toString().isEmpty())
                st.delayMinutes = row.value("differenceInMinutes").toInt();
        }
        statuses.insert(number, st);
    }
    m_model->setTrainMetadata(types, categories, commuterLines);
    m_model->setTrainStatuses(statuses);
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
