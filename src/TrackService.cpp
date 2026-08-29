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
        m_grid = loaded.grid;
        m_graph = loaded.graph;
        setLoading(false);
        // On success, clear status rather than reporting the total network size:
        // the sidebar's live "N track segments" label already shows the current
        // viewport count, and a permanent "ready" message would otherwise block
        // the statusText fallback to the live train-fetch status forever.
        setStatus(m_all.isEmpty() ? QStringLiteral("Rail geometry could not be loaded")
                                  : QString());
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
        seg.mainTrack = t.paaraide;
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

    // Build the spatial grid over the segment bboxes so loadForBounds() can query
    // by viewport without scanning the whole network. Cell ~0.1 deg (~11 km in
    // latitude) is a broadphase only — the exact bbox test still runs per hit, so
    // the cell size trades cell-lookup count against false positives, not result
    // correctness.
    if (!out.segments.isEmpty()) {
        double minLat = 90.0, minLon = 180.0, maxLat = -90.0, maxLon = -180.0;
        for (const Segment &s : out.segments) {
            minLat = std::min(minLat, s.minLat);
            minLon = std::min(minLon, s.minLon);
            maxLat = std::max(maxLat, s.maxLat);
            maxLon = std::max(maxLon, s.maxLon);
        }
        Grid &g = out.grid;
        g.cell = 0.1;
        g.minLat = minLat;
        g.minLon = minLon;
        g.cols = std::max(1, static_cast<int>((maxLon - minLon) / g.cell) + 1);
        g.rows = std::max(1, static_cast<int>((maxLat - minLat) / g.cell) + 1);
        for (int i = 0; i < out.segments.size(); ++i) {
            const Segment &s = out.segments.at(i);
            const int c0 = std::clamp(g.colOf(s.minLon), 0, g.cols - 1);
            const int c1 = std::clamp(g.colOf(s.maxLon), 0, g.cols - 1);
            const int r0 = std::clamp(g.rowOf(s.minLat), 0, g.rows - 1);
            const int r1 = std::clamp(g.rowOf(s.maxLat), 0, g.rows - 1);
            for (int ry = r0; ry <= r1; ++ry)
                for (int cx = c0; cx <= c1; ++cx)
                    g.cells[ry * g.cols + cx].push_back(i);
        }
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

void TrackService::precomputeRoutes(const QVector<QStringList> &routes)
{
    // Stash the latest requested set so kickPrecompute() can (re)drive against it
    // once the graph is ready and any in-flight precompute has finished.
    m_pendingRoutes = routes;
    kickPrecompute();
}

void TrackService::pinRoute(const QStringList &stationCodes)
{
    if (stationCodes == m_pinnedRoute)
        return;
    m_pinnedRoute = stationCodes;
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
    const bool havePinned = m_pinnedRoute.size() >= 2;
    if (m_pendingRoutes.isEmpty() && !havePinned)
        return;   // nothing to resolve, and no live set to evict against

    // One pass over the routes, keying each exactly once (I8): the key feeds both
    // the eviction set and the dedupe below. It used to be built twice per route
    // per pass — a join over the full station sequence — across a set that
    // handleCategories refills with the whole live fleet every 60 s.
    //
    // `live` drives eviction of cached routes no longer in the current set (plus
    // the pinned one) so the cache can't grow unbounded as departureDates roll
    // over (#7). `todo` skips routes already resolved — or already known
    // unresolvable (a sentinel invalid polyline, #8) — so a refresh only computes
    // genuinely new ones. The pinned route is resolved alongside them.
    QSet<QString> live;
    live.reserve(m_pendingRoutes.size() + 1);
    QVector<QStringList> todo;
    const auto consider = [&](const QStringList &codes) {
        const QString key = RailGraph::routeKey(codes);
        if (live.contains(key))
            return;   // same route twice in one pass: keyed and queued already
        live.insert(key);
        if (!m_routePolys.contains(key))
            todo.append(codes);
    };
    for (const QStringList &codes : m_pendingRoutes)
        consider(codes);
    if (havePinned)
        consider(m_pinnedRoute);

    // Eviction runs after `live` is complete. Order against `todo` is immaterial:
    // any key `consider` found in the cache is by construction in `live`, so it
    // is never the one erased here.
    for (auto it = m_routePolys.begin(); it != m_routePolys.end();) {
        if (live.contains(it.key()))
            ++it;
        else
            it = m_routePolys.erase(it);
    }

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
        for (const QStringList &codes : todo)
            out.insert(RailGraph::routeKey(codes),
                       graph->buildPolyline(graph->routePath(codes)));
        return out;
    }));
}

QVariantList TrackService::routePolyline(const QStringList &stationCodes) const
{
    QVariantList out;
    const auto it = m_routePolys.constFind(RailGraph::routeKey(stationCodes));
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
    if (!m_grid.isEmpty()) {
        // Broadphase: gather segment ids from the grid cells the viewport rect
        // overlaps, then apply the exact bbox test. A segment straddling cells
        // can be gathered more than once, so sort + unique afterwards; that also
        // restores the ascending-id order setVisibleSegments() requires for its
        // incremental diff.
        const int c0 = std::clamp(m_grid.colOf(west),  0, m_grid.cols - 1);
        const int c1 = std::clamp(m_grid.colOf(east),  0, m_grid.cols - 1);
        const int r0 = std::clamp(m_grid.rowOf(south), 0, m_grid.rows - 1);
        const int r1 = std::clamp(m_grid.rowOf(north), 0, m_grid.rows - 1);
        for (int ry = r0; ry <= r1; ++ry) {
            for (int cx = c0; cx <= c1; ++cx) {
                const auto it = m_grid.cells.constFind(ry * m_grid.cols + cx);
                if (it == m_grid.cells.constEnd())
                    continue;
                for (int id : *it) {
                    const Segment &s = m_all.at(id);
                    if (s.maxLat < south || s.minLat > north
                        || s.maxLon < west || s.minLon > east)
                        continue;
                    ids.push_back(id);
                }
            }
        }
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    } else {
        // No grid (network not loaded yet): fall back to the linear scan.
        for (int i = 0; i < m_all.size(); ++i) {
            const Segment &s = m_all.at(i);
            if (s.maxLat < south || s.minLat > north || s.maxLon < west || s.minLon > east)
                continue;
            ids.push_back(i);
        }
    }

    QVector<QVariantList> paths;
    QVector<bool> mains;
    paths.reserve(ids.size());
    mains.reserve(ids.size());
    for (int id : ids) {
        paths.push_back(m_all.at(id).path);
        mains.push_back(m_all.at(id).mainTrack);
    }

    m_model->setVisibleSegments(ids, paths, mains);
    // Not setStatus() here: the sidebar already shows this figure live via the
    // dedicated track-count label (model.count), so repeating it in status on
    // every viewport pan just duplicated it (UI audit).
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
