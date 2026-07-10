# TrainDetailsService

## 1. Class Overview

When the user taps a train marker, TrainsOnMap opens a detail panel showing that
train's timetable, journey progress and carriage makeup. `TrainDetailsService` is
the backend that drives that panel.

On selection it fetches the run's timetable
(`/trains/{date}/{number}`) and its carriage composition
(`/compositions/{date}/{number}`) on demand, resolves station short codes to
names (shared from `DigitrafficClient.stationNames()` via the `fleet` property,
so `/metadata/stations` is fetched once per launch), and — through a shared
`DigitrafficMqttClient` — streams live timetable updates for the selected train so
delays and estimates advance in real time. It owns two models exposed to QML: a
`TimetableModel` (the stop list) and a `CompositionModel` (the carriage strip),
plus a set of header properties (title, subtitle, cancelled flag, route stations).

## 2. Project Structure and Dependencies

- **Declared in QML** (`Main.qml`) as `TrainDetailsService { id: trainDetails;
  stream: trainStream; fleet: trainClient }`. Its `show(...)` slot is called from
  a `TrainMarker` click; its properties/models feed `TrainDetailPanel`.
  `hasSelection` gates the detail-panel `Loader` and the map's Tier-2 debug
  overlay.
- **Owns:** a `QNetworkAccessManager`, a `TimetableModel` and a
  `CompositionModel` (all children).
- **Holds (not owned):** a `DigitrafficMqttClient *stream` and a
  `DigitrafficClient *fleet` (station-name source).
- **Qt modules:** Qt6::Core, Qt6::Network, Qt6::Qml (`QML_ELEMENT`).
- **Project-internal dependencies:** `TimetableModel` (+ `TimetableStop`),
  `CompositionModel` (+ `CompositionVehicle`), `DigitrafficMqttClient` (live
  stream), `DigitrafficClient` (station names),
  `RailGraph::canonicalRouteCodes` (route-key canonicalisation).

## 3. Class Hierarchy and Role

`TrainDetailsService : public QObject`. From `QObject`: signals/slots, properties,
parent ownership. Its role is an on-demand, single-selection detail controller: it
holds the currently-selected run, fans out the timetable/composition fetches,
merges live MQTT updates, and republishes the resolved data through its two models
and its header properties.

## 4. Q_PROPERTY Declarations

| Property | Type | READ | WRITE | NOTIFY | Description |
|----------|------|------|-------|--------|-------------|
| `model` | `TimetableModel *` | `model` | — | — (`CONSTANT`) | The timetable stop model. Read-only. |
| `composition` | `CompositionModel *` | `composition` | — | — (`CONSTANT`) | The carriage-order model. Read-only. |
| `hasComposition` | `bool` | `hasComposition` | — | `compositionChanged` | True when a consist was resolved for the selection. Read-only. |
| `compositionSummary` | `QString` | `compositionSummary` | — | `compositionChanged` | e.g. "6 cars · 178 m · max 200 km/h". Read-only. |
| `compositionLeg` | `QString` | `compositionLeg` | — | `compositionChanged` | Departure-section leg, resolved to names, e.g. "Helsinki → Joensuu". Read-only. |
| `compositionSectionCount` | `int` | `compositionSectionCount` | — | `compositionChanged` | Number of journey sections; > 1 when the consist changes en route. Read-only. |
| `hasSelection` | `bool` | `hasSelection` | — | `selectionChanged` | True while a train is selected. Read-only. |
| `loading` | `bool` | `isLoading` | — | `loadingChanged` | True while the timetable request is in flight. Read-only. |
| `status` | `QString` | `status` | — | `statusChanged` | Human-readable status ("Loading timetable…", "12 stops · live 14:05:09"). Read-only. |
| `title` | `QString` | `title` | — | `selectionChanged` | Header title, e.g. "IC 55027". Read-only. |
| `subtitle` | `QString` | `subtitle` | — | `selectionChanged` | Header subtitle, e.g. "Long-distance · VR". Read-only. |
| `trainNumber` | `int` | `trainNumber` | — | `selectionChanged` | Selected train number. Read-only. |
| `departureDate` | `QString` | `departureDate` | — | `selectionChanged` | Selected run's departure date ("YYYY-MM-DD"). Read-only. |
| `cancelled` | `bool` | `cancelled` | — | `selectionChanged` | True when the run is cancelled. Read-only. |
| `routeStations` | `QStringList` | `routeStations` | — | `routeStationsChanged` | Ordered station short codes of the selection, canonicalised to match `TrackService.routePolyline`'s route key. Read-only. |
| `stream` | `DigitrafficMqttClient *` | `stream` | `setStream` | `streamChanged` | The MQTT client used to stream live updates for the selected train. |
| `fleet` | `DigitrafficClient *` | `fleet` | `setFleet` | `fleetChanged` | Source of the station code → name map (its one-shot `/metadata/stations` fetch), so the endpoint isn't fetched twice at startup. |

## 5. Enumerations

None.

## 6. Public Member Variables

None public.

## 7. Signals

#### void selectionChanged()

Emitted when the selection or any header field (title/subtitle/number/date/
cancelled/`hasSelection`) changes.

#### void loadingChanged()

Emitted when the `loading` flag flips.

#### void statusChanged()

Emitted when `status` changes.

#### void streamChanged()

Emitted when the `stream` is (re)assigned.

#### void fleetChanged()

Emitted when the `fleet` (station-name source) is (re)assigned.

#### void routeStationsChanged()

Emitted when the resolved `routeStations` change (after a timetable loads or a
live update rebuilds the stops). `Main.qml` (re)pins the route for the Tier-2
overlay in response.

#### void compositionChanged()

Emitted when any composition field (`hasComposition`, summary, leg, section
count) changes.

## 8. Public Slots and Q_INVOKABLE Methods

#### void show(int trainNumber, const QString &departureDate)

Selects a run and loads its detail. Sets the header to a placeholder, clears the
old timetable, issues the timetable GET, clears + fetches the composition, and —
if a `stream` is set — subscribes to the train's live MQTT topic. Reports an error
status and does nothing further when `departureDate` is empty.

#### void clear()

Deselects: resets all header state, clears both models and the composition,
clears the status, and (if a stream is set) unsubscribes from the per-train MQTT
topic. No-op when nothing is selected.

## 9. Public Methods

The property getters listed in section 4 (`model()`, `composition()`,
`hasComposition()`, `compositionSummary()`, `compositionLeg()`,
`compositionSectionCount()`, `hasSelection()`, `isLoading()`, `status()`,
`title()`, `subtitle()`, `trainNumber()`, `departureDate()`, `cancelled()`),
plus:

#### QStringList routeStations() const

Builds the route-overlay key from the last fetched stops, run through
`RailGraph::canonicalRouteCodes` (drop empty codes, collapse consecutive
duplicates) so it is byte-for-byte the same `|`-joined key `DigitrafficClient`
precomputes — preventing a silent route-lookup miss.

#### void setStream(DigitrafficMqttClient *stream)

Assigns the live stream. Disconnects from any previous stream, connects the new
one's `trainMessage` signal to `onStreamTrainMessage`, and emits `streamChanged`.

#### void setFleet(DigitrafficClient *fleet)

Assigns the station-name source. Disconnects from any previous fleet, connects the
new one's `stationNamesChanged` to `onStationNames`, pulls the current map
immediately (the names may have loaded before the wiring), and emits
`fleetChanged`. Until a fleet is set (or its fetch lands), station labels fall
back to the raw short codes.

## 10. Protected Virtual Methods / Event Handlers

None overridden. Private helpers: `onStationNames` (pull the fleet's name map),
`handleTrain`/`applyTrainObject` (timetable header + stops),
`fetchComposition`/`handleComposition`/`clearComposition` (carriage strip),
`onStreamTrainMessage` (live MQTT update), `rebuildStops` (re-resolve names and
push to the model), `setLoading`/`setStatus`, and `stationLabel`.

## 11. Ownership and Lifecycle

Parent-owned `QObject` (owned by the QML engine in `Main.qml`). The
`QNetworkAccessManager`, `TimetableModel` and `CompositionModel` are constructed
with `this` as parent, so Qt destroys them with the service. Each `QNetworkReply`
is `deleteLater()`-d in its handler; a composition reply is additionally tagged
with the run it was issued for and dropped if the selection has since changed (so a
slow reply can't overwrite the consist now on screen). The `DigitrafficMqttClient
*m_stream` and `DigitrafficClient *m_fleet` are **not owned** — their setters
carefully disconnect the old object before swapping.

## 12. Thread Safety

**GUI-thread only.** All network handling, model updates and MQTT-message handling
run on the GUI-thread event loop. No internal synchronisation.

## 13. QML Exposure

Registered with `QML_ELEMENT` (module `TrainsOnMap` 1.0). QML instantiates
`TrainDetailsService {}`, binds `stream` and `fleet`, reads the many
header/composition/status properties and the two models, and calls `show()` (from
a marker click) and `clear()`. `hasSelection` gates the `TrainDetailPanel` loader
and the map overlay.

## 14. Inter-Class Interactions

- **`TimetableModel`** (owned): receives `setStops`/`clear`; the model derives
  journey progress (`passed`/`isNext`) from the stops.
- **`CompositionModel`** (owned): receives `setVehicles`/`clear` for the carriage
  strip.
- **`DigitrafficMqttClient`** (shared, not owned): `subscribeTrain`/
  `unsubscribeTrain` are driven on selection changes, and its `trainMessage`
  signal feeds `onStreamTrainMessage` for live updates.
- **`DigitrafficClient`** (shared, not owned): its `stationNames()` map is pulled
  on `stationNamesChanged` (and on wiring) for station-label resolution.
- **`Main.qml`** polls `model.matchInfoFor(...)` for diagnostics, pins
  `routeStations` into `TrackService.pinRoute`, and binds
  `TrackService.routePolyline(routeStations)` for the route overlay.

## 15. External Communication

**Outbound HTTPS (REST), GUI-thread async**, against `https://rata.digitraffic.fi`:

- `GET /api/v1/trains/{departureDate}/{trainNumber}` — the run's timetable rows,
  folded into merged stops.
- `GET /api/v1/compositions/{departureDate}/{trainNumber}` — the carriage
  composition; a 404 just means no stock data (common for commuter/freight) and
  leaves the strip hidden.

All requests send the `Digitraffic-User` identification header; the transfer
timeout is 15 s. Live updates additionally arrive **inbound over MQTT** indirectly,
via the injected `DigitrafficMqttClient`'s `trainMessage` signal (this class does
not open the MQTT connection itself). All reply/signal callbacks fire on the GUI
thread.
