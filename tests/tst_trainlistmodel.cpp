#include "TrainListModel.h"

#include <QDateTime>
#include <QGeoCoordinate>
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
};

QTEST_MAIN(TestTrainListModel)
#include "tst_trainlistmodel.moc"
