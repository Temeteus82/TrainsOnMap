# TrackService

## 1. Class Overview

`TrackService` is the GUI-facing owner of the railway track geometry in
TrainsOnMap. It provides two things to the rest of the app:

1. **Render geometry** — the track polylines drawn beneath the trains. It loads a
   pre-baked national rail snapshot embedded in the binary
   (`:/data/rails.geojson.qz`), flattens it into render segments, and (via a
   spatial grid) filters them to the current map viewport on demand.
2. **Map-matching** — its `matchToNetwork` / `matchOnRoute` methods (and the
   `TrackMatch` / `RouteMatchRequest` value types declared alongside it) let
   `TrainListModel` snap and flag GPS fixes against the rails, including Tier-2
   route-constrained matching and platform snapping.

The rail network changes rarely, so it ships in the repo (baked by
`scripts/bake_rails.py`) rather than being fetched from the Digitraffic infra-api
on every launch. Parsing and projecting the blob is done **off the GUI thread** at
startup so the first frame isn't blocked; `geometryReady` fires when the network
is resident.

## 2. Project Structure and Dependencies

- **Declared in QML** (`Main.qml`) as `TrackService { id: trackService }`, and
  wired to `DigitrafficClient.matcher`. Its `model` feeds a `MapItemView`, and
  its `routePolyline`/`loadForBounds`/`pinRoute` slots are called from the map.
- **Owns:** a `TrackListModel` (child `QObject`), a `std::shared_ptr<RailGraph>`,
  the flattened render `Segment`s and the spatial `Grid`.
- **Qt modules:** Qt6::Core, Qt6::Qml (`QML_ELEMENT`), Qt6::Positioning
  (`QGeoCoordinate`), Qt6::Concurrent (`QtConcurrent::run`, `QFutureWatcher`).
- **Project-internal dependencies:** `RailGraph` (Tier-2 core), `TrackListModel`
  (render model), `Projection.h` (`tm35fin` tangent-plane maths).
- **Declares** the `TrackMatch` and `RouteMatchRequest` value types consumed by
  `TrainListModel` (which holds only a forward-declared `const TrackService *`).

## 3. Class Hierarchy and Role

`TrackService : public QObject`.

- From **`QObject`**: the meta-object system, signals/slots, properties, and
  parent-based ownership.

Its role is the bridge between the pure `RailGraph` core and the Qt UI/threading
world, and the app's map-matcher: `matchToNetwork` (Tier 1) and `matchOnRoute`
(Tier 2) supply matching from the heavy geometry it owns.

## 4. Q_PROPERTY Declarations

| Property | Type | READ | WRITE | NOTIFY | Description |
|----------|------|------|-------|--------|-------------|
| `model` | `TrackListModel *` | `model` | — | — (`CONSTANT`) | The viewport-filtered render model bound to a `MapItemView`. Read-only, set once at construction. |
| `loading` | `bool` | `isLoading` | — | `loadingChanged` | True while the network is being parsed on the worker thread. |
| `status` | `QString` | `status` | — | `statusChanged` | Human-readable status line; only non-empty while loading or on failure (e.g. "Loading rail geometry…", "Rail geometry could not be loaded"). Empty on success so the UI's `statusText` falls back to the live train-fetch status instead of a stale one-time message. |

## 5. Enumerations

None.

## 6. Public Member Variables

None public. (Private state: render segments, the spatial grid, the
`shared_ptr<RailGraph>`, the resolved route-polyline cache, the pinned route, the
pending-route set, and the precompute-in-flight flag.)

## 7. Signals

#### void loadingChanged()

Emitted when the `loading` property flips — true at construction, false once the
worker thread's parse result has been applied.

#### void statusChanged()

Emitted when the `status` string changes.

#### void geometryReady()

Emitted on the GUI thread once the network has been parsed and applied (segments,
grid and graph are resident). The map connects to this to seed the first viewport
load; `kickPrecompute()` is also re-driven here for any routes that arrived before
the graph finished parsing.

#### void routesReady()

Emitted when an off-thread route precompute batch finishes and the resolved
polylines have been merged into the cache. The map's debug route overlay
re-evaluates its `routePolyline()` binding in response.

## 8. Public Slots and Q_INVOKABLE Methods

#### void loadForBounds(double west, double south, double east, double north)

Filters the render segments to those intersecting the given WGS84 bounding box and
pushes them — paths plus their `paaraide` line-category flags — to the
`TrackListModel`. Arguments follow the GeoJSON/OGC convention
(west, south, east, north). Uses the spatial grid as a broadphase (gathering
segment ids from overlapping cells, de-duplicating, then applying the exact bbox
test), falling back to a linear scan if the grid isn't built yet. Results are kept
in ascending id order, which the model's incremental diff requires.

#### QVariantList routePolyline(const QStringList &stationCodes) const

Returns the resolved route polyline for an ordered station sequence as a
`QVariantList` of `QGeoCoordinate`, ready to bind to a `MapPolyline.path`. Empty
until `precomputeRoutes()` has resolved that route (or if it resolved to a
sentinel-invalid polyline). Used by the Tier-2 debug overlay.

#### void pinRoute(const QStringList &stationCodes)

Pins one route (the selected train's) so it is resolved and kept in the cache even
after the train leaves the live fleet — otherwise eviction would drop a
still-selected train's overlay and Tier-2 match. Pass an empty or single-element
list to unpin. Triggers a precompute/eviction re-evaluation.

## 9. Public Methods

#### TrackListModel *model() const

The render model (also the `model` property getter).

#### bool isLoading() const

The `loading` property getter.

#### QString status() const

The `status` property getter.

#### TrackMatch matchToNetwork(const QGeoCoordinate &fix, double headingDeg = -1.0) const

Tier-1. Snaps `fix` to the nearest in-memory rail segment, matched
directly against the graph's plain-`QGeoCoordinate` tracks (no per-vertex QVariant
unboxing). Bounds the candidate cull to ~600 m around the fix; a candidate within
150 m is reported `onTrack`. When `headingDeg >= 0`, a cross-cutting track is
charged a misalignment penalty (up to 120 m) so an aligned track wins ties at a
junction. Returns an empty match when the network isn't loaded or `fix` is
invalid.

#### TrackMatch matchOnRoute(const QGeoCoordinate &fix, const RouteMatchRequest &req) const

Tier-2. If the request carries a `platformStation` +
`platformTrack` and the train is near (≤ 250 m of) that booked platform, snaps to
it and holds the chainage while stopped. Otherwise looks up the request's route in
the resolved-polyline cache and delegates to `RailGraph::projectOntoRoute` with
the carried chainage and advance. Returns an empty (off-route) match when the
route isn't resolved yet, so the caller falls back to Tier-1.

#### void precomputeRoutes(const QVector<QVector<QString>> &routes)

Stashes the latest requested route set and kicks an off-GUI-thread resolve +
cache. Routes are static per run, so this dedupes identical routes by `routeKey`,
skips already-resolved (and already-known-unresolvable) ones, and evicts cached
routes no longer in the live set (plus the pinned one). A cheap no-op until the
network has loaded. Re-entrant-safe: a no-op while a precompute is in flight (the
finished handler re-runs it).

## 10. Protected Virtual Methods / Event Handlers

None.

## 11. Ownership and Lifecycle

- `TrackService` is a `QObject`, normally parent-owned; when declared in
  `Main.qml` it is owned by the QML engine.
- Its `TrackListModel *m_model` is constructed with `this` as parent, so Qt
  deletes it with the service.
- `QFutureWatcher` instances created for the load and for each precompute batch
  are parented to `this` and `deleteLater()`-d in their finished handlers.
- The `RailGraph` is held by `std::shared_ptr`; worker tasks take a `shared_ptr`
  copy, so the graph outlives the service only as long as a task still references
  it (RAII, no manual delete).

## 12. Thread Safety

**GUI-thread object with worker-thread offload.** All slots, properties, signals
and the matcher methods are intended to be used on the GUI thread. The heavy
work — parsing the blob and resolving route polylines — runs on the global thread
pool via `QtConcurrent::run`; results are applied back on the GUI thread inside
`QFutureWatcher::finished` handlers. The shared `RailGraph` is treated as
immutable after load, so concurrent reads (GUI-thread matching vs. worker-thread
route resolution) are safe.

## 13. QML Exposure

Registered with `QML_ELEMENT` in the `TrainsOnMap` module (URI `TrainsOnMap`,
version 1.0), so QML can instantiate `TrackService {}` directly. From QML the
`model`, `loading` and `status` properties are bound (InfoPanel, the track
`MapItemView`), and the `loadForBounds`, `routePolyline` and `pinRoute` slots are
invoked. The `geometryReady`/`routesReady` signals are handled in `Main.qml` to
seed and refresh the map.

## 14. Inter-Class Interactions

- **`DigitrafficClient`** sets `TrackService` as its `matcher`; the client's
  `handleCategories` calls `precomputeRoutes()` with the whole fleet's route
  sequences each cycle.
- **`TrainListModel`** calls `matchToNetwork` / `matchOnRoute` to snap fixes.
- **`Main.qml`** drives `loadForBounds` (on pan/zoom debounce), `routePolyline`
  (route overlay), and `pinRoute` (on selection change / `routeStationsChanged`).
- **`TrackListModel`** receives the viewport-filtered segment set via
  `setVisibleSegments`.

## 15. External Communication

None at runtime. `TrackService` reads only the embedded Qt resource
`:/data/rails.geojson.qz` (a local file via `QFile` + `qUncompress`). It makes no
network requests — that is precisely why the rail network is baked into the
binary.
