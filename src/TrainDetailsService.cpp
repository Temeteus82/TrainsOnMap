#include "TrainDetailsService.h"

#include "DigitrafficClient.h"
#include "DigitrafficFormat.h"
#include "DigitrafficMqttClient.h"
#include "NetworkDiagnostics.h"

#include <QTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <utility>

namespace {
// Abort a stalled request rather than leaving the panel stuck on "Loading…".
constexpr auto kRequestTimeout = std::chrono::seconds{15};

QString estimate(const QJsonObject &row, const QString &scheduled)
{
    // Best known time: actual if it happened, otherwise the live estimate.
    QString iso = row.value(QStringLiteral("actualTime")).toString();
    if (iso.isEmpty())
        iso = row.value(QStringLiteral("liveEstimateTime")).toString();
    const QString est = digitraffic::hhmm(iso);
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
        const QString sched = digitraffic::hhmm(row.value(QStringLiteral("scheduledTime")).toString());
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

        // Delay cause (top-level + detailed category; a row rarely carries more
        // than one cause). Departure preferred over arrival, mirroring
        // delayMinutes above.
        const QJsonArray causes = row.value(QStringLiteral("causes")).toArray();
        if (!causes.isEmpty() && (!isArrival || s.causeCode.isEmpty())) {
            const QJsonObject cause = causes.first().toObject();
            const QString code = cause.value(QStringLiteral("categoryCode")).toString();
            if (!code.isEmpty()) {
                s.causeCode = code;
                s.causeDetailedCode = cause.value(QStringLiteral("detailedCategoryCode")).toString();
            }
        }

        const QString track = row.value(QStringLiteral("commercialTrack")).toString();
        if (!track.isEmpty())
            s.track = track;

        if (row.value(QStringLiteral("commercialStop")).toBool())
            s.sawCommercial = true;
        if (row.value(QStringLiteral("trainStopping")).toBool())
            s.sawTrainStopping = true;
        // A recorded actualTime means the train has been here — drives journey
        // progress (the `passed`/`isNext` flags computed in TimetableModel).
        if (!row.value(QStringLiteral("actualTime")).toString().isEmpty())
            s.sawActual = true;
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

// Curated, passenger-facing amenities for a wagon (the API also carries smoking/
// video/luggage flags; these four are the ones worth a glance at boarding time).
QStringList wagonAmenities(const QJsonObject &w)
{
    QStringList a;
    if (w.value(QStringLiteral("catering")).toBool())   a << QStringLiteral("Catering");
    if (w.value(QStringLiteral("disabled")).toBool())   a << QStringLiteral("Accessible");
    if (w.value(QStringLiteral("playground")).toBool()) a << QStringLiteral("Family");
    if (w.value(QStringLiteral("pet")).toBool())        a << QStringLiteral("Pet");
    return a;
}

// Flatten one journeySection's locomotives + wagons into a single list ordered by
// physical position (API `location`) — the carriage order a passenger walks past.
QVector<CompositionVehicle> buildVehicles(const QJsonObject &section)
{
    QVector<CompositionVehicle> vehicles;

    for (const QJsonValue &lv : section.value(QStringLiteral("locomotives")).toArray()) {
        const QJsonObject l = lv.toObject();
        CompositionVehicle cv;
        cv.position = l.value(QStringLiteral("location")).toInt();
        cv.locomotive = true;
        cv.vehicleType = l.value(QStringLiteral("locomotiveType")).toString();
        cv.powerType = l.value(QStringLiteral("powerType")).toString();
        vehicles.push_back(cv);
    }

    for (const QJsonValue &wv : section.value(QStringLiteral("wagons")).toArray()) {
        const QJsonObject w = wv.toObject();
        CompositionVehicle cv;
        cv.position = w.value(QStringLiteral("location")).toInt();
        cv.locomotive = false;
        const int sales = w.value(QStringLiteral("salesNumber")).toInt();
        cv.label = sales > 0 ? QString::number(sales) : QString();
        cv.vehicleType = w.value(QStringLiteral("wagonType")).toString();
        cv.amenities = wagonAmenities(w);
        vehicles.push_back(cv);
    }

    std::sort(vehicles.begin(), vehicles.end(),
              [](const CompositionVehicle &a, const CompositionVehicle &b) {
                  return a.position < b.position;
              });
    return vehicles;
}
}

TrainDetailsService::TrainDetailsService(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_model(new TimetableModel(this))
    , m_composition(new CompositionModel(this))
{
    m_net->setTransferTimeout(kRequestTimeout);
    netdiag::logSslErrors(m_net, "TrainDetailsService");
}

QString TrainDetailsService::stationLabel(const QString &shortCode) const
{
    return m_stationNames.value(shortCode, shortCode);
}

void TrainDetailsService::onStationNames()
{
    if (m_fleet)
        m_stationNames = m_fleet->stationNames();
    if (!m_stationNames.isEmpty() && !m_stops.isEmpty())
        rebuildStops();   // a timetable arrived before the names did
}

void TrainDetailsService::onCauseCategoryNames()
{
    if (m_fleet) {
        m_causeCategoryNames = m_fleet->causeCategoryNames();
        m_detailedCauseCategoryNames = m_fleet->detailedCauseCategoryNames();
    }
    if (!m_causeCategoryNames.isEmpty() && !m_stops.isEmpty())
        rebuildStops();   // a timetable arrived before the cause map did
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
    req.setRawHeader("Digitraffic-User", digitraffic::kUserAgent);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleTrain(reply); });

    // Carriage order is static per run — fetch it once alongside the timetable.
    clearComposition();
    fetchComposition(trainNumber, departureDate);

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

void TrainDetailsService::fetchComposition(int trainNumber, const QString &departureDate)
{
    const QUrl url(QStringLiteral("https://rata.digitraffic.fi/api/v1/compositions/%1/%2")
                       .arg(departureDate)
                       .arg(trainNumber));
    QNetworkRequest req{url};
    req.setRawHeader("Digitraffic-User", digitraffic::kUserAgent);
    QNetworkReply *reply = m_net->get(req);
    // Tag the reply with the run it was issued for: a slow composition reply for a
    // previously-selected train must not overwrite the consist now on screen.
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, trainNumber, departureDate] {
                if (trainNumber == m_trainNumber && departureDate == m_departureDate)
                    handleComposition(reply);
                else
                    reply->deleteLater();   // stale selection — drop it
            });
}

void TrainDetailsService::handleComposition(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        // A 404 just means this run has no stock data (common for commuter/freight);
        // leave the strip hidden rather than surfacing an error.
        clearComposition();
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    // The endpoint returns a single object; tolerate an array form defensively.
    const QJsonObject comp = doc.isArray()
        ? (doc.array().isEmpty() ? QJsonObject() : doc.array().first().toObject())
        : doc.object();

    const QJsonArray sections = comp.value(QStringLiteral("journeySections")).toArray();
    if (sections.isEmpty()) {
        clearComposition();
        return;
    }

    // Show the departure consist (first section). Runs that join/split mid-route
    // have more sections; we surface the count so the panel can flag it.
    const QJsonObject first = sections.first().toObject();
    const QVector<CompositionVehicle> vehicles = buildVehicles(first);
    m_composition->setVehicles(vehicles);

    int wagons = 0;
    for (const CompositionVehicle &cv : vehicles)
        if (!cv.locomotive)
            ++wagons;

    const int totalLen = first.value(QStringLiteral("totalLength")).toInt();
    const int maxSpeed = first.value(QStringLiteral("maximumSpeed")).toInt();
    QStringList parts;
    parts << (wagons == 1 ? QStringLiteral("1 car") : QStringLiteral("%1 cars").arg(wagons));
    if (totalLen > 0)
        parts << QStringLiteral("%1 m").arg(totalLen);
    if (maxSpeed > 0)
        parts << QStringLiteral("max %1 km/h").arg(maxSpeed);
    m_compositionSummary = parts.join(QStringLiteral(" · "));

    const QString begin = first.value(QStringLiteral("beginTimeTableRow")).toObject()
                              .value(QStringLiteral("stationShortCode")).toString();
    const QString end = first.value(QStringLiteral("endTimeTableRow")).toObject()
                            .value(QStringLiteral("stationShortCode")).toString();
    m_compositionLeg = (begin.isEmpty() && end.isEmpty())
        ? QString()
        : QStringLiteral("%1 → %2").arg(stationLabel(begin), stationLabel(end));

    m_compositionSectionCount = sections.size();
    m_hasComposition = !vehicles.isEmpty();
    emit compositionChanged();
}

void TrainDetailsService::clearComposition()
{
    const bool had = m_hasComposition || !m_compositionSummary.isEmpty()
                     || !m_compositionLeg.isEmpty() || m_compositionSectionCount != 0;
    m_composition->clear();
    m_hasComposition = false;
    m_compositionSummary.clear();
    m_compositionLeg.clear();
    m_compositionSectionCount = 0;
    if (had)
        emit compositionChanged();
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
    for (TimetableStop &s : resolved) {
        s.stationName = m_stationNames.value(s.stationShortCode, s.stationShortCode);
        s.causeText = digitraffic::causeText(s.causeCode, s.causeDetailedCode,
                                             m_causeCategoryNames,
                                             m_detailedCauseCategoryNames);
    }
    m_model->setStops(std::move(resolved));
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
    clearComposition();
    setStatus({});
    emit selectionChanged();
    emit routeStationsChanged();

    if (m_stream)
        m_stream->unsubscribeTrain();
}

void TrainDetailsService::setFleet(DigitrafficClient *fleet)
{
    if (m_fleet == fleet)
        return;
    if (m_fleet)
        disconnect(m_fleet, nullptr, this, nullptr);
    m_fleet = fleet;
    if (m_fleet) {
        connect(m_fleet, &DigitrafficClient::stationNamesChanged,
                this, &TrainDetailsService::onStationNames);
        onStationNames();   // the names may have loaded before we were wired up
        connect(m_fleet, &DigitrafficClient::causeCategoryNamesChanged,
                this, &TrainDetailsService::onCauseCategoryNames);
        onCauseCategoryNames();   // the cause map may have loaded before we were wired up
    }
    emit fleetChanged();
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
