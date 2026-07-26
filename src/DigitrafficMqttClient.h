#pragma once

#include <QByteArray>
#include <QObject>
#include <QTimer>
#include <QtQmlIntegration>

#include "TrainListModel.h"

class QWebSocket;

/// Streams live train positions from the Digitraffic MQTT broker over a
/// WebSocket and upserts them into a TrainListModel.
///
/// Broker (https://www.digitraffic.fi/rautatieliikenne/):
///   wss://rata.digitraffic.fi:443/mqtt   (subprotocol "mqtt", no credentials)
///   topic: train-locations/<departureDate>/<trainNumber>   (subscribe with #)
///
/// Implements just enough MQTT 3.1.1 (see MqttCodec.h) — no external broker
/// library, so it builds against the LGPL Qt WebSockets module alone.
class DigitrafficMqttClient : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TrainListModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit DigitrafficMqttClient(QObject *parent = nullptr);
    ~DigitrafficMqttClient() override;

    TrainListModel *model() const { return m_model; }
    void setModel(TrainListModel *model);

    bool isActive() const { return m_active; }
    void setActive(bool active);

    bool isConnected() const { return m_connected; }
    QString status() const { return m_status; }

public slots:
    /// Subscribe to live updates for one train (topic trains/<date>/<number>/#),
    /// replacing any previous per-train subscription. Used by the detail panel.
    void subscribeTrain(const QString &departureDate, int trainNumber);
    /// Drop the current per-train subscription.
    void unsubscribeTrain();

signals:
    void modelChanged();
    void activeChanged();
    void connectedChanged();
    void statusChanged();

    /// Emitted for each PUBLISH on a trains/# topic (full running-train JSON).
    void trainMessage(const QByteArray &payload);

private:
    void openConnection();
    void closeConnection();
    void onSocketConnected();
    void onSocketDisconnected();
    void onBinaryMessage(const QByteArray &message);
    void dispatchPacket(quint8 type, quint8 flags, const QByteArray &body);
    void handleLocationPayload(const QByteArray &payload);
    void sendTrainSubscription();   ///< (re)subscribe to m_trainTopic if set & connected
    void send(const QByteArray &packet);
    /// Next MQTT packet identifier. 0 is not a valid id in MQTT 3.1.1, so wrap
    /// back to 1 instead of through 0 after 65535 SUBSCRIBE/UNSUBSCRIBEs.
    quint16 nextPacketId()
    {
        const quint16 id = m_packetId++;
        if (m_packetId == 0)
            m_packetId = 1;
        return id;
    }
    void setConnected(bool connected);
    void setStatus(const QString &status);

    QWebSocket *m_socket = nullptr;
    TrainListModel *m_model = nullptr;
    QTimer m_pingTimer;        ///< MQTT keep-alive
    QTimer m_reconnectTimer;   ///< retry after a drop while active
    QByteArray m_rxBuffer;     ///< MQTT packets may span / share WebSocket frames
    QString m_trainTopic;      ///< current per-train subscription, e.g. trains/<date>/<n>/#
    quint16 m_packetId = 1;
    bool m_active = false;
    bool m_connected = false;
    QString m_status;
};
