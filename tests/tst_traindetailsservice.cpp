#include "TrainDetailsService.h"

#include "DigitrafficMqttClient.h"
#include "FakeNetwork.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

/// Pins the two user-visible defects from the 2026-08-28 C++ review that had
/// shipped fixes but no test, because the service built its own
/// QNetworkAccessManager and there was no way to hand it an out-of-order reply
/// pair. It now takes one (see FakeNetwork.h).
///
/// - **CPP-C1** — a timetable reply for a train the user has already navigated
///   away from must be dropped, not applied against the current selection.
/// - **CPP-C2** — a live MQTT refresh re-applies the same header and route every
///   few seconds; it must not re-emit selectionChanged / routeStationsChanged,
///   because QML resets the breadcrumb trail on the first and rebuilds the whole
///   overlay polyline on the second.
class TestTrainDetailsService : public QObject
{
    Q_OBJECT

private:
    static constexpr auto kDate = "2026-09-03";

    /// One timing point. Two rows per station is what the real feed sends;
    /// one is enough for buildStops and keeps the fixtures readable.
    static QJsonObject row(const QString &code, const QString &time)
    {
        return QJsonObject{{"stationShortCode", code},
                           {"type", "DEPARTURE"},
                           {"scheduledTime", time},
                           {"trainStopping", true},
                           {"commercialStop", true}};
    }

    /// A /trains payload: the outer array the endpoint returns.
    static QByteArray trainsPayload(int number, const QString &type,
                                    const QStringList &route)
    {
        QJsonArray rows;
        int hour = 8;
        for (const QString &code : route)
            rows.push_back(row(code, QStringLiteral("2026-09-03T%1:00:00.000Z")
                                         .arg(hour++, 2, 10, QLatin1Char('0'))));
        const QJsonObject train{{"trainNumber", number},
                                {"departureDate", QString::fromLatin1(kDate)},
                                {"trainType", type},
                                {"trainCategory", "Long-distance"},
                                {"operatorShortCode", "vr"},
                                {"cancelled", false},
                                {"timeTableRows", rows}};
        return QJsonDocument(QJsonArray{train}).toJson(QJsonDocument::Compact);
    }

    /// The MQTT form: the same object, unwrapped.
    static QByteArray streamPayload(int number, const QString &type,
                                    const QStringList &route)
    {
        const QJsonArray outer =
            QJsonDocument::fromJson(trainsPayload(number, type, route)).array();
        return QJsonDocument(outer.first().toObject()).toJson(QJsonDocument::Compact);
    }

private slots:
    /// CPP-C1. Select A, select B before A's reply lands, then land A's. The
    /// panel must still be showing B — and must still be waiting for B, since
    /// nothing has answered B's request yet.
    void staleTimetableReplyIsDropped()
    {
        fake::Manager net;
        TrainDetailsService svc(nullptr, &net);

        svc.show(101, QString::fromLatin1(kDate));
        fake::Reply *replyA = net.take(QStringLiteral("/trains/2026-09-03/101"));
        QVERIFY(replyA);

        svc.show(202, QString::fromLatin1(kDate));
        fake::Reply *replyB = net.take(QStringLiteral("/trains/2026-09-03/202"));
        QVERIFY(replyB);

        QCOMPARE(svc.trainNumber(), 202);
        QVERIFY(svc.isLoading());

        QSignalSpy routeSpy(&svc, &TrainDetailsService::routeStationsChanged);

        // A's reply arrives late, naming a different train and a different route.
        replyA->respond(trainsPayload(101, QStringLiteral("IC"),
                                      {QStringLiteral("HKI"), QStringLiteral("TPE")}));

        QCOMPARE(svc.trainNumber(), 202);
        QCOMPARE(svc.title(), QStringLiteral("Train 202"));   // untouched placeholder
        QVERIFY(svc.routeStations().isEmpty());               // no A polyline under B
        QCOMPARE(routeSpy.count(), 0);
        // The spinner belongs to B's request, which is still in flight: a stale
        // reply lowering it would clear the panel's BusyIndicator early.
        QVERIFY(svc.isLoading());

        replyB->respond(trainsPayload(202, QStringLiteral("S"),
                                      {QStringLiteral("HKI"), QStringLiteral("PSL"),
                                       QStringLiteral("TKL")}));

        QCOMPARE(svc.title(), QStringLiteral("S 202"));
        QCOMPARE(svc.routeStations(),
                 (QStringList{QStringLiteral("HKI"), QStringLiteral("PSL"),
                              QStringLiteral("TKL")}));
        QCOMPARE(routeSpy.count(), 1);
        QVERIFY(!svc.isLoading());
    }

    /// CPP-C1, second half. clear() while a request is in flight must lower
    /// `loading` itself — the reply that used to do it is now dropped as stale.
    void clearWhileInFlightStopsTheSpinner()
    {
        fake::Manager net;
        TrainDetailsService svc(nullptr, &net);

        svc.show(101, QString::fromLatin1(kDate));
        fake::Reply *reply = net.take(QStringLiteral("/trains/2026-09-03/101"));
        QVERIFY(reply);
        QVERIFY(svc.isLoading());

        svc.clear();
        QVERIFY(!svc.isLoading());

        reply->respond(trainsPayload(101, QStringLiteral("IC"), {QStringLiteral("HKI")}));
        QVERIFY(!svc.isLoading());
        QVERIFY(!svc.hasSelection());
    }

    /// CPP-C2. A live refresh carrying the same header and route must be silent
    /// on both signals, or the trail resets and the overlay rebuilds every few
    /// seconds. A refresh that genuinely changes them must still speak up.
    void liveRefreshOnlySignalsRealChanges()
    {
        fake::Manager net;
        DigitrafficMqttClient stream;   // inert until connect() — never called here
        TrainDetailsService svc(nullptr, &net);
        svc.setStream(&stream);

        const QStringList route{QStringLiteral("HKI"), QStringLiteral("PSL"),
                                QStringLiteral("TPE")};

        svc.show(202, QString::fromLatin1(kDate));
        fake::Reply *reply = net.take(QStringLiteral("/trains/2026-09-03/202"));
        QVERIFY(reply);
        reply->respond(trainsPayload(202, QStringLiteral("IC"), route));
        QCOMPARE(svc.routeStations(), route);

        QSignalSpy selectionSpy(&svc, &TrainDetailsService::selectionChanged);
        QSignalSpy routeSpy(&svc, &TrainDetailsService::routeStationsChanged);

        // Same run, same header, same route — a plain delay refresh.
        emit stream.trainMessage(streamPayload(202, QStringLiteral("IC"), route));
        QCOMPARE(selectionSpy.count(), 0);
        QCOMPARE(routeSpy.count(), 0);
        QVERIFY(svc.status().contains(QStringLiteral("live")));   // it *was* applied

        // A changed header (cancelled, rerouted, retyped) still has to announce
        // itself — otherwise this guard would pass by never emitting at all.
        emit stream.trainMessage(streamPayload(202, QStringLiteral("PYO"), route));
        QCOMPARE(selectionSpy.count(), 1);
        QCOMPARE(routeSpy.count(), 0);

        const QStringList diverted{QStringLiteral("HKI"), QStringLiteral("PSL"),
                                   QStringLiteral("RI"), QStringLiteral("TPE")};
        emit stream.trainMessage(streamPayload(202, QStringLiteral("PYO"), diverted));
        QCOMPARE(selectionSpy.count(), 1);
        QCOMPARE(routeSpy.count(), 1);
        QCOMPARE(svc.routeStations(), diverted);
    }
};

QTEST_MAIN(TestTrainDetailsService)
#include "tst_traindetailsservice.moc"
