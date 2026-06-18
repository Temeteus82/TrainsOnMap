#pragma once

#include <QGeoCoordinate>

/// Result of map-matching one GPS fix against the rail network.
struct TrackMatch {
    QGeoCoordinate snapped;        ///< nearest point on the network; invalid if no match
    double distanceMeters = -1.0;  ///< metres from the input fix to `snapped`; <0 if unknown
    bool onTrack = false;          ///< distance within the snap-accept threshold
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
};
