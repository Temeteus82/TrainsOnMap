#pragma once

#include <QGeoCoordinate>
#include <cmath>

/// Conversions between EPSG:3067 (ETRS-TM35FIN, the CRS the Digitraffic
/// infra-api returns) and WGS84 lon/lat (what the map layer expects).
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

/// WGS84 lat/lon (degrees) -> EPSG:3067 easting/northing (metres).
inline void fromWgs84(double latDeg, double lonDeg, double &E, double &N)
{
    const double e2  = f * (2.0 - f);
    const double ep2 = e2 / (1.0 - e2);

    const double phi = latDeg * kPi / 180.0;
    const double lam = lonDeg * kPi / 180.0;

    const double sinp = std::sin(phi);
    const double cosp = std::cos(phi);
    const double tanp = std::tan(phi);

    const double Nn = a / std::sqrt(1 - e2 * sinp * sinp);
    const double T  = tanp * tanp;
    const double C  = ep2 * cosp * cosp;
    const double A  = (lam - lon0) * cosp;

    const double M = a
        * ((1 - e2 / 4 - 3 * e2 * e2 / 64 - 5 * e2 * e2 * e2 / 256) * phi
           - (3 * e2 / 8 + 3 * e2 * e2 / 32 + 45 * e2 * e2 * e2 / 1024) * std::sin(2 * phi)
           + (15 * e2 * e2 / 256 + 45 * e2 * e2 * e2 / 1024) * std::sin(4 * phi)
           - (35 * e2 * e2 * e2 / 3072) * std::sin(6 * phi));

    E = FE + k0 * Nn
            * (A
               + (1 - T + C) * std::pow(A, 3) / 6
               + (5 - 18 * T + T * T + 72 * C - 58 * ep2) * std::pow(A, 5) / 120);

    N = FN + k0
            * (M
               + Nn * tanp
                     * (A * A / 2
                        + (5 - T + 9 * C + 4 * C * C) * std::pow(A, 4) / 24
                        + (61 - 58 * T + T * T + 600 * C - 330 * ep2) * std::pow(A, 6) / 720));
}

} // namespace tm35fin
