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
    static int roleFor(const QAbstractItemModel &m, const QByteArray &name)
    {
        const QHash<int, QByteArray> roles = m.roleNames();
        for (auto it = roles.cbegin(); it != roles.cend(); ++it)
            if (it.value() == name)
                return it.key();
        return -1;
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

private slots:
    // Every role the QML delegate (TrainDetailPanel.qml) binds to must be present
    // in the auto-derived role table.
    void exposesExpectedRoleNames()
    {
        TimetableModel model;
        const QList<QByteArray> expected = {
            "stationName", "stationShortCode", "scheduledArrival", "estimatedArrival",
            "scheduledDeparture", "estimatedDeparture", "delayMinutes", "track",
            "cancelled", "stopping",
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
};

QTEST_MAIN(tst_TimetableModel)
#include "tst_timetablemodel.moc"
