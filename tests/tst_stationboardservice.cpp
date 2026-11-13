#include "StationBoardService.h"

#include "DigitrafficClient.h"
#include "FakeNetwork.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

/// Pins two findings from the 2026-09-25 review:
/// - **CPP2-C1**: a superseded board reply must be dropped before it touches
///   `loading`, or the spinner goes off while the current station's request is
///   still in flight.
/// - **CPP2-W2**: destinations must re-resolve when station names land after
///   the board did.
class TestStationBoardService : public QObject
{
    Q_OBJECT

private:
    /// A /live-trains/station/{code} payload with one train departing `code`,
    /// terminating at `destination` when given.
    static QByteArray boardPayload(const QString &code, const QString &destination = {})
    {
        QJsonArray rows{QJsonObject{{"stationShortCode", code},
                                    {"type", "DEPARTURE"},
                                    {"scheduledTime", "2026-09-25T08:00:00.000Z"}}};
        if (!destination.isEmpty())
            rows.push_back(QJsonObject{{"stationShortCode", destination},
                                       {"type", "ARRIVAL"},
                                       {"scheduledTime", "2026-09-25T09:00:00.000Z"}});
        const QJsonObject train{{"trainNumber", 1},
                                {"trainType", "IC"},
                                {"timeTableRows", rows}};
        return QJsonDocument(QJsonArray{train}).toJson(QJsonDocument::Compact);
    }

private slots:
    /// Select A, select B before A lands, land A: still waiting for B.
    void staleReplyKeepsSpinnerForCurrentStation()
    {
        fake::Manager net;
        StationBoardService svc(nullptr, &net);

        svc.show(QStringLiteral("HKI"), {});
        fake::Reply *replyA = net.take(QStringLiteral("/station/HKI"));
        svc.show(QStringLiteral("TPE"), {});
        fake::Reply *replyB = net.take(QStringLiteral("/station/TPE"));
        QVERIFY(replyA && replyB);

        replyA->respond(boardPayload(QStringLiteral("HKI")));
        QVERIFY(svc.isLoading());
        QCOMPARE(svc.board()->count(), 0);

        replyB->respond(boardPayload(QStringLiteral("TPE")));
        QVERIFY(!svc.isLoading());
        QCOMPARE(svc.board()->count(), 1);
    }

    /// show(A), clear(), show(A): the first reply names the station now shown,
    /// but it belongs to a superseded request.
    void showClearShowDropsTheFirstReply()
    {
        fake::Manager net;
        StationBoardService svc(nullptr, &net);

        svc.show(QStringLiteral("HKI"), {});
        fake::Reply *first = net.take(QStringLiteral("/station/HKI"));
        svc.clear();
        QVERIFY(!svc.isLoading());
        svc.show(QStringLiteral("HKI"), {});
        fake::Reply *second = net.take(QStringLiteral("/station/HKI"));
        QVERIFY(first && second);

        first->respond(boardPayload(QStringLiteral("HKI")));
        QVERIFY(svc.isLoading());
        QCOMPARE(svc.board()->count(), 0);

        second->respond(boardPayload(QStringLiteral("HKI")));
        QVERIFY(!svc.isLoading());
        QCOMPARE(svc.board()->count(), 1);
    }

    /// R3-6. refresh() (what the timer calls) re-fetches the open board in place:
    /// no spinner, the rows stay up until the new ones land, and a failed
    /// refresh leaves them alone.
    void refreshUpdatesTheOpenBoardInPlace()
    {
        fake::Manager net;
        StationBoardService svc(nullptr, &net);

        svc.refresh();   // nothing selected: no request
        QCOMPARE(net.take(QStringLiteral("/station/")), nullptr);

        svc.show(QStringLiteral("HKI"), {});
        svc.refresh();   // the first load is still out: not superseded
        fake::Reply *first = net.take(QStringLiteral("/station/HKI"));
        QVERIFY(first);
        QCOMPARE(net.take(QStringLiteral("/station/HKI")), nullptr);
        first->respond(boardPayload(QStringLiteral("HKI"), QStringLiteral("TPE")));

        StationBoardModel *model = svc.board();
        const int role = model->roleNames().key("destination");
        QCOMPARE(model->index(0, 0).data(role).toString(), QStringLiteral("TPE"));

        svc.refresh();
        fake::Reply *failed = net.take(QStringLiteral("/station/HKI"));
        QVERIFY(failed);
        QVERIFY(!svc.isLoading());
        QCOMPARE(model->count(), 1);
        failed->fail();
        QCOMPARE(model->count(), 1);
        QVERIFY(svc.status().isEmpty());

        svc.refresh();
        fake::Reply *second = net.take(QStringLiteral("/station/HKI"));
        QVERIFY(second);
        second->respond(boardPayload(QStringLiteral("HKI"), QStringLiteral("OL")));
        QCOMPARE(model->count(), 1);
        QCOMPARE(model->index(0, 0).data(role).toString(), QStringLiteral("OL"));

        svc.clear();
        svc.refresh();   // closed again: no request
        QCOMPARE(net.take(QStringLiteral("/station/")), nullptr);
    }

    /// CPP2-W2. The board lands while /metadata/stations is still out, so the
    /// destination shows as a code; when the names land it must become a name
    /// without the user re-selecting the station.
    void destinationResolvesWhenNamesLandLate()
    {
        fake::Manager net;
        DigitrafficClient fleet(nullptr, &net);   // issues the metadata fetches
        StationBoardService svc(nullptr, &net);
        svc.setFleet(&fleet);

        svc.show(QStringLiteral("HKI"), {});
        fake::Reply *board = net.take(QStringLiteral("/station/HKI"));
        QVERIFY(board);
        board->respond(boardPayload(QStringLiteral("HKI"), QStringLiteral("TPE")));

        StationBoardModel *model = svc.board();
        QCOMPARE(model->count(), 1);
        const int role = model->roleNames().key("destination");
        QCOMPARE(model->index(0, 0).data(role).toString(), QStringLiteral("TPE"));

        fake::Reply *stations = net.take(QStringLiteral("metadata/stations"));
        QVERIFY(stations);
        const QJsonObject tpe{{"stationShortCode", "TPE"},
                              {"stationName", "Tampere"},
                              {"latitude", 61.4981},
                              {"longitude", 23.7735},
                              {"passengerTraffic", true}};
        stations->respond(QJsonDocument(QJsonArray{tpe}).toJson(QJsonDocument::Compact));

        QCOMPARE(model->index(0, 0).data(role).toString(), QStringLiteral("Tampere"));
    }
};

QTEST_MAIN(TestStationBoardService)
#include "tst_stationboardservice.moc"
