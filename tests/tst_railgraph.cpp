#include "RailGraph.h"
#include "Projection.h"

#include <QByteArray>
#include <QFile>
#include <QTest>

// A schema-v2 blob is normally qCompress'd; RailGraph::loadFromJson takes the
// uncompressed JSON, so the fixtures here are plain JSON byte arrays.
class TestRailGraph : public QObject
{
    Q_OBJECT

private:
    // Two co-linear tracks sharing an endpoint node, plus two single-track
    // stations — the minimal network that exercises routing + projection.
    static QByteArray fixtureBlob()
    {
        return R"({
          "schemaVersion": 2,
          "features": [
            { "type":"Feature",
              "geometry":{"type":"MultiLineString","coordinates":[[[500000,6700000],[500100,6700000]]]},
              "properties":{"tunniste":"A","paaraide":true,"kaupallinenNumero":"1",
                            "ratakmvalit":[{"ratanumero":"001"}],"viereisetRaiteet":[]}},
            { "type":"Feature",
              "geometry":{"type":"MultiLineString","coordinates":[[[500100,6700000],[500200,6700000]]]},
              "properties":{"tunniste":"B","paaraide":true,"kaupallinenNumero":"2",
                            "ratakmvalit":[{"ratanumero":"001"}],"viereisetRaiteet":[]}}
          ],
          "stations": {
            "AAA": {"opOid":"op1","name":"Alpha","tracks":["A"]},
            "BBB": {"opOid":"op2","name":"Beta","tracks":["B"]}
          },
          "operatingPoints": {"op1":"AAA","op2":"BBB"}
        })";
    }

private slots:
    void rejectsNonV2Blob()
    {
        RailGraph g;
        QVERIFY(!g.loadFromJson(R"({"schemaVersion":1,"features":[]})"));
        QVERIFY(g.isEmpty());
    }

    void parsesFixture()
    {
        RailGraph g;
        QVERIFY(g.loadFromJson(fixtureBlob()));
        QCOMPARE(g.schemaVersion(), 2);
        QCOMPARE(g.trackCount(), 2);
        QVERIFY(g.stations().contains("AAA"));
        QCOMPARE(g.stations().value("AAA").tracks.size(), 1);
        QCOMPARE(g.stations().value("BBB").name, QStringLiteral("Beta"));
    }

    void routeStitchesAdjacentTracks()
    {
        RailGraph g;
        QVERIFY(g.loadFromJson(fixtureBlob()));
        // A and B share the node at (500100,6700000) -> connected.
        const QVector<int> path = g.routePath({QStringLiteral("AAA"), QStringLiteral("BBB")});
        QCOMPARE(path.size(), 2);

        const RailGraph::RoutePolyline rp = g.buildPolyline(path);
        QVERIFY(rp.isValid());
        // ~200 m end to end, chainage monotonic increasing.
        QVERIFY(qAbs(rp.length - 200.0) < 5.0);
        for (int i = 1; i < rp.chainage.size(); ++i)
            QVERIFY(rp.chainage.at(i) >= rp.chainage.at(i - 1));
    }

    void projectsFixOntoRoute()
    {
        RailGraph g;
        QVERIFY(g.loadFromJson(fixtureBlob()));
        const RailGraph::RoutePolyline rp =
            g.buildPolyline(g.routePath({QStringLiteral("AAA"), QStringLiteral("BBB")}));
        QVERIFY(rp.isValid());

        // A fix ~5 m north of the line at easting 500150 (i.e. on track B).
        const QGeoCoordinate fix = tm35fin::toWgs84(500150, 6700005);
        const RailGraph::RouteProjection p = g.projectOntoRoute(rp, fix, -1.0, 0.0);
        QVERIFY(p.isValid());
        QVERIFY2(p.offsetMeters < 8.0,
                 qPrintable(QStringLiteral("offset=%1").arg(p.offsetMeters)));
        QVERIFY(qAbs(p.chainage - 150.0) < 8.0);
    }

    void platformSnapPicksCommercialTrack()
    {
        RailGraph g;
        QVERIFY(g.loadFromJson(fixtureBlob()));
        const QGeoCoordinate fix = tm35fin::toWgs84(500150, 6700005);
        QString chosen;
        const QGeoCoordinate snapped = g.platformSnap(QStringLiteral("BBB"),
                                                      QStringLiteral("2"), fix, &chosen);
        QVERIFY(snapped.isValid());
        QCOMPARE(chosen, QStringLiteral("B"));
        // Wrong platform number at this station -> no snap.
        QVERIFY(!g.platformSnap(QStringLiteral("BBB"),
                                QStringLiteral("9"), fix).isValid());
    }

    void routeKeyIsOrderSensitive()
    {
        QCOMPARE(RailGraph::routeKey({QStringLiteral("HKI"), QStringLiteral("PSL")}),
                 QStringLiteral("HKI|PSL"));
        QVERIFY(RailGraph::routeKey({QStringLiteral("A"), QStringLiteral("B")})
                != RailGraph::routeKey({QStringLiteral("B"), QStringLiteral("A")}));
    }

    // Smoke test over the real baked blob (if present in the source tree).
    void realBlobResolvesHubRoute()
    {
        QFile f(QStringLiteral(RAILS_BLOB));
        if (!f.exists())
            QSKIP("baked rails blob not found");
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray raw = qUncompress(f.readAll());
        QVERIFY(!raw.isEmpty());

        RailGraph g;
        QVERIFY(g.loadFromJson(raw));
        QCOMPARE(g.schemaVersion(), 2);
        QVERIFY(g.trackCount() > 1000);

        for (const QString &code : {QStringLiteral("HKI"), QStringLiteral("PSL"),
                                    QStringLiteral("TPE")}) {
            QVERIFY2(g.stations().contains(code), qPrintable(code));
            QVERIFY2(!g.stations().value(code).tracks.isEmpty(), qPrintable(code));
        }

        const RailGraph::RoutePolyline rp = g.buildPolyline(
            g.routePath({QStringLiteral("HKI"), QStringLiteral("PSL"), QStringLiteral("TPE")}));
        QVERIFY(rp.isValid());

        // A point taken from the route polyline must project back onto it ~exactly.
        const QGeoCoordinate onRoute = rp.points.at(rp.points.size() / 2);
        const RailGraph::RouteProjection p = g.projectOntoRoute(rp, onRoute, -1.0, 0.0);
        QVERIFY(p.isValid());
        QVERIFY2(p.offsetMeters < 1.0,
                 qPrintable(QStringLiteral("offset=%1").arg(p.offsetMeters)));
    }
};

QTEST_MAIN(TestRailGraph)
#include "tst_railgraph.moc"
