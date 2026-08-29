// Pins the presentation helpers shared by StationBoardService and
// TrainDetailsService, which were previously a private copy in each .cpp.
// hhmmAcceptsBothIsoForms() is the load-bearing one: Digitraffic omits
// fractional seconds on some fields, and the single Qt::ISODateWithMs parse
// only covers those because Qt treats the fractional part as optional when
// parsing. If that ever changes, the panels would silently blank those times —
// this catches it at build time instead.
#include "DigitrafficFormat.h"

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QRegularExpression>
#include <QTest>
#include <QTime>
#include <QTimeZone>

class tst_DigitrafficFormat : public QObject
{
    Q_OBJECT

private slots:

    void hhmmAcceptsBothIsoForms()
    {
        // Same instant, with and without fractional seconds — Digitraffic emits
        // both shapes and the panels must render them identically. See the file
        // comment: this is the assumption parseIso()'s single call rests on.
        const QString withMs = digitraffic::hhmm(QStringLiteral("2026-07-26T09:12:00.000Z"));
        const QString plain = digitraffic::hhmm(QStringLiteral("2026-07-26T09:12:00Z"));
        QVERIFY(!withMs.isEmpty());
        QCOMPARE(plain, withMs);
    }

    void hhmmRendersLocalTime()
    {
        // Build the instant directly rather than through the helper's own
        // parsing, so the expectation is independent of the code under test
        // (and of the machine's time zone).
        const QDateTime utc{QDate(2026, 7, 26), QTime(9, 12), QTimeZone::UTC};
        const QString expected = utc.toLocalTime().toString(QStringLiteral("HH:mm"));
        QCOMPARE(digitraffic::hhmm(QStringLiteral("2026-07-26T09:12:00.000Z")), expected);
    }

    void hhmmIsEmptyForMissingOrJunk()
    {
        // A missing time is normal (no estimate yet), not an error.
        QVERIFY(digitraffic::hhmm(QString()).isEmpty());
        QVERIFY(digitraffic::hhmm(QStringLiteral("not a timestamp")).isEmpty());
    }

    void parseIsoReportsInvalidInput()
    {
        QVERIFY(digitraffic::parseIso(QStringLiteral("2026-07-26T09:12:00Z")).isValid());
        QVERIFY(!digitraffic::parseIso(QStringLiteral("nonsense")).isValid());
    }

    void causeTextCombinesCategoryAndDetail()
    {
        const QHash<QString, QString> categories{
            {QStringLiteral("A"), QStringLiteral("Onnettomuus")}};
        const QHash<QString, QString> details{
            {QStringLiteral("A1"), QStringLiteral("Tasoristeysonnettomuus")}};

        QCOMPARE(digitraffic::causeText(QStringLiteral("A"), QStringLiteral("A1"),
                                        categories, details),
                 QStringLiteral("Onnettomuus: Tasoristeysonnettomuus"));

        // No detailed code, or one the metadata doesn't know: category alone.
        QCOMPARE(digitraffic::causeText(QStringLiteral("A"), QString(), categories, details),
                 QStringLiteral("Onnettomuus"));
        QCOMPARE(digitraffic::causeText(QStringLiteral("A"), QStringLiteral("ZZ"),
                                        categories, details),
                 QStringLiteral("Onnettomuus"));
    }

    void causeTextIsEmptyWithoutACause()
    {
        const QHash<QString, QString> categories{
            {QStringLiteral("A"), QStringLiteral("Onnettomuus")}};
        // No cause code at all — the row is on time.
        QVERIFY(digitraffic::causeText(QString(), QStringLiteral("A1"), categories, {}).isEmpty());
        // Cause present but the metadata hasn't loaded yet; the services rebuild
        // once it lands, so an empty string here is expected, not a hole.
        QVERIFY(digitraffic::causeText(QStringLiteral("A"), QString(), {}, {}).isEmpty());
    }

    /// An empty array is a successful parse, not a failure — the delta endpoints
    /// use it for "nothing changed", and conflating the two would make a full
    /// snapshot skip its reset. A failure must both report nothing and say why.
    void parseArraySeparatesEmptyFromBroken()
    {
        const auto ok = digitraffic::parseArray("[{\"a\":1}]", "test");
        QVERIFY(ok.has_value());
        QCOMPARE(ok->size(), 1);

        const auto empty = digitraffic::parseArray("[]", "test");
        QVERIFY(empty.has_value());
        QVERIFY(empty->isEmpty());

        // Both failure modes name the endpoint (CPP-W13): four handlers used to
        // return with no status change and no log line at all.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("^test: JSON parse failed")));
        QVERIFY(!digitraffic::parseArray("[{", "test").has_value());

        QTest::ignoreMessage(QtWarningMsg, "test: expected a JSON array root");
        QVERIFY(!digitraffic::parseArray("{\"a\":1}", "test").has_value());
    }

    /// parseObject takes the bare object and the one-element array wrapping it —
    /// two callers spelled that tolerance out by hand before it moved here.
    void parseObjectAcceptsEitherShape()
    {
        const auto bare = digitraffic::parseObject("{\"trainNumber\":5}", "test");
        QVERIFY(bare.has_value());
        QCOMPARE(bare->value(QStringLiteral("trainNumber")).toInt(), 5);

        const auto wrapped = digitraffic::parseObject("[{\"trainNumber\":5}]", "test");
        QVERIFY(wrapped.has_value());
        QCOMPARE(wrapped->value(QStringLiteral("trainNumber")).toInt(), 5);

        QTest::ignoreMessage(QtWarningMsg, "test: expected a JSON object root");
        QVERIFY(!digitraffic::parseObject("[]", "test").has_value());
    }

    /// The whole point of centralising this: three private copies disagreed on
    /// what a *present but blank* name means. A code beats a blank (CPP-W5).
    void stationLabelPrefersACodeOverABlankName()
    {
        const QHash<QString, QString> names{
            {QStringLiteral("HKI"), QStringLiteral("Helsinki")},
            {QStringLiteral("XXX"), QString()}};   // present, blank — the drift case

        QCOMPARE(digitraffic::stationLabel(names, QStringLiteral("HKI")),
                 QStringLiteral("Helsinki"));
        QCOMPARE(digitraffic::stationLabel(names, QStringLiteral("XXX")),
                 QStringLiteral("XXX"));
        QCOMPARE(digitraffic::stationLabel(names, QStringLiteral("TKU")),
                 QStringLiteral("TKU"));   // absent

        // The caller's own name (e.g. what came with the click) beats the code,
        // but never beats a known name.
        QCOMPARE(digitraffic::stationLabel(names, QStringLiteral("TKU"), QStringLiteral("Turku")),
                 QStringLiteral("Turku"));
        QCOMPARE(digitraffic::stationLabel(names, QStringLiteral("HKI"), QStringLiteral("Turku")),
                 QStringLiteral("Helsinki"));
    }

    void inFinlandBoxRejectsTheParseFailureSentinel()
    {
        QVERIFY(digitraffic::inFinlandBox(60.17, 24.94));    // Helsinki
        QVERIFY(!digitraffic::inFinlandBox(0.0, 0.0));       // toDouble() failure value
        QVERIFY(!digitraffic::inFinlandBox(59.33, 18.07));   // Stockholm — outside
    }

    void stationShortCodeRejectsStructuralCharacters()
    {
        QVERIFY(digitraffic::isStationShortCode(QStringLiteral("HKI")));
        QVERIFY(digitraffic::isStationShortCode(QStringLiteral("ÄS")));   // codes carry Ä/Ö/Å
        QVERIFY(!digitraffic::isStationShortCode(QString()));
        QVERIFY(!digitraffic::isStationShortCode(QStringLiteral("HKI/..")));
        QVERIFY(!digitraffic::isStationShortCode(QStringLiteral("HKI?x=1")));
        QVERIFY(!digitraffic::isStationShortCode(QStringLiteral("AVERYLONGCODE")));
    }

    void departureDateIsStrictIsoOnly()
    {
        QVERIFY(digitraffic::isDepartureDate(QStringLiteral("2026-08-29")));
        QVERIFY(!digitraffic::isDepartureDate(QString()));
        QVERIFY(!digitraffic::isDepartureDate(QStringLiteral("+")));           // MQTT wildcard
        QVERIFY(!digitraffic::isDepartureDate(QStringLiteral("#")));           // MQTT wildcard
        QVERIFY(!digitraffic::isDepartureDate(QStringLiteral("2026-13-01")));  // no such month
        QVERIFY(!digitraffic::isDepartureDate(QStringLiteral("2026-8-9")));    // not zero-padded
    }
};

QTEST_MAIN(tst_DigitrafficFormat)
#include "tst_digitrafficformat.moc"
