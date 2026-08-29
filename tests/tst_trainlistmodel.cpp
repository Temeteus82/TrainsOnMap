#include "TrainListModel.h"

#include <QDateTime>
#include <QGeoCoordinate>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

/// Pins TrainListModel::recomputeNearestNeighbors(), which feeds the
/// `nearestNeighborMeters` role driving label declutter near a busy terminus.
///
/// The scan only visits pairs with j > i and writes the one haversine to *both*
/// rows (CPP-W1). That halving is easy to get subtly wrong — a one-sided write
/// leaves the later row at the -1 sentinel, and forgetting to clear the previous
/// pass leaves a stale distance behind — so both directions and the re-run are
/// pinned here rather than left to the map to reveal.
class TestTrainListModel : public QObject
{
    Q_OBJECT

private:
    /// Trains at the given latitudes, all on the same meridian so the expected
    /// nearest neighbour of each is unambiguous. `ageSecs` backdates the fixes,
    /// which is how a later snapshot gets to prune one of them.
    static QVector<TrainPosition> fleetAt(const QVector<double> &lats, qint64 ageSecs = 0)
    {
        const QDateTime now = QDateTime::currentDateTimeUtc().addSecs(-ageSecs);
        QVector<TrainPosition> out;
        int number = 100;
        for (double lat : lats) {
            TrainPosition p;
            p.trainNumber = number++;
            p.departureDate = QStringLiteral("2026-08-29");
            p.coordinate = QGeoCoordinate(lat, 24.9);
            p.timestamp = now;
            out.push_back(p);
        }
        return out;
    }

    /// nearestNeighborMeters for the row carrying `trainNumber`, or NaN if the
    /// train is not in the model.
    static double neighborOf(const TrainListModel &m, int trainNumber)
    {
        for (int i = 0; i < m.rowCount(); ++i) {
            const QModelIndex idx = m.index(i, 0);
            if (m.data(idx, TrainListModel::TrainNumberRole).toInt() == trainNumber)
                return m.data(idx, TrainListModel::NearestNeighborRole).toDouble();
        }
        return std::numeric_limits<double>::quiet_NaN();
    }

private slots:
    /// A lone train has no neighbour, and reports the -1 sentinel rather than 0.
    void singleTrainHasNoNeighbour()
    {
        TrainListModel m;
        m.updateTrains(fleetAt({60.0}));
        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(neighborOf(m, 100), -1.0);
    }

    /// Every row gets its own nearest, including the last one — which is only
    /// ever written as the `j` side of a pair, so a one-sided write leaves it
    /// at -1. The middle row keeps the smaller of its two distances.
    void everyRowGetsItsNearest()
    {
        TrainListModel m;
        m.updateTrains(fleetAt({60.0, 60.1, 60.4}));
        QCOMPARE(m.rowCount(), 3);

        const double shortLeg = QGeoCoordinate(60.0, 24.9).distanceTo({60.1, 24.9});
        const double longLeg = QGeoCoordinate(60.1, 24.9).distanceTo({60.4, 24.9});
        QVERIFY(shortLeg < longLeg);

        QCOMPARE(neighborOf(m, 100), shortLeg);   // written as the `i` side
        QCOMPARE(neighborOf(m, 101), shortLeg);   // the nearer of its two pairs
        QCOMPARE(neighborOf(m, 102), longLeg);    // written only as the `j` side
    }

    /// A later pass must not leave the previous pass's distance behind: once the
    /// crowd departs, the survivor is alone again.
    void staleDistanceIsCleared()
    {
        // Backdated past the prune grace window, so the second snapshot is
        // allowed to drop train 101 rather than keep it as a recently-seen row.
        TrainListModel m;
        m.updateTrains(fleetAt({60.0, 60.1}, 300));
        QVERIFY(neighborOf(m, 100) > 0.0);

        m.updateTrains(fleetAt({60.0}));
        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(neighborOf(m, 100), -1.0);
    }

    /// parseTrainLocation must leave the coordinate *invalid* on a malformed
    /// pair: a null/string element converts to 0.0 and (0,0) is a valid
    /// QGeoCoordinate in the Gulf of Guinea, which the isValid() gates at both
    /// call sites would wave through (CPP-W7).
    void malformedCoordinatesStayInvalid()
    {
        auto locationOf = [](const QJsonArray &coords) {
            return parseTrainLocation(QJsonObject{
                {QStringLiteral("trainNumber"), 1},
                {QStringLiteral("departureDate"), QStringLiteral("2026-08-29")},
                {QStringLiteral("location"),
                 QJsonObject{{QStringLiteral("coordinates"), coords}}}});
        };
        QVERIFY(locationOf({24.94, 60.17}).coordinate.isValid());          // Helsinki, [lon, lat]
        QVERIFY(!locationOf({QJsonValue::Null, 60.17}).coordinate.isValid());
        QVERIFY(!locationOf({QStringLiteral("24.94"), 60.17}).coordinate.isValid());
        QVERIFY(!locationOf({0.0, 0.0}).coordinate.isValid());             // outside Finland
        QVERIFY(!locationOf({}).coordinate.isValid());                     // absent field
    }

    /// Metadata/status refreshes used to invalidate the whole model even when a
    /// delta poll changed one train (CPP-W9). The values are now stamped onto
    /// the rows (CPP-W11), so data() serves them without the side tables and a
    /// refresh emits only over the rows that actually changed.
    void statusRefreshTouchesOnlyChangedRows()
    {
        TrainListModel m;
        m.updateTrains(fleetAt({60.0, 61.0, 62.0}));   // trains 100, 101, 102

        const TrainKey key0{QStringLiteral("2026-08-29"), 100};
        const TrainKey key1{QStringLiteral("2026-08-29"), 101};
        TrainStatus late;
        late.delayMinutes = 7;
        late.known = true;
        m.setTrainStatuses({ { key1, late } });
        m.setTrainMetadata({ { key1, QStringLiteral("IC") } },
                           { { key1, QStringLiteral("Long-distance") } }, {});

        // Denormalised reads: the stamped row serves the roles.
        QCOMPARE(m.data(m.index(1, 0), TrainListModel::DelayMinutesRole).toInt(), 7);
        QCOMPARE(m.data(m.index(1, 0), TrainListModel::CategoryRole).toString(),
                 QStringLiteral("Long-distance"));
        QCOMPARE(m.data(m.index(0, 0), TrainListModel::DelayMinutesRole).toInt(), 0);

        QSignalSpy dataSpy(&m, &QAbstractItemModel::dataChanged);

        // Same maps again: nothing changed, nothing emitted.
        m.setTrainStatuses({ { key1, late } });
        QCOMPARE(dataSpy.count(), 0);

        // One train's status changes: one emission spanning only its row.
        TrainStatus onTime;
        onTime.known = true;
        m.setTrainStatuses({ { key0, onTime }, { key1, late } });
        QCOMPARE(dataSpy.count(), 1);
        QCOMPARE(dataSpy.at(0).at(0).toModelIndex().row(), 0);
        QCOMPARE(dataSpy.at(0).at(1).toModelIndex().row(), 0);
    }
};

QTEST_MAIN(TestTrainListModel)
#include "tst_trainlistmodel.moc"
