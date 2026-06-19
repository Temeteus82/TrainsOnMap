#include "TrainDetailsService.h"

#include "DigitrafficMqttClient.h"

#include <QDateTime>
#include <QTime>
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
constexpr auto kUserAgent = "TrainsOnMap/0.1 (Qt6 scaffolding)";
constexpr auto kStationsUrl = "https://rata.digitraffic.fi/api/v1/metadata/stations";
// Abort a stalled request rather than leaving the panel stuck on "Loading…".
constexpr auto kRequestTimeout = std::chrono::seconds{15};

QString hhmm(const QString &iso)
{
    if (iso.isEmpty())
        return {};
    const QDateTime dt = QDateTime::fromString(iso, Qt::ISODateWithMs);
    if (!dt.isValid())
        return {};
    return dt.toLocalTime().toString(QStringLiteral("HH:mm"));
}

QString estimate(const QJsonObject &row, const QString &scheduled)
{
    // Best known time: actual if it happened, otherwise the live estimate.
    QString iso = row.value(QStringLiteral("actualTime")).toString();
    if (iso.isEmpty())
        iso = row.value(QStringLiteral("liveEstimateTime")).toString();
    const QString est = hhmm(iso);
    return est == scheduled ? QString() : est;   // only surface when it differs
}

// Build merged timing points from timeTableRows — every station the train
// passes, with ARRIVAL+DEPARTURE rows folded into one entry. Each point is
// tagged `stopping` (a booked stop) or not (passed through); the UI hides the
// non-stopping ones until the user asks to see all timing points.
QVector<TimetableStop> buildStops(const QJsonArray &rows)
{
    QVector<TimetableStop> stops;
    int current = -1;

    for (const QJsonValue &rv : rows) {
        const QJsonObject row = rv.toObject();

        const QString code = row.value(QStringLiteral("stationShortCode")).toString();
        if (current < 0 || stops[current].stationShortCode != code) {
            TimetableStop s;
            s.stationShortCode = code;
            stops.push_back(s);
            current = stops.size() - 1;
        }

        TimetableStop &s = stops[current];
        const QString sched = hhmm(row.value(QStringLiteral("scheduledTime")).toString());
        const bool isArrival = row.value(QStringLiteral("type")).toString() == QLatin1String("ARRIVAL");

        if (isArrival) {
            s.scheduledArrival = sched;
            s.estimatedArrival = estimate(row, sched);
        } else {
            s.scheduledDeparture = sched;
            s.estimatedDeparture = estimate(row, sched);
        }

        // Departure delay is the more useful headline; fall back to arrival.
        const int diff = row.value(QStringLiteral("differenceInMinutes")).toInt();
        if (!isArrival || s.delayMinutes == 0)
            s.delayMinutes = diff;

        if (row.value(QStringLiteral("cancelled")).toBool())
            s.cancelled = true;

        const QString track = row.value(QStringLiteral("commercialTrack")).toString();
        if (!track.isEmpty())
            s.track = track;

        if (row.value(QStringLiteral("commercialStop")).toBool())
            s.sawCommercial = true;
        if (row.value(QStringLiteral("trainStopping")).toBool())
            s.sawTrainStopping = true;
    }

    // Decide which points are "stops". Prefer commercial passenger stops; if the
    // run has none (e.g. freight), fall back to any booked stopping point; if it
    // has neither, treat everything as a stop so the panel is never empty.
    bool anyCommercial = false;
    bool anyStopping = false;
    for (const TimetableStop &s : stops) {
        anyCommercial = anyCommercial || s.sawCommercial;
        anyStopping = anyStopping || s.sawTrainStopping;
    }
    for (TimetableStop &s : stops)
        s.stopping = anyCommercial ? s.sawCommercial
                                   : (anyStopping ? s.sawTrainStopping : true);

    return stops;
}
}

TrainDetailsService::TrainDetailsService(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_model(new TimetableModel(this))
{
    m_net->setTransferTimeout(kRequestTimeout);
    fetchStations();   // warm the code->name cache in the background
}

void TrainDetailsService::fetchStations()
{
    QNetworkRequest req{QUrl(QString::fromLatin1(kStationsUrl))};
    req.setRawHeader("Digitraffic-User", kUserAgent);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleStations(reply); });
}

void TrainDetailsService::handleStations(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
        return;   // names just fall back to short codes

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray())
        return;

    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        m_stationNames.insert(o.value(QStringLiteral("stationShortCode")).toString(),
                              o.value(QStringLiteral("stationName")).toString());
    }

    if (!m_stops.isEmpty())   // a timetable arrived before the names did
        rebuildStops();
}

void TrainDetailsService::show(int trainNumber, const QString &departureDate)
{
    if (departureDate.isEmpty()) {
        setStatus(QStringLiteral("No departure date for train %1").arg(trainNumber));
        return;
    }

    m_trainNumber = trainNumber;
    m_departureDate = departureDate;
    m_hasSelection = true;
    m_title = QStringLiteral("Train %1").arg(trainNumber);
    m_subtitle.clear();
    m_cancelled = false;
    emit selectionChanged();

    m_stops.clear();
    m_model->clear();
    setLoading(true);
    setStatus(QStringLiteral("Loading timetable…"));

    const QUrl url(QStringLiteral("https://rata.digitraffic.fi/api/v1/trains/%1/%2")
                       .arg(departureDate)
                       .arg(trainNumber));
    QNetworkRequest req{url};
    req.setRawHeader("Digitraffic-User", kUserAgent);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleTrain(reply); });

    // Also stream live updates for this train (delays/estimates) over MQTT.
    if (m_stream)
        m_stream->subscribeTrain(departureDate, trainNumber);
}

void TrainDetailsService::handleTrain(QNetworkReply *reply)
{
    reply->deleteLater();
    setLoading(false);

    if (reply->error() != QNetworkReply::NoError) {
        setStatus(QStringLiteral("Timetable error: %1").arg(reply->errorString()));
        return;
    }

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isArray() || doc.array().isEmpty()) {
        setStatus(QStringLiteral("No timetable for train %1").arg(m_trainNumber));
        return;
    }

    applyTrainObject(doc.array().first().toObject(), /*live=*/false);
}

void TrainDetailsService::applyTrainObject(const QJsonObject &train, bool live)
{
    // Header: prefer a commuter line label (e.g. "U"), else "<type> <number>".
    const QString line = train.value(QStringLiteral("commuterLineID")).toString();
    const QString type = train.value(QStringLiteral("trainType")).toString();
    m_title = line.isEmpty() ? QStringLiteral("%1 %2").arg(type).arg(m_trainNumber)
                             : QStringLiteral("%1 (%2)").arg(line).arg(m_trainNumber);
    const QString category = train.value(QStringLiteral("trainCategory")).toString();
    const QString op = train.value(QStringLiteral("operatorShortCode")).toString().toUpper();
    m_subtitle = QStringLiteral("%1 · %2").arg(category, op);
    m_cancelled = train.value(QStringLiteral("cancelled")).toBool();
    emit selectionChanged();

    const QJsonArray rows = train.value(QStringLiteral("timeTableRows")).toArray();
    m_stops = buildStops(rows);
    emit routeStationsChanged();

    int stopCount = 0;
    for (const TimetableStop &s : m_stops)
        if (s.stopping)
            ++stopCount;

    rebuildStops();
    setStatus(live ? QStringLiteral("%1 stops · live %2")
                         .arg(stopCount)
                         .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")))
                   : QStringLiteral("%1 stops").arg(stopCount));
}

void TrainDetailsService::onStreamTrainMessage(const QByteArray &payload)
{
    if (!m_hasSelection || payload.isEmpty())
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    // MQTT publishes a single object; tolerate an array just in case.
    const QJsonObject train = doc.isArray() ? doc.array().first().toObject() : doc.object();
    if (train.value(QStringLiteral("trainNumber")).toInt() != m_trainNumber)
        return;   // not the train currently shown
    applyTrainObject(train, /*live=*/true);
}

void TrainDetailsService::rebuildStops()
{
    QVector<TimetableStop> resolved = m_stops;
    for (TimetableStop &s : resolved)
        s.stationName = m_stationNames.value(s.stationShortCode, s.stationShortCode);
    m_model->setStops(resolved);
}

void TrainDetailsService::clear()
{
    if (!m_hasSelection)
        return;
    m_hasSelection = false;
    m_trainNumber = 0;
    m_departureDate.clear();
    m_title.clear();
    m_subtitle.clear();
    m_cancelled = false;
    m_stops.clear();
    m_model->clear();
    setStatus({});
    emit selectionChanged();
    emit routeStationsChanged();

    if (m_stream)
        m_stream->unsubscribeTrain();
}

void TrainDetailsService::setStream(DigitrafficMqttClient *stream)
{
    if (m_stream == stream)
        return;
    if (m_stream)
        disconnect(m_stream, nullptr, this, nullptr);
    m_stream = stream;
    if (m_stream) {
        connect(m_stream, &DigitrafficMqttClient::trainMessage,
                this, &TrainDetailsService::onStreamTrainMessage);
    }
    emit streamChanged();
}

void TrainDetailsService::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void TrainDetailsService::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}
