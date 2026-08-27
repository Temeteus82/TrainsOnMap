# TrainSpotter — Data Fetching & Handling

How live train positions (and supporting metadata) move from the Fintraffic
Digitraffic API into the app's state and onto the map. All data comes from the
free [Digitraffic API](https://www.digitraffic.fi/rautatieliikenne/) — no API
key, no external Swift packages.

## At a glance

```
                ┌─────────────────────────── DigitrafficService (stateless) ──────────────────────────┐
                │  REST: /train-locations/latest/   /trains/{date}   /metadata/*   /live-trains/{n}    │
                └───────────────────────────────────────────────────────────────────────────────────-─┘
                         ▲                                  ▲
        one snapshot +   │                                  │  on-demand (train tapped)
        60 s resync      │                                  │
                ┌────────┴───────────┐            ┌─────────┴──────────┐
   MQTT deltas  │     TrainStore     │            │  TrainDetails /    │
  ┌────────────▶│  @Observable       │            │  Composition       │
  │  apply(_:)  │  @MainActor        │            └────────────────────┘
  │             │  trains:[Location] │
┌─┴───────────┐ │  previousPositions │  ── filteredTrains() ──▶  MapView annotations
│  MQTTClient │ │  trainSummaries    │  ── bearing(for:)    ──▶  direction arrow
│  (actor)    │ │  stations / causes │
└─────────────┘ └────────────────────┘
 wss://rata.digitraffic.fi/mqtt   topic: train-locations/#
```

Two transports feed positions:

- **REST** (`/train-locations/latest/`) — used once to bootstrap, then every 60 s
  to prune. Returns the full current set of active trains.
- **MQTT** (`train-locations/#`) — the live firehose. Delivers **deltas only**:
  a position is published when a train moves/reports. A freshly-connected
  subscriber sees nothing until trains start reporting, which is exactly why the
  REST bootstrap is required.

## Layers

### 1. `DigitrafficService` — the network layer

`Services/DigitrafficService.swift` is a stateless `Sendable` struct. No mutable
state, safe to call from any context. Every method is `async throws` and returns
decoded model types. Endpoints used:

| Method | Endpoint | Purpose |
|---|---|---|
| `fetchTrainLocations()` | `/train-locations/latest/` | All active train positions (REST snapshot) |
| `fetchTrainSummaries(date:)` | `/trains/{yyyy-MM-dd}` | trainNumber → type / category / commuter line cache |
| `fetchStations()` | `/metadata/stations` | Station short-code → name catalogue |
| `fetchCauseCategories()` | `/metadata/cause-category-codes` | Delay-cause id → human name |
| `fetchRunningTrainStatuses()` | `/live-trains` | Bulk trainNumber → delay / running flags for the marker status rings (every 120 s) |
| `fetchComposition(...)` | `/compositions/{date}/{n}` | Wagon composition (per-train, on demand) |
| `fetchTrainDetails(...)` | `/live-trains/{n}` | Full timetable + live delays (per-train, on demand) |

**Date decoding.** Digitraffic emits ISO8601 with fractional seconds. A custom
`dateDecodingStrategy` tries `[.withInternetDateTime, .withFractionalSeconds]`
first, then falls back to plain `.withInternetDateTime`. The configured decoder
is exposed as `static let sharedDecoder` so the MQTT path decodes payloads
identically.

**Response validation.** `validate(response:)` throws `DigitrafficError.httpError`
on any non-2xx status. `fetchComposition` treats a `404` as "no composition"
(returns `nil`) rather than an error, since many trains legitimately have none.

### 2. `MQTTClient` — the live transport

`Services/MQTTClient.swift` is a ~250-line `actor` implementing the subset of
MQTT 3.1.1 needed for the firehose, over `URLSessionWebSocketTask` with the
`mqtt` subprotocol. No SPM dependency.

- Implements: CONNECT, CONNACK, SUBSCRIBE (QoS 0), PUBLISH (receive), PINGREQ
  keepalive, DISCONNECT. SUBACK is **not** awaited — a QoS 0 firehose tolerates
  a missed ack.
- WebSocket binary frames don't align to MQTT packet boundaries (they can carry
  partial or coalesced packets), so an internal `buffer: Data` reassembles the
  byte stream and `readRemainingLength()` decodes MQTT's Variable Byte Integer
  length field (1–4 bytes, 7 bits each).
- Single public API: `connect(subscribe:)` returns an `AsyncStream<Message>`
  whose termination (socket error / broker drop / `disconnect()`) tears the
  connection down.

### 3. `TrainStore` — state & orchestration

`Store/TrainStore.swift` is an `@Observable @MainActor final class` and the
single source of truth. Views observe it directly (no Combine, no `@Published`).
Key state:

- `trains: [TrainLocation]` — the live set rendered on the map.
- `previousPositions: [String: CLLocationCoordinate2D]` — each train's prior
  coordinate, used to compute bearing.
- `trainSummaries: [Int: TrainSummary]`, `stations`, `causeCategoryNames` —
  metadata caches loaded at startup.
- `selectedTrain` / `selectedTrainDetails` / `selectedTrainComposition` — the
  tapped train's on-demand detail.

## The position lifecycle

### Startup — `startRefreshing(resyncInterval:)`

Spawns three long-lived tasks:

**`streamTask`** (live path):
1. Fetch metadata in parallel — `fetchCategories()`, `fetchStations()`,
   `fetchCauseCategories()` via `async let`.
2. `await fetchTrains()` — one REST snapshot so stationary trains appear
   immediately.
3. Enter a reconnect loop around `consumeMQTT()`:
   - `consumeMQTT()` builds an `MQTTClient`, subscribes to `train-locations/#`,
     and for every inbound message decodes a `TrainLocation` with
     `sharedDecoder` and calls `apply(_:)`. It returns whether any message
     arrived.
   - The loop applies **exponential backoff** (`minReconnectDelay` 1 s →
     `maxReconnectDelay` 30 s): reset to 1 s after a productive session, double
     after an immediate failure. A network blip self-heals without relaunch.

**`resyncTask`** (prune path):
- Every 60 s: if the operating day rolled over midnight, re-run
  `fetchCategories()` (so new-day trains don't all decode as `.other` and get
  filtered out); then `await fetchTrains()`.

**`statusTask`** (delay/status path — feeds the marker status rings):
- Once on startup, then every 120 s (`statusInterval`): `fetchStatuses()` →
  `fetchRunningTrainStatuses()` hits the bulk `/live-trains` endpoint (one request,
  ~6 MB gzip) and decodes a lean `TrainRunningStatus` per train (current delay,
  `cancelled`, `runningCurrently`, plus `trainType` / `trainCategory` /
  `commuterLineID`) into `trainStatuses`. Slower cadence than the position resync
  because delays drift gradually and the payload is large. The position feed has no
  delay data — this is the only source for it on the map, and it's bulk (not
  per-train), so it respects the "no eager per-train details" rule.
  `ringState(for:)` combines it with each `TrainLocation.timestamp` age to pick a
  `TrainRingState`. See [`MarkerColors.md`](MarkerColors.md) § Status rings. The
  type/category/line fields also serve as a **fallback type source** for the
  look-ups below — see § Derived data.

`stopRefreshing()` cancels all three tasks and disconnects the MQTT client.

### Upsert — `apply(_:)`

The single funnel for **every** position update, REST or MQTT:

1. If a train with the same `id` (`"{trainNumber}-{departureDate}"`) exists:
   - **Guard on timestamp:** `loc.timestamp >= trains[idx].timestamp`, else
     drop. The 60 s REST resync lags the MQTT firehose, so without this a stale
     snapshot could overwrite a fresher position and record a ~180° reversed
     bearing.
   - Snapshot the existing coordinate into `previousPositions[loc.id]` **before**
     overwriting — this is the "from" point for the next bearing calculation.
   - Overwrite `trains[idx]`.
2. Otherwise append as a new train.
3. Update `lastUpdated`, clear `errorMessage`.

### Prune & merge — `fetchTrains()`

The REST snapshot **merges** rather than replaces, so concurrent MQTT updates
aren't clobbered and the UI doesn't flicker:

1. Compute `activeIDs` from the fetched snapshot.
2. Remove a train only if it's absent from the snapshot **and** its timestamp is
   older than a 120 s grace window (`staleTrainGrace`). The grace keeps a
   just-arrived MQTT-only train from being deleted before REST catches up; a
   genuinely-gone train stops receiving updates and ages out.
3. `apply(_:)` every fetched row (re-using the timestamp guard).
4. Garbage-collect `previousPositions` down to currently-live ids so it doesn't
   accumulate every id ever seen.

On failure it sets `errorMessage` (surfaced by the empty-state overlay on first
load). `isLoadingList` is only `true` while `trains.isEmpty`.

## Derived data the UI consumes

- **`filteredTrains(userLocation:)`** — applies the category toggles (via
  `category(for:)`; skipped until the `trainSummaries` cache loads) and the optional
  proximity radius. This is what `MapView` renders.
- **`bearing(for:)`** — returns `nil` if `speed == 0` or there's no prior
  position (first message), otherwise the haversine bearing from
  `previousPositions[id]` to the current coordinate. Drives the animated
  direction arrow (a `CAShapeLayer` on the annotation, **not** baked into the
  badge image).
- **`trainType(for:)` / `category(for:)` / `commuterLineID(for:)`** — look-ups
  into `trainSummaries`, **falling back to `trainStatuses`** (the bulk
  `/live-trains` poll) when the summary cache misses. `TrainLocation` itself carries
  no type/line info, so these caches are the only way to colour markers and label
  badges without a per-train detail fetch. The fallback matters because
  `trainSummaries` is built from `/trains/{today}` and keyed by `trainNumber`: a
  train running across the midnight boundary — cross-midnight cargo (irregular
  numbers) and night trains (`PYO`) that departed *yesterday* — isn't in today's
  schedule, so without the `/live-trains` fallback it would render with the wrong
  (heuristic orange/gray) colour. See [`MarkerColors.md`](MarkerColors.md)
  § Implementation → Cross-midnight fallback.

## On-demand detail (when a train is tapped)

`select(_:)` sets `selectedTrain` and fires two independent fetches:

- `fetchDetails(for:)` → `/live-trains/{n}` (full timetable + live delays).
- `fetchComposition(for:)` → `/compositions/{date}/{n}` (wagon makeup; absent
  for many trains).

Both guard `selectedTrain?.id == train.id` before writing results, so a slow
response for a now-deselected train can't clobber the panel if the user taps a
different marker mid-flight. Details are fetched **on demand only** — never
eagerly for all trains, which would be hundreds of requests against a per-train
endpoint.

## Why it's built this way

- **MQTT + REST bootstrap:** MQTT gives low-latency live movement but only
  deltas; the REST snapshot fills in stationary trains and is the only signal
  that a train's run has ended (→ pruning).
- **Merge, don't replace:** keeps the fast MQTT updates from being stomped by
  the slower 60 s resync.
- **Timestamp guard:** prevents the lagging resync from reversing bearing arrows.
- **Grace window:** prevents flicker/deletion of trains that MQTT knows about
  before REST does.
- **`@Observable @MainActor`:** all mutation happens on the main actor, so the
  UI updates safely with no Combine and no manual locking.

## Source map

| File | Responsibility |
|---|---|
| `Models/TrainLocation.swift` | Position struct, GeoJSON→coordinate, static `bearing()` |
| `Services/DigitrafficService.swift` | All REST calls + shared date decoder |
| `Services/MQTTClient.swift` | Hand-rolled MQTT 3.1.1 over WebSocket |
| `Store/TrainStore.swift` | State, fetch orchestration, `apply()` / prune / bearing |
