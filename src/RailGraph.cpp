#include "RailGraph.h"

#include "Projection.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace {
constexpr int kSchemaVersion = 2;

// Endpoint-node quantisation: two track ends within this grid (metres, in the
// source EPSG:3067 CRS) are treated as the same switch/join node. 10 m closes the
// small gaps between independently-digitised track ends without merging genuinely
// distinct switches. Verified to connect the major hubs (see the Tier-2 plan).
constexpr double kNodeGridMeters = 10.0;

// A siding/loop edge costs this much more than a main-track edge, so routing
// prefers running lines through a station throat.
constexpr double kSidingPenalty = 1.4;

// Route projection windowing (metres). Around the predicted chainage we search
// kWindowBack behind / kWindowFwd ahead. A windowed projection is trusted for
// continuity as long as the fix is within kRouteReacquireMeters of it; only when
// the fix is farther — i.e. the train genuinely left its predicted spot — do we
// fall back to an unconstrained global re-acquire. This stops a fix near a
// parallel/earlier limb (nearer in 2D, far in chainage) from yanking the marker
// off its booked progression onto the wrong limb.
//
// kRouteReacquireMeters (300) is deliberately 2x the caller's snap-accept
// distance (TrackService::kSnapAcceptMeters = 150): a hysteresis margin that
// keeps *windowing* across a fix that's momentarily 150–300 m off-route instead
// of re-acquiring globally. It is NOT the on-track gate — the caller re-applies
// its 150 m snap-accept to the returned hit, so a windowed hit in the (150, 300]
// band is returned here but rejected as off-track upstream, dropping that one fix
// to Tier-1. Tradeoff (R5): for a fix in that band a global search *might* have
// found a genuine <150 m on-route hit just outside the window on a sparse
// polyline; the continuity guarantee is judged worth that rare miss. The
// reacquireRespectsHysteresisBand test pins both sides of the 300 m boundary.
constexpr double kWindowBack = 150.0;
constexpr double kWindowFwd = 400.0;
constexpr double kRouteReacquireMeters = 300.0;   // 2x kSnapAcceptMeters (hysteresis)

// Pack two 32-bit grid cells into one key.
inline qint64 cellKey(double e, double n)
{
    const qint64 cx = static_cast<qint64>(std::llround(e / kNodeGridMeters));
    const qint64 cy = static_cast<qint64>(std::llround(n / kNodeGridMeters));
    return (cx << 32) ^ (cy & 0xffffffffLL);
}
}

bool RailGraph::loadFromJson(const QByteArray &json)
{
    m_tracks.clear();
    m_indexByTunniste.clear();
    m_stations.clear();
    m_adjacency.clear();
    m_schemaVersion = 0;

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    m_schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt();
    if (m_schemaVersion != kSchemaVersion)
        return false;

    const QJsonArray features = root.value(QStringLiteral("features")).toArray();
    m_tracks.reserve(features.size());

    // Endpoint-node map and the per-track viereiset (parallel) adjacency, both
    // resolved into the final graph after every track has an index.
    QHash<qint64, int> nodeByCell;
    QVector<QVector<int>> nodeTracks;
    QVector<QStringList> viereisetByTrack;
    viereisetByTrack.reserve(features.size());

    const auto nodeFor = [&](double e, double n) -> int {
        const qint64 k = cellKey(e, n);
        auto it = nodeByCell.constFind(k);
        if (it != nodeByCell.constEnd())
            return it.value();
        const int id = nodeTracks.size();
        nodeByCell.insert(k, id);
        nodeTracks.append(QVector<int>{});
        return id;
    };

    for (const QJsonValue &fv : features) {
        const QJsonObject feat = fv.toObject();
        const QJsonObject geom = feat.value(QStringLiteral("geometry")).toObject();
        const QString gtype = geom.value(QStringLiteral("type")).toString();
        const QJsonArray coords = geom.value(QStringLiteral("coordinates")).toArray();

        // Flatten LineString / MultiLineString to one ordered polyline, keeping
        // the raw first/last EPSG points for endpoint-node bucketing.
        Track t;
        t.minLat = t.minLon = 1e9;
        t.maxLat = t.maxLon = -1e9;
        double firstE = 0, firstN = 0, lastE = 0, lastN = 0;
        bool haveFirst = false;
        const auto addPart = [&](const QJsonArray &part) {
            for (const QJsonValue &cv : part) {
                const QJsonArray p = cv.toArray();
                if (p.size() < 2)
                    continue;
                const double e = p.at(0).toDouble();
                const double n = p.at(1).toDouble();
                const QGeoCoordinate c = tm35fin::toWgs84(e, n);
                if (!c.isValid())
                    continue;
                t.path.append(c);
                t.minLat = (std::min)(t.minLat, c.latitude());
                t.maxLat = (std::max)(t.maxLat, c.latitude());
                t.minLon = (std::min)(t.minLon, c.longitude());
                t.maxLon = (std::max)(t.maxLon, c.longitude());
                if (!haveFirst) { firstE = e; firstN = n; haveFirst = true; }
                lastE = e; lastN = n;
            }
        };
        if (gtype == QLatin1String("LineString")) {
            addPart(coords);
        } else if (gtype == QLatin1String("MultiLineString")) {
            for (const QJsonValue &pv : coords)
                addPart(pv.toArray());
        }
        if (t.path.size() < 2)
            continue;

        for (int i = 1; i < t.path.size(); ++i)
            t.lengthMeters += t.path.at(i - 1).distanceTo(t.path.at(i));

        const QJsonObject props = feat.value(QStringLiteral("properties")).toObject();
        t.tunniste = props.value(QStringLiteral("tunniste")).toString();
        t.paaraide = props.value(QStringLiteral("paaraide")).toBool();
        t.kaupallinenNumero = props.value(QStringLiteral("kaupallinenNumero")).toString();
        // Endpoint nodes are only needed to build the adjacency below, so they
        // stay function-local rather than living on every Track for the run.
        const int nodeA = nodeFor(firstE, firstN);
        const int nodeB = nodeFor(lastE, lastN);

        const int idx = m_tracks.size();
        if (!t.tunniste.isEmpty())
            m_indexByTunniste.insert(t.tunniste, idx);
        nodeTracks[nodeA].append(idx);
        if (nodeB != nodeA)
            nodeTracks[nodeB].append(idx);

        QStringList vier;
        for (const QJsonValue &vv : props.value(QStringLiteral("viereisetRaiteet")).toArray())
            vier.append(vv.toString());
        viereisetByTrack.append(vier);

        m_tracks.append(std::move(t));
    }

    // Build the routing adjacency: tracks meeting at a shared endpoint node are
    // connected, and viereisetRaiteet adds parallel/adjacent edges (needed because
    // seuraavatRaiteet is empty in the source data).
    QVector<QSet<int>> adj(m_tracks.size());
    for (const QVector<int> &ts : nodeTracks) {
        for (int a : ts)
            for (int b : ts)
                if (a != b)
                    adj[a].insert(b);
    }
    for (int i = 0; i < m_tracks.size(); ++i) {
        for (const QString &oid : viereisetByTrack.at(i)) {
            const auto it = m_indexByTunniste.constFind(oid);
            if (it != m_indexByTunniste.constEnd() && it.value() != i) {
                adj[i].insert(it.value());
                adj[it.value()].insert(i);
            }
        }
    }
    m_adjacency.resize(m_tracks.size());
    for (int i = 0; i < m_tracks.size(); ++i)
        m_adjacency[i] = QVector<int>(adj.at(i).cbegin(), adj.at(i).cend());

    // Station crosswalk: resolve each station's member track OIDs to indices.
    const QJsonObject stationsObj = root.value(QStringLiteral("stations")).toObject();
    for (auto it = stationsObj.constBegin(); it != stationsObj.constEnd(); ++it) {
        const QJsonObject so = it.value().toObject();
        Station st;
        st.name = so.value(QStringLiteral("name")).toString();
        for (const QJsonValue &tv : so.value(QStringLiteral("tracks")).toArray()) {
            const auto ti = m_indexByTunniste.constFind(tv.toString());
            if (ti != m_indexByTunniste.constEnd())
                st.tracks.append(ti.value());
        }
        m_stations.insert(it.key(), st);
    }

    return !m_tracks.isEmpty();
}

QString RailGraph::routeKey(const QStringList &stationCodes)
{
    return stationCodes.join(QLatin1Char('|'));
}

QStringList RailGraph::canonicalRouteCodes(const QStringList &rawCodes)
{
    QStringList out;
    out.reserve(rawCodes.size());
    for (const QString &code : rawCodes) {
        if (code.isEmpty())
            continue;
        if (out.isEmpty() || out.last() != code)
            out.push_back(code);
    }
    return out;
}

QVector<int> RailGraph::routePath(const QStringList &stationCodes) const
{
    // Dijkstra over the track graph from any track in `sources` to the nearest
    // track in `targets`. Returns the ordered track path (inclusive) and the
    // arrival track via `arrival`.
    const auto dijkstra = [this](const QVector<int> &sources, const QSet<int> &targets,
                                 int &arrival) -> QVector<int> {
        arrival = -1;
        if (sources.isEmpty() || targets.isEmpty())
            return {};
        const int n = m_tracks.size();
        QVector<double> dist(n, std::numeric_limits<double>::infinity());
        QVector<int> prev(n, -1);
        using Node = std::pair<double, int>;
        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;
        for (int s : sources) {
            if (s >= 0 && s < n && dist[s] > 0.0) {
                dist[s] = 0.0;
                pq.push({0.0, s});
            }
        }
        while (!pq.empty()) {
            const auto [d, u] = pq.top();
            pq.pop();
            if (d > dist[u])
                continue;
            if (targets.contains(u)) {
                arrival = u;
                break;
            }
            for (int v : m_adjacency.at(u)) {
                const double w = m_tracks.at(v).lengthMeters
                                 * (m_tracks.at(v).paaraide ? 1.0 : kSidingPenalty);
                if (dist[u] + w < dist[v]) {
                    dist[v] = dist[u] + w;
                    prev[v] = u;
                    pq.push({dist[v], v});
                }
            }
        }
        if (arrival < 0)
            return {};
        QVector<int> path;
        for (int c = arrival; c >= 0; c = prev[c])
            path.append(c);
        std::reverse(path.begin(), path.end());
        return path;
    };

    // The ordered station sets that actually resolve to member tracks.
    QVector<QVector<int>> sets;
    for (const QString &code : stationCodes) {
        const auto it = m_stations.constFind(code);
        if (it != m_stations.constEnd() && !it->tracks.isEmpty())
            sets.append(it->tracks);
    }
    if (sets.size() < 2)
        return {};

    QVector<int> full;
    int arrival = -1;
    full = dijkstra(sets.at(0), QSet<int>(sets.at(1).cbegin(), sets.at(1).cend()), arrival);
    if (full.isEmpty())
        return {};                          // first leg unroutable -> route unresolved
    for (int k = 2; k < sets.size(); ++k) {
        // Each leg continues from the previous arrival track. If a leg is
        // unroutable we must NOT skip it and stitch the next one on: that would
        // join two graph-non-adjacent tracks and draw a multi-km chord across the
        // gap (inflating all chainage past it). Instead stop at the gap and keep
        // the routable *prefix* (R6): `full` so far is one connected path (still no
        // chord), so the train gets route-constrained Tier-2 up to the gap and
        // falls back to Tier-1 beyond it — strictly better than dropping the whole
        // journey to Tier-1. (A gap in the very first leg leaves no prefix, handled
        // by the empty-`full` early return above.)
        const QVector<int> leg =
            dijkstra(QVector<int>{arrival}, QSet<int>(sets.at(k).cbegin(), sets.at(k).cend()), arrival);
        if (leg.isEmpty())
            break;
        for (int j = 1; j < leg.size(); ++j)
            full.append(leg.at(j));
    }
    // Collapse any accidental consecutive repeats.
    QVector<int> out;
    out.reserve(full.size());
    for (int t : full)
        if (out.isEmpty() || out.last() != t)
            out.append(t);
    return out;
}

RailGraph::RoutePolyline RailGraph::buildPolyline(const QVector<int> &trackPath) const
{
    RoutePolyline rp;
    if (trackPath.isEmpty())
        return rp;

    for (int k = 0; k < trackPath.size(); ++k) {
        const Track &t = m_tracks.at(trackPath.at(k));
        QVector<QGeoCoordinate> seg = t.path;

        // Orient each track so it continues from the polyline's current end.
        if (rp.points.isEmpty()) {
            if (trackPath.size() > 1) {
                // Orient the first track so its shared join with track[1] ends up
                // last. track[1]'s digitisation direction is arbitrary, so compare
                // each of this track's ends to the *nearer* of track[1]'s two ends
                // (the join), not to track[1].first() — otherwise a non-colinear
                // junction can reverse the leading segment the wrong way.
                const Track &next = m_tracks.at(trackPath.at(1));
                const double firstToJoin = (std::min)(seg.first().distanceTo(next.path.first()),
                                                      seg.first().distanceTo(next.path.last()));
                const double lastToJoin = (std::min)(seg.last().distanceTo(next.path.first()),
                                                     seg.last().distanceTo(next.path.last()));
                if (firstToJoin < lastToJoin)
                    std::reverse(seg.begin(), seg.end());
            }
        } else {
            const QGeoCoordinate last = rp.points.last();
            if (seg.last().distanceTo(last) < seg.first().distanceTo(last))
                std::reverse(seg.begin(), seg.end());
        }

        for (const QGeoCoordinate &c : seg) {
            if (!rp.points.isEmpty()) {
                const double step = rp.points.last().distanceTo(c);
                if (step < 0.5)            // drop duplicate join points
                    continue;
                rp.length += step;
            }
            rp.points.append(c);
            rp.chainage.append(rp.length);
        }
    }
    return rp;
}

RailGraph::RouteProjection RailGraph::projectOntoRoute(const RoutePolyline &rp,
                                                       const QGeoCoordinate &fix,
                                                       double prevChainage,
                                                       double advanceMeters) const
{
    RouteProjection result;
    if (!rp.isValid() || !fix.isValid())
        return result;

    const double mPerLon = tm35fin::metresPerDegLon(fix);

    // Search a chainage window [lo, hi]; an empty window (lo<0, hi=inf) scans all.
    const auto search = [&](double lo, double hi) -> RouteProjection {
        RouteProjection best;
        double bestDist = std::numeric_limits<double>::infinity();
        for (int i = 1; i < rp.points.size(); ++i) {
            if (rp.chainage.at(i) < lo || rp.chainage.at(i - 1) > hi)
                continue;
            const tm35fin::SegmentHit h =
                tm35fin::projectToSegment(fix, mPerLon, rp.points.at(i - 1), rp.points.at(i));
            if (h.dist < bestDist) {
                bestDist = h.dist;
                best.offsetMeters = h.dist;
                best.snapped = tm35fin::offsetToCoord(fix, mPerLon, h.east, h.north);
                const double segLen = rp.chainage.at(i) - rp.chainage.at(i - 1);
                best.chainage = rp.chainage.at(i - 1) + h.t * segLen;
            }
        }
        return best;
    };

    if (prevChainage >= 0.0) {
        const double centre = prevChainage + advanceMeters;
        const RouteProjection windowed = search(prevChainage - kWindowBack, centre + kWindowFwd);
        // Trust the windowed (continuity-preserving) hit unless the fix is far
        // enough from it that the train has clearly left the window. The forward
        // re-acquire below (not a global 2-D nearest) handles the miss; the
        // caller's onTrack gate still decides whether to keep a windowed hit just
        // over the snap distance or fall to Tier-1.
        if (windowed.isValid() && windowed.offsetMeters <= kRouteReacquireMeters)
            return windowed;
        // Re-acquire after the window misses (e.g. a tunnel GPS blackout dropped
        // fixes for several km, so speed·Δt under-estimated the gap): a train can
        // only have moved *forward* along its booked route, so re-acquire over the
        // route ahead of the last chainage instead of an unconstrained global
        // search. The global scan could otherwise snap to a point behind the train
        // or to a parallel limb it already passed (nearer in 2-D, kilometres away
        // in chainage) — the backward "jump onto the wrong track" seen at tunnels.
        const RouteProjection forward =
            search(prevChainage - kWindowBack, std::numeric_limits<double>::infinity());
        if (forward.isValid())
            return forward;
    }
    return search(-1.0, std::numeric_limits<double>::infinity());
}

QGeoCoordinate RailGraph::nearestOnTrack(int trackIndex, const QGeoCoordinate &fix,
                                         double &outDist) const
{
    outDist = std::numeric_limits<double>::infinity();
    QGeoCoordinate snapped;
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !fix.isValid())
        return snapped;
    const Track &t = m_tracks.at(trackIndex);
    const double mPerLon = tm35fin::metresPerDegLon(fix);
    for (int i = 1; i < t.path.size(); ++i) {
        const tm35fin::SegmentHit h =
            tm35fin::projectToSegment(fix, mPerLon, t.path.at(i - 1), t.path.at(i));
        if (h.dist < outDist) {
            outDist = h.dist;
            snapped = tm35fin::offsetToCoord(fix, mPerLon, h.east, h.north);
        }
    }
    return snapped;
}

QGeoCoordinate RailGraph::platformSnap(const QString &stationCode, const QString &commercialTrack,
                                       const QGeoCoordinate &fix, QString *chosenTunniste) const
{
    QGeoCoordinate best;
    if (commercialTrack.isEmpty())
        return best;
    const auto it = m_stations.constFind(stationCode);
    if (it == m_stations.constEnd())
        return best;

    double bestDist = std::numeric_limits<double>::infinity();
    for (int ti : it->tracks) {
        if (m_tracks.at(ti).kaupallinenNumero != commercialTrack)
            continue;
        double d = 0.0;
        const QGeoCoordinate snapped = nearestOnTrack(ti, fix, d);
        if (snapped.isValid() && d < bestDist) {
            bestDist = d;
            best = snapped;
            if (chosenTunniste)
                *chosenTunniste = m_tracks.at(ti).tunniste;
        }
    }
    return best;
}
