// Pins the QRangeModel-backed CompositionModel to the contract the QML carriage
// strip relies on: the role names auto-derived from CompositionVehicle's
// Q_PROPERTYs must match what the delegate binds to (including the QStringList
// `amenities` role), data() must return the right value per role, and
// setVehicles()/clear() must drive rowCount + countChanged correctly.
#include "CompositionModel.h"

#include <QSignalSpy>
#include <QTest>

class tst_CompositionModel : public QObject
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

    static CompositionVehicle makeLoco()
    {
        CompositionVehicle v;
        v.position = 1;
        v.locomotive = true;
        v.vehicleType = QStringLiteral("Sr2");
        v.powerType = QStringLiteral("Electric");
        return v;
    }

    static CompositionVehicle makeWagon()
    {
        CompositionVehicle v;
        v.position = 5;
        v.locomotive = false;
        v.label = QStringLiteral("3");
        v.vehicleType = QStringLiteral("ERd");
        v.amenities = { QStringLiteral("Catering"), QStringLiteral("Accessible") };
        return v;
    }

private slots:
    // Every role the QML delegate (TrainDetailPanel.qml carriage strip) binds to
    // must be present in the auto-derived role table.
    void exposesExpectedRoleNames()
    {
        CompositionModel model;
        const QList<QByteArray> expected = {
            "position", "locomotive", "label", "vehicleType", "powerType", "amenities",
        };
        const QList<QByteArray> have = model.roleNames().values();
        for (const QByteArray &name : expected)
            QVERIFY2(have.contains(name), name.constData());
    }

    void setVehiclesPopulatesRowsAndData()
    {
        CompositionModel model;
        QSignalSpy countSpy(&model, &CompositionModel::countChanged);

        model.setVehicles({ makeLoco(), makeWagon() });

        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.count(), 2);
        QCOMPARE(countSpy.count(), 1);

        const QModelIndex loco = model.index(0, 0);
        QVERIFY(loco.isValid());
        QCOMPARE(model.data(loco, roleFor(model, "locomotive")).toBool(), true);
        QCOMPARE(model.data(loco, roleFor(model, "vehicleType")).toString(), QStringLiteral("Sr2"));
        QCOMPARE(model.data(loco, roleFor(model, "powerType")).toString(), QStringLiteral("Electric"));

        const QModelIndex wagon = model.index(1, 0);
        QCOMPARE(model.data(wagon, roleFor(model, "locomotive")).toBool(), false);
        QCOMPARE(model.data(wagon, roleFor(model, "label")).toString(), QStringLiteral("3"));
        QCOMPARE(model.data(wagon, roleFor(model, "position")).toInt(), 5);
        // The QStringList amenity role must survive as a list with its members intact.
        QCOMPARE(model.data(wagon, roleFor(model, "amenities")).toStringList(),
                 (QStringList{ QStringLiteral("Catering"), QStringLiteral("Accessible") }));
    }

    void clearEmptiesAndSignals()
    {
        CompositionModel model;
        model.setVehicles({ makeLoco(), makeWagon() });
        QCOMPARE(model.rowCount(), 2);

        QSignalSpy countSpy(&model, &CompositionModel::countChanged);
        model.clear();
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.count(), 0);
        QCOMPARE(countSpy.count(), 1);

        // clear() on an already-empty model is a no-op (no spurious signal).
        model.clear();
        QCOMPARE(countSpy.count(), 1);
    }
};

QTEST_MAIN(tst_CompositionModel)
#include "tst_compositionmodel.moc"
