#pragma once

#include <QGeoCoordinate>

#include <algorithm>
#include <cmath>

/// Conversion from EPSG:3067 (ETRS-TM35FIN, the CRS the Digitraffic infra-api
/// returns) to WGS84 lon/lat (what the map layer expects).
///
/// ETRS-TM35FIN is a Transverse Mercator projection on the GRS80 ellipsoid.
/// ETRS89 and WGS84 differ by only a few centimetres here, so the result is
/// used directly as WGS84. Formulas: Snyder inverse/forward Transverse Mercator
/// (sub-metre accurate across Finland).
namespace tm35fin {

inline constexpr double kPi = 3.14159265358979323846;

// EPSG:3067 parameters.
inline constexpr double a    = 6378137.0;            // GRS80 semi-major axis
inline constexpr double f    = 1.0 / 298.257222101;  // GRS80 flattening
inline constexpr double k0   = 0.9996;               // scale factor
inline constexpr double lon0 = 27.0 * kPi / 180.0;   // central meridian (27°E)
inline constexpr double FE   = 500000.0;             // false easting
inline constexpr double FN   = 0.0;                  // false northing

/// EPSG:3067 easting/northing (metres) -> WGS84 coordinate.
inline QGeoCoordinate toWgs84(double E, double N)
{
    const double e2  = f * (2.0 - f);
    const double ep2 = e2 / (1.0 - e2);

    const double M  = (N - FN) / k0;
    const double mu = M / (a * (1 - e2 / 4 - 3 * e2 * e2 / 64 - 5 * e2 * e2 * e2 / 256));
    const double e1 = (1 - std::sqrt(1 - e2)) / (1 + std::sqrt(1 - e2));

    const double phi1 = mu
        + (3 * e1 / 2 - 27 * e1 * e1 * e1 / 32) * std::sin(2 * mu)
        + (21 * e1 * e1 / 16 - 55 * e1 * e1 * e1 * e1 / 32) * std::sin(4 * mu)
        + (151 * e1 * e1 * e1 / 96) * std::sin(6 * mu)
        + (1097 * e1 * e1 * e1 * e1 / 512) * std::sin(8 * mu);

    const double sinp = std::sin(phi1);
    const double cosp = std::cos(phi1);
    const double tanp = std::tan(phi1);

    const double C1 = ep2 * cosp * cosp;
    const double T1 = tanp * tanp;
    const double N1 = a / std::sqrt(1 - e2 * sinp * sinp);
    const double R1 = a * (1 - e2) / std::pow(1 - e2 * sinp * sinp, 1.5);
    const double D  = (E - FE) / (N1 * k0);

    const double lat = phi1
        - (N1 * tanp / R1)
            * (D * D / 2
               - (5 + 3 * T1 + 10 * C1 - 4 * C1 * C1 - 9 * ep2) * std::pow(D, 4) / 24
               + (61 + 90 * T1 + 298 * C1 + 45 * T1 * T1 - 252 * ep2 - 3 * C1 * C1)
                     * std::pow(D, 6) / 720);

    const double lon = lon0
        + (D
           - (1 + 2 * T1 + C1) * std::pow(D, 3) / 6
           + (5 - 2 * C1 + 28 * T1 - 3 * C1 * C1 + 8 * ep2 + 24 * T1 * T1)
                 * std::pow(D, 5) / 120)
              / cosp;

    return QGeoCoordinate(lat * 180.0 / kPi, lon * 180.0 / kPi);
}

// --- Local tangent-plane nearest-point-on-segment matching -------------------
// Shared by Tier-1 (TrackService::matchToNetwork), route projection
// (RailGraph::projectOntoRoute) and platform snapping (RailGraph::nearestOnTrack)
// so the snap maths lives in exactly one place.

// (Near-)constant metres per degree of latitude; longitude is scaled by cos(lat).
inline constexpr double kMetresPerDegLat = 111320.0;

/// Metres per degree of longitude at `fix`'s latitude, clamped away from 0 near
/// the poles so the tangent plane stays well-conditioned.
inline double metresPerDegLon(const QGeoCoordinate &fix)
{
    const double cosLat = std::cos(fix.latitude() * kPi / 180.0);
    return kMetresPerDegLat * (std::max)(0.05, cosLat);
}

/// Result of projecting a fix onto a segment [a,b] in a local east/north tangent
/// plane (metres) centred on the fix.
struct SegmentHit {
    double dist = 0.0;     ///< distance fix -> nearest point on the segment (m)
    double t = 0.0;        ///< clamped parameter along the segment (0..1)
    double east = 0.0;     ///< east offset (m) of the nearest point from the fix
    double north = 0.0;    ///< north offset (m) of the nearest point from the fix
    double dirEast = 0.0;  ///< segment direction east component (b - a, m)
    double dirNorth = 0.0; ///< segment direction north component (b - a, m)
    double len2 = 0.0;     ///< squared segment length (m^2)
};

/// Project `fix` onto segment [a,b]. `mPerLon` must be metresPerDegLon(fix).
inline SegmentHit projectToSegment(const QGeoCoordinate &fix, double mPerLon,
                                   const QGeoCoordinate &a, const QGeoCoordinate &b)
{
    const double ax = (a.longitude() - fix.longitude()) * mPerLon;
    const double ay = (a.latitude() - fix.latitude()) * kMetresPerDegLat;
    const double bx = (b.longitude() - fix.longitude()) * mPerLon;
    const double by = (b.latitude() - fix.latitude()) * kMetresPerDegLat;
    const double dx = bx - ax, dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0.0 ? -(ax * dx + ay * dy) / len2 : 0.0;
    t = (std::clamp)(t, 0.0, 1.0);
    const double cx = ax + t * dx, cy = ay + t * dy;
    SegmentHit h;
    h.dist = std::sqrt(cx * cx + cy * cy);
    h.t = t;
    h.east = cx;
    h.north = cy;
    h.dirEast = dx;
    h.dirNorth = dy;
    h.len2 = len2;
    return h;
}

/// Convert an east/north offset (m) from `fix` back to a WGS84 coordinate.
inline QGeoCoordinate offsetToCoord(const QGeoCoordinate &fix, double mPerLon,
                                    double east, double north)
{
    return QGeoCoordinate(fix.latitude() + north / kMetresPerDegLat,
                          fix.longitude() + east / mPerLon);
}

} // namespace tm35fin
