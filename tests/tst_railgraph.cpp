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

    // Four single-track stations on two disconnected islands: T0-T1 share a node
    // and T2-T3 share a node, but the two islands are 100 km apart. A route
    // S0->S1->S2->S3 therefore has an unroutable S1->S2 middle leg (regression
    // fixture for the chord-across-the-gap splice, finding #1).
    static QByteArray gappedBlob()
    {
        return R"({
          "schemaVersion": 2,
          "features": [
            { "type":"Feature",
              "geometry":{"type":"MultiLineString","coordinates":[[[500000,6700000],[500100,6700000]]]},
              "properties":{"tunniste":"T0","paaraide":true,"kaupallinenNumero":"1",
                            "ratakmvalit":[],"viereisetRaiteet":[]}},
            { "type":"Feature",
              "geometry":{"type":"MultiLineString","coordinates":[[[500100,6700000],[500200,6700000]]]},
              "properties":{"tunniste":"T1","paaraide":true,"kaupallinenNumero":"1",
                            "ratakmvalit":[],"viereisetRaiteet":[]}},
            { "type":"Feature",
              "geometry":{"type":"MultiLineString","coordinates":[[[600000,6700000],[600100,6700000]]]},
              "properties":{"tunniste":"T2","paaraide":true,"kaupallinenNumero":"1",
                            "ratakmvalit":[],"viereisetRaiteet":[]}},
            { "type":"Feature",
              "geometry":{"type":"MultiLineString","coordinates":[[[600100,6700000],[600200,6700000]]]},
              "properties":{"tunniste":"T3","paaraide":true,"kaupallinenNumero":"1",
                            "ratakmvalit":[],"viereisetRaiteet":[]}}
          ],
          "stations": {
            "S0": {"opOid":"o0","name":"S0","tracks":["T0"]},
            "S1": {"opOid":"o1","name":"S1","tracks":["T1"]},
            "S2": {"opOid":"o2","name":"S2","tracks":["T2"]},
            "S3": {"opOid":"o3","name":"S3","tracks":["T3"]}
          }
        })";
    }

    // An L-junction whose two tracks share a join node J, with track Q (the
    // *second* track of the route) digitised end->start so Q.path.first() is its
    // far end, not the join. P's far end A is bent back toward Q's far end B, so
    // orienting the first track by Q.path.first() reverses it the wrong way
    // (regression fixture for finding #2). J=(500000,6700000), A=(500030,6700090),
    // B=(500000,6700100).
    static QByteArray lJunctionBlob()
    {
        return R"({
          "schemaVersion": 2,
          "features": [
            { "type":"Feature",
              "geometry":{"type":"MultiLineString","coordinates":[[[500000,6700000],[500030,6700090]]]},
              "properties":{"tunniste":"P","paaraide":true,"kaupallinenNumero":"1",
                            "ratakmvalit":[],"viereisetRaiteet":[]}},
            { "type":"Feature",
              "geometry":{"type":"MultiLineString","coordinates":[[[500000,6700100],[500000,6700000]]]},
              "properties":{"tunniste":"Q","paaraide":true,"kaupallinenNumero":"1",
                            "ratakmvalit":[],"viereisetRaiteet":[]}}
          ],
          "stations": {
            "PPP": {"opOid":"op","name":"P","tracks":["P"]},
            "QQQ": {"opOid":"oq","name":"Q","tracks":["Q"]}
          }
        })";
    }

    // An out-and-back route: outbound track A (y=0) and inbound track C (y=70),
    // 70 m apart and joined by a short connector B, so the route doubles back on
    // itself. Chainage runs 0..~1000 (A), ~1070 (B), ~2070 (C). Used to check
    // that a fix near both limbs doesn't jump from the outbound to the inbound
    // limb on re-acquire (finding #4).
    static QByteArray outAndBackBlob()
    {
        return R"({
          "schemaVersion": 2,
          "features": [
            { "type":"Feature",
              "geometry":{"type":"MultiLineString","coordinates":[[[500000,6700000],[501000,6700000]]]},
              "properties":{"tunniste":"A","paaraide":true,"kaupallinenNumero":"1",
                            "ratakmvalit":[],"viereisetRaiteet":[]}},
            { "type":"Feature",
              "geometry":{"type":"MultiLineString","coordinates":[[[501000,6700000],[501000,6700070]]]},
              "properties":{"tunniste":"B","paaraide":true,"kaupallinenNumero":"1",
                            "ratakmvalit":[],"viereisetRaiteet":[]}},
            { "type":"Feature",
              "geometry":{"type":"MultiLineString","coordinates":[[[501000,6700070],[500000,6700070]]]},
              "properties":{"tunniste":"C","paaraide":true,"kaupallinenNumero":"1",
                            "ratakmvalit":[],"viereisetRaiteet":[]}}
          ],
          "stations": {
            "WST": {"opOid":"ow","name":"West","tracks":["A"]},
            "TRN": {"opOid":"ot","name":"Turn","tracks":["B"]},
            "WND": {"opOid":"oe","name":"WestEnd","tracks":["C"]}
          }
        })";
    }

    // Two parallel limbs 300 m apart joined by a connector, so the route doubles
    // back: outbound A (y=0), connector B (x=501000, y 0->300), inbound C (y=300).
    // Chainage: A 0..~1000, B ~1000..1300, C ~1300..2300. Used to pin the
    // kRouteReacquireMeters (300 m) hysteresis band (finding R5).
    static QByteArray reacquireBlob()
    {
        return R"({
          "schemaVersion": 2,
          "features": [
            { "type":"Feature",
              "geometry":{"type":"MultiLineString","coordinates":[[[500000,6700000],[501000,6700000]]]},
              "properties":{"tunniste":"A","paaraide":true,"kaupallinenNumero":"1",
                            "ratakmvalit":[],"viereisetRaiteet":[]}},
            { "type":"Feature",
              "geometry":{"type":"MultiLineString","coordinates":[[[501000,6700000],[501000,6700300]]]},
              "properties":{"tunniste":"B","paaraide":true,"kaupallinenNumero":"1",
                            "ratakmvalit":[],"viereisetRaiteet":[]}},
            { "type":"Feature",
              "geometry":{"type":"MultiLineString","coordinates":[[[501000,6700300],[500000,6700300]]]},
              "properties":{"tunniste":"C","paaraide":true,"kaupallinenNumero":"1",
                            "ratakmvalit":[],"viereisetRaiteet":[]}}
          ],
          "stations": {
            "WST": {"opOid":"ow","name":"West","tracks":["A"]},
            "TRN": {"opOid":"ot","name":"Turn","tracks":["B"]},
            "WND": {"opOid":"oe","name":"WestEnd","tracks":["C"]}
          }
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

    // Finding #1 / R6: an internal gap must never splice a chord from the pre-gap
    // track onto the post-gap one. The route truncates to the routable prefix
    // (Tier-2 up to the gap, Tier-1 beyond) rather than aborting entirely.
    void gappedRouteKeepsRoutablePrefixWithoutChord()
    {
        RailGraph g;
        QVERIFY(g.loadFromJson(gappedBlob()));

        // S0->S1->S2->S3 has a disconnected S1->S2 middle leg. The result is the
        // routable S0->S1 prefix (2 tracks) — a short connected path (~200 m),
        // never a chord spanning the ~100 km gap to the second island.
        const QVector<int> gapped =
            g.routePath({QStringLiteral("S0"), QStringLiteral("S1"),
                         QStringLiteral("S2"), QStringLiteral("S3")});
        QCOMPARE(gapped.size(), 2);
        const RailGraph::RoutePolyline rp = g.buildPolyline(gapped);
        QVERIFY(rp.isValid());
        QVERIFY2(rp.length < 1000.0,
                 qPrintable(QStringLiteral("prefix length=%1 — chord across the gap?").arg(rp.length)));
        for (int i = 1; i < rp.chainage.size(); ++i)
            QVERIFY(rp.chainage.at(i) >= rp.chainage.at(i - 1));

        // A gap in the *first* leg leaves no prefix -> unresolved (Tier-1 only).
        const QVector<int> noPrefix =
            g.routePath({QStringLiteral("S0"), QStringLiteral("S2")});
        QVERIFY2(noPrefix.isEmpty(), "first-leg gap must resolve to no path");

        // A fully-connected sub-route still resolves, so truncation isn't over-eager.
        const QVector<int> ok = g.routePath({QStringLiteral("S0"), QStringLiteral("S1")});
        QCOMPARE(ok.size(), 2);
        QVERIFY(g.buildPolyline(ok).isValid());
    }

    // Finding #2: the first track must be oriented by the shared join node, so a
    // route over an L-junction whose second track is digitised end->start does
    // not reverse the leading segment and backtrack.
    void buildPolylineOrientsReversedLJunction()
    {
        RailGraph g;
        QVERIFY(g.loadFromJson(lJunctionBlob()));
        const QVector<int> path =
            g.routePath({QStringLiteral("PPP"), QStringLiteral("QQQ")});
        QCOMPARE(path.size(), 2);

        const RailGraph::RoutePolyline rp = g.buildPolyline(path);
        QVERIFY(rp.isValid());

        const QGeoCoordinate A = tm35fin::toWgs84(500030, 6700090);  // P's far end
        const QGeoCoordinate J = tm35fin::toWgs84(500000, 6700000);  // shared join
        const QGeoCoordinate B = tm35fin::toWgs84(500000, 6700100);  // Q's far end

        // Correct orientation runs A -> J -> B with the join interior; a wrongly
        // reversed first track makes the join an endpoint and backtracks (longer).
        const double expected = A.distanceTo(J) + J.distanceTo(B);
        QVERIFY2(qAbs(rp.length - expected) < 3.0,
                 qPrintable(QStringLiteral("length=%1 expected=%2")
                                .arg(rp.length).arg(expected)));
        QVERIFY2(rp.points.first().distanceTo(A) < 3.0, "route must start at P's far end");
        QVERIFY2(rp.points.last().distanceTo(B) < 3.0, "route must end at Q's far end");
        for (int i = 1; i < rp.chainage.size(); ++i)
            QVERIFY(rp.chainage.at(i) >= rp.chainage.at(i - 1));
    }

    // Finding #4: with continuity, a fix near a self-parallel limb must stay on
    // the windowed (outbound) limb rather than back-jumping to the inbound one.
    void projectPrefersWindowedOverParallelLimb()
    {
        RailGraph g;
        QVERIFY(g.loadFromJson(outAndBackBlob()));
        const RailGraph::RoutePolyline rp = g.buildPolyline(
            g.routePath({QStringLiteral("WST"), QStringLiteral("TRN"), QStringLiteral("WND")}));
        QVERIFY(rp.isValid());

        // 130 m off the outbound limb (chainage ~200) but only ~60 m from the
        // inbound limb (chainage ~1870). prevChainage=200 puts the train on the
        // outbound leg; the projection must not jump ~1.6 km ahead.
        const QGeoCoordinate fix = tm35fin::toWgs84(500200, 6700130);
        const RailGraph::RouteProjection p = g.projectOntoRoute(rp, fix, 200.0, 0.0);
        QVERIFY(p.isValid());
        QVERIFY2(p.chainage < 700.0,
                 qPrintable(QStringLiteral("chainage=%1 (jumped to parallel limb)").arg(p.chainage)));
        QVERIFY2(p.offsetMeters > 100.0,
                 qPrintable(QStringLiteral("offset=%1").arg(p.offsetMeters)));
    }

    // Finding R5: kRouteReacquireMeters (300 m) is the hysteresis boundary for
    // trusting a windowed hit over a global re-acquire. A fix in the (150, 300]
    // band must stay windowed (continuity) even when a nearer hit exists on a
    // parallel limb; a fix beyond 300 m must re-acquire globally onto it.
    void reacquireRespectsHysteresisBand()
    {
        RailGraph g;
        QVERIFY(g.loadFromJson(reacquireBlob()));
        const RailGraph::RoutePolyline rp = g.buildPolyline(
            g.routePath({QStringLiteral("WST"), QStringLiteral("TRN"), QStringLiteral("WND")}));
        QVERIFY(rp.isValid());

        // Within the band: 250 m off the outbound limb (chainage ~300) but only
        // ~50 m from the inbound limb (chainage ~2000). prevChainage=300 windows
        // the outbound leg; 250 m <= 300 m, so the windowed hit is trusted and the
        // nearer inbound hit must NOT win.
        const QGeoCoordinate inBand = tm35fin::toWgs84(500300, 6700250);
        const RailGraph::RouteProjection pIn = g.projectOntoRoute(rp, inBand, 300.0, 0.0);
        QVERIFY(pIn.isValid());
        QVERIFY2(pIn.chainage < 700.0,
                 qPrintable(QStringLiteral("chainage=%1 (should stay on outbound limb)")
                                .arg(pIn.chainage)));
        QVERIFY2(pIn.offsetMeters > 200.0,
                 qPrintable(QStringLiteral("offset=%1 (should be the ~250 m windowed hit)")
                                .arg(pIn.offsetMeters)));

        // Beyond the band: 400 m off the outbound limb, ~100 m from the inbound
        // limb. 400 m > 300 m, so the window is abandoned and the global search
        // re-acquires onto the (nearer) inbound limb far ahead in chainage.
        const QGeoCoordinate beyond = tm35fin::toWgs84(500300, 6700400);
        const RailGraph::RouteProjection pOut = g.projectOntoRoute(rp, beyond, 300.0, 0.0);
        QVERIFY(pOut.isValid());
        QVERIFY2(pOut.chainage > 1300.0,
                 qPrintable(QStringLiteral("chainage=%1 (should re-acquire onto inbound limb)")
                                .arg(pOut.chainage)));
        QVERIFY2(pOut.offsetMeters < 150.0,
                 qPrintable(QStringLiteral("offset=%1 (should be the ~100 m inbound hit)")
                                .arg(pOut.offsetMeters)));
    }

    // Tunnel-blackout re-acquire: after the window misses (fixes dropped for
    // several km), the train can only be *ahead* on its route, so re-acquisition
    // must search forward of the last chainage, never the global 2-D nearest —
    // which here would back-jump onto the outbound limb the train already passed.
    void gapReacquireStaysForwardOfLastChainage()
    {
        RailGraph g;
        QVERIFY(g.loadFromJson(reacquireBlob()));
        const RailGraph::RoutePolyline rp = g.buildPolyline(
            g.routePath({QStringLiteral("WST"), QStringLiteral("TRN"), QStringLiteral("WND")}));
        QVERIFY(rp.isValid());

        // Train was at chainage 1400 on the inbound limb C (y=300). A blackout
        // drops fixes; it re-emerges near easting 500150 on C (chainage ~2150),
        // but the fix is pulled south to y=130 — 130 m from the outbound limb A
        // (chainage ~150, already passed) and ~170 m from its true spot on C.
        // The window [1250,1800] misses, so a global nearest would snap back to A
        // (130 < 170); the forward re-acquire must keep it on C, far ahead.
        const QGeoCoordinate fix = tm35fin::toWgs84(500150, 6700130);
        const RailGraph::RouteProjection p = g.projectOntoRoute(rp, fix, 1400.0, 0.0);
        QVERIFY(p.isValid());
        QVERIFY2(p.chainage > 1300.0,
                 qPrintable(QStringLiteral("chainage=%1 (back-jumped to the passed outbound limb)")
                                .arg(p.chainage)));
        QVERIFY2(p.offsetMeters < 250.0,
                 qPrintable(QStringLiteral("offset=%1").arg(p.offsetMeters)));
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
