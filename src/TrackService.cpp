#include "TrackService.h"

#include "Projection.h"

#include <QByteArray>
#include <QFile>
#include <QFutureWatcher>
#include <QGeoCoordinate>
#include <QSet>
#include <QtConcurrentRun>

#include <algorithm>

namespace {
// Pre-baked, Qt-compressed national rail network embedded as a resource
// (see scripts/bake_rails.py). qUncompress reads the qCompress container.
constexpr auto kResourcePath = ":/data/rails.geojson.qz";

// Map-matching thresholds. A fix within kSnapAcceptMeters of a rail is treated
// as on-track and snapped to it; kSearchMeters bounds the candidate cull so we
// don't scan the whole network per fix.
constexpr double kSnapAcceptMeters = 150.0;
constexpr double kSearchMeters = 600.0;

// Direction-aware snapping penalty (Tier-1): a cross-cutting candidate track is
// charged this in metres, scaled by misalignment, so an aligned track wins ties.
constexpr double kHeadingPenaltyMeters = 120.0;

// A stopped train within this distance of its booked platform is snapped to it.
constexpr double kPlatformAcceptMeters = 250.0;
}

TrackService::TrackService(QObject *parent)
    : QObject(parent)
    , m_model(new TrackListModel(this))
{
    // Parse + project the network off the GUI thread so the startup cost doesn't
    // block the first frame. The result is applied back on the GUI thread.
    setLoading(true);
    setStatus(QStringLiteral("Loading rail geometry…"));

    auto *watcher = new QFutureWatcher<Loaded>(this);
    connect(watcher, &QFutureWatcher<Loaded>::finished, this, [this, watcher] {
        const Loaded loaded = watcher->result();
        watcher->deleteLater();
        m_all = loaded.segments;
        m_graph = loaded.graph;
        setLoading(false);
        setStatus(m_all.isEmpty()
                      ? QStringLiteral("Rail geometry could not be loaded")
                      : QStringLiteral("%1 track segments ready").arg(m_all.size()));
        emit geometryReady();
        // Categories (and their route sequences) can arrive before the graph has
        // finished parsing; resolve anything stashed in the meantime now (#5).
        kickPrecompute();
    });
    watcher->setFuture(QtConcurrent::run(&TrackService::loadNetwork));
}

TrackService::Loaded TrackService::loadNetwork()
{
    Loaded out;
    out.graph = std::make_shared<RailGraph>();

    QFile file(QString::fromLatin1(kResourcePath));
    if (!file.open(QIODevice::ReadOnly))
        return out;
    const QByteArray raw = qUncompress(file.readAll());
    file.close();
    if (raw.isEmpty() || !out.graph->loadFromJson(raw))
        return out;

    // Flatten the graph's tracks into render segments (bind path to MapPolyline).
    const QVector<RailGraph::Track> &tracks = out.graph->tracks();
    out.segments.reserve(tracks.size());
    for (const RailGraph::Track &t : tracks) {
        Segment seg;
        seg.path.reserve(t.path.size());
        seg.minLat = t.minLat;
        seg.maxLat = t.maxLat;
        seg.minLon = t.minLon;
        seg.maxLon = t.maxLon;
        for (const QGeoCoordinate &c : t.path)
            seg.path.append(QVariant::fromValue(c));
        if (seg.path.size() >= 2)
            out.segments.push_back(std::move(seg));
    }
    return out;
}

TrackMatch TrackService::matchToNetwork(const QGeoCoordinate &fix, double headingDeg) const
{
    TrackMatch match;   // snapped invalid, distance -1, onTrack false
    if (!fix.isValid() || !m_graph || m_graph->isEmpty())
        return match;

    // Local east/north tangent plane (metres) centred on the fix.
    const double mPerLon = tm35fin::metresPerDegLon(fix);
    const double latMargin = kSearchMeters / tm35fin::kMetresPerDegLat;
    const double lonMargin = kSearchMeters / mPerLon;

    const bool useHeading = headingDeg >= 0.0;
    const double headingRad = headingDeg * tm35fin::kPi / 180.0;
    const double hx = std::sin(headingRad);
    const double hy = std::cos(headingRad);

    double bestCost = kSearchMeters;
    double bestDist = kSearchMeters;
    double bestEast = 0.0, bestNorth = 0.0;
    bool found = false;

    // Match against the graph's plain-QGeoCoordinate tracks (with their bbox),
    // not the boxed QVariantList render segments — no per-vertex QVariant unbox
    // on this hot path, and the geometry isn't resident twice for matching.
    for (const RailGraph::Track &t : m_graph->tracks()) {
        if (fix.latitude()  < t.minLat - latMargin || fix.latitude()  > t.maxLat + latMargin
            || fix.longitude() < t.minLon - lonMargin || fix.longitude() > t.maxLon + lonMargin)
            continue;

        for (int i = 1; i < t.path.size(); ++i) {
            const tm35fin::SegmentHit h =
                tm35fin::projectToSegment(fix, mPerLon, t.path.at(i - 1), t.path.at(i));
            double cost = h.dist;
            if (useHeading && h.len2 > 0.0) {
                const double inv = 1.0 / std::sqrt(h.len2);
                const double align = std::abs(h.dirEast * inv * hx + h.dirNorth * inv * hy);
                cost += kHeadingPenaltyMeters * (1.0 - align);
            }
            if (cost < bestCost) {
                bestCost = cost;
                bestDist = h.dist;
                bestEast = h.east;
                bestNorth = h.north;
                found = true;
            }
        }
    }

    if (!found)
        return match;

    match.snapped = tm35fin::offsetToCoord(fix, mPerLon, bestEast, bestNorth);
    match.distanceMeters = bestDist;
    match.onTrack = bestDist <= kSnapAcceptMeters;
    return match;
}

TrackMatch TrackService::matchOnRoute(const QGeoCoordinate &fix, const RouteMatchRequest &req) const
{
    TrackMatch match;
    if (!m_graph || !fix.isValid())
        return match;

    // Stopped at a station: snap to the booked platform track if it's nearby.
    if (!req.platformStation.isEmpty() && !req.platformTrack.isEmpty()) {
        QString tn;
        const QGeoCoordinate p =
            m_graph->platformSnap(req.platformStation, req.platformTrack, fix, &tn);
        if (p.isValid()) {
            const double d = fix.distanceTo(p);
            if (d <= kPlatformAcceptMeters) {
                match.snapped = p;
                match.distanceMeters = d;
                match.onTrack = true;
                match.onRoute = true;
                match.tunniste = tn;
                match.chainageMeters = req.prevChainage;   // hold position while stopped
                return match;
            }
        }
    }

    const auto it = m_routePolys.constFind(RailGraph::routeKey(req.stationCodes));
    if (it == m_routePolys.constEnd())
        return match;   // route not resolved yet -> caller falls back to Tier-1

    const RailGraph::RouteProjection proj =
        m_graph->projectOntoRoute(*it, fix, req.prevChainage, req.advanceMeters);
    if (!proj.isValid())
        return match;

    match.snapped = proj.snapped;
    match.distanceMeters = proj.offsetMeters;
    match.onTrack = proj.offsetMeters <= kSnapAcceptMeters;
    match.onRoute = true;
    match.chainageMeters = proj.chainage;
    return match;
}

void TrackService::precomputeRoutes(const QVector<QVector<QString>> &routes)
{
    // Stash the latest requested set so kickPrecompute() can (re)drive against it
    // once the graph is ready and any in-flight precompute has finished.
    m_pendingRoutes = routes;
    kickPrecompute();
}

void TrackService::pinRoute(const QStringList &stationCodes)
{
    QVector<QString> codes(stationCodes.cbegin(), stationCodes.cend());
    if (codes == m_pinnedRoute)
        return;
    m_pinnedRoute = std::move(codes);
    kickPrecompute();   // resolve it now if needed, and re-evaluate eviction
}

void TrackService::kickPrecompute()
{
    if (!m_graph || m_graph->isEmpty())
        return;

    if (m_precomputing)
        return;   // a batch is in flight; its finished handler re-runs us (#6),
                  // including the eviction pass and any routes pending since.

    // The selected train's route is pinned: kept from eviction and resolved even
    // after the train drops out of the live fleet, so its overlay and Tier-2
    // match survive while it stays selected (R7).
    const QString pinnedKey =
        m_pinnedRoute.size() >= 2 ? RailGraph::routeKey(m_pinnedRoute) : QString();

    if (m_pendingRoutes.isEmpty() && pinnedKey.isEmpty())
        return;   // nothing to resolve, and no live set to evict against

    // Evict cached routes no longer in the current set (plus the pinned one) so
    // the cache can't grow unbounded as departureDates roll over (#7). Done below
    // the in-flight early-return so a 60 s resync arriving while a precompute runs
    // doesn't rescan the whole cache for nothing — the finished handler reaches
    // this on completion anyway (R2).
    QSet<QString> live;
    live.reserve(m_pendingRoutes.size() + 1);
    for (const QVector<QString> &codes : m_pendingRoutes)
        live.insert(RailGraph::routeKey(codes));
    if (!pinnedKey.isEmpty())
        live.insert(pinnedKey);
    for (auto it = m_routePolys.begin(); it != m_routePolys.end();) {
        if (live.contains(it.key()))
            ++it;
        else
            it = m_routePolys.erase(it);
    }

    // Dedupe by routeKey and skip routes already resolved — or already known
    // unresolvable (a sentinel invalid polyline, #8) — so a 60 s refresh only
    // computes genuinely new ones. The pinned route is resolved alongside them.
    QVector<QVector<QString>> todo;
    QSet<QString> seen;
    const auto consider = [&](const QVector<QString> &codes) {
        const QString key = RailGraph::routeKey(codes);
        if (m_routePolys.contains(key) || seen.contains(key))
            return;
        seen.insert(key);
        todo.append(codes);
    };
    for (const QVector<QString> &codes : m_pendingRoutes)
        consider(codes);
    if (m_pinnedRoute.size() >= 2)
        consider(m_pinnedRoute);
    if (todo.isEmpty())
        return;

    m_precomputing = true;
    auto graph = m_graph;   // shared_ptr copy: read-only access from the worker
    auto *watcher = new QFutureWatcher<QHash<QString, RailGraph::RoutePolyline>>(this);
    connect(watcher, &QFutureWatcher<QHash<QString, RailGraph::RoutePolyline>>::finished,
            this, [this, watcher] {
        const auto resolved = watcher->result();
        watcher->deleteLater();
        for (auto it = resolved.constBegin(); it != resolved.constEnd(); ++it)
            m_routePolys.insert(it.key(), it.value());
        m_precomputing = false;
        emit routesReady();
        // New routes may have arrived while we were busy; resolve them now (#6).
        kickPrecompute();
    });
    watcher->setFuture(QtConcurrent::run([graph, todo] {
        QHash<QString, RailGraph::RoutePolyline> out;
        // Cache every route key — including a sentinel invalid polyline for ones
        // that don't resolve — so unresolvable routes aren't re-Dijkstra'd on
        // every refresh (#8). routePolyline()/matchOnRoute() treat an invalid
        // entry the same as "not found".
        for (const QVector<QString> &codes : todo)
            out.insert(RailGraph::routeKey(codes),
                       graph->buildPolyline(graph->routePath(codes)));
        return out;
    }));
}

QVariantList TrackService::routePolyline(const QStringList &stationCodes) const
{
    QVariantList out;
    const auto it = m_routePolys.constFind(
        RailGraph::routeKey(QVector<QString>(stationCodes.cbegin(), stationCodes.cend())));
    if (it == m_routePolys.constEnd())
        return out;
    out.reserve(it->points.size());
    for (const QGeoCoordinate &c : it->points)
        out.append(QVariant::fromValue(c));
    return out;
}

void TrackService::loadForBounds(double west, double south, double east, double north)
{
    QVector<int> ids;
    QVector<QVariantList> paths;
    for (int i = 0; i < m_all.size(); ++i) {
        const Segment &s = m_all.at(i);
        if (s.maxLat < south || s.minLat > north || s.maxLon < west || s.minLon > east)
            continue;
        ids.push_back(i);
        paths.push_back(s.path);
    }
    m_model->setVisibleSegments(ids, paths);
    setStatus(QStringLiteral("%1 track segments").arg(ids.size()));
}

void TrackService::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void TrackService::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}
