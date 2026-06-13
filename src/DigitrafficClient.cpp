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
// Digitraffic asks every client to identify itself. Replace with your own app id.
constexpr auto kUserAgent = "TrainsOnMap/0.1 (Qt6 scaffolding)";
}

DigitrafficClient::DigitrafficClient(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_model(new TrainListModel(this))
{
    m_timer.setInterval(5000);
    connect(&m_timer, &QTimer::timeout, this, &DigitrafficClient::refresh);
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
    req.setRawHeader("Accept-Encoding", "gzip");   // Qt transparently inflates the reply

    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleReply(reply); });
    setStatus(QStringLiteral("Fetching train positions…"));
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
