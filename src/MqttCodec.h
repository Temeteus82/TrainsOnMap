#pragma once

#include <QByteArray>
#include <QString>

/// Minimal MQTT 3.1.1 wire helpers — just enough to CONNECT, SUBSCRIBE and read
/// PUBLISH packets over a transport (here, a WebSocket). Not a general client.
///
/// Reference: MQTT Version 3.1.1, OASIS Standard.
namespace mqttwire {

enum PacketType : quint8 {
    Connect = 1,
    ConnAck = 2,
    Publish = 3,
    SubAck = 9,
    PingResp = 13,
};

/// Encode a "Remaining Length" field (1–4 bytes, 7 bits each, MSB = continue).
inline QByteArray encodeRemainingLength(int length)
{
    QByteArray out;
    do {
        quint8 byte = length % 128;
        length /= 128;
        if (length > 0)
            byte |= 0x80;
        out.append(static_cast<char>(byte));
    } while (length > 0);
    return out;
}

/// Decode a "Remaining Length" starting at `offset`.
/// Returns false if more bytes are needed; otherwise sets value/bytesUsed.
inline bool decodeRemainingLength(const QByteArray &buf, int offset, int &value, int &bytesUsed)
{
    value = 0;
    bytesUsed = 0;
    int multiplier = 1;
    for (int i = 0; i < 4; ++i) {
        if (offset + i >= buf.size())
            return false; // need more data
        const quint8 b = static_cast<quint8>(buf.at(offset + i));
        value += (b & 0x7F) * multiplier;
        ++bytesUsed;
        if ((b & 0x80) == 0)
            return true;
        multiplier *= 128;
    }
    return true; // 4-byte maximum reached
}

/// Encode a UTF-8 string with a 2-byte big-endian length prefix.
inline QByteArray encodeString(const QString &s)
{
    const QByteArray u = s.toUtf8();
    QByteArray out;
    out.append(static_cast<char>((u.size() >> 8) & 0xFF));
    out.append(static_cast<char>(u.size() & 0xFF));
    out.append(u);
    return out;
}

inline QByteArray makePacket(quint8 firstByte, const QByteArray &body)
{
    QByteArray pkt;
    pkt.append(static_cast<char>(firstByte));
    pkt.append(encodeRemainingLength(body.size()));
    pkt.append(body);
    return pkt;
}

/// CONNECT with a clean session and no credentials (rail broker needs none).
inline QByteArray buildConnect(const QString &clientId, quint16 keepAliveSecs)
{
    QByteArray body;
    body.append(encodeString(QStringLiteral("MQTT"))); // protocol name
    body.append(static_cast<char>(0x04));              // protocol level 4 (3.1.1)
    body.append(static_cast<char>(0x02));              // connect flags: clean session
    body.append(static_cast<char>((keepAliveSecs >> 8) & 0xFF));
    body.append(static_cast<char>(keepAliveSecs & 0xFF));
    body.append(encodeString(clientId));               // payload
    return makePacket(0x10, body);
}

/// SUBSCRIBE to a single topic filter at the given QoS.
inline QByteArray buildSubscribe(quint16 packetId, const QString &topicFilter, quint8 qos)
{
    QByteArray body;
    body.append(static_cast<char>((packetId >> 8) & 0xFF));
    body.append(static_cast<char>(packetId & 0xFF));
    body.append(encodeString(topicFilter));
    body.append(static_cast<char>(qos));
    return makePacket(0x82, body); // SUBSCRIBE requires flags 0b0010
}

/// UNSUBSCRIBE from a single topic filter.
inline QByteArray buildUnsubscribe(quint16 packetId, const QString &topicFilter)
{
    QByteArray body;
    body.append(static_cast<char>((packetId >> 8) & 0xFF));
    body.append(static_cast<char>(packetId & 0xFF));
    body.append(encodeString(topicFilter));
    return makePacket(0xA2, body); // UNSUBSCRIBE requires flags 0b0010
}

inline QByteArray buildPingReq()
{
    return QByteArray(2, '\0').replace(0, 1, "\xC0"); // 0xC0 0x00
}

inline QByteArray buildDisconnect()
{
    return QByteArray(2, '\0').replace(0, 1, "\xE0"); // 0xE0 0x00
}

/// Decoded PUBLISH payload: the application topic and message bytes.
struct PublishMessage {
    QString topic;
    QByteArray payload;
};

/// Extract topic + payload from a PUBLISH packet body (everything after the
/// fixed header's remaining-length). `flags` is the low nibble of byte 0.
inline PublishMessage parsePublish(const QByteArray &body, quint8 flags)
{
    PublishMessage msg;
    if (body.size() < 2)
        return msg;
    const int topicLen = (static_cast<quint8>(body.at(0)) << 8) | static_cast<quint8>(body.at(1));
    int idx = 2 + topicLen;
    if (idx > body.size())
        return msg;
    msg.topic = QString::fromUtf8(body.constData() + 2, topicLen);
    const quint8 qos = (flags >> 1) & 0x03;
    if (qos > 0)
        idx += 2; // skip the packet identifier
    if (idx <= body.size())
        msg.payload = body.mid(idx);
    return msg;
}

} // namespace mqttwire
