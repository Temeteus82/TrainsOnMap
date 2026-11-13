#include "MqttCodec.h"

#include <QTest>

/// Pins the wire helpers the MQTT client frames its stream with (CPP2-I10):
/// the remaining-length varint at its edges, including the malformed
/// five-byte form, and parsePublish against short and oversized topic lengths.
class TestMqttCodec : public QObject
{
    Q_OBJECT

private:
    static QByteArray bytes(std::initializer_list<int> values)
    {
        QByteArray out;
        for (int v : values)
            out.append(static_cast<char>(v));
        return out;
    }

private slots:
    void remainingLengthRoundTrips_data()
    {
        QTest::addColumn<int>("length");
        QTest::addColumn<int>("encodedBytes");
        QTest::newRow("0") << 0 << 1;
        QTest::newRow("127") << 127 << 1;
        QTest::newRow("128") << 128 << 2;
        QTest::newRow("16383") << 16383 << 2;
        QTest::newRow("16384") << 16384 << 3;
        QTest::newRow("2097151") << 2097151 << 3;
        QTest::newRow("2097152") << 2097152 << 4;
        QTest::newRow("max") << 268435455 << 4;
    }

    void remainingLengthRoundTrips()
    {
        QFETCH(int, length);
        QFETCH(int, encodedBytes);

        const QByteArray encoded = mqttwire::encodeRemainingLength(length);
        QCOMPARE(encoded.size(), encodedBytes);

        // Decoded from an offset, as the client does past the fixed header byte.
        int value = 0;
        int used = 0;
        QVERIFY(mqttwire::decodeRemainingLength(QByteArray(1, '\x30') + encoded, 1, value, used));
        QCOMPARE(value, length);
        QCOMPARE(used, encodedBytes);
    }

    void remainingLengthWaitsForMoreBytes()
    {
        int value = 0;
        int used = 0;
        QVERIFY(!mqttwire::decodeRemainingLength(QByteArray(), 0, value, used));
        // Continuation bit set on the last byte present: the field isn't complete.
        QVERIFY(!mqttwire::decodeRemainingLength(bytes({0x80}), 0, value, used));
        QVERIFY(!mqttwire::decodeRemainingLength(bytes({0xFF, 0xFF, 0xFF}), 0, value, used));
    }

    /// Four bytes that all ask for a fifth. This used to decode as a valid
    /// length (0 for 80 80 80 80), so the next byte was read as a packet.
    void remainingLengthRejectsFifthByte()
    {
        int value = 0;
        int used = 0;
        QVERIFY(mqttwire::decodeRemainingLength(bytes({0x80, 0x80, 0x80, 0x80, 0x01}), 0,
                                                value, used));
        QCOMPARE(value, -1);
        QVERIFY(mqttwire::decodeRemainingLength(bytes({0xFF, 0xFF, 0xFF, 0xFF}), 0, value, used));
        QCOMPARE(value, -1);
    }

    void parsesPublish()
    {
        const QByteArray body = mqttwire::encodeString(QStringLiteral("trains/x")) + "payload";
        const mqttwire::PublishMessage msg = mqttwire::parsePublish(body, 0);
        QCOMPARE(msg.topic, QStringLiteral("trains/x"));
        QCOMPARE(msg.payload, QByteArray("payload"));
    }

    void parsePublishSkipsPacketIdAtQos1()
    {
        const QByteArray body = mqttwire::encodeString(QStringLiteral("t")) + bytes({0x00, 0x07})
                                + "p";
        const mqttwire::PublishMessage msg = mqttwire::parsePublish(body, 0x02);
        QCOMPARE(msg.topic, QStringLiteral("t"));
        QCOMPARE(msg.payload, QByteArray("p"));

        // QoS 1 with the packet id cut off: a topic, no payload, no over-read.
        const mqttwire::PublishMessage cut =
            mqttwire::parsePublish(mqttwire::encodeString(QStringLiteral("t")) + bytes({0x00}),
                                   0x02);
        QCOMPARE(cut.topic, QStringLiteral("t"));
        QVERIFY(cut.payload.isEmpty());
    }

    void parsePublishRejectsBadTopicLength()
    {
        // Too short to hold the length prefix at all.
        QVERIFY(mqttwire::parsePublish(bytes({0x00}), 0).topic.isEmpty());
        // Claims a 16-byte topic in a 4-byte body.
        const mqttwire::PublishMessage msg = mqttwire::parsePublish(bytes({0x00, 0x10, 'a', 'b'}), 0);
        QVERIFY(msg.topic.isEmpty());
        QVERIFY(msg.payload.isEmpty());
    }

    void subscribePacketLayout()
    {
        const QByteArray pkt = mqttwire::buildSubscribe(0x0102, QStringLiteral("a/#"), 0);
        QCOMPARE(pkt, bytes({0x82, 0x08, 0x01, 0x02, 0x00, 0x03, 'a', '/', '#', 0x00}));
    }
};

QTEST_APPLESS_MAIN(TestMqttCodec)
#include "tst_mqttcodec.moc"
