#pragma once

#include <QGeoCoordinate>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

/// The Tier-2 rail network: track geometry **plus** identity, topology and a
/// station crosswalk, parsed from the schema-v2 baked blob (scripts/bake_rails.py).
///
/// This is the pure, Qt-Core-only core behind TrackService — no QObject, no
/// resource/threading concerns — so the routing and projection maths can be unit
/// tested directly (see tests/tst_railgraph.cpp).
///
/// Because the infra-api's `seuraavatRaiteet` is empty in practice (see the
/// Tier-2 plan), the routing graph is **reconstructed from geometry**: two tracks
/// are connected when they share a quantised endpoint node (a switch/join), with
/// `viereisetRaiteet` adding parallel/adjacent edges. A train's scheduled station
/// sequence is then resolved to an ordered track path by Dijkstra over that graph,
/// and map-matching is constrained to a moving window along the resulting
/// chainage-parameterised polyline.
class RailGraph
{
public:
    struct Track {
        QString tunniste;             ///< stable track OID
        bool paaraide = false;        ///< main-track flag (running lines preferred)
        QString kaupallinenNumero;    ///< platform / commercial track number
        QVector<QGeoCoordinate> path; ///< centreline, projected to WGS84
        double lengthMeters = 0.0;
        double minLat = 0, maxLat = 0, minLon = 0, maxLon = 0;
        // Endpoint-node ids live only as locals in loadFromJson (adjacency build).
    };

    struct Station {
        QString name;
        QVector<int> tracks;          ///< indices into the track vector
    };

    /// A route resolved to an ordered, chainage-parameterised polyline. `chainage`
    /// is the cumulative along-route distance (metres) of each point in `points`.
    struct RoutePolyline {
        QVector<QGeoCoordinate> points;
        QVector<double> chainage;
        double length = 0.0;
        bool isValid() const { return points.size() >= 2; }
    };

    /// Result of projecting a raw fix onto a route polyline.
    struct RouteProjection {
        QGeoCoordinate snapped;
        double offsetMeters = -1.0;   ///< distance from the fix to the route
        double chainage = -1.0;       ///< along-route position of `snapped`
        bool isValid() const { return snapped.isValid(); }
    };

    bool isEmpty() const { return m_tracks.isEmpty(); }
    int trackCount() const { return m_tracks.size(); }
    int schemaVersion() const { return m_schemaVersion; }

    /// Parse an **uncompressed** schema-v2 JSON document (the bytes after
    /// qUncompress). Returns false for an empty/old/non-v2 blob.
    bool loadFromJson(const QByteArray &json);

    /// Ordered track indices forming a connected path through `stationCodes`
    /// (consecutive station pairs joined by Dijkstra over the derived graph).
    /// Empty when fewer than two stations resolve, or when any consecutive pair
    /// is unroutable — an internal gap aborts the whole route rather than
    /// stitching a chord across it (the caller then falls back to Tier-1).
    QVector<int> routePath(const QVector<QString> &stationCodes) const;

    /// Build a chainage-parameterised polyline from an ordered track-index path.
    RoutePolyline buildPolyline(const QVector<int> &trackPath) const;

    /// Project `fix` onto `rp`. With `prevChainage >= 0` the search is limited to
    /// a window around `prevChainage + advanceMeters` (continuity/hysteresis: the
    /// marker can't jump back to a parallel earlier section); otherwise the whole
    /// route is searched. `offsetMeters` is the true geometric distance.
    RouteProjection projectOntoRoute(const RoutePolyline &rp, const QGeoCoordinate &fix,
                                     double prevChainage, double advanceMeters) const;

    /// Nearest point on a station's platform track — the member track whose
    /// `kaupallinenNumero` equals `commercialTrack`. Invalid coordinate if the
    /// station or platform number is unknown. Returns the chosen track id via
    /// `chosenTunniste` when supplied.
    QGeoCoordinate platformSnap(const QString &stationCode, const QString &commercialTrack,
                                const QGeoCoordinate &fix, QString *chosenTunniste = nullptr) const;

    /// Stable cache key for an ordered station sequence (identical routes share
    /// one resolved polyline).
    static QString routeKey(const QVector<QString> &stationCodes);

    const QHash<QString, Station> &stations() const { return m_stations; }
    const QVector<Track> &tracks() const { return m_tracks; }

private:
    /// Nearest point on one track's polyline to `fix`, in a local tangent plane.
    /// Returns the point and sets `outDist` to the geometric distance (metres).
    QGeoCoordinate nearestOnTrack(int trackIndex, const QGeoCoordinate &fix,
                                  double &outDist) const;

    int m_schemaVersion = 0;
    QVector<Track> m_tracks;
    QHash<QString, int> m_indexByTunniste;
    QHash<QString, Station> m_stations;     ///< stationShortCode -> station
    QVector<QVector<int>> m_adjacency;      ///< per-track connected track indices
};
