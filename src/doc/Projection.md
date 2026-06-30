# Projection.h (`tm35fin` namespace)

## A. Overview

`Projection.h` is the coordinate-maths foundation of TrainsOnMap — a header-only,
inline-only utility that lives in the `tm35fin` namespace. It solves two related
problems:

1. **Datum conversion.** The Digitraffic infrastructure API publishes track
   geometry in EPSG:3067 (ETRS-TM35FIN) easting/northing metres, while the Qt
   Quick map layer and the live-train feed work in WGS84 latitude/longitude.
   `toWgs84()` / `fromWgs84()` convert between the two using the Snyder
   inverse/forward Transverse Mercator series (sub-metre accurate across
   Finland).
2. **Local nearest-point-on-segment matching.** A small set of helpers projects a
   GPS fix onto a track segment in a local east/north tangent plane (metres),
   which is the shared kernel behind all of the app's map-matching: Tier-1
   nearest-track snapping (`TrackService::matchToNetwork`), Tier-2 route
   projection (`RailGraph::projectOntoRoute`) and platform snapping
   (`RailGraph::nearestOnTrack`). Keeping the snap maths here means it lives in
   exactly one place.

A developer reaches for this header any time a coordinate must cross the
EPSG:3067 / WGS84 boundary, or any time a point must be snapped to a polyline in
metric space.

## B. Namespaces

| Namespace | Groups |
|-----------|--------|
| `tm35fin` | All projection constants, the datum conversions, and the tangent-plane segment-matching helpers. The name reflects the source CRS (ETRS-TM35FIN / EPSG:3067). |

## C. Types and Type Aliases

| Name | Kind | Description |
|------|------|-------------|
| `SegmentHit` | `struct` | Result of projecting a fix onto a segment `[a,b]` in a local east/north tangent plane centred on the fix. |

`SegmentHit` members (all `double`, all in metres unless noted):

| Member | Description |
|--------|-------------|
| `dist` | Distance from the fix to the nearest point on the segment. |
| `t` | Clamped parameter along the segment, in the range 0..1 (0 = at `a`, 1 = at `b`). |
| `east` | East offset of the nearest point from the fix. |
| `north` | North offset of the nearest point from the fix. |
| `dirEast` | East component of the segment direction (`b − a`). |
| `dirNorth` | North component of the segment direction (`b − a`). |
| `len2` | Squared segment length (m²); 0 for a degenerate segment. |

## D. Constants

| Name | Type / Value | Description |
|------|--------------|-------------|
| `kPi` | `constexpr double` 3.14159265358979323846 | π, used for degree/radian conversion. |
| `a` | `constexpr double` 6378137.0 | GRS80 semi-major axis (m). |
| `f` | `constexpr double` 1 / 298.257222101 | GRS80 flattening. |
| `k0` | `constexpr double` 0.9996 | EPSG:3067 scale factor. |
| `lon0` | `constexpr double` 27° in radians | Central meridian (27°E). |
| `FE` | `constexpr double` 500000.0 | False easting (m). |
| `FN` | `constexpr double` 0.0 | False northing (m). |
| `kMetresPerDegLat` | `constexpr double` 111320.0 | (Near-)constant metres per degree of latitude. Longitude is scaled by cos(lat). |

## E. Functions

#### QGeoCoordinate toWgs84(double E, double N)

Converts an EPSG:3067 easting/northing pair (metres) to a WGS84 `QGeoCoordinate`.
Implements the Snyder inverse Transverse Mercator series. Because ETRS89 and
WGS84 differ by only a few centimetres over Finland, the result is used directly
as WGS84. Used by `RailGraph::loadFromJson` to project every baked track vertex
into map space.

#### void fromWgs84(double latDeg, double lonDeg, double &E, double &N)

Converts a WGS84 latitude/longitude (degrees) to EPSG:3067 easting/northing
(metres), returned through the out-parameters `E` and `N`. The forward of
`toWgs84`. Provided for completeness/symmetry with the datum used by the source
data.

#### double metresPerDegLon(const QGeoCoordinate &fix)

Returns the number of metres per degree of longitude at `fix`'s latitude —
`kMetresPerDegLat · cos(lat)`. The cosine is clamped to a floor of 0.05 so the
tangent plane stays well-conditioned near the poles (irrelevant for Finland in
practice, but it keeps the maths safe). Callers compute this once per fix and
pass it into `projectToSegment` / `offsetToCoord` to avoid recomputing the
cosine per segment.

#### SegmentHit projectToSegment(const QGeoCoordinate &fix, double mPerLon, const QGeoCoordinate &a, const QGeoCoordinate &b)

Projects `fix` onto the segment `[a,b]` in a local east/north tangent plane
centred on `fix`, returning a fully-populated `SegmentHit`. `mPerLon` **must** be
`metresPerDegLon(fix)` — it is passed in rather than recomputed so a hot loop
over many segments pays the cosine cost once. The parameter `t` is clamped to
`[0,1]`, so the returned nearest point always lies on the segment (never on its
infinite extension).

#### QGeoCoordinate offsetToCoord(const QGeoCoordinate &fix, double mPerLon, double east, double north)

Inverse of the tangent-plane mapping: converts an east/north offset (metres) from
`fix` back to an absolute WGS84 `QGeoCoordinate`. Typically called with the
`east`/`north` from a `SegmentHit` to obtain the snapped coordinate. `mPerLon`
must again be `metresPerDegLon(fix)`.

## F. Dependencies

| Include | Provides |
|---------|----------|
| `<QGeoCoordinate>` | The `QGeoCoordinate` value type used throughout (from Qt Positioning). |
| `<algorithm>` | `std::max`, `std::clamp` used in the helpers. |
| `<cmath>` | `std::sin`, `std::cos`, `std::tan`, `std::sqrt`, `std::pow`. |

Build requirement: **Qt6::Positioning** (for `QGeoCoordinate`). The header is
otherwise standalone and is compiled into both the application target and the
`tst_railgraph` test target.

## G. Usage Example

```cpp
#include "Projection.h"
#include <QGeoCoordinate>

// Snap a GPS fix onto the nearest point of a track polyline.
QGeoCoordinate snapToPolyline(const QGeoCoordinate &fix,
                              const QVector<QGeoCoordinate> &path,
                              double &outDistanceMeters)
{
    const double mPerLon = tm35fin::metresPerDegLon(fix);
    double bestDist = std::numeric_limits<double>::infinity();
    QGeoCoordinate best;
    for (int i = 1; i < path.size(); ++i) {
        const tm35fin::SegmentHit h =
            tm35fin::projectToSegment(fix, mPerLon, path.at(i - 1), path.at(i));
        if (h.dist < bestDist) {
            bestDist = h.dist;
            best = tm35fin::offsetToCoord(fix, mPerLon, h.east, h.north);
        }
    }
    outDistanceMeters = bestDist;
    return best;
}

// Project a baked EPSG:3067 vertex into map space.
const QGeoCoordinate vertex = tm35fin::toWgs84(385000.0, 6672000.0); // E, N (m)
```
