#include "StationBoardService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>

#include "DigitrafficClient.h"
#include "DigitrafficFormat.h"
#include "NetworkDiagnostics.h"

namespace {
}   // namespace

StationBoardService::StationBoardService(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_board(new StationBoardModel(this))
{
    netdiag::logSslErrors(m_net, "StationBoardService");
}

void StationBoardService::setFleet(DigitrafficClient *fleet)
{
    if (m_fleet == fleet)
        return;
    if (m_fleet)
        disconnect(m_fleet, nullptr, this, nullptr);
    m_fleet = fleet;
    if (m_fleet) {
        connect(m_fleet, &DigitrafficClient::stationNamesChanged,
                this, &StationBoardService::onStationNames);
        onStationNames();   // the names may already be loaded
        connect(m_fleet, &DigitrafficClient::causeCategoryNamesChanged,
                this, &StationBoardService::onCauseCategoryNames);
        onCauseCategoryNames();   // the cause map may already be loaded
    }
    emit fleetChanged();
}

void StationBoardService::onCauseCategoryNames()
{
    if (m_fleet) {
        m_causeCategoryNames = m_fleet->causeCategoryNames();
        m_detailedCauseCategoryNames = m_fleet->detailedCauseCategoryNames();
    }
    if (!m_causeCategoryNames.isEmpty() && !m_rows.isEmpty())
        rebuildBoard();   // the board arrived before the cause map did
}

void StationBoardService::onStationNames()
{
    if (m_fleet)
        m_stationNames = m_fleet->stationNames();
    // Refresh the visible title if the code resolved to a name late.
    if (m_hasSelection) {
        const QString resolved = stationLabel(m_stationCode);
        if (resolved != m_stationName) {
            m_stationName = resolved;
            emit selectionChanged();
        }
    }
}

QString StationBoardService::stationLabel(const QString &code) const
{
    const QString name = m_stationNames.value(code);
    return name.isEmpty() ? code : name;
}

void StationBoardService::show(const QString &code, const QString &name)
{
    if (code.isEmpty())
        return;
    m_stationCode = code;
    m_stationName = m_stationNames.value(code, name.isEmpty() ? code : name);
    m_hasSelection = true;
    emit selectionChanged();

    m_rows.clear();
    m_board->clear();
    setLoading(true);
    setStatus(QStringLiteral("Loading board…"));

    const QString url = QStringLiteral(
        "https://rata.digitraffic.fi/api/v1/live-trains/station/%1"
        "?arriving_trains=8&departing_trains=8&include_nonstopping=false").arg(code);
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("Digitraffic-User", digitraffic::kUserAgent);
    req.setTransferTimeout(15000);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, code] { handleReply(reply, code); });
}

void StationBoardService::clear()
{
    if (!m_hasSelection)
        return;
    m_stationCode.clear();
    m_stationName.clear();
    m_hasSelection = false;
    m_rows.clear();
    m_board->clear();
    setLoading(false);
    setStatus(QString());
    emit selectionChanged();
}

void StationBoardService::handleReply(QNetworkReply *reply, const QString &code)
{
    reply->deleteLater();
    setLoading(false);
    // Drop a late reply for a station we've since navigated away from.
    if (code != m_stationCode || !m_hasSelection)
        return;
    if (reply->error() != QNetworkReply::NoError) {
        setStatus(QStringLiteral("Board unavailable"));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray()) {
        setStatus(QStringLiteral("Board unavailable"));
        return;
    }

    QVector<StationBoardRow> rows;
    const QJsonArray arr = doc.array();
    rows.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        const QJsonArray tt = o.value("timeTableRows").toArray();
        if (tt.isEmpty())
            continue;

        // This station's row: prefer the DEPARTURE (train leaving) over the
        // ARRIVAL (train terminating here).
        QJsonObject depRow, arrRow;
        for (const QJsonValue &rv : tt) {
            const QJsonObject r = rv.toObject();
            if (r.value("stationShortCode").toString() != code)
                continue;
            if (r.value("type").toString() == QLatin1String("DEPARTURE"))
                depRow = r;
            else
                arrRow = r;
        }
        const bool arriving = depRow.isEmpty();
        const QJsonObject r = arriving ? arrRow : depRow;
        if (r.isEmpty())
            continue;

        StationBoardRow row;
        row.arriving = arriving;

        // Train label: commuter line letter, else "TYPE NUMBER".
        const QString line = o.value("commuterLineID").toString();
        const QString type = o.value("trainType").toString();
        const int number = o.value("trainNumber").toInt();
        if (!line.isEmpty())
            row.trainLabel = line;
        else if (!type.isEmpty())
            row.trainLabel = QStringLiteral("%1 %2").arg(type).arg(number);
        else
            row.trainLabel = QString::number(number);

        // Destination = the train's final stop.
        row.destination = stationLabel(tt.last().toObject().value("stationShortCode").toString());

        const QString sched = r.value("scheduledTime").toString();
        const QString live = r.value("liveEstimateTime").toString().isEmpty()
                                 ? r.value("actualTime").toString()
                                 : r.value("liveEstimateTime").toString();
        row.timeText = digitraffic::hhmm(sched);
        const QString est = digitraffic::hhmm(live);
        if (!est.isEmpty() && est != row.timeText)
            row.estimateText = est;
        row.track = r.value("commercialTrack").toString();
        row.delayMinutes = r.value("differenceInMinutes").toInt();
        row.cancelled = o.value("cancelled").toBool() || r.value("cancelled").toBool();

        // Delay cause (top-level + detailed category), same extraction as
        // TrainDetailsService::buildStops.
        const QJsonArray causes = r.value("causes").toArray();
        if (!causes.isEmpty()) {
            const QJsonObject cause = causes.first().toObject();
            row.causeCode = cause.value("categoryCode").toString();
            row.causeDetailedCode = cause.value("detailedCategoryCode").toString();
        }

        row.sortTime = digitraffic::parseIso(live.isEmpty() ? sched : live);

        rows.push_back(std::move(row));
    }

    std::sort(rows.begin(), rows.end(), [](const StationBoardRow &a, const StationBoardRow &b) {
        return a.sortTime < b.sortTime;
    });

    m_rows = std::move(rows);
    rebuildBoard();
    setStatus(m_rows.isEmpty() ? QStringLiteral("No trains") : QString());
}

void StationBoardService::rebuildBoard()
{
    QVector<StationBoardRow> resolved = m_rows;
    for (StationBoardRow &row : resolved) {
        row.causeText = digitraffic::causeText(row.causeCode, row.causeDetailedCode,
                                               m_causeCategoryNames,
                                               m_detailedCauseCategoryNames);
    }
    m_board->setRows(resolved);
}

void StationBoardService::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void StationBoardService::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}
