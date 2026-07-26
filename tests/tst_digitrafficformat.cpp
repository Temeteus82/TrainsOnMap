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
};

QTEST_MAIN(tst_DigitrafficFormat)
#include "tst_digitrafficformat.moc"
