#include "TrainFilterModel.h"
#include "TrainListModel.h"

#include <QDateTime>
#include <QGeoCoordinate>
#include <QSignalSpy>
#include <QTest>

/// Pins TrainFilterModel — the proxy backing the sidebar train list (U2-C1).
///
/// Run against the real TrainListModel rather than a stub: the proxy resolves
/// its role numbers from the source's roleNames(), so a stub with a different
/// role table would test the wrong thing.
class TestTrainFilter : public QObject
{
    Q_OBJECT

private:
    /// Three trains: an IC, a commuter on line R, and a cargo run.
    static QVector<TrainPosition> fixture()
    {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QVector<TrainPosition> out;
        for (auto [num, lat] : {std::pair{967, 60.2}, {9012, 60.3}, {5280, 60.4}}) {
            TrainPosition p;
            p.trainNumber = num;
            p.departureDate = QStringLiteral("2026-08-27");
            p.coordinate = QGeoCoordinate(lat, 24.9);
            p.timestamp = now;
            out.push_back(p);
        }
        return out;
    }

    static void applyMetadata(TrainListModel &m)
    {
        const QString d = QStringLiteral("2026-08-27");
        QHash<TrainKey, QString> types, cats, lines;
        types.insert({d, 967}, QStringLiteral("IC"));
        cats.insert({d, 967}, QStringLiteral("Long-distance"));
        types.insert({d, 9012}, QStringLiteral("HL"));
        cats.insert({d, 9012}, QStringLiteral("Commuter"));
        lines.insert({d, 9012}, QStringLiteral("R"));
        types.insert({d, 5280}, QStringLiteral("T"));
        cats.insert({d, 5280}, QStringLiteral("Cargo"));
        m.setTrainMetadata(types, cats, lines);
    }

    static QStringList labelsOf(const TrainFilterModel &p)
    {
        const int typeRole = p.roleNames().key("trainType", -1);
        const int numRole = p.roleNames().key("trainNumber", -1);
        QStringList out;
        for (int r = 0; r < p.rowCount(); ++r) {
            const QModelIndex i = p.index(r, 0);
            out << p.data(i, typeRole).toString() + " "
                       + QString::number(p.data(i, numRole).toInt());
        }
        return out;
    }

private slots:
    void passesEverythingByDefault()
    {
        TrainListModel src;
        src.updateTrains(fixture());
        applyMetadata(src);
        TrainFilterModel proxy;
        proxy.setSourceModel(&src);
        QCOMPARE(proxy.rowCount(), 3);
        QCOMPARE(proxy.count(), 3);
    }

    /// Rows are sorted by train number regardless of arrival order, so the list
    /// doesn't reshuffle under the user as the fleet turns over.
    void sortsByTrainNumber()
    {
        TrainListModel src;
        src.updateTrains(fixture());
        applyMetadata(src);
        TrainFilterModel proxy;
        proxy.setSourceModel(&src);
        QCOMPARE(labelsOf(proxy), QStringList({"IC 967", "T 5280", "HL 9012"}));
    }

    void categoryTogglesFilter()
    {
        TrainListModel src;
        src.updateTrains(fixture());
        applyMetadata(src);
        TrainFilterModel proxy;
        proxy.setSourceModel(&src);

        proxy.setShowCargo(false);
        QCOMPARE(labelsOf(proxy), QStringList({"IC 967", "HL 9012"}));
        proxy.setShowCommuter(false);
        QCOMPARE(labelsOf(proxy), QStringList({"IC 967"}));
        proxy.setShowLongDistance(false);
        QCOMPARE(proxy.rowCount(), 0);
        proxy.setShowCargo(true);
        proxy.setShowCommuter(true);
        proxy.setShowLongDistance(true);
        QCOMPARE(proxy.rowCount(), 3);
    }

    /// Metadata arrives after the position snapshot, so a train whose category
    /// isn't known yet must stay visible — otherwise the whole fleet vanishes
    /// for the first seconds after launch.
    void unknownCategoryFailsOpen()
    {
        TrainListModel src;
        src.updateTrains(fixture());   // deliberately no setTrainMetadata()
        TrainFilterModel proxy;
        proxy.setSourceModel(&src);
        proxy.setShowCargo(false);
        proxy.setShowCommuter(false);
        proxy.setShowLongDistance(false);
        QCOMPARE(proxy.rowCount(), 3);
    }

    void searchMatchesNumberTypeAndLine()
    {
        TrainListModel src;
        src.updateTrains(fixture());
        applyMetadata(src);
        TrainFilterModel proxy;
        proxy.setSourceModel(&src);

        proxy.setSearchText(QStringLiteral("967"));           // by number
        QCOMPARE(labelsOf(proxy), QStringList({"IC 967"}));
        proxy.setSearchText(QStringLiteral("ic"));            // by type, case-insensitive
        QCOMPARE(labelsOf(proxy), QStringList({"IC 967"}));
        proxy.setSearchText(QStringLiteral("R"));             // by commuter line
        QCOMPARE(labelsOf(proxy), QStringList({"HL 9012"}));
        proxy.setSearchText(QStringLiteral("52"));            // partial number
        QCOMPARE(labelsOf(proxy), QStringList({"T 5280"}));
        proxy.setSearchText(QStringLiteral("zzz"));
        QCOMPARE(proxy.rowCount(), 0);
        proxy.setSearchText(QString());
        QCOMPARE(proxy.rowCount(), 3);
    }

    /// Search and category filters are AND-ed, not OR-ed.
    void searchAndCategoryCombine()
    {
        TrainListModel src;
        src.updateTrains(fixture());
        applyMetadata(src);
        TrainFilterModel proxy;
        proxy.setSourceModel(&src);
        proxy.setSearchText(QStringLiteral("9"));   // matches 967 and 9012
        QCOMPARE(proxy.rowCount(), 2);
        proxy.setShowCommuter(false);
        QCOMPARE(labelsOf(proxy), QStringList({"IC 967"}));
    }

    /// Re-filtering must not reset the model: a reset discards the ListView's
    /// instantiated delegates and scroll position, which is the whole reason
    /// this is a proxy rather than a rebuild. Row counts can't see the
    /// difference, so assert on the signal protocol.
    void refilterDoesNotResetTheModel()
    {
        TrainListModel src;
        src.updateTrains(fixture());
        applyMetadata(src);
        TrainFilterModel proxy;
        proxy.setSourceModel(&src);

        QSignalSpy resets(&proxy, &QAbstractItemModel::modelReset);
        QSignalSpy removed(&proxy, &QAbstractItemModel::rowsRemoved);
        proxy.setShowCargo(false);
        QCOMPARE(resets.count(), 0);
        QCOMPARE(removed.count(), 1);
    }

    /// The role numbers must be cached before the base class emits modelReset,
    /// or the proxy's first mapping pass runs with every role at -1 and fails
    /// open — leaving the filter inert until something else invalidates it.
    /// Query from inside the reset to catch that.
    void cachesRolesBeforeBaseReset()
    {
        TrainListModel src;
        src.updateTrains(fixture());
        applyMetadata(src);

        TrainFilterModel proxy;
        proxy.setShowCargo(false);      // a filter that must already be in force

        int rowsDuringReset = -1;
        connect(&proxy, &QAbstractItemModel::modelReset, &proxy,
                [&] { rowsDuringReset = proxy.rowCount(); }, Qt::DirectConnection);
        proxy.setSourceModel(&src);
        QCOMPARE(rowsDuringReset, 2);   // 3 unfiltered would mean the roles were late
    }

    void countChangesWithTheFilter()
    {
        TrainListModel src;
        src.updateTrains(fixture());
        applyMetadata(src);
        TrainFilterModel proxy;
        proxy.setSourceModel(&src);

        QSignalSpy spy(&proxy, &TrainFilterModel::countChanged);
        proxy.setSearchText(QStringLiteral("967"));
        QVERIFY(spy.count() > 0);
        QCOMPARE(proxy.count(), 1);
    }

    /// The list delegate binds these by name; a missing one is a runtime error
    /// in QML, so pin the set the panel actually uses.
    void exposesRolesTheDelegateBinds()
    {
        TrainListModel src;
        src.updateTrains(fixture());
        TrainFilterModel proxy;
        proxy.setSourceModel(&src);
        const QHash<int, QByteArray> roles = proxy.roleNames();
        for (const char *name : {"trainNumber", "departureDate", "coordinate", "speed",
                                 "trainType", "category", "commuterLine", "delayMinutes"})
            QVERIFY2(roles.key(name, -1) >= 0, name);
    }
};

QTEST_MAIN(TestTrainFilter)
#include "tst_trainfilter.moc"
