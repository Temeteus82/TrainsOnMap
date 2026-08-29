// Pins TrackListModel::setVisibleSegments(), the incremental viewport diff behind
// the track layer. It is worth pinning because the diff is the whole point: the
// MapItemView must rebuild only the polylines that entered or left, so the model
// has to end up holding exactly the incoming set, in ascending id order, with all
// three parallel vectors still aligned — while announcing every structural change.
//
// The insert path in particular was rewritten to make room for a whole run with
// one shift per vector instead of inserting element-by-element (review CPP-I1),
// which is exactly the kind of index arithmetic that silently mis-orders rows.
#include "TrackListModel.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

class TestTrackListModel : public QObject
{
    Q_OBJECT

private:
    /// A path whose single "coordinate" is just the id, so a row's path can be
    /// checked against the id it is supposed to be parallel to. The real paths are
    /// QGeoCoordinate lists; nothing here depends on that.
    static QVariantList pathFor(int id) { return QVariantList{ id * 1000 }; }

    /// Feed a viewport: ids ascending, paths tagged by id, `mains` alternating so
    /// a misalignment between the three vectors shows up.
    static void apply(TrackListModel &m, const QVector<int> &ids)
    {
        QVector<QVariantList> paths;
        QVector<bool> mains;
        for (int id : ids) {
            paths.push_back(pathFor(id));
            mains.push_back(id % 2 == 0);
        }
        m.setVisibleSegments(ids, paths, mains);
    }

    /// The ids the model currently holds, read back through the role the QML
    /// delegate binds to rather than the private vector.
    static QVector<int> idsOf(const TrackListModel &m)
    {
        QVector<int> out;
        for (int row = 0; row < m.rowCount(); ++row) {
            const QVariantList path =
                m.data(m.index(row, 0), TrackListModel::PathRole).toList();
            out.push_back(path.isEmpty() ? -1 : path.first().toInt() / 1000);
        }
        return out;
    }

    static void verifyAligned(const TrackListModel &m, const QVector<int> &expected)
    {
        QCOMPARE(m.rowCount(), expected.size());
        QCOMPARE(m.count(), expected.size());
        QCOMPARE(idsOf(m), expected);
        for (int row = 0; row < m.rowCount(); ++row) {
            const int id = expected.at(row);
            QCOMPARE(m.data(m.index(row, 0), TrackListModel::MainTrackRole).toBool(),
                     id % 2 == 0);
        }
    }

private slots:
    /// QAbstractItemModelTester cross-checks every structural signal against the
    /// row counts around it, so an insert or remove that announces the wrong range
    /// fails here rather than as a corrupted map layer.
    void diffsAcrossViewportsStayConsistent()
    {
        TrackListModel m;
        QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::Fatal);

        apply(m, { 2, 5, 9 });                  // first viewport: all new
        verifyAligned(m, { 2, 5, 9 });

        apply(m, { 1, 2, 3, 5, 7, 9, 11 });     // pan: runs inserted before, between and after
        verifyAligned(m, { 1, 2, 3, 5, 7, 9, 11 });

        apply(m, { 3, 5, 7 });                  // zoom in: runs removed at both ends
        verifyAligned(m, { 3, 5, 7 });

        apply(m, { 4, 6 });                     // disjoint viewport: nothing survives
        verifyAligned(m, { 4, 6 });

        apply(m, {});                           // panned off the network entirely
        verifyAligned(m, {});
    }

    /// An unchanged viewport must be a no-op: this runs on every pan/zoom tick, and
    /// a spurious reset or count signal would rebuild the whole layer.
    void unchangedViewportEmitsNothing()
    {
        TrackListModel m;
        apply(m, { 1, 4, 8 });

        QSignalSpy countSpy(&m, &TrackListModel::countChanged);
        QSignalSpy insertSpy(&m, &QAbstractItemModel::rowsInserted);
        QSignalSpy removeSpy(&m, &QAbstractItemModel::rowsRemoved);
        QSignalSpy resetSpy(&m, &QAbstractItemModel::modelAboutToBeReset);

        apply(m, { 1, 4, 8 });

        QCOMPARE(countSpy.count(), 0);
        QCOMPARE(insertSpy.count(), 0);
        QCOMPARE(removeSpy.count(), 0);
        QCOMPARE(resetSpy.count(), 0);
        verifyAligned(m, { 1, 4, 8 });
    }

    /// A run entering the middle is the case the one-shift insert has to get right:
    /// the tail must end up after it, still parallel, in one announced range.
    void insertedRunLandsInOrder()
    {
        TrackListModel m;
        apply(m, { 1, 10 });

        QSignalSpy insertSpy(&m, &QAbstractItemModel::rowsInserted);
        apply(m, { 1, 4, 5, 6, 10 });

        QCOMPARE(insertSpy.count(), 1);          // one contiguous run, one signal
        QCOMPARE(insertSpy.at(0).at(1).toInt(), 1);   // first
        QCOMPARE(insertSpy.at(0).at(2).toInt(), 3);   // last
        verifyAligned(m, { 1, 4, 5, 6, 10 });
    }
};

QTEST_MAIN(TestTrackListModel)
#include "tst_tracklistmodel.moc"
