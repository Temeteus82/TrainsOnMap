// Pins TimetableFilterModel to what TrainDetailPanel.qml relies on: with showAll
// off only booked stops are rows (that row-set shrink is what lets the ListView
// virtualise), roles pass straight through so the delegate binds unchanged names,
// proxyRowForSource maps the NEXT-stop row it scrolls to, and a source model with
// no "stopping" role fails OPEN rather than hiding everything.
#include "TimetableFilterModel.h"
#include "TimetableModel.h"

#include <QSignalSpy>
#include <QStringListModel>
#include <QTest>

class tst_TimetableFilter : public QObject
{
    Q_OBJECT

private:
    static TimetableStop stop(const QString &name, bool stopping, bool sawActual = false)
    {
        TimetableStop s;
        s.stationName = name;
        s.stopping = stopping;
        s.sawActual = sawActual;
        return s;
    }

    // HKI (booked) → PSL (passing) → TPE (booked): the shape of a real route,
    // where the passing point sits between two stops.
    static QVector<TimetableStop> route()
    {
        return { stop(QStringLiteral("HKI"), true, true),
                 stop(QStringLiteral("PSL"), false, true),
                 stop(QStringLiteral("TPE"), true) };
    }

    static int roleFor(const QAbstractItemModel &m, const QByteArray &name)
    {
        return m.roleNames().key(name, -1);
    }

    static QStringList namesOf(const QAbstractItemModel &m)
    {
        const int role = roleFor(m, "stationName");
        QStringList out;
        for (int r = 0; r < m.rowCount(); ++r)
            out << m.data(m.index(r, 0), role).toString();
        return out;
    }

private slots:
    // Default (collapsed): passing points are not rows at all.
    void hidesPassingPointsByDefault()
    {
        TimetableModel source;
        source.setStops(route());
        TimetableFilterModel proxy;
        proxy.setSourceModel(&source);

        QVERIFY(!proxy.showAll());
        QCOMPARE(proxy.rowCount(), 2);
        QCOMPARE(namesOf(proxy), QStringList({ QStringLiteral("HKI"), QStringLiteral("TPE") }));
    }

    // Toggling showAll republishes the full timing-point list, and back again.
    void showAllRevealsEveryTimingPoint()
    {
        TimetableModel source;
        source.setStops(route());
        TimetableFilterModel proxy;
        proxy.setSourceModel(&source);

        QSignalSpy spy(&proxy, &TimetableFilterModel::showAllChanged);
        proxy.setShowAll(true);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(proxy.rowCount(), 3);
        QCOMPARE(namesOf(proxy).at(1), QStringLiteral("PSL"));

        proxy.setShowAll(false);
        QCOMPARE(spy.size(), 2);
        QCOMPARE(proxy.rowCount(), 2);

        // Setting the same value again is a no-op (no spurious signal).
        proxy.setShowAll(false);
        QCOMPARE(spy.size(), 2);
    }

    // The toggle must move rows INCREMENTALLY, not reset the model. A reset would
    // produce the same row set — so rowCount assertions alone can't see the
    // difference — but it throws away the ListView's instantiated delegates and
    // its scroll position, which is the whole point of filtering in the model.
    void togglesRowsIncrementallyWithoutResetting()
    {
        TimetableModel source;
        source.setStops(route());
        TimetableFilterModel proxy;
        proxy.setSourceModel(&source);

        QSignalSpy inserted(&proxy, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(&proxy, &QAbstractItemModel::rowsRemoved);
        QSignalSpy reset(&proxy, &QAbstractItemModel::modelReset);

        // Expanding inserts PSL between the two booked stops (proxy row 1).
        proxy.setShowAll(true);
        QCOMPARE(reset.size(), 0);
        QCOMPARE(removed.size(), 0);
        QCOMPARE(inserted.size(), 1);
        QCOMPARE(inserted.at(0).at(1).toInt(), 1);   // first
        QCOMPARE(inserted.at(0).at(2).toInt(), 1);   // last

        // Collapsing removes that same row.
        proxy.setShowAll(false);
        QCOMPARE(reset.size(), 0);
        QCOMPARE(removed.size(), 1);
        QCOMPARE(removed.at(0).at(1).toInt(), 1);
        QCOMPARE(removed.at(0).at(2).toInt(), 1);
    }

    // Every role the timetable delegate declares as a `required property` must
    // resolve through the proxy — a missing one is a hard runtime error in QML.
    // Named explicitly rather than compared against source.roleNames(): the proxy
    // forwards that call, so comparing the two is true whatever the source exposes.
    void passesSourceRolesThrough()
    {
        TimetableModel source;
        source.setStops(route());
        TimetableFilterModel proxy;
        proxy.setSourceModel(&source);

        const QList<QByteArray> required = {
            "stationName", "cancelled", "track", "scheduledArrival", "estimatedArrival",
            "scheduledDeparture", "estimatedDeparture", "delayMinutes", "stopping",
            "passed", "isNext", "causeText",
        };
        const QList<QByteArray> have = proxy.roleNames().values();
        for (const QByteArray &name : required)
            QVERIFY2(have.contains(name), name.constData());

        const QModelIndex idx = proxy.index(1, 0);   // TPE, source row 2
        QCOMPARE(proxy.data(idx, roleFor(proxy, "stationName")).toString(), QStringLiteral("TPE"));
        QCOMPARE(proxy.data(idx, roleFor(proxy, "stopping")).toBool(), true);
        QCOMPARE(proxy.data(idx, roleFor(proxy, "isNext")).toBool(), true);
    }

    // The panel scrolls to nextStopRow(), which is a SOURCE row — it has to survive
    // the filter's row shift, and a filtered-out or out-of-range row must give -1.
    void mapsSourceRowsToProxyRows()
    {
        TimetableModel source;
        source.setStops(route());
        TimetableFilterModel proxy;
        proxy.setSourceModel(&source);

        QCOMPARE(source.nextStopRow(), 2);            // TPE in source coordinates
        QCOMPARE(proxy.proxyRowForSource(2), 1);      // ...is row 1 when collapsed
        QCOMPARE(proxy.proxyRowForSource(0), 0);
        QCOMPARE(proxy.proxyRowForSource(1), -1);     // PSL currently hidden
        QCOMPARE(proxy.proxyRowForSource(3), -1);     // past the end
        QCOMPARE(proxy.proxyRowForSource(-1), -1);

        proxy.setShowAll(true);
        QCOMPARE(proxy.proxyRowForSource(2), 2);      // identity once nothing is filtered
        QCOMPARE(proxy.proxyRowForSource(1), 1);
    }

    // setStops() resets the source on every live MQTT update; the cached "stopping"
    // role must stay valid so the filter keeps applying afterwards.
    void refiltersAfterSourceReset()
    {
        TimetableModel source;
        source.setStops(route());
        TimetableFilterModel proxy;
        proxy.setSourceModel(&source);
        QCOMPARE(proxy.rowCount(), 2);

        source.setStops({ stop(QStringLiteral("OL"), false), stop(QStringLiteral("KEM"), true) });
        QCOMPARE(proxy.rowCount(), 1);
        QCOMPARE(namesOf(proxy), QStringList({ QStringLiteral("KEM") }));

        source.clear();
        QCOMPARE(proxy.rowCount(), 0);
    }

    // The base setSourceModel() emits modelReset from inside its own endResetModel(),
    // and QSortFilterProxyModel builds its row mapping lazily on the first query
    // after that — so a client already attached to the proxy (in the app: the
    // ListView, since sourceModel is a QML binding) filters through whatever the
    // cached role is AT THAT MOMENT, and the mapping it builds is kept. The role
    // therefore has to be cached before the base call, not after.
    void cachesStoppingRoleBeforeBaseReset()
    {
        TimetableModel source;
        source.setStops(route());
        TimetableFilterModel proxy;

        int rowsDuringReset = -1;
        connect(&proxy, &QAbstractItemModel::modelReset, &proxy,
                [&] { rowsDuringReset = proxy.rowCount(); }, Qt::DirectConnection);

        proxy.setSourceModel(&source);

        QCOMPARE(rowsDuringReset, 2);   // filtered already, not the unfiltered 3
        QCOMPARE(proxy.rowCount(), 2);
    }

    // No source, or a source with no "stopping" role: show every row rather than
    // silently emptying the timetable.
    void failsOpenWithoutStoppingRole()
    {
        TimetableFilterModel proxy;
        QCOMPARE(proxy.rowCount(), 0);
        QCOMPARE(proxy.proxyRowForSource(0), -1);

        QStringListModel plain({ QStringLiteral("a"), QStringLiteral("b") });
        QVERIFY(plain.roleNames().key("stopping", -1) < 0);
        proxy.setSourceModel(&plain);
        QCOMPARE(proxy.rowCount(), 2);
    }
};

QTEST_MAIN(tst_TimetableFilter)
#include "tst_timetablefilter.moc"
