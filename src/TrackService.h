#pragma once

#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QVector>
#include <QtQmlIntegration>

#include <cmath>
#include <memory>

#include "RailGraph.h"
#include "TrackListModel.h"
#include "TrackMatcher.h"

/// Provides railway track geometry to the map from a pre-baked snapshot embedded
/// in the binary (`:/data/rails.geojson.qz`, produced by `scripts/bake_rails.py`).
///
/// The rail network changes rarely, so it ships in the repo instead of being
/// fetched from the Digitraffic infra-api on every launch. The schema-v2 blob is
/// parsed into a RailGraph (geometry + identity + topology + station crosswalk)
/// once on a worker thread at startup (geometryReady fires when done). The whole
/// network is also flattened into render Segments; loadForBounds() then filters
/// them to the current viewport.
///
/// Tier 2: matchOnRoute() constrains a fix to a train's scheduled route polyline
/// (resolved off-thread by precomputeRoutes()), and platform-snaps a stopped
/// train to its booked commercialTrack.
class TrackService : public QObject, public TrackMatcher
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TrackListModel *model READ model CONSTANT)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit TrackService(QObject *parent = nullptr);

    TrackListModel *model() const { return m_model; }
    bool isLoading() const { return m_loading; }
    QString status() const { return m_status; }

    /// TrackMatcher: snap a WGS84 fix to the nearest in-memory rail segment.
    TrackMatch matchToNetwork(const QGeoCoordinate &fix,
                              double headingDeg = -1.0) const override;

    /// TrackMatcher (Tier 2): snap a fix against the train's scheduled route.
    TrackMatch matchOnRoute(const QGeoCoordinate &fix,
                            const RouteMatchRequest &req) const override;

    /// Resolve + cache route polylines for the given station sequences off the
    /// GUI thread (routes are static per run, so this runs once per refresh and
    /// dedupes identical routes). Cheap no-op until the network has loaded.
    void precomputeRoutes(const QVector<QVector<QString>> &routes);

public slots:
    /// Show only tracks intersecting the given WGS84 bounding box.
    /// Arguments follow the GeoJSON/OGC convention: west, south, east, north.
    void loadForBounds(double west, double south, double east, double north);

    /// The resolved route polyline for an ordered station sequence, as a
    /// QVariantList of QGeoCoordinate ready to bind to a MapPolyline.path. Empty
    /// until precomputeRoutes() has resolved it. Used by the debug route overlay.
    QVariantList routePolyline(const QStringList &stationCodes) const;

    /// Pin one route (the selected train's) so it's resolved and kept in the
    /// cache even after the train leaves the live fleet — otherwise eviction
    /// would drop a still-selected train's overlay/route match (R7). Pass an
    /// empty/short list to unpin.
    void pinRoute(const QStringList &stationCodes);

signals:
    void loadingChanged();
    void statusChanged();
    void geometryReady();
    void routesReady();

private:
    /// One render segment: a WGS84 polyline plus a lat/lon bbox for viewport cull.
    struct Segment {
        QVariantList path;
        bool mainTrack = false;   ///< paaraide: running line (true) vs siding (false)
        double minLat = 0.0;
        double maxLat = 0.0;
        double minLon = 0.0;
        double maxLon = 0.0;
    };

    /// Uniform spatial grid over the render segments' bounding boxes, so a
    /// viewport query touches only the overlapping cells instead of scanning the
    /// whole network. Cell size is in degrees; cells are keyed by a row-major
    /// index into a sparse hash (most of the country's bbox is empty). Each
    /// segment is registered in every cell its bbox overlaps. Built once, off the
    /// GUI thread, when the network loads.
    struct Grid {
        double cell = 0.0;                  ///< cell size, degrees (lat & lon)
        double minLat = 0.0;                ///< grid origin (south edge)
        double minLon = 0.0;                ///< grid origin (west edge)
        int cols = 0;
        int rows = 0;
        QHash<int, QVector<int>> cells;     ///< row-major cell index -> segment ids

        bool isEmpty() const { return cells.isEmpty(); }
        int colOf(double lon) const { return static_cast<int>(std::floor((lon - minLon) / cell)); }
        int rowOf(double lat) const { return static_cast<int>(std::floor((lat - minLat) / cell)); }
    };

    /// Worker-thread result: the parsed graph, the flattened render segments, and
    /// the spatial grid indexing them.
    struct Loaded {
        std::shared_ptr<RailGraph> graph;
        QVector<Segment> segments;
        Grid grid;
    };

    static Loaded loadNetwork();
    /// Resolve any not-yet-cached routes (m_pendingRoutes plus the pinned route)
    /// off-thread, evicting departed routes once a launch is actually possible.
    /// Re-entrant-safe: a no-op while a precompute is in flight (the finished
    /// handler re-runs it, eviction included) or before the graph has loaded
    /// (geometryReady re-runs it).
    void kickPrecompute();
    void setLoading(bool loading);
    void setStatus(const QString &status);

    TrackListModel *m_model = nullptr;
    QVector<Segment> m_all;                 ///< render segments (viewport cull only)
    Grid m_grid;                            ///< spatial index over m_all (by id)
    std::shared_ptr<RailGraph> m_graph;     ///< Tier-2 network; null until loaded
    QHash<QString, RailGraph::RoutePolyline> m_routePolys;  ///< routeKey -> polyline
    QVector<QString> m_pinnedRoute;         ///< selected train's route, kept from eviction (R7)
    ///< Latest requested route set (stashed so precompute can be re-driven once
    ///< the graph is ready / a busy precompute finishes, and so departed routes
    ///< can be evicted from m_routePolys).
    QVector<QVector<QString>> m_pendingRoutes;
    bool m_precomputing = false;
    bool m_loading = false;
    QString m_status;
};
