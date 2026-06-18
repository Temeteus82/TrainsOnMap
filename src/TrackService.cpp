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
constexpr double kMetresPerDegLat = 111320.0;   // ~constant; lon scaled by cos(lat)

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
    if (!fix.isValid() || m_all.isEmpty())
        return match;

    // Local east/north tangent plane (metres) centred on the fix.
    const double cosLat = std::cos(fix.latitude() * tm35fin::kPi / 180.0);
    const double mPerLon = kMetresPerDegLat * (std::max)(0.05, cosLat);
    const double latMargin = kSearchMeters / kMetresPerDegLat;
    const double lonMargin = kSearchMeters / mPerLon;

    const bool useHeading = headingDeg >= 0.0;
    const double headingRad = headingDeg * tm35fin::kPi / 180.0;
    const double hx = std::sin(headingRad);
    const double hy = std::cos(headingRad);

    double bestCost = kSearchMeters;
    double bestDist = kSearchMeters;
    double bestX = 0.0, bestY = 0.0;
    bool found = false;

    for (const Segment &s : m_all) {
        if (fix.latitude()  < s.minLat - latMargin || fix.latitude()  > s.maxLat + latMargin
            || fix.longitude() < s.minLon - lonMargin || fix.longitude() > s.maxLon + lonMargin)
            continue;

        bool havePrev = false;
        double ax = 0.0, ay = 0.0;
        for (const QVariant &pv : s.path) {
            const QGeoCoordinate c = pv.value<QGeoCoordinate>();
            const double bx = (c.longitude() - fix.longitude()) * mPerLon;
            const double by = (c.latitude() - fix.latitude()) * kMetresPerDegLat;
            if (havePrev) {
                const double dx = bx - ax, dy = by - ay;
                const double len2 = dx * dx + dy * dy;
                double t = len2 > 0.0 ? -(ax * dx + ay * dy) / len2 : 0.0;
                t = (std::clamp)(t, 0.0, 1.0);
                const double cx = ax + t * dx, cy = ay + t * dy;
                const double dist = std::sqrt(cx * cx + cy * cy);

                double cost = dist;
                if (useHeading && len2 > 0.0) {
                    const double inv = 1.0 / std::sqrt(len2);
                    const double align = std::abs(dx * inv * hx + dy * inv * hy);
                    cost += kHeadingPenaltyMeters * (1.0 - align);
                }
                if (cost < bestCost) {
                    bestCost = cost;
                    bestDist = dist;
                    bestX = cx;
                    bestY = cy;
                    found = true;
                }
            }
            ax = bx;
            ay = by;
            havePrev = true;
        }
    }

    if (!found)
        return match;

    match.snapped = QGeoCoordinate(fix.latitude() + bestY / kMetresPerDegLat,
                                   fix.longitude() + bestX / mPerLon);
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
    if (!m_graph || m_graph->isEmpty() || m_precomputing || routes.isEmpty())
        return;

    // Dedupe by routeKey and skip routes already resolved, so a 60 s refresh only
    // computes genuinely new ones.
    QVector<QVector<QString>> todo;
    QSet<QString> seen;
    for (const QVector<QString> &codes : routes) {
        const QString key = RailGraph::routeKey(codes);
        if (m_routePolys.contains(key) || seen.contains(key))
            continue;
        seen.insert(key);
        todo.append(codes);
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
    });
    watcher->setFuture(QtConcurrent::run([graph, todo] {
        QHash<QString, RailGraph::RoutePolyline> out;
        for (const QVector<QString> &codes : todo) {
            const RailGraph::RoutePolyline poly = graph->buildPolyline(graph->routePath(codes));
            if (poly.isValid())
                out.insert(RailGraph::routeKey(codes), poly);
        }
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
