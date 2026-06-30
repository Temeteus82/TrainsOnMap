# MqttCodec.h (`mqttwire` namespace)

## A. Overview

`MqttCodec.h` is a header-only, inline-only collection of MQTT 3.1.1 wire-format
helpers in the `mqttwire` namespace. It implements *just enough* of the protocol
to drive a single subscription session over a transport — CONNECT, SUBSCRIBE,
UNSUBSCRIBE, PINGREQ, DISCONNECT on the encode side, and "remaining length" /
PUBLISH parsing on the decode side. It is deliberately **not** a general-purpose
MQTT client: there is no QoS-1/2 state machine, no session persistence, and no
retained-message handling.

The point of the file is to let `DigitrafficMqttClient` speak MQTT over a Qt
`QWebSocket` without pulling in an external broker library, so the app builds
against the LGPL Qt WebSockets module alone. Reference: *MQTT Version 3.1.1,
OASIS Standard*.

A developer reaches for this header only from the MQTT transport layer; nothing
else in the app needs it.

## B. Namespaces

| Namespace | Groups |
|-----------|--------|
| `mqttwire` | The packet-type enum, the byte-level encoders, and the PUBLISH/remaining-length decoders. |

## C. Types and Type Aliases

| Name | Kind | Description |
|------|------|-------------|
| `PacketType` | `enum : quint8` | MQTT control-packet type codes (the high nibble of a fixed header's first byte). |
| `PublishMessage` | `struct` | A decoded PUBLISH: its application `topic` (`QString`) and `payload` (`QByteArray`). |

`PacketType` values:

| Value | Integer | Description |
|-------|---------|-------------|
| `Connect` | 1 | CONNECT — session open (client → broker). |
| `ConnAck` | 2 | CONNACK — connection acknowledgement (broker → client). |
| `Publish` | 3 | PUBLISH — an application message. |
| `SubAck` | 9 | SUBACK — subscription acknowledgement. |
| `PingResp` | 13 | PINGRESP — keep-alive response. |

`PublishMessage` members:

| Member | Type | Description |
|--------|------|-------------|
| `topic` | `QString` | The topic the message was published on (UTF-8 decoded). |
| `payload` | `QByteArray` | The raw application message bytes (here, train JSON). |

## D. Constants

This file declares no standalone constants; the protocol constants (protocol
name "MQTT", level `0x04`, the fixed-header type bytes such as `0x10`, `0x82`,
`0xA2`, `0xC0`, `0xE0`) are written inline at their point of use in the builder
functions.

## E. Functions

#### QByteArray encodeRemainingLength(int length)

Encodes an MQTT "Remaining Length" field: a variable-length integer of 1–4 bytes,
7 data bits each, with the high bit set on all but the last byte to signal
continuation. Used by `makePacket`.

#### bool decodeRemainingLength(const QByteArray &buf, int offset, int &value, int &bytesUsed)

Decodes a "Remaining Length" starting at `offset` in `buf`. Returns `false` when
more bytes are still needed (so the caller can wait for the next frame);
otherwise returns `true` and sets `value` (the decoded length) and `bytesUsed`
(how many bytes the field occupied). Reads at most 4 bytes. This is the function
that lets `DigitrafficMqttClient::onBinaryMessage` re-frame a byte stream whose
WebSocket boundaries don't line up with MQTT packet boundaries.

#### QByteArray encodeString(const QString &s)

Encodes a UTF-8 string with the MQTT 2-byte big-endian length prefix. Used for
the protocol name, client id, and topic filters.

#### QByteArray makePacket(quint8 firstByte, const QByteArray &body)

Assembles a complete packet: the fixed-header first byte, the encoded remaining
length of `body`, then `body`. The lower-level builders below all funnel through
this.

#### QByteArray buildConnect(const QString &clientId, quint16 keepAliveSecs)

Builds a CONNECT packet for a clean session with no credentials (the Digitraffic
rail broker needs none), advertising protocol level 4 (MQTT 3.1.1) and the given
keep-alive interval.

#### QByteArray buildSubscribe(quint16 packetId, const QString &topicFilter, quint8 qos)

Builds a SUBSCRIBE to a single topic filter at the given QoS, tagged with
`packetId`. Emits the mandatory `0b0010` header flags.

#### QByteArray buildUnsubscribe(quint16 packetId, const QString &topicFilter)

Builds an UNSUBSCRIBE from a single topic filter, tagged with `packetId`. Emits
the mandatory `0b0010` header flags.

#### QByteArray buildPingReq()

Builds a PINGREQ keep-alive packet (`0xC0 0x00`).

#### QByteArray buildDisconnect()

Builds a DISCONNECT packet (`0xE0 0x00`), sent before closing the socket on a
clean teardown.

#### PublishMessage parsePublish(const QByteArray &body, quint8 flags)

Extracts the topic and payload from a PUBLISH packet **body** (everything after
the fixed header's remaining-length field). `flags` is the low nibble of the
packet's first byte; its QoS bits decide whether a 2-byte packet identifier
precedes the payload (skipped when QoS > 0). Returns an empty `PublishMessage`
(empty topic/payload) on a truncated body rather than reading out of bounds.

## F. Dependencies

| Include | Provides |
|---------|----------|
| `<QByteArray>` | The byte buffer type used for every packet and for parsing. |
| `<QString>` | Topic/client-id string handling and UTF-8 conversion. |

Build requirement: **Qt6::Core** only. The header has no networking or transport
dependency of its own — the caller supplies the transport.

## G. Usage Example

```cpp
#include "MqttCodec.h"

// Open an MQTT session over an already-connected binary transport.
sendBinary(mqttwire::buildConnect(QStringLiteral("my-client-7f3a"), 60));

// After CONNACK, subscribe to a wildcard topic.
quint16 packetId = 1;
sendBinary(mqttwire::buildSubscribe(packetId++, QStringLiteral("train-locations/#"), 0));

// Re-frame an incoming byte stream and dispatch complete packets.
buffer += incomingBytes;
while (buffer.size() >= 2) {
    int remaining = 0, lengthBytes = 0;
    if (!mqttwire::decodeRemainingLength(buffer, 1, remaining, lengthBytes))
        break;                                   // length field incomplete
    const int total = 1 + lengthBytes + remaining;
    if (buffer.size() < total)
        break;                                   // body incomplete
    const quint8 first = static_cast<quint8>(buffer.at(0));
    const QByteArray body = buffer.mid(1 + lengthBytes, remaining);
    if (((first >> 4) & 0x0F) == mqttwire::Publish) {
        const mqttwire::PublishMessage msg = mqttwire::parsePublish(body, first & 0x0F);
        handle(msg.topic, msg.payload);
    }
    buffer.remove(0, total);
}
```
