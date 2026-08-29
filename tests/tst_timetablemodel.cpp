// Pins the QRangeModel-backed TimetableModel to the contract the QML detail
// panel relies on: the role names auto-derived from TimetableStop's Q_PROPERTYs
// must match what the delegate binds to, data() must return the right value per
// role, and setStops()/clear() must drive rowCount + countChanged correctly.
#include "TimetableModel.h"

#include <QSignalSpy>
#include <QTest>
#include <QHash>

class tst_TimetableModel : public QObject
{
    Q_OBJECT

private:
    // Resolve a QML role name to its integer role id via the model's roleNames().
    // Reverse lookup is unambiguous: role names come from Q_PROPERTY names, which
    // are unique per metaobject.
    static int roleFor(const QAbstractItemModel &m, const QByteArray &name)
    {
        return m.roleNames().key(name, -1);
    }

    static TimetableStop makeStop()
    {
        TimetableStop s;
        s.stationShortCode = QStringLiteral("HKI");
        s.stationName = QStringLiteral("Helsinki");
        s.scheduledDeparture = QStringLiteral("12:00");
        s.estimatedDeparture = QStringLiteral("12:03");
        s.delayMinutes = 3;
        s.track = QStringLiteral("5");
        s.cancelled = false;
        s.stopping = true;
        return s;
    }

    // A progress fixture stop: `stopping` = booked passenger stop, `sawActual` =
    // the train has recorded an actualTime here (so it's behind the train).
    static TimetableStop progressStop(bool stopping, bool sawActual)
    {
        TimetableStop s;
        s.stopping = stopping;
        s.sawActual = sawActual;
        return s;
    }

private slots:
    // Every role the QML delegate (TrainDetailPanel.qml) binds to must be present
    // in the auto-derived role table.
    void exposesExpectedRoleNames()
    {
        TimetableModel model;
        const QList<QByteArray> expected = {
            "stationName", "stationShortCode", "scheduledArrival", "estimatedArrival",
            "scheduledDeparture", "estimatedDeparture", "delayMinutes", "track",
            "cancelled", "stopping", "passed", "isNext",
        };
        const QHash<int, QByteArray> roles = model.roleNames();
        const QList<QByteArray> have = roles.values();
        for (const QByteArray &name : expected)
            QVERIFY2(have.contains(name), name.constData());
    }

    // The two build-time accumulators must NOT leak into the model as roles.
    void hidesAccumulatorFields()
    {
        TimetableModel model;
        const QList<QByteArray> have = model.roleNames().values();
        QVERIFY(!have.contains("sawCommercial"));
        QVERIFY(!have.contains("sawTrainStopping"));
        QVERIFY(!have.contains("sawActual"));
    }

    void setStopsPopulatesRowsAndData()
    {
        TimetableModel model;
        QSignalSpy countSpy(&model, &TimetableModel::countChanged);

        model.setStops({ makeStop() });

        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.count(), 1);
        QCOMPARE(countSpy.count(), 1);

        const QModelIndex idx = model.index(0, 0);
        QVERIFY(idx.isValid());
        QCOMPARE(model.data(idx, roleFor(model, "stationName")).toString(), QStringLiteral("Helsinki"));
        QCOMPARE(model.data(idx, roleFor(model, "track")).toString(), QStringLiteral("5"));
        QCOMPARE(model.data(idx, roleFor(model, "delayMinutes")).toInt(), 3);
        QCOMPARE(model.data(idx, roleFor(model, "scheduledDeparture")).toString(), QStringLiteral("12:00"));
        QCOMPARE(model.data(idx, roleFor(model, "stopping")).toBool(), true);
        QCOMPARE(model.data(idx, roleFor(model, "cancelled")).toBool(), false);
    }

    void clearEmptiesAndSignals()
    {
        TimetableModel model;
        model.setStops({ makeStop(), makeStop() });
        QCOMPARE(model.rowCount(), 2);

        QSignalSpy countSpy(&model, &TimetableModel::countChanged);
        model.clear();
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.count(), 0);
        QCOMPARE(countSpy.count(), 1);

        // clear() on an already-empty model is a no-op (no spurious signal).
        model.clear();
        QCOMPARE(countSpy.count(), 1);
    }

    // Mid-journey: stops up to the last actualTime are `passed`; the first booked
    // stop after it is `isNext`; progress counts only booked (stopping) stops.
    void derivesJourneyProgressMidRoute()
    {
        TimetableModel model;
        QSignalSpy progressSpy(&model, &TimetableModel::progressChanged);

        // idx0 booked+passed, idx1 passing-point+passed, idx2 booked+next, idx3 booked.
        model.setStops({
            progressStop(/*stopping*/ true,  /*sawActual*/ true),
            progressStop(/*stopping*/ false, /*sawActual*/ true),
            progressStop(/*stopping*/ true,  /*sawActual*/ false),
            progressStop(/*stopping*/ true,  /*sawActual*/ false),
        });
        QCOMPARE(progressSpy.count(), 1);

        const int passedRole = roleFor(model, "passed");
        const int isNextRole = roleFor(model, "isNext");
        auto passedAt = [&](int r) { return model.data(model.index(r, 0), passedRole).toBool(); };
        auto isNextAt = [&](int r) { return model.data(model.index(r, 0), isNextRole).toBool(); };

        QCOMPARE(passedAt(0), true);
        QCOMPARE(passedAt(1), true);   // passing point still behind the train
        QCOMPARE(passedAt(2), false);
        QCOMPARE(passedAt(3), false);

        QCOMPARE(isNextAt(2), true);   // first booked stop past the last actual
        QCOMPARE(isNextAt(1), false);  // the passing point is never "NEXT"
        QCOMPARE(isNextAt(3), false);

        QCOMPARE(model.totalStops(), 3);   // three booked stops (idx 0,2,3)
        QCOMPARE(model.passedStops(), 1);  // only idx0 is booked AND passed
        QCOMPARE(model.nextStopRow(), 2);  // source row of the NEXT booked stop
    }

    // Not yet departed: nothing passed, NEXT is the first booked stop.
    void derivesJourneyProgressNotStarted()
    {
        TimetableModel model;
        model.setStops({
            progressStop(true, false),
            progressStop(true, false),
        });
        QCOMPARE(model.passedStops(), 0);
        QCOMPARE(model.totalStops(), 2);
        QCOMPARE(model.nextStopRow(), 0);
        QCOMPARE(model.data(model.index(0, 0), roleFor(model, "isNext")).toBool(), true);
    }

    // Journey complete: every stop passed, no NEXT.
    void derivesJourneyProgressComplete()
    {
        TimetableModel model;
        model.setStops({
            progressStop(true, true),
            progressStop(true, true),
        });
        QCOMPARE(model.passedStops(), 2);
        QCOMPARE(model.totalStops(), 2);
        QCOMPARE(model.nextStopRow(), -1);
        QCOMPARE(model.data(model.index(1, 0), roleFor(model, "passed")).toBool(), true);
    }

    // clear() resets progress back to zero and re-signals.
    void clearResetsProgress()
    {
        TimetableModel model;
        model.setStops({ progressStop(true, true) });
        QCOMPARE(model.passedStops(), 1);

        model.clear();
        QCOMPARE(model.passedStops(), 0);
        QCOMPARE(model.totalStops(), 0);
        QCOMPARE(model.nextStopRow(), -1);
    }

    /// A live MQTT refresh re-sends a structurally identical stop list with a
    /// few estimate/progress fields moved. That must be an in-place update with
    /// one dataChanged over the changed span — a model reset would destroy the
    /// delegates and drop the scroll position on every update (CPP-W3). The
    /// reset stays reserved for a genuinely different station sequence.
    void liveRefreshUpdatesInPlaceWithoutReset()
    {
        auto stopAt = [](const char *code) {
            TimetableStop s;
            s.stationShortCode = QLatin1String(code);
            s.stopping = true;
            return s;
        };
        TimetableModel model;
        model.setStops({ stopAt("HKI"), stopAt("PSL"), stopAt("TPE") });

        QSignalSpy resetSpy(&model, &QAbstractItemModel::modelAboutToBeReset);
        QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);

        // Identical refresh: no reset, no dataChanged.
        model.setStops({ stopAt("HKI"), stopAt("PSL"), stopAt("TPE") });
        QCOMPARE(resetSpy.count(), 0);
        QCOMPARE(dataSpy.count(), 0);

        // One field moved on the middle stop: one dataChanged, rows 1..1.
        auto rows = QVector<TimetableStop>{ stopAt("HKI"), stopAt("PSL"), stopAt("TPE") };
        rows[1].estimatedArrival = QStringLiteral("12:07");
        model.setStops(std::move(rows));
        QCOMPARE(resetSpy.count(), 0);
        QCOMPARE(dataSpy.count(), 1);
        QCOMPARE(dataSpy.at(0).at(0).toModelIndex().row(), 1);
        QCOMPARE(dataSpy.at(0).at(1).toModelIndex().row(), 1);
        QCOMPARE(model.data(model.index(1, 0), roleFor(model, "estimatedArrival")).toString(),
                 QStringLiteral("12:07"));

        // Different station sequence (new selection): the reset path.
        model.setStops({ stopAt("HKI"), stopAt("TKU") });
        QCOMPARE(resetSpy.count(), 1);
        QCOMPARE(model.rowCount(), 2);
    }
};

QTEST_MAIN(tst_TimetableModel)
#include "tst_timetablemodel.moc"
