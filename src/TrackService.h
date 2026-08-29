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

/// Result of map-matching one GPS fix against the rail network.
struct TrackMatch {
    QGeoCoordinate snapped;        ///< nearest point on the network; invalid if no match
    double distanceMeters = -1.0;  ///< metres from the input fix to `snapped`; <0 if unknown
    bool onTrack = false;          ///< distance within the snap-accept threshold

    // Tier-2 (route-constrained) extras; defaulted so Tier-1 results ignore them.
    QString tunniste;              ///< chosen track OID, for diagnostics; empty if none
    double chainageMeters = -1.0;  ///< 1-D position along the train's route; -1 if off-route
    bool onRoute = false;          ///< matched against the train's scheduled route, not just nearest
};

/// Inputs for a route-constrained match: the train's scheduled path plus the
/// continuity/platform context the matcher needs to disambiguate parallel tracks.
struct RouteMatchRequest {
    QStringList stationCodes;      ///< ordered scheduled station short codes
    double prevChainage = -1.0;    ///< last route position (m); <0 => global search
    double advanceMeters = 0.0;    ///< expected progress since the last fix (speed·Δt)
    QString platformStation;       ///< when stopped at a station, snap to its platform
    QString platformTrack;         ///< commercialTrack number at `platformStation`
};

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
class TrackService : public QObject
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

    /// Nearest point on the network to `fix`. `headingDeg` is the train's
    /// direction of travel (0 = north, clockwise) or < 0 when unknown; when
    /// supplied, a candidate track whose tangent aligns with it is preferred
    /// over a slightly nearer but cross-cutting one (so a fix near a junction
    /// snaps to the track the train is actually running on, not the one it
    /// crosses). Returns an empty match (distance < 0) when the network isn't
    /// loaded yet or `fix` is invalid.
    TrackMatch matchToNetwork(const QGeoCoordinate &fix,
                              double headingDeg = -1.0) const;

    /// Route-constrained match (Tier 2): snap `fix` to the nearest point on the
    /// train's resolved route polyline within a chainage window of its last
    /// position, or to its booked platform track when stopped at a station.
    /// Returns `onRoute == false` when the route isn't resolved yet — the caller
    /// then falls back to matchToNetwork().
    TrackMatch matchOnRoute(const QGeoCoordinate &fix,
                            const RouteMatchRequest &req) const;

    /// Resolve + cache route polylines for the given station sequences off the
    /// GUI thread (routes are static per run, so this runs once per refresh and
    /// dedupes identical routes). Cheap no-op until the network has loaded.
    void precomputeRoutes(const QVector<QStringList> &routes);

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
    /// One render segment: a lat/lon bbox for viewport cull plus the index of the
    /// graph track holding the geometry. The QML-facing QVariantList is *not*
    /// stored here — see boxedPath() for why (CPP-W14).
    struct Segment {
        int trackIndex = -1;      ///< index into m_graph->tracks(); geometry lives there
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

    /// The QML-facing boxed polyline for segment `id`, built on first request and
    /// cached.
    ///
    /// loadNetwork() used to box all 211k QGeoCoordinates across the whole country
    /// into permanently-resident QVariantLists, on top of the unboxed copy
    /// RailGraph already keeps — and QVariant cannot hold a QGeoCoordinate inline,
    /// so that is one heap allocation per vertex for geometry the viewport will
    /// mostly never ask for (CPP-W14). Now nothing is boxed until a viewport
    /// selects it.
    ///
    /// The cache is unbounded, but its ceiling is "segments the user actually
    /// panned over", which is at worst the old eager cost and in practice a small
    /// fraction of it. An LRU is the upgrade if that ever stops being true.
    const QVariantList &boxedPath(int id) const;

    TrackListModel *m_model = nullptr;
    QVector<Segment> m_all;                 ///< render segments (viewport cull only)
    mutable QHash<int, QVariantList> m_boxed;   ///< segment id -> boxed path; see boxedPath()
    Grid m_grid;                            ///< spatial index over m_all (by id)
    std::shared_ptr<RailGraph> m_graph;     ///< Tier-2 network; null until loaded
    QHash<QString, RailGraph::RoutePolyline> m_routePolys;  ///< routeKey -> polyline
    QStringList m_pinnedRoute;              ///< selected train's route, kept from eviction (R7)
    ///< Latest requested route set (stashed so precompute can be re-driven once
    ///< the graph is ready / a busy precompute finishes, and so departed routes
    ///< can be evicted from m_routePolys).
    QVector<QStringList> m_pendingRoutes;
    bool m_precomputing = false;
    bool m_loading = false;
    QString m_status;
};
