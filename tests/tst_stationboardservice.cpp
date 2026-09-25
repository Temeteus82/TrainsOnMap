#include "StationBoardService.h"

#include "FakeNetwork.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

/// Pins CPP2-C1 from the 2026-09-25 review: a superseded board reply must be
/// dropped before it touches `loading`, or the spinner goes off while the
/// current station's request is still in flight.
class TestStationBoardService : public QObject
{
    Q_OBJECT

private:
    /// A /live-trains/station/{code} payload with one train departing `code`.
    static QByteArray boardPayload(const QString &code)
    {
        const QJsonObject row{{"stationShortCode", code},
                              {"type", "DEPARTURE"},
                              {"scheduledTime", "2026-09-25T08:00:00.000Z"}};
        const QJsonObject train{{"trainNumber", 1},
                                {"trainType", "IC"},
                                {"timeTableRows", QJsonArray{row}}};
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
};

QTEST_MAIN(TestStationBoardService)
#include "tst_stationboardservice.moc"
