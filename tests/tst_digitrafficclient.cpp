#include "DigitrafficClient.h"

#include "FakeNetwork.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

/// Pins CPP-W2: the three /metadata fetches issued from the constructor are
/// re-issued from the 60 s resync until they land, with a doubling backoff.
///
/// Untestable before the manager became a constructor dependency — the fetches
/// fire before any setter could run, so there was no moment at which a test
/// could interpose. It is one now (see FakeNetwork.h).
///
/// Without the retry, a client that starts before the network is up shows
/// station codes instead of names for the rest of the session, with no way back
/// short of a restart.
class TestDigitrafficClient : public QObject
{
    Q_OBJECT

private:
    static constexpr auto kStations = "metadata/stations";
    static constexpr auto kCauses = "metadata/cause-category-codes";
    static constexpr auto kDetailed = "metadata/detailed-cause-category-codes";

    /// `kDetailed` contains `kCauses` as a substring, so a plain contains() match
    /// on the cause endpoint would also take the detailed one. Anchor it.
    static QString causesNeedle() { return QStringLiteral("/metadata/cause-category-codes"); }

    static void failAll(fake::Manager &net, const QString &needle)
    {
        while (fake::Reply *r = net.take(needle))
            r->fail();
    }

    /// Enough of a /metadata/stations body to mark the fetch loaded.
    static QByteArray stationsPayload()
    {
        const QJsonObject hki{{"stationShortCode", "HKI"},
                              {"stationName", "Helsinki"},
                              {"latitude", 60.1719},
                              {"longitude", 24.9414},
                              {"passengerTraffic", true}};
        return QJsonDocument(QJsonArray{hki}).toJson(QJsonDocument::Compact);
    }

private slots:
    /// The constructor issues all three, and a failed one is re-issued on the
    /// next refresh — while an endpoint that already landed is not re-fetched.
    void failedMetadataIsRetriedUntilItLands()
    {
        fake::Manager net;
        DigitrafficClient client(nullptr, &net);

        QCOMPARE(net.pending(QString::fromLatin1(kStations)), 1);
        QCOMPARE(net.pending(causesNeedle()), 1);
        QCOMPARE(net.pending(QString::fromLatin1(kDetailed)), 1);

        // The network is down at startup: all three fail.
        failAll(net, QString::fromLatin1(kStations));
        failAll(net, causesNeedle());
        failAll(net, QString::fromLatin1(kDetailed));
        QVERIFY(client.stationNames().isEmpty());

        // First resync: all three are re-issued.
        client.refresh();
        QCOMPARE(net.pending(QString::fromLatin1(kStations)), 1);
        QCOMPARE(net.pending(causesNeedle()), 1);
        QCOMPARE(net.pending(QString::fromLatin1(kDetailed)), 1);

        // Stations come back this time; the other two fail again.
        fake::Reply *stations = net.take(QString::fromLatin1(kStations));
        QVERIFY(stations);
        stations->respond(stationsPayload());
        QCOMPARE(client.stationNames().value(QStringLiteral("HKI")),
                 QStringLiteral("Helsinki"));

        failAll(net, causesNeedle());
        failAll(net, QString::fromLatin1(kDetailed));
        net.forget();

        // The next retry cycle re-issues only what is still missing — stations
        // must not be fetched a second time. (The first of these two refreshes
        // is swallowed by the backoff, which retryBacksOffAfterRepeatedFailures
        // pins on its own; here it just has to arrive.)
        client.refresh();
        client.refresh();
        QCOMPARE(net.pending(QString::fromLatin1(kStations)), 0);
        QCOMPARE(net.pending(causesNeedle()), 1);
        QCOMPARE(net.pending(QString::fromLatin1(kDetailed)), 1);
    }

    /// The retry thins out rather than hammering a dead network: it attempts on
    /// the next cycle, then every 2nd, then every 4th — so the runs of skipped
    /// cycles between attempts are 1, then 3, then 7 (`skips = backoff - 1`).
    ///
    /// Every outstanding reply is failed before each cycle here on purpose —
    /// leaving one in flight would let the "nothing to re-issue" guard suppress
    /// the request instead, and the test would pass with no backoff at all.
    void retryBacksOffAfterRepeatedFailures()
    {
        fake::Manager net;
        DigitrafficClient client(nullptr, &net);

        auto failEverything = [&net] {
            failAll(net, QString::fromLatin1(kStations));
            failAll(net, causesNeedle());
            failAll(net, QString::fromLatin1(kDetailed));
            net.forget();
        };
        auto issuedCount = [&net] {
            return net.pending(QString::fromLatin1(kStations))
                   + net.pending(causesNeedle())
                   + net.pending(QString::fromLatin1(kDetailed));
        };

        failEverything();   // the three the constructor issued

        client.refresh();          // first retry: attempted immediately
        QCOMPARE(issuedCount(), 3);
        failEverything();

        client.refresh();          // then one cycle is skipped
        QCOMPARE(issuedCount(), 0);
        client.refresh();
        QCOMPARE(issuedCount(), 3);
        failEverything();

        for (int i = 0; i < 3; ++i) {   // then three
            client.refresh();
            QCOMPARE(issuedCount(), 0);
        }
        client.refresh();
        QCOMPARE(issuedCount(), 3);
        failEverything();

        for (int i = 0; i < 7; ++i) {   // then seven — the cap
            client.refresh();
            QCOMPARE(issuedCount(), 0);
        }
        client.refresh();
        QCOMPARE(issuedCount(), 3);
    }

    /// A retry is not issued while the previous attempt is still outstanding —
    /// otherwise `active: true` calling refresh() in the same breath as the
    /// constructor would double every startup request.
    void inFlightMetadataIsNotReissued()
    {
        fake::Manager net;
        DigitrafficClient client(nullptr, &net);

        QCOMPARE(net.pending(QString::fromLatin1(kStations)), 1);

        client.refresh();   // what `active: true` does immediately after construction

        QCOMPARE(net.pending(QString::fromLatin1(kStations)), 1);
        QCOMPARE(net.pending(causesNeedle()), 1);
        QCOMPARE(net.pending(QString::fromLatin1(kDetailed)), 1);
    }

    /// Once all three have landed, the resync stops asking for them entirely.
    void loadedMetadataIsNeverRefetched()
    {
        fake::Manager net;
        DigitrafficClient client(nullptr, &net);

        fake::Reply *stations = net.take(QString::fromLatin1(kStations));
        QVERIFY(stations);
        stations->respond(stationsPayload());

        for (const QString &needle : {causesNeedle(), QString::fromLatin1(kDetailed)}) {
            fake::Reply *r = net.take(needle);
            QVERIFY(r);
            r->respond(QByteArrayLiteral("[]"));
        }
        net.forget();

        client.refresh();
        client.refresh();
        QCOMPARE(net.pending(QString::fromLatin1(kStations)), 0);
        QCOMPARE(net.pending(causesNeedle()), 0);
        QCOMPARE(net.pending(QString::fromLatin1(kDetailed)), 0);
    }
};

QTEST_MAIN(TestDigitrafficClient)
#include "tst_digitrafficclient.moc"
