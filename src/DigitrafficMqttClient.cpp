#include "DigitrafficMqttClient.h"

#include "DigitrafficFormat.h"
#include "MqttCodec.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QStringList>
#include <QUrl>
#include <QWebSocket>
#include <QWebSocketHandshakeOptions>

#if QT_CONFIG(ssl)
#include <QSslError>
#endif

namespace {
constexpr auto kBrokerUrl = "wss://rata.digitraffic.fi:443/mqtt";
constexpr auto kLocationsTopic = "train-locations/#";
constexpr quint16 kKeepAliveSecs = 60;
constexpr int kReconnectMs = 5000;
// Train messages are a few KB; anything near the 256 MB MQTT ceiling means the
// stream is out of sync. Cap well below that and resync rather than buffer it.
constexpr int kMaxPacketBytes = 1 << 20;   // 1 MiB
}

DigitrafficMqttClient::DigitrafficMqttClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
{
    connect(m_socket, &QWebSocket::connected, this, &DigitrafficMqttClient::onSocketConnected);
    connect(m_socket, &QWebSocket::disconnected, this, &DigitrafficMqttClient::onSocketDisconnected);
    connect(m_socket, &QWebSocket::binaryMessageReceived, this, &DigitrafficMqttClient::onBinaryMessage);
    connect(m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        setStatus(QStringLiteral("Socket error: %1").arg(m_socket->errorString()));
    });
#if QT_CONFIG(ssl)
    // Diagnostics only — deliberately no ignoreSslErrors(): the handshake still
    // fails, and errorOccurred above still drives the reconnect. Without this a
    // cert failure reaches the user as a bare "Socket error" with no reason.
    // (netdiag::logSslErrors covers the QNetworkAccessManager owners; QWebSocket
    // has its own signal shape, and inlining it here keeps Qt WebSockets out of
    // a header four non-WebSocket translation units include.)
    connect(m_socket, &QWebSocket::sslErrors, this, [](const QList<QSslError> &errors) {
        QStringList reasons;
        reasons.reserve(errors.size());
        for (const QSslError &e : errors)
            reasons << e.errorString();
        qWarning("DigitrafficMqttClient: TLS error connecting to the broker: %ls",
                 qUtf16Printable(reasons.join(QStringLiteral("; "))));
    });
#endif

    m_pingTimer.setInterval(kKeepAliveSecs * 1000 / 2);
    connect(&m_pingTimer, &QTimer::timeout, this, [this] { send(mqttwire::buildPingReq()); });

    m_reconnectTimer.setSingleShot(true);
    m_reconnectTimer.setInterval(kReconnectMs);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this] {
        if (m_active)
            openConnection();
    });
}

DigitrafficMqttClient::~DigitrafficMqttClient()
{
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->sendBinaryMessage(mqttwire::buildDisconnect());
        m_socket->close();
    }
}

void DigitrafficMqttClient::setModel(TrainListModel *model)
{
    if (m_model == model)
        return;
    m_model = model;
    emit modelChanged();
}

void DigitrafficMqttClient::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    emit activeChanged();
    if (m_active)
        openConnection();
    else
        closeConnection();
}

void DigitrafficMqttClient::openConnection()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        return;

    setStatus(QStringLiteral("Connecting…"));
    QNetworkRequest request{QUrl(QString::fromLatin1(kBrokerUrl))};
    request.setRawHeader("Origin", "https://www.digitraffic.fi");
    // Identify on the handshake too: the REST calls do, and a broker connection
    // that identifies differently is still the same client to Digitraffic.
    request.setRawHeader("Digitraffic-User", digitraffic::kUserAgent);

    // MQTT-over-WebSocket requires the "mqtt" subprotocol in the handshake.
    QWebSocketHandshakeOptions options;
    options.setSubprotocols({QStringLiteral("mqtt")});
    m_socket->open(request, options);
}

void DigitrafficMqttClient::subscribeTrain(const QString &departureDate, int trainNumber)
{
    // In a topic filter `+` and `#` are wildcards and there is no escaping —
    // a `+` date would subscribe to every run of this train number (W8).
    if (!digitraffic::isDepartureDate(departureDate))
        return;
    // "#" matches the train-specific topic regardless of its category/operator tail.
    const QString topic = QStringLiteral("trains/%1/%2/#").arg(departureDate).arg(trainNumber);
    if (topic == m_trainTopic)
        return;

    unsubscribeTrain();        // drop the previous selection, if any
    m_trainTopic = topic;
    sendTrainSubscription();
}

void DigitrafficMqttClient::unsubscribeTrain()
{
    if (m_trainTopic.isEmpty())
        return;
    if (m_connected)
        send(mqttwire::buildUnsubscribe(nextPacketId(), m_trainTopic));
    m_trainTopic.clear();
}

void DigitrafficMqttClient::sendTrainSubscription()
{
    if (!m_trainTopic.isEmpty() && m_connected)
        send(mqttwire::buildSubscribe(nextPacketId(), m_trainTopic, 0));
}

void DigitrafficMqttClient::closeConnection()
{
    m_pingTimer.stop();
    m_reconnectTimer.stop();
    m_rxBuffer.clear();
    if (m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->sendBinaryMessage(mqttwire::buildDisconnect());
    m_socket->close();
    setConnected(false);
    setStatus(QStringLiteral("Disconnected"));
}

void DigitrafficMqttClient::onSocketConnected()
{
    // WebSocket is up; begin the MQTT session with a unique client id.
    m_rxBuffer.clear();
    const QString clientId = QStringLiteral("TrainsOnMap-%1")
                                 .arg(QRandomGenerator::global()->generate(), 8, 16, QChar('0'));
    send(mqttwire::buildConnect(clientId, kKeepAliveSecs));
    setStatus(QStringLiteral("Authenticating…"));
}

void DigitrafficMqttClient::onSocketDisconnected()
{
    m_pingTimer.stop();
    setConnected(false);
    if (m_active) {
        setStatus(QStringLiteral("Reconnecting…"));
        m_reconnectTimer.start();
    }
}

void DigitrafficMqttClient::onBinaryMessage(const QByteArray &message)
{
    // WebSocket frame boundaries are independent of MQTT packet boundaries, so
    // accumulate and parse as many complete packets as are available.
    m_rxBuffer += message;

    while (m_rxBuffer.size() >= 2) {
        int remaining = 0;
        int lengthBytes = 0;
        if (!mqttwire::decodeRemainingLength(m_rxBuffer, 1, remaining, lengthBytes))
            break; // length field not fully arrived yet

        const int total = 1 + lengthBytes + remaining;
        if (total > kMaxPacketBytes) {
            // Implausibly large frame: the stream is corrupt or out of sync.
            // Drop everything and let the socket reconnect from a clean MQTT
            // session instead of buffering toward the protocol ceiling.
            setStatus(QStringLiteral("Dropping malformed MQTT stream; reconnecting…"));
            m_rxBuffer.clear();
            m_socket->close();   // → onSocketDisconnected → reconnect while active
            return;
        }
        if (m_rxBuffer.size() < total)
            break; // packet body not fully arrived yet

        const quint8 first = static_cast<quint8>(m_rxBuffer.at(0));
        const QByteArray body = m_rxBuffer.mid(1 + lengthBytes, remaining);
        dispatchPacket((first >> 4) & 0x0F, first & 0x0F, body);
        m_rxBuffer.remove(0, total);
    }
}

void DigitrafficMqttClient::dispatchPacket(quint8 type, quint8 flags, const QByteArray &body)
{
    switch (type) {
    case mqttwire::ConnAck:
        if (body.size() >= 2 && body.at(1) == 0) {
            setConnected(true);
            m_pingTimer.start();
            send(mqttwire::buildSubscribe(nextPacketId(), QString::fromLatin1(kLocationsTopic), 0));
            sendTrainSubscription();   // restore a selected-train sub across reconnects
            setStatus(QStringLiteral("Subscribing…"));
        } else {
            setStatus(QStringLiteral("Broker refused connection"));
            m_socket->close();
        }
        break;
    case mqttwire::SubAck:
        setStatus(QStringLiteral("Live (MQTT)"));
        break;
    case mqttwire::Publish: {
        const mqttwire::PublishMessage msg = mqttwire::parsePublish(body, flags);
        if (msg.topic.startsWith(QLatin1String("train-locations/")))
            handleLocationPayload(msg.payload);
        else if (msg.topic.startsWith(QLatin1String("trains/")))
            emit trainMessage(msg.payload);
        break;
    }
    case mqttwire::PingResp:
    default:
        break;
    }
}

void DigitrafficMqttClient::handleLocationPayload(const QByteArray &payload)
{
    if (!m_model || payload.isEmpty())
        return;
    const auto train = digitraffic::parseObject(payload, "train-locations stream");
    if (!train)
        return;
    const TrainPosition tp = parseTrainLocation(*train);
    if (tp.coordinate.isValid())
        m_model->upsertTrain(tp);
}

void DigitrafficMqttClient::send(const QByteArray &packet)
{
    if (m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->sendBinaryMessage(packet);
}

void DigitrafficMqttClient::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    emit connectedChanged();
}

void DigitrafficMqttClient::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}
