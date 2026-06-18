#pragma once

#include <QGeoCoordinate>
#include <QString>
#include <QVector>

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
    QVector<QString> stationCodes; ///< ordered scheduled station short codes
    double prevChainage = -1.0;    ///< last route position (m); <0 => global search
    double advanceMeters = 0.0;    ///< expected progress since the last fix (speed·Δt)
    QString platformStation;       ///< when stopped at a station, snap to its platform
    QString platformTrack;         ///< commercialTrack number at `platformStation`
};

/// Snaps a WGS84 fix to the nearest rail segment. Implemented by TrackService,
/// which holds the projected national network; consumed by TrainListModel to
/// correct/flag GPS positions. Kept as a tiny interface so the model needn't
/// depend on the track-geometry classes (only QGeoCoordinate).
class TrackMatcher
{
public:
    virtual ~TrackMatcher() = default;

    /// Nearest point on the network to `fix`. `headingDeg` is the train's
    /// direction of travel (0 = north, clockwise) or < 0 when unknown; when
    /// supplied, a candidate track whose tangent aligns with it is preferred
    /// over a slightly nearer but cross-cutting one (so a fix near a junction
    /// snaps to the track the train is actually running on, not the one it
    /// crosses). Returns an empty match (distance < 0) when the network isn't
    /// loaded yet or `fix` is invalid.
    virtual TrackMatch matchToNetwork(const QGeoCoordinate &fix,
                                      double headingDeg = -1.0) const = 0;

    /// Route-constrained match (Tier 2): snap `fix` to the nearest point on the
    /// train's resolved route polyline within a chainage window of its last
    /// position, or to its booked platform track when stopped at a station.
    /// Returns `onRoute == false` when the route isn't resolved yet — the caller
    /// then falls back to matchToNetwork(). Default returns an empty (off-route)
    /// match so a matcher needn't implement it.
    virtual TrackMatch matchOnRoute(const QGeoCoordinate & /*fix*/,
                                    const RouteMatchRequest & /*req*/) const { return {}; }
};
