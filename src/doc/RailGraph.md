# RailGraph

## 1. Class Overview

TrainsOnMap matches live train positions not just to the *nearest* rail but to
the rail a train is actually scheduled to run on (Tier 2). `RailGraph` is the
data structure that makes that possible: the Tier-2 rail network — track
**geometry** plus **identity**, **topology** and a **station crosswalk** — parsed
from the schema-v2 baked blob produced by `scripts/bake_rails.py`.

It is the pure, Qt-Core-only core behind `TrackService`: no `QObject`, no
resource loading, no threading. That separation is deliberate — it lets the
routing and projection maths be unit-tested directly (`tests/tst_railgraph.cpp`)
without spinning up the service.

Because the infra-api's `seuraavatRaiteet` ("next tracks") field is empty in
practice, the routing graph is **reconstructed from geometry**: two tracks are
connected when they share a quantised endpoint node (a switch/join), and
`viereisetRaiteet` adds parallel/adjacent edges. A train's scheduled station
sequence is resolved to an ordered track path by Dijkstra over that graph, and
map-matching is then constrained to a moving window along the resulting
chainage-parameterised polyline.

## 2. Project Structure and Dependencies

- **Instantiated by:** `TrackService::loadNetwork()` creates a
  `std::shared_ptr<RailGraph>` on a worker thread and calls `loadFromJson()`.
  Also instantiated directly by `tests/tst_railgraph.cpp`.
- **Built in CMake** into both the `TrainsOnMap` executable and the
  `tst_railgraph` test target (which links only Qt6::Core, Qt6::Positioning,
  Qt6::Test — proof of the class's narrow dependency surface).
- **Qt modules:** Qt6::Core (`QString`, `QHash`, `QVector`, `QStringList`,
  `QByteArray`, `QJsonDocument`) and Qt6::Positioning (`QGeoCoordinate`).
- **Project-internal dependency:** `Projection.h` (`tm35fin`), used to project
  EPSG:3067 vertices to WGS84 on load and to do all tangent-plane segment
  projection during matching.

## 3. Class Hierarchy and Role

`RailGraph` is a plain C++ class with **no base class** and no Qt meta-object
macros. It is a value-semantics container of parsed network state. Its role is to
be the testable algorithmic core: parsing, graph construction, route resolution
(Dijkstra), polyline building, route projection with continuity windowing, and
platform snapping all live here, free of any I/O or UI concern.

## 4. Q_PROPERTY Declarations

None — not a `QObject`.

## 5. Enumerations

None.

## 6. Public Member Variables

`RailGraph` exposes no public data members; state is private and reached through
accessors. The public nested types it works with are:

`RailGraph::Track` — one parsed track segment:

| Member | Type | Description |
|--------|------|-------------|
| `tunniste` | `QString` | Stable track OID. |
| `paaraide` | `bool` | Main-track flag; running lines are preferred during routing. |
| `kaupallinenNumero` | `QString` | Platform / commercial track number (for platform snapping). |
| `path` | `QVector<QGeoCoordinate>` | Centreline, projected to WGS84. |
| `lengthMeters` | `double` | Total polyline length (used as the Dijkstra edge weight). |
| `minLat`/`maxLat`/`minLon`/`maxLon` | `double` | Bounding box for broadphase culling. |

`RailGraph::Station` — a station's crosswalk entry:

| Member | Type | Description |
|--------|------|-------------|
| `name` | `QString` | Station name. |
| `tracks` | `QVector<int>` | Indices into the track vector for this station's member tracks. |

`RailGraph::RoutePolyline` — a route resolved to a chainage-parameterised line:

| Member | Type | Description |
|--------|------|-------------|
| `points` | `QVector<QGeoCoordinate>` | Ordered polyline vertices. |
| `chainage` | `QVector<double>` | Cumulative along-route distance (m) of each point in `points`. |
| `length` | `double` | Total route length (m). |
| `isValid()` | `bool` | True when `points.size() >= 2`. |

`RailGraph::RouteProjection` — result of projecting a fix onto a route:

| Member | Type | Description |
|--------|------|-------------|
| `snapped` | `QGeoCoordinate` | Nearest point on the route. |
| `offsetMeters` | `double` | True geometric distance from the fix to the route; default −1.0. |
| `chainage` | `double` | Along-route position of `snapped`; default −1.0. |
| `isValid()` | `bool` | True when `snapped` is valid. |

## 7. Signals

None.

## 8. Public Slots and Q_INVOKABLE Methods

None.

## 9. Public Methods

#### bool isEmpty() const

True when no tracks are loaded.

#### int trackCount() const

Number of parsed tracks.

#### int schemaVersion() const

The `schemaVersion` read from the blob (expected to be 2). Left at 0 if loading
failed.

#### bool loadFromJson(const QByteArray &json)

Parses an **uncompressed** schema-v2 JSON document (the bytes after
`qUncompress`). Projects every track vertex to WGS84, builds the endpoint-node
adjacency (two ends within a 10 m grid count as the same switch/join node), folds
in `viereisetRaiteet` parallel edges, and resolves each station's member-track
OIDs to indices. Returns `false` for an empty, old, or non-v2 blob, or when no
tracks survive parsing. Clears any previously loaded state first, so it is safe
to call more than once.

#### QVector<int> routePath(const QVector<QString> &stationCodes) const

Resolves an ordered station sequence to a connected path of track indices, joining
each consecutive station pair by Dijkstra over the derived graph (siding/loop
edges carry a 1.4× penalty so running lines win). Returns an empty vector when
fewer than two stations resolve to member tracks. If the **first** leg is
unroutable the whole route is empty; if a **later** leg is unroutable the routable
prefix is kept and the rest dropped (rather than stitching a chord across the gap,
which would inflate all downstream chainage). Consecutive duplicate track indices
are collapsed.

#### RoutePolyline buildPolyline(const QVector<int> &trackPath) const

Builds a chainage-parameterised polyline from an ordered track-index path. Each
track is oriented to continue from the polyline's current end (the first track is
oriented by its shared join with the second), duplicate join points within 0.5 m
are dropped, and cumulative chainage is recorded per point. Returns an invalid
(empty) polyline for an empty path.

#### RouteProjection projectOntoRoute(const RoutePolyline &rp, const QGeoCoordinate &fix, double prevChainage, double advanceMeters) const

Projects `fix` onto `rp`. With `prevChainage >= 0` the search is limited to a
window around `prevChainage + advanceMeters` (150 m back, 400 m forward) for
continuity/hysteresis, so the marker can't jump back onto a parallel earlier
limb. A windowed hit is trusted while the fix stays within 300 m of it
(`kRouteReacquireMeters`, deliberately 2× the caller's 150 m snap-accept distance
— a hysteresis margin, not the on-track gate). If the window misses, it
re-acquires *forward only* (a train can only have advanced along its booked
route) rather than doing an unconstrained global search, which avoids the
"jump onto the wrong track at a tunnel" failure. With `prevChainage < 0` it scans
the whole route. `offsetMeters` is the true geometric distance.

#### QGeoCoordinate platformSnap(const QString &stationCode, const QString &commercialTrack, const QGeoCoordinate &fix, QString *chosenTunniste = nullptr) const

Returns the nearest point on a station's platform track — the member track whose
`kaupallinenNumero` equals `commercialTrack`. Returns an invalid coordinate when
the station or platform number is unknown or `commercialTrack` is empty. When
`chosenTunniste` is supplied, it receives the chosen track's OID.

#### static QString routeKey(const QVector<QString> &stationCodes)

A stable cache key for an ordered station sequence (the codes joined by `|`), so
identical routes share one resolved polyline.

#### static QStringList canonicalRouteCodes(const QStringList &rawCodes)

Canonicalises a station-code sequence: drops empty codes and collapses
consecutive duplicates (timetable ARRIVAL+DEPARTURE rows repeat a code). Shared
by `DigitrafficClient` (the precompute key) and
`TrainDetailsService::routeStations` (the overlay key) so the two `|`-joined keys
are built identically and can't diverge into a silent lookup miss.

#### const QHash<QString, Station> &stations() const

The station crosswalk, keyed by station short code.

#### const QVector<Track> &tracks() const

All parsed tracks, in network order. `TrackService` iterates this both to flatten
render segments and to run Tier-1 nearest-track matching directly against the
plain-`QGeoCoordinate` geometry.

## 10. Protected Virtual Methods / Event Handlers

None. (One private helper, `nearestOnTrack`, projects a fix onto a single track's
polyline and backs `platformSnap`.)

## 11. Ownership and Lifecycle

`RailGraph` owns all of its state by value (`QVector`/`QHash` members), so copying
or destroying one is straightforward. In the application it is heap-allocated and
held by `std::shared_ptr<RailGraph>` in `TrackService`: the loader thread builds
it, then a `shared_ptr` *copy* is handed to subsequent route-precompute worker
tasks for read-only access. There is no parent/child ownership and no manual
`delete`; the `shared_ptr` controls the lifetime.

## 12. Thread Safety

Not internally synchronised. The intended pattern (used by `TrackService`) is:
build/`loadFromJson` on one worker thread; after that the object is treated as
**immutable** and read concurrently — the GUI thread runs `matchToNetwork`-style
queries while a worker thread reads the same `shared_ptr` to resolve routes. This
is safe only because nothing mutates the graph after load. Do not call
`loadFromJson` again while other threads hold read references.

## 13. QML Exposure

None.

## 14. Inter-Class Interactions

- **`TrackService`** owns the `RailGraph`, flattens its `tracks()` into render
  segments, runs Tier-1 matching against `tracks()`, and calls `routePath` +
  `buildPolyline` (off-thread) and `projectOntoRoute` / `platformSnap` (Tier-2)
  through it.
- **`DigitrafficClient`** calls the static `RailGraph::canonicalRouteCodes` to
  build route sequences whose `routeKey` matches what `TrackService` precomputes.
- **`TrainDetailsService::routeStations`** uses the same static helper so the
  debug-overlay route key matches the precompute key.

## 15. External Communication

None. `RailGraph` consumes an already-loaded `QByteArray`; it performs no I/O,
no network access, and launches no processes.

## 16. Usage Example

```cpp
#include "RailGraph.h"
#include <QFile>

RailGraph graph;
QFile f(QStringLiteral(":/data/rails.geojson.qz"));
f.open(QIODevice::ReadOnly);
if (graph.loadFromJson(qUncompress(f.readAll()))) {
    // Resolve a scheduled route to a chainage-parameterised polyline.
    const QVector<QString> codes = { "HKI", "PSL", "TKL", "RI", "HL" };
    const QVector<int> path = graph.routePath(codes);
    const RailGraph::RoutePolyline route = graph.buildPolyline(path);

    // Continuity-windowed projection of a fix advancing along that route.
    const RailGraph::RouteProjection p =
        graph.projectOntoRoute(route, fix, /*prevChainage=*/1200.0, /*advance=*/300.0);
    if (p.isValid() && p.offsetMeters <= 150.0)
        useSnapped(p.snapped, p.chainage);
}
```
