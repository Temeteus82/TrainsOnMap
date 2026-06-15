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

    /// Nearest point on the network to `fix`. Returns an empty match (distance
    /// < 0) when the network isn't loaded yet or `fix` is invalid.
    virtual TrackMatch matchToNetwork(const QGeoCoordinate &fix) const = 0;
};
