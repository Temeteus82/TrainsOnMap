# TrackMatcher

## 1. Class Overview

TrainsOnMap plots live Finnish train positions on a Qt Quick map. Raw GPS fixes
from the Digitraffic feed jitter beside the rails and occasionally land off the
network entirely; to make markers ride the tracks cleanly the app *map-matches*
each fix against the national rail geometry.

`TrackMatcher` is the tiny abstract interface that decouples the consumer of
map-matching (`TrainListModel`) from its provider (`TrackService`, which holds
the projected national network). The model depends only on this interface and on
`QGeoCoordinate`, never on the heavy track-geometry classes (`RailGraph`,
`TrackService`). This is the seam that keeps the model testable and free of a
QtLocation/geometry dependency.

The header also declares the two plain-struct value types that flow across the
interface: `TrackMatch` (the result) and `RouteMatchRequest` (the Tier-2 input).

## 2. Project Structure and Dependencies

- **Implemented by:** `TrackService` (`src/TrackService.h`), which inherits
  `TrackMatcher` and fills in both virtuals against the in-memory `RailGraph`.
- **Consumed by:** `TrainListModel` (`src/TrainListModel.h`), which holds a
  `const TrackMatcher *` (not owned) and calls it from `applyOne()` to snap and
  flag every incoming position. `DigitrafficClient::setMatcher` wires the two
  together.
- **Qt modules:** Qt6::Positioning (for `QGeoCoordinate`) and Qt6::Core (for
  `QString`, `QVector`).

Project-internal types declared here:

| Type | Provides |
|------|----------|
| `TrackMatch` | The outcome of matching one fix — snapped coordinate, distance, and the Tier-2 route extras. |
| `RouteMatchRequest` | The inputs for a route-constrained (Tier-2) match — the scheduled path plus continuity/platform context. |

## 3. Class Hierarchy and Role

`TrackMatcher` is a standalone abstract base class with no Qt base — no
`QObject`, no meta-object. It exists purely as a pure-virtual contract:

- One **pure virtual** method (`matchToNetwork`) that any implementation must
  provide.
- One **virtual method with a default implementation** (`matchOnRoute`) that
  returns an empty (off-route) match, so a minimal matcher needn't implement
  Tier-2.
- A **virtual destructor** (`= default`), signalling that instances are intended
  to be owned and deleted through a `TrackMatcher *`.

Its role is the classic dependency-inversion interface: the high-level policy
(`TrainListModel`) and the low-level mechanism (`TrackService`) both depend on
this abstraction rather than on each other.

## 4. Q_PROPERTY Declarations

None. `TrackMatcher` is not a `QObject` and exposes no properties.

## 5. Enumerations

None.

## 6. Public Member Variables

`TrackMatcher` itself has no members. The two associated structs carry the data.

`TrackMatch` — result of map-matching one GPS fix:

| Variable | Type | Description |
|----------|------|-------------|
| `snapped` | `QGeoCoordinate` | Nearest point on the network; an invalid coordinate when there is no match. |
| `distanceMeters` | `double` | Metres from the input fix to `snapped`; `< 0` when unknown. Default −1.0. |
| `onTrack` | `bool` | True when `distanceMeters` is within the implementation's snap-accept threshold. |
| `tunniste` | `QString` | Tier-2 only — the chosen track OID, for diagnostics; empty if none. |
| `chainageMeters` | `double` | Tier-2 only — 1-D position along the train's route; −1 if off-route. |
| `onRoute` | `bool` | Tier-2 only — true when the fix was matched against the train's scheduled route rather than merely the nearest track. |

`RouteMatchRequest` — inputs for a route-constrained match:

| Variable | Type | Description |
|----------|------|-------------|
| `stationCodes` | `QVector<QString>` | Ordered scheduled station short codes defining the route. |
| `prevChainage` | `double` | Last accepted route position (m); `< 0` requests a global (unwindowed) search. Default −1.0. |
| `advanceMeters` | `double` | Expected progress since the last fix (speed · Δt); centres the search window ahead of `prevChainage`. Default 0.0. |
| `platformStation` | `QString` | When the train is stopped at a station, the short code of that station, so the matcher snaps to its platform. |
| `platformTrack` | `QString` | The booked `commercialTrack` number at `platformStation`. |

## 7. Signals

None (not a `QObject`).

## 8. Public Slots and Q_INVOKABLE Methods

None.

## 9. Public Methods

#### virtual TrackMatch matchToNetwork(const QGeoCoordinate &fix, double headingDeg = -1.0) const = 0

**Pure virtual.** Returns the nearest point on the whole network to `fix` (Tier
1). `headingDeg` is the train's direction of travel (0 = north, increasing
clockwise) or `< 0` when unknown; when supplied, a candidate track whose tangent
aligns with the heading is preferred over a slightly nearer but cross-cutting one,
so a fix near a junction snaps to the track the train is actually running on
rather than the one it crosses. Implementations must return an empty match
(`distanceMeters < 0`, `snapped` invalid) when the network isn't loaded yet or
`fix` is invalid.

#### virtual TrackMatch matchOnRoute(const QGeoCoordinate &fix, const RouteMatchRequest &req) const

**Virtual, defaulted to return `{}`.** Route-constrained match (Tier 2): snaps
`fix` to the nearest point on the train's resolved route polyline within a
chainage window of its last position, or to its booked platform track when
stopped at a station. Returns `onRoute == false` (an empty match by default) when
the route isn't resolved yet — the caller then falls back to `matchToNetwork()`.
A matcher that only does Tier-1 work can leave this unimplemented.

## 10. Protected Virtual Methods / Event Handlers

None beyond the two interface methods documented above.

## 11. Ownership and Lifecycle

`TrackMatcher` defines a public `virtual ~TrackMatcher() = default`, so instances
may safely be deleted through a base pointer. In practice the only implementation
is `TrackService`, a `QObject` that is parent-owned (and owned by QML when
declared in `Main.qml`); `TrainListModel` holds a **non-owning** `const
TrackMatcher *` to it and never deletes it. The interface imposes no lifetime
contract of its own beyond the virtual destructor.

## 12. Thread Safety

The interface is agnostic. The concrete `TrackService` implementation is designed
to be called on the GUI thread (it reads geometry that a worker thread populated
once at load). `matchToNetwork` / `matchOnRoute` are declared `const` and perform
read-only lookups, but callers should still treat a given matcher as
single-threaded unless its implementation documents otherwise.

## 13. QML Exposure

Not exposed to QML directly. The interface has no `QML_*` macro; only its
implementation `TrackService` is a `QML_ELEMENT`.

## 14. Inter-Class Interactions

- `DigitrafficClient::setMatcher(TrackService*)` forwards the matcher to
  `TrainListModel::setMatcher(const TrackMatcher*)`, so both the REST and the
  shared MQTT update paths snap fixes through one matcher.
- `TrainListModel::applyOne()` calls `matchOnRoute()` first (Tier 2) and falls
  back to `matchToNetwork()` (Tier 1) when the route isn't resolved or the fix is
  too far from it.
- Diagnostics derived from a match (`tunniste`, `onRoute`, raw-vs-snapped
  coordinates) surface through `TrainListModel::matchInfoFor` to the QML debug
  overlay.

## 15. External Communication

None. `TrackMatcher` performs only in-memory geometry lookups.
