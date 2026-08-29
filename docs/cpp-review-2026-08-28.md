# Qt6 C++ review — `src/` (2026-08-28)

From `/qt-cpp-review` over the whole of `src/` (35 files, 5,346 lines) on a clean
`main` at `f9045ae`. Deterministic lint (60+ rules) followed by six parallel
deep-analysis passes: model contracts, ownership/lifecycle, thread safety, API &
C++ correctness, error handling, performance & quality.

Ranked most-severe first. Every finding below `CPP-I1` was independently verified
against the source before being written down; the investigation targets were not,
and say why.

**Lint was noisy:** 28 of 53 raw hits are false positives, listed under
[What the review did not find](#what-the-review-did-not-find) so they are not
re-litigated next round.

**Thread safety came back genuinely clean** — see the same section.

Suggested fix-order: **CPP-C1, CPP-C2** first (user-visible bugs), then
**CPP-W1, CPP-W2** (one-line win + startup robustness), then the rest.

> **Status — both criticals and the first two warnings fixed** (2026-08-29).
>
> `src/TrainDetailsService.cpp`:
> - **CPP-C1** — the timetable reply now carries the run it was issued for and is
>   dropped when the selection has moved on, mirroring `fetchComposition`.
>   `clear()` additionally lowers `loading`, which the late reply used to do.
> - **CPP-C2** — `selectionChanged` and `routeStationsChanged` are now emitted
>   only when the header fields / canonicalised route codes actually change, so a
>   live MQTT refresh no longer resets the trail or rebuilds the overlay polyline.
>
> `src/TrainListModel.cpp`, `src/DigitrafficClient.cpp`:
> - **CPP-W1** — the neighbour scan visits only `j > i` and writes the one
>   haversine to both rows, after a prologue resetting every row to the `-1`
>   sentinel. Pinned by a new `tst_trainlistmodel` target.
> - **CPP-W2** — the three metadata fetches carry `loaded`/`inFlight` state and
>   are re-issued from the 60 s resync until they land, with a capped backoff,
>   routed through `adjustPending()`, and a status line that names the degraded
>   state.
>
> Builds clean, all **7** ctest targets pass. The two criticals and CPP-W2 are
> **not covered by a regression test** — see the note under CPP-C1, which
> applies to `DigitrafficClient` for the same reason.
>
> **CPP-W6, CPP-W7, CPP-W8 fixed** (2026-08-29, parse-boundary pass): shared
> validators `inFinlandBox` / `isStationShortCode` / `isDepartureDate` in
> `DigitrafficFormat.h`, applied at the FMI `<pos>` parse, `parseTrainLocation`,
> both `show()` slots, and the MQTT topic filter; live payloads now match on
> `departureDate` as well. Pinned by four new test slots.
>
> **CPP-W5 + CPP-O1 fixed** (2026-08-29, shared-plumbing pass): one
> `digitraffic::stationLabel()` replaces three drifted copies, and the new
> `FleetMetadata` helper reads the fleet's three metadata maps through the
> client pointer instead of each service caching its own. Pinned by one new
> test slot.
>
> **CPP-W3, CPP-W9, CPP-W11 fixed** (2026-08-29, invalidation pass): the
> timetable refresh updates in place instead of resetting when the station
> sequence is unchanged; the metadata/status values are denormalised onto
> `TrainListModel::Row` at write time, so `data()` stops hashing side tables
> and the setters emit only over the rows that actually changed. Pinned by two
> new test slots.

---

## Critical — user-visible defects

### CPP-C1 — A stale timetable reply corrupts the detail panel — **Fixed**
`src/TrainDetailsService.cpp:226`

**Fixed (2026-08-29):** the reply lambda now captures `trainNumber`/`departureDate`
and drops the reply (`deleteLater` only) when they no longer match the current
selection — the same shape `fetchComposition` already used. Because a stale reply
no longer reaches `handleTrain`, nothing lowers `loading` after a `clear()` that
happens mid-flight, so `clear()` now calls `setLoading(false)` itself.

> **No regression test.** `TrainDetailsService` builds its own
> `QNetworkAccessManager` in its constructor, so there is no seam to inject a
> canned out-of-order reply pair through. Pinning this would mean taking the
> manager (or a small request-issuing interface) as a constructor dependency —
> worth doing, but a design change beyond the fix itself. Tracked as a follow-up
> rather than done silently.

The timetable request's lambda captures only `reply` — no run identity. Select
train A, then train B before A's reply lands, and `handleTrain(replyA)` applies
A's payload against the *current* selection:

- `applyTrainObject` composes `m_title` from A's `trainType` but **B's**
  `m_trainNumber` (`:262`) — the header names B.
- `m_stops` becomes A's stops; `routeStationsChanged` fires with A's route, so
  `Main.qml:75` pins and draws **A's** polyline under B's header.
- `setLoading(false)` at `:240` fires unconditionally, clearing the spinner for
  B's still-in-flight request.

If A's reply arrives after B's, the panel stays wrong until the next selection.
Both `show()` call sites (`Main.qml:424` marker click, `:526` list click) are
undebounced, and two GETs on one `QNetworkAccessManager` over HTTP/2 can complete
out of order — so this is reachable from ordinary rapid clicking.

**The asymmetry is the proof this is an oversight, not a decision.**
`fetchComposition` at `:296-302` captures `trainNumber`/`departureDate` and tests
them against current state, with a comment naming exactly this hazard ("a slow
composition reply for a previously-selected train must not overwrite the consist
now on screen"). `StationBoardService::handleReply:119` carries the same guard.
The timetable path — ten lines above the composition one — is the only one of the
three without it.

**Fix idea:** capture `trainNumber`/`departureDate` in the timetable lambda and
apply the same equality test, gating `setLoading(false)` behind it too. The guard
now exists in three hand-rolled shapes; one shared helper beats a fourth copy.

---

### CPP-C2 — Every live MQTT update wipes the breadcrumb trail — **Fixed**
`src/TrainDetailsService.cpp:268`

**Fixed (2026-08-29):** `applyTrainObject` now computes the header into locals and
emits `selectionChanged()` only when `title`/`subtitle`/`cancelled` actually
differ from what is stored, and compares the canonicalised route codes before and
after `buildStops` to decide whether to emit `routeStationsChanged()`. The first
apply after `show()` still emits both (the placeholder title `"Train N"` and the
empty subtitle always differ from the real ones), so nothing is lost on selection.
Verified that `win.selMatch` — and therefore the ring, connector and diagnostics —
is refreshed independently by the 750 ms `Timer` at `Main.qml:52`, so suppressing
the redundant `selectionChanged` does not stale them; that same timer's
`onSelMatchChanged` is what *appends* trail points, which is exactly what the
unconditional emit was erasing.

`applyTrainObject()` emits `selectionChanged()` unconditionally — including on the
`live=true` path, where the *selection* did not change and only the timetable
content refreshed. `Main.qml:66` handles `onSelectionChanged` with
`win.trailPoints = []`, so every live MQTT push for the selected train clears the
trail. With `maxTrailPoints: 8`, the trail can effectively never fill for the one
train the user is actually watching.

`routeStationsChanged()` at `:272` compounds it: it re-invalidates the
`routeOverlay.path` binding (`Main.qml:298-302`), which rebuilds a `QStringList`
and boxes the entire route polyline into a fresh `QVariantList` — tens of
thousands of `QVariant<QGeoCoordinate>` for a long-distance run — and forces
`MapPolyline` to rebuild its geometry. All of it on every live update, for a route
that never changes mid-run.

**Fix idea:** emit `selectionChanged()` only when the header fields actually
differ from the stored ones, and gate `routeStationsChanged()` on the station-code
sequence differing. Live content refreshes should reach QML through the timetable
model alone. Splitting "selection identity changed" from "header text changed"
would let the trail reset bind to the narrower signal.

---

## Warnings — real defects, lower blast radius

### CPP-W1 — The all-pairs neighbour scan computes every pair twice — **Fixed**
`src/TrainListModel.cpp:427`

**Fixed (2026-08-29):** the inner loop starts at `i + 1` and feeds the single
`distanceTo()` to both `m_rows[i]` and `m_rows[j]`. Because a row is now written
from either side of a pair, "no neighbour yet" can no longer be a loop-local
`best`: a prologue resets every row to the `-1` sentinel before the scan, which
also guarantees a pass cannot leave the previous pass's distance behind.

New `tst_trainlistmodel` ctest target pins the contract — the lone-train `-1`,
every row getting its own nearest (including the last row, only ever written as
the `j` side), and the stale-distance clear. Confirmed non-vacuous: reverting to
a one-sided write fails `everyRowGetsItsNearest`.

The scan is still O(n²); the `TrackService::Grid` idea below is untouched.

`recomputeNearestNeighbors()` runs the inner loop `j` over the full range with
`if (j == i) continue`, rather than `j > i`, so each symmetric pair is computed,
discarded, and recomputed. Each pair is a `QGeoCoordinate::distanceTo()` — a full
haversine. A 300–500 train fleet costs 90k–250k transcendental-heavy computations
on the GUI thread every 60 s snapshot, concentrated into one hitch alongside three
whole-model `dataChanged` bursts (see `CPP-W9`).

The result *is* consumed (`Main.qml:416` label declutter), so this is not dead
work — just doubled. The header comment at `TrainListModel.h:192-196` acknowledges
the O(n²) but not the 2× waste.

**Fix idea:** restrict the inner loop to `j > i` and write both `m_rows[i]` and
`m_rows[j]` from the one distance — a free 2×, one line. Beyond that, the fleet is
spatially sparse: the uniform-grid idea already implemented in `TrackService::Grid`
would make this near-linear.

---

### CPP-W2 — Metadata is fetched once at startup and never retried — **Fixed**
`src/DigitrafficClient.cpp:62-64` (handlers at `:244`, `:288`, `:319`)

**Fixed (2026-08-29):** each of the three endpoints carries a `MetadataFetch`
(`loaded` / `inFlight`), and `retryMetadata()` — called from `refresh()`, i.e.
on the existing 60 s resync — re-issues whichever have not landed. Attempts thin
out over 1, 2, 4, 8 cycles and stay capped at 8, so a long outage is not hammered
but recovery lands within ~8 minutes. The backoff is not spent on a cycle where
every outstanding attempt is still in flight, which matters because startup
issues all three and `active: true` calls `refresh()` in the same breath.

All three now go through `adjustPending()`, closing the `loading` under-report,
and the status line reads `… • station names unavailable, retrying` while any
endpoint is missing, so the degraded state is named rather than silent.

> **No regression test**, for the reason given under CPP-C1: `DigitrafficClient`
> constructs its own `QNetworkAccessManager`, so there is no seam to inject a
> canned failing reply through. Verified by reading and a live run.

`fetchStations()`, `fetchCauseCategories()` and `fetchDetailedCauseCategories()`
are called only from the constructor, and every failure path is a bare `return`
with a comment ("parked-train station snapping just stays disabled", "the
delay-cause line just stays blank"). No retry, no re-issue on any signal, no
user-visible status.

One failure permanently disables, **for the whole process lifetime**: station
code→name resolution in both services, the clickable passenger-station layer,
parked-train station pinning, and all delay-cause text. The user sees raw short
codes and blank cause lines with no indication anything failed.

This is a common real scenario, not a corner case — an app launched before Wi-Fi
associates loses these features until restart.

These three also bypass `adjustPending()` (unlike `refresh()`/`refreshCategories()`),
so the `loading` property under-reports while they are in flight.

**Fix idea:** track a loaded-flag per endpoint and re-issue from the existing 60 s
resync timer while any flag is still false, with bounded backoff. Surface a
distinguishable status so the degraded state reads as known rather than as a
rendering bug. Route the three through `adjustPending()`.

---

### CPP-W3 — The timetable model resets on every live update — **Fixed**
`src/TimetableModel.cpp:48`

`setStops()` wraps its replacement in `beginResetModel`/`endResetModel` and is
called on *every* live MQTT update, not just on a new selection. A reset destroys
every instantiated delegate and drops the user's scroll position. In practice the
stop list is identical between updates apart from a few
`estimatedArrival`/`delayMinutes`/`passed`/`isNext` fields.

The QML side already recognises the hazard and guards its *own* auto-scroll
(`TrainDetailPanel.qml:461-476`, *"Once per train — live updates won't yank the
view"*) — but that guard cannot protect against a reset the model itself performs.
A `TimetableFilterModel` proxy sits in between, so a source reset also forces the
proxy to rebuild its mapping.

**Fix idea:** keep the reset for the first load of a new selection; on a live
refresh, diff incoming stops by `stationShortCode` and, when the structure is
unchanged, assign the changed fields in place and emit one `dataChanged` over the
affected range. `TrackListModel::setVisibleSegments()` in this codebase is the
model for how to do incremental structural updates — the timetable case is easier,
since the structure is usually unchanged.

**Fixed (2026-08-29)** exactly as written: when the station sequence is
unchanged, `setStops()` assigns in place and emits one `dataChanged` over the
first..last rows whose role-visible fields moved (none at all on an identical
refresh); the reset survives only for a genuinely new selection. Pinned by
`tst_timetablemodel::liveRefreshUpdatesInPlaceWithoutReset`.

---

### CPP-W4 — Conversions between two spellings of the same Qt 6 type
`src/RailGraph.cpp:206`, `src/TrackService.cpp:236`, `src/TrackService.cpp:329`

Route station sequences are declared `QVector<QString>` in the graph/matcher API
but `QStringList` at the QML and canonicaliser boundaries, and the code bridges
them with element-wise range construction:

```cpp
QStringList(stationCodes.cbegin(), stationCodes.cend()).join(...)
```

In Qt 6 these are **the same type** — `QVector<T>` is `using QVector = QList<T>`
and `QStringList` is `using QStringList = QList<QString>`, both in
`qcontainerfwd.h` (the `class QStringList : public QList<QString>` form is
`Q_QDOC`-only). So every one of these is a full reallocation plus per-element
refcount bump to convert a type to itself.

`routeKey` is the costly one: `kickPrecompute` calls it twice per route per pass
(`TrackService.cpp:269` eviction loop and `:285` in `consider()`) over
`m_pendingRoutes`, which `handleCategories` refills with the whole live fleet on
every 60 s poll.

**Fix idea:** pick `QStringList` as the single spelling across `RailGraph`,
`TrackService`, `RouteMatchRequest::stationCodes` and `TrainRoute::codes` — it is
the idiomatic Qt name and carries the `join()` that `routeKey` wants anyway. All
three expressions collapse to passing the argument straight through. No call-site
changes, since the types were already identical.

---

### CPP-W5 — `stationLabel` exists three times with two different semantics — **Fixed**
`src/TrainDetailsService.cpp:179` and `:391` vs `src/StationBoardService.cpp:72`

- `TrainDetailsService::stationLabel` → `m_stationNames.value(code, code)`. The
  default applies only when the **key is absent**, so a code present with an empty
  name yields `""`.
- `StationBoardService::stationLabel` → reads the value, tests `isEmpty()`, falls
  back to the code for both absent *and* empty.
- A third inline copy at `TrainDetailsService.cpp:391` bypasses the class's own
  method and repeats the `value(k, k)` form.

The empty case is reachable: `DigitrafficClient::handleStations:268` inserts
`stationName` with **no** non-empty guard, while it *does* guard `code` five lines
earlier at `:263`. So one station renders as its short code on the board and as a
blank cell in the timetable panel and in `compositionLeg` (`:355`).

This is exactly the drift `DigitrafficFormat.h:9-11` was created to stop —
`hhmm`/`parseIso`/`causeText` were centralised and `stationLabel` was left behind.

**Fix idea:** move the resolver next to `causeText` in `DigitrafficFormat.h` with
the empty-tolerant fallback (the board's behaviour is the correct one — a code
beats a blank), and delete all three local copies.

**Fixed (2026-08-29)** exactly as written: `digitraffic::stationLabel(names, code,
fallback = {})` sits next to `causeText`, takes the board's empty-tolerant
behaviour, and all three copies are gone. The optional `fallback` covers
`StationBoardService::show`, the one caller that has a name of its own to prefer
over the bare code. Pinned by
`tst_digitrafficformat::stationLabelPrefersACodeOverABlankName`.

---

### CPP-W6 — FMI coordinates parsed without checking the conversion succeeded — **Fixed**
`src/FmiWeatherClient.cpp:97`

```cpp
coord = QGeoCoordinate(parts.at(0).toDouble(), parts.at(1).toDouble());
```

The `ok` out-parameter is omitted — six lines above a `ParameterValue` parse that
*does* use it. `QString::toDouble()` returns 0.0 on failure and
`QGeoCoordinate(0, 0)` is **valid**, so a malformed or truncated `<pos>` sails
through the `!coord.isValid()` gate at `:103` and is emitted as a weather overlay
point carrying a real temperature. Every malformed record in a response also
collapses onto the single `"0,0"` hash key.

The `parts.size() >= 2` guard protects against out-of-bounds, not wrong content.

**Fix idea:** use the `ok` flag on both `toDouble()` calls and skip the record when
either fails, matching the pattern already used immediately below. Since the query
pins `bbox=19,59,32,71`, also reject coordinates outside it.

**Fixed (2026-08-29)** exactly as written: both `ok` flags checked, plus a bbox
reject via the new shared `digitraffic::inFinlandBox()` — the same box the query
pins, and the same helper CPP-W7 uses. Pinned by
`tst_digitrafficformat::inFinlandBoxRejectsTheParseFailureSentinel`.

---

### CPP-W7 — Train coordinates accepted without an element type check — **Fixed**
`src/TrainListModel.h:78`

Same class of bug as `CPP-W6`, on the train path.
`QGeoCoordinate(c.at(1).toDouble(), c.at(0).toDouble())` has no type check on the
array elements. A `null`, string, or bool element yields 0.0, and (0,0) is valid —
so a malformed record plants a train marker in the Gulf of Guinea, ~7000 km
outside the network.

Impact compounds: the O(n²) neighbour pass (`CPP-W1`) reports a garbage distance
for *every* real train against it, `matchToNetwork` scans the whole track vector
for it, and because a newly-inserted row has no `rawCoordinate` anchor, the
teleport guard at `TrainListModel.cpp:243-257` cannot reject it on first sight.

**The absent-field case is already safe** — a missing `location` yields an empty
array caught by `c.size() >= 2`. The exposure is specifically a *present but
malformed* coordinate pair.

**Fix idea:** check `QJsonValue::isDouble()` before constructing, and add a coarse
Finnish bounding-box reject, mirroring the bbox the FMI client already constrains
itself to. Returning an invalid `QGeoCoordinate` makes both existing call-site
gates (`DigitrafficClient.cpp:365`, `DigitrafficMqttClient.cpp:248`) do the right
thing with no further change.

**Fixed (2026-08-29)** exactly as written: `isDouble()` on both elements and the
shared `inFinlandBox()` reject; on failure the coordinate stays invalid and the
existing call-site gates do the rest. Pinned by
`tst_trainlistmodel::malformedCoordinatesStayInvalid`.

---

### CPP-W8 — Remote strings spliced unvalidated into URL paths and an MQTT topic — **Fixed**
`src/StationBoardService.cpp:91`, `src/TrainDetailsService.cpp:220` and `:288`,
`src/DigitrafficMqttClient.cpp:120`

`stationShortCode` and `departureDate` come from remote JSON, are never
format-checked, and are interpolated directly into URL paths and an MQTT topic
string. `QUrl(url)` on an already-assembled string does not escape structural
characters, so `?`, `#`, `&` or `/` restructure the request rather than being
escaped. Both `show()` methods are `public slots`, reachable from QML with any
string.

The MQTT case is the sharper one:

```cpp
QStringLiteral("trains/%1/%2/#").arg(departureDate).arg(trainNumber)
```

This is a topic **filter**, where `+` and `#` are structural wildcards. A
`departureDate` of `+` widens the subscription to every departure date for that
train number — and `onStreamTrainMessage:382` matches only on `trainNumber`, so it
would accept the wrong run's data as live updates for the selected train.

**Fix idea:** validate at the parse boundary — a Digitraffic station short code is
a short alphanumeric token and `departureDate` is a fixed `yyyy-MM-dd`. That kills
the whole class at once and is the only option that protects the MQTT topic, where
escaping is not available. Also match live payloads on `departureDate` as well as
`trainNumber`.

**Fixed (2026-08-29)** with two validators in `DigitrafficFormat.h` —
`isStationShortCode()` (1–8 letters/digits, so `/ ? # &` and the MQTT wildcards
can never enter) and `isDepartureDate()` (strict `yyyy-MM-dd`) — applied at
`StationBoardService::show`, `TrainDetailsService::show`, and, because it is
public API in its own right, `DigitrafficMqttClient::subscribeTrain`.
`onStreamTrainMessage` now matches live payloads on `departureDate` too. Pinned
by the two new validator tests in `tst_digitrafficformat`.

---

### CPP-W9 — Whole-model `dataChanged` on delta-only updates — **Fixed**
`src/TrainListModel.cpp:455` and `:463`

`setTrainMetadata()` and `setTrainStatuses()` each emit `dataChanged` spanning the
entire model, with no comparison against previous state.
`DigitrafficClient::handleCategories()` calls both on every `/live-trains` poll —
and most polls are `?version=` **deltas** carrying only the handful of trains that
changed (`DigitrafficClient.cpp:124-134`), yet the accumulated map is pushed
wholesale and every row is invalidated.

Combined with `CPP-W1`'s full-range emission, a single 60 s cycle fires three
whole-model `dataChanged` bursts covering 7 roles, forcing QML to re-read and
repaint every marker in the `MapItemView` even when nothing about them moved.

**Fix idea:** walk `m_rows` once, note which indices actually changed, and emit
over the contiguous runs only — which is what `applyOne` already does correctly
per row. Better still, have `DigitrafficClient` pass down only the delta keys it
merged rather than the full accumulated maps.

**Fixed (2026-08-29)** by the first option, folded with CPP-W11: the setters
stamp the new values onto each `Row`, which both provides the previous value to
diff against and lets `data()` skip the side tables. `emitChangedRuns()` then
emits one `dataChanged` per contiguous run of rows that actually changed —
nothing at all when a delta poll changed nothing. Pinned by
`tst_trainlistmodel::statusRefreshTouchesOnlyChangedRows`.

---

### CPP-W10 — The hot map-matching path ignores the spatial index that already exists
`src/TrackService.cpp:151`

`matchToNetwork()` linearly scans all 4,934 tracks on every fix, rejecting each
with an inline bbox test. The class **already builds a uniform grid over exactly
these bounding boxes** in `loadNetwork():99-123` — but wires it only to
`loadForBounds()`, the *rendering* path that fires on debounced pan/zoom, while
the genuinely hot path gets no index at all. Each `Track` is a large struct, so
this is a strided, cache-hostile walk of ~600 KB of headers per call.

Tier-1 is not a rare path: it runs for every train with no resolved route
(cargo/shunting), throughout the warm-up window before `precomputeRoutes` lands,
and whenever `matchOnRoute` returns `onRoute == false` or fails its 150 m gate.

The grid cells are keyed by segment id into `m_all`, which is index-parallel to
`m_graph->tracks()`, so the existing index is directly reusable with no new data
structure.

**Fix idea:** query `m_grid` with the fix's `kSearchMeters` margin box for
candidate ids, then run the existing per-vertex projection only on those.
Correctness is unchanged because the exact bbox test still runs per hit — the same
contract `loadForBounds()` already relies on. Keep the linear scan as the
empty-grid fallback, exactly as `loadForBounds()` does.

---

### CPP-W11 — `data()` re-hashes side tables once per role — **Fixed**
`src/TrainListModel.cpp:83`

Five roles (`CategoryRole`, `TrainTypeRole`, `CommuterLineRole`,
`DelayMinutesRole`, `RingStateRole`) resolve by constructing a `TrainKey` via
`keyOf(row.pos)` and hashing it against a side table — a `qHashMulti` over the
full `departureDate` string per lookup. `ringStateFor()` adds two more such
lookups **and** calls `QDateTime::currentDateTimeUtc()` twice (`:481`, `:490`) for
what is the same instant.

A delegate reading the full role set therefore costs ~7 string hashes and 2 clock
reads — in the hottest function in the class, re-entered for every role of every
visible marker on every repaint and after each whole-model burst from `CPP-W9`.

**Fix idea:** denormalise the side tables onto `Row` at write time.
`setTrainMetadata`/`setTrainStatuses` already walk the whole model to emit
`dataChanged` — stamp the values in that same pass and let `data()` become a plain
member read. Hoist the single `currentDateTimeUtc()` into a local regardless.

**Fixed (2026-08-29)** exactly as written: `Row` carries
`category`/`trainType`/`commuterLine`/`status`, stamped by the setters and on
insert, so `data()` and `ringStateFor()` are plain member reads;
`currentDateTimeUtc()` is read once per ring resolution. The side tables remain
only to stamp rows inserted after the metadata landed.

---

### CPP-W12 — The live MQTT path lacks the REST path's payload guards
`src/TrainDetailsService.cpp:375`

`handleTrain` checks `QJsonParseError`, `doc.isArray()` and non-empty before
applying, and reports "No timetable for train %1" on failure.
`onStreamTrainMessage` forty lines later discards the parse error and calls
`applyTrainObject` on whatever `doc.object()` yields.

Since `applyTrainObject` unconditionally overwrites title, subtitle, cancelled
flag and `m_stops`, any object that parses and whose `trainNumber` matches but
which lacks `timeTableRows` **silently blanks a correctly-loaded timetable**:
`buildStops({})` is empty, `setStops()` resets the view, status reads
"0 stops · live HH:mm:ss". A missing `trainType` also degrades the header to
" &lt;number&gt;". No error is surfaced, and there is no path back short of
reselecting the train.

The exposure is a *partial* payload — an all-defaults object is correctly rejected
by the `trainNumber` test at `:382`.

**Fix idea:** require the live payload to parse cleanly and carry a non-empty
`timeTableRows` before it may replace the timetable; drop it silently otherwise,
leaving the last good REST-loaded state on screen — the behaviour
`FmiWeatherClient.cpp:117-124` already implements deliberately for its own
partial-response case.

---

### CPP-W13 — Parse-failure reporting is inconsistent across the four clients
`src/DigitrafficClient.cpp:152`, `:250`, `:294`, `:325`;
`src/TrainDetailsService.cpp:316`; `src/StationBoardService.cpp:126`;
`src/DigitrafficMqttClient.cpp:244`

Two call sites do it properly — `DigitrafficClient::handleReply:352-357` and
`TrainDetailsService::handleTrain:247-252` pass a `QJsonParseError` and surface
`errorString()`. The other seven call the single-argument `fromJson()` and discard
the reason entirely, distinguishing only "was it an array/object" from everything
else.

Four of those seven then return with **no status change and no log line at all**
(`handleCategories`, `handleStations`, `handleCauseCategories`,
`handleDetailedCauseCategories`), so a Digitraffic response-shape change, a
truncated body, or a content-encoding regression is indistinguishable from "the
feed is quiet".

This contrasts sharply with the care taken elsewhere in the same codebase —
`FmiWeatherClient` logs `xml.errorString()` with a line number, and
`NetworkDiagnostics.h` exists solely to make TLS failures attributable.

**Fix idea:** one small parse helper taking the body plus a context label,
checking the parse error and expected root type together and logging the reason on
failure. Silence remains the right *user-facing* behaviour for the optional
metadata endpoints, but a `qWarning` costs nothing and turns a silent feature
outage into something a bug report can name.

---

### CPP-W14 — The entire national network is eagerly boxed into `QVariantList`
`src/TrackService.cpp:88`

`loadNetwork()` boxes all **211,054** `QGeoCoordinate` vertices across 4,934
segments into permanently-resident `QVariantList`s in `m_all` — on top of the
unboxed copy `RailGraph` already keeps in `m_tracks`. `QVariant` cannot store a
`QGeoCoordinate` inline, so each of the 211k entries carries a separate heap
allocation; the boxed copy plausibly costs 15–20 MB against ~5 MB for the plain
one. Only viewport-intersecting segments are ever handed to QML, so the
overwhelming majority of that boxing is never consumed.

The comment at `:148-150` says the matcher avoids the boxed copy — true, but it
does not follow that the boxed copy needs to exist for the whole country up front.

**Fix idea:** store the bbox and a track index in `Segment` and box lazily the
first time `loadForBounds()` selects a segment, cached in a bounded LRU keyed by
segment id (a viewport holds a few hundred at most). If the eager form is kept for
startup simplicity, at least document the memory cost next to `m_all`.

---

## Opportunities — consistency and hygiene

### CPP-O1 — Duplicated fleet-metadata plumbing across two services — **Fixed**
`src/StationBoardService.cpp:28-74` vs `src/TrainDetailsService.cpp:177-198`

Near-identical copies of: the same three `QHash<QString,QString>` members, the
same `setFleet()` disconnect/connect/prime sequence, the same
`onStationNames()`/`onCauseCategoryNames()` handlers, the same `stationLabel()`,
and the same "copy the row vector, resolve `causeText` into it, push to the model"
rebuild (`rebuildBoard()` vs `rebuildStops()`).

Both are wired to the same `DigitrafficClient` in `Main.qml`, so they hold a
second and third refcounted copy of the same three maps. The copies have already
drifted — see `CPP-W5`.

**Fix idea:** extract the shared plumbing into a small non-QObject helper (or a
common base) owning the three maps, the wiring, `stationLabel()` and a
`resolveCauseText()`. Both `rebuild*()` also deep-copy their whole row vector on
every metadata arrival purely to stamp `causeText`; resolving that at model-read
time removes the copy from both.

**Fixed (2026-08-29)** with `src/FleetMetadata.h`, a non-QObject helper — but it
owns *no* maps. Nothing ever wrote to the per-service copies: they were assigned
wholesale from the client that owns the originals, so the helper holds the
`DigitrafficClient *` and reads through it, and the three accessors now return
`const &`. That deletes six members, both `stationLabel()`s, and the copy halves
of all four handlers; each service keeps its own `fleet` property and signal
wiring (the handlers differ in what they rebuild) and adds one
`m_meta.setFleet()` line to it.

**Not done:** resolving `causeText` at model-read time. The `rebuild*()` copy
runs when a metadata map lands — once or twice per session — not per repaint, and
pushing resolution into the models would couple them to the services and cut
against the write-time denormalisation `CPP-W11` just adopted. Revisit only if a
metadata arrival ever shows up in a profile.

---

### CPP-O2 — Transfer timeouts set three different ways
`src/StationBoardService.cpp:95`, `src/FmiWeatherClient.cpp:44`

- `DigitrafficClient.cpp:45,54` and `TrainDetailsService.cpp:24,173` — named
  `constexpr std::chrono::seconds{15}`, set on the manager.
- `StationBoardService.cpp:95` — bare `15000`, set **per request**.
- `FmiWeatherClient.cpp:44` — bare `20000`, set on the manager.

Beyond the naming inconsistency, setting it per-request in one service means any
request later added to `StationBoardService` silently gets no timeout at all. The
`int` overloads are also the deprecated form in Qt 6.7+.

**Fix idea:** hoist one `kRequestTimeout` (as `std::chrono::seconds`) into
`DigitrafficFormat.h` alongside `kUserAgent` — which exists for precisely this
"all four callers grew their own copy" problem — and set it on the manager in each
constructor via the `std::chrono` overload.

---

### CPP-O3 — `QRangeModel` rows are reported as editable
`src/TimetableModel.cpp:11`, `CompositionModel.cpp:8`, `StationBoardModel.cpp:7`,
`StationListModel.cpp:8`, `WeatherStationModel.cpp:7`

All five pass `&m_container` — a non-const pointer to a mutable `QVector` —
making the model mutable. Verified in `qrangemodel_impl.h:1302-1311`: `flags()`
adds `Qt::ItemIsEditable` when `isMutable()` and the gadget property
`isWritable()`. These rows use `Q_PROPERTY(... MEMBER ...)` with no `CONSTANT`, so
every property is writable and every item reports editable — though all five
models are semantically read-only projections republished wholesale by
`setStops`/`setRows`/`setVehicles`/`setPoints`/`setStations`.

Separately, `countChanged` is emitted **only** from the hand-written `set*`/`clear`
methods. A structural change through the inherited `insertRows()`/`removeRows()`
would change `rowCount()` without notifying, leaving QML `count` bindings stale.
Contrast `TrainFilterModel.cpp:12-14`, which correctly wires `countChanged` to
`rowsInserted`/`rowsRemoved`/`modelReset`.

**Ranked lowest of the confirmed findings deliberately:** nothing currently calls
the write API, and a QML `ListView` does not act on `ItemIsEditable`. This is
correctness-of-intent today, not a live defect.

**Fix idea:** the cheap durable half is driving `countChanged` off the model's own
`rowsInserted`/`rowsRemoved`/`modelReset` in each constructor, so the property
cannot go stale regardless of which API changed the rows. Handing `QRangeModel` a
const view would additionally strip `ItemIsEditable` — check it against the
in-place assignment pattern the `set*` methods rely on before adopting it.

---

### CPP-O4 — Surviving lint hits
25 of 53, all style/hygiene:

| Rule | Count | Where | Note |
|---|---|---|---|
| `VAR-3` | 12 | across the four clients | Direct brace init (`QNetworkRequest req{url}`). A Qt-framework-internal convention with little force in app code — safe to ignore, or normalise. |
| `HDR-3` | 8 | `TrackService.cpp:102-105,111-112`, `DigitrafficClient.cpp:177`, `TrainListModel.cpp:358` | Unparenthesised `std::min`/`std::max`. Only bites on MSVC without `NOMINMAX`; harmless on this macOS-only build. |
| `TMO-1` | 4 | `DigitrafficClient.h:30,53`, `.cpp:38,43` | Poll interval as bare `int` ms. The file already uses `std::chrono` for `kRequestTimeout`. Moot if `CPP-I9` resolves to "delete". |
| `DEP-7` | 1 | `DigitrafficClient.cpp:94` | `qMax` → `std::max`. Also moot under `CPP-I9`. |

---

## Investigation targets

Not verified — each says why, and how to settle it. Capped at 10 by confidence;
five further candidates in the 60–62 band were dropped by the cap.

### CPP-I1 — `TrackListModel` inserts run elements one at a time (74)
`src/TrackListModel.cpp:82` — a run of `n` new segments into a model of `m` rows
costs O(n·m) element moves across three parallel vectors. A pan into a dense
region (Helsinki, Tampere) can insert hundreds into a model holding thousands, on
the GUI thread. `QSet<int> wanted(...)` is also rebuilt per call.
**Unverified:** run sizes and model depth under a real pan not measured; the
viewport is debounced in QML, so it may sit inside the frame budget.
**Verify:** log `ids.size()`, `oldCount` and the insertion phase's wall time while
panning across Helsinki at several zoom levels; batch if it exceeds ~2 ms.

### CPP-I2 — `ringState` ages with wall-clock time but emits no `dataChanged` (72)
`src/TrainListModel.cpp:467`, consumed at `:86` — `ringStateFor()` compares
against `kStalePositionSecs` (300 s) using the current time, so a row's ring state
changes with time alone. `TrainMarker.qml:69-74` binds it once and only
re-evaluates on a `dataChanged` naming `RingStateRole`. A train that stops
reporting keeps its colour past the threshold until something else pokes the row.
**Unverified:** the 60 s poll masks it — the ring self-corrects within 60 s in the
normal path, which may be an accepted tradeoff. The failure paths (network-error
early return, `setActive(false)`) are not reachable from the shipped QML.
**Verify:** sever the network after the fleet populates and watch for a marker
transitioning to the stale presentation after 300 s. Or call
`data(index(0), RingStateRole)` twice >300 s apart with no intervening
`dataChanged` and compare.

### CPP-I3 — The resync clock is consumed on issue, not on success (72)
`src/DigitrafficClient.cpp:129` — `m_lastFullCategories = now` is assigned when
the full snapshot is *issued*. If it fails, `handleCategories` returns at `:149`
before the reset block at `:159-166`, so the clock is spent and the next ~5 minutes
are deltas merging onto never-cleared accumulated maps.
**Unverified:** the header comment at `DigitrafficClient.h:150` says "when the last
full snapshot was **issued**", so the wording may be deliberate; endpoint failure
rate unknown. Growth is bounded by fleet size — delayed trimming, not a leak.
**Verify:** inject a `HostNotFoundError` on a cycle where `full == true` and log
`m_accStatuses.size()` / `m_routePolys.size()` over the following ten minutes.

### CPP-I4 — Category comparisons against bare `const char*` (72)
`src/DigitrafficClient.cpp:404` — `cat != "Long-distance" && ...` constructs three
temporary `QString`s per iteration over the whole accumulated status map. Every
other category test uses `QLatin1String` (`TrainListModel.cpp:502`,
`TrainFilterModel.cpp:105-109`, `StationBoardService.cpp:148`), and the raw form
breaks under `QT_NO_CAST_FROM_ASCII`.
**Unverified:** cost unmeasured; the broader "four files hard-code the same three
category strings" question not chased to a recommendation.
**Verify:** build with `-DQT_NO_CAST_FROM_ASCII` and confirm this is the only new
failure in the file. Grep `"Long-distance"` across `src/` and `qml/` to size the
shared-vocabulary question.

### CPP-I5 — Row pruning happens only on the REST path (68)
`src/TrainListModel.cpp:175` — pruning and bearing-history GC live only in
`updateTrains()`, reachable only from REST success (`DigitrafficClient.cpp:369`,
behind the error early-return at `:346`). The MQTT path (`upsertTrain` →
`applyOne`) only inserts. If REST fails persistently while MQTT stays up —
independent transports, independent failure modes, and Digitraffic rate-limits
REST — `m_rows`, `m_indexByKey` and `m_previous` grow monotonically. Keys include
`departureDate`, so a midnight rollover mints a fresh key set: roughly one
fleet-day per day.
**Unverified:** requires a sustained REST-down / MQTT-up split, not reproducible
read-only.
**Verify:** point the REST base URL at an unroutable host, leave
`wss://rata.digitraffic.fi` reachable, run across a date boundary and watch
`trainClient.model.count` — it should plateau near fleet size.

### CPP-I6 — Board sort key may be an invalid `QDateTime` (68)
`src/StationBoardService.cpp:196` — `row.sortTime = digitraffic::parseIso(...)` is
stored with no `isValid()` check and is the sole `std::sort` key for the board
(`:201-203`). `parseIso` returns an invalid `QDateTime` when the string does not
parse; `hhmm()` guards on `isValid()` but `parseIso`'s callers do not.
**Unverified:** whether the comparator degenerates into a non-strict-weak ordering
(making `std::sort` itself UB rather than merely mis-ordering) depends on Qt 6's
invalid-`QDateTime` comparison semantics, not confirmable from the source tree.
Severity ranges from "a few odd rows" to something worse.
**Verify:** compare a default-constructed `QDateTime` against a valid one in both
directions and against another invalid one; check the three results form a
consistent strict weak ordering. Then feed `handleReply` a canned response with
one malformed `scheduledTime`.

### CPP-I7 — MQTT frame reassembly drains quadratically (68)
`src/DigitrafficMqttClient.cpp:204` — `m_rxBuffer.remove(0, total)` after each
decoded packet memmoves the whole remainder, and `mid()` at `:202` deep-copies
each body. Draining a frame of `k` packets is O(k²) in buffer bytes plus `k`
allocations. The buffer is only cleared on a malformed stream or disconnect, so it
can hold a backlog when the GUI thread stalls — e.g. during `CPP-W1`'s 60 s pass.
**Unverified:** no capture of real Digitraffic frame sizes or packets-per-frame;
for typical single-packet frames the current code is fine.
**Verify:** log `message.size()` and packets-decoded per `onBinaryMessage` over a
few minutes of live traffic.

### CPP-I8 — `routeKey` computed twice per route per pass (66)
`src/TrackService.cpp:266` — `kickPrecompute()` builds each route's key once for
the `live` eviction set (`:269`) and again inside `consider()` (`:285`) — a
`QStringList` construction plus `'|'` join over each route's full station
sequence, most of which immediately hit the `m_routePolys.contains(key)`
early-out. `handleCategories` also rebuilds and copies the whole `routeSequences`
vector per poll. Note `kickPrecompute` is re-entered from the `QFutureWatcher`
handler at `:310`, so it can run more than once per poll.
**Unverified:** live route count and per-key join cost unmeasured; at 60 s cadence
a few ms is invisible.
**Verify:** log `m_pendingRoutes.size()` and time `kickPrecompute()` at peak fleet
size. Under a millisecond makes this a non-issue.

### CPP-I9 — `pollIntervalMs` may be dead API (65)
`src/DigitrafficClient.h:30` — the property, its setter and its signal have no
reader or writer in `qml/`, `src/` or `tests/`. The interval is set once in the
constructor from `kResyncIntervalMs` and never changed. The setter carries the
only `qMin`/`qMax` hit from lint.
**Unverified:** it is a public `QML_ELEMENT` property, so it may be deliberate
future tuning API, and it is documented in `src/doc/DigitrafficClient.md`.
**Verify:** confirm whether the interval is meant to be QML-tunable; `git log` the
property to see whether it ever had a caller.

### CPP-I10 — Station coordinate insert unguarded, one line above a guarded one (65)
`src/DigitrafficClient.cpp:265-268` — `coords.insert(code, coord)` is
unconditional while the very next block filters `passengerStations` on
`coord.isValid()`. A record with a missing lat/lon yields (0,0) — valid — and
reaches `TrainListModel::m_stationCoords`. The asymmetry within one loop body
suggests the guard was intended for both.
**Unverified:** both readers (`nearestRouteStationCode`, the stopped-train pin)
apply a 250 m radius, and (0,0) is ~7000 km from any Finnish fix, so it can never
win the comparison. Latent, contingent on a future reader without a distance bound.
**Verify:** decide whether `m_stationCoords` is intended as a general lookup or
permanently a radius-gated snapping table.

---

## What the review did not find

**Thread safety is genuinely clean — zero findings.** The codebase is effectively
single-threaded. A grep for `QThread`, `moveToThread`, `std::thread`, `QRunnable`,
`QThreadPool`, `QMutex`, `std::atomic` and friends hits exactly one file:
`TrackService.cpp`. Both `QtConcurrent::run` tasks are correctly structured —
`loadNetwork` is a *static* member touching no instance state, and the route
precompute at `:312` captures a `shared_ptr` **copy** and `todo` by value with no
`this` capture. Results are applied to members only in the
`QFutureWatcher::finished` handler, on the GUI thread. No `Qt::DirectConnection`
anywhere (the only explicit connection type in the tree is `Qt::QueuedConnection`
at `main.cpp:15`), no model mutation off-thread, no GUI-thread blocking (zero hits
for `QEventLoop`, `waitFor*`, `msleep`, `processEvents`).

> **Invariant worth writing down:** this rests on `RailGraph` staying free of
> mutable state — the GUI thread and the precompute worker read the same instance
> concurrently with no lock. Memoising Dijkstra results in a `mutable QHash`
> inside `routePath()` is exactly the optimisation someone reaches for next, and
> it would introduce a silent race with no compiler or review signal. The comment
> at `RailGraph.h:12-14` ("no QObject, no resource/threading concerns") reads as
> thread-*agnostic* rather than *must be immutable after load*. Worth tightening
> there and at `TrackService.cpp:299` where the `shared_ptr` is copied.

**Also checked and clean:** every `QNetworkReply` finished-handler calls
`deleteLater()` (all nine call sites); no lambda captures `this` without a context
object (all 30 `connect()` calls pass a receiver); no `Q_ASSERT` anywhere, so
neither the side-effect-in-assert nor assert-as-null-guard pattern occurs;
`QFile::open()` and the `qUncompress` result *are* checked
(`TrackService.cpp:70`); `RailGraph::loadFromJson` validates a schema version;
all endpoints are `https://`/`wss://`; `roleNames()` is a function-local
`static const` in both hand-written models; no `QRegularExpression` anywhere;
begin/end model-signal pairs all balance and no `layoutChanged` is emitted; both
proxies go through `sourceModel()->index()`/`data()` and use the non-deprecated
`beginFilterChange()`/`endFilterChange()`; `Projection.h` guards its degenerate
cases (`cos(lat)` clamp, `len2 > 0.0`); `MqttCodec::parsePublish` bounds-checks
correctly; the `QRangeModel(&m_member, parent)` lifetime pattern is safe (verified
against the Qt 6.11.1 headers — `deleteOwnedRows()` is behind an
`if constexpr` that is false here).

### Suppressed lint false positives — 28 of 53

Verified against the source; do not re-file these.

| Rule | Count | Why suppressed |
|---|---|---|
| `ERR-9` | 16 | Every endpoint is `https://`/`wss://`. Not connecting `sslErrors` is the **secure** default — connecting it is how you'd weaken validation. |
| `ERR-3` | 5 | Every handler checks `reply->error()` before `readAll()`; the linter wanted the check on an adjacent line. |
| `ERR-6` | 4 | All use the multi-arg `.arg(a, b)` overload or a chained `.arg().arg()`; the linter counts literal `.arg(` occurrences. One is a literal trailing `%` (`DigitrafficClient.cpp:421`). |
| `ERR-5` | 1 | `DigitrafficMqttClient.cpp:103` is a `QWebSocket` handshake request, where `setTransferTimeout` does not apply. All four `QNetworkAccessManager`s do set timeouts. |
| `DEP-11` | 1 | `currentDateTime()` at `:372` formats a **local** wall-clock timestamp for display; UTC would be wrong. |
| `MDL-7` | 1 | `data()` switches on the `int role` parameter, not an enum — `default:` is mandatory there, not a `-Wswitch` suppression. |

---

## Summary

| Category | Lint | Deep | Investigate | Total |
|---|---|---|---|---|
| Model Contracts | 0 | 1 | 1 | 2 |
| Ownership & Lifecycle | 0 | 1 | 2 | 3 |
| Thread Safety | 0 | 0 | 0 | 0 |
| API & C++ Correctness | 5 | 4 | 2 | 11 |
| Error Handling | 0 | 6 | 2 | 8 |
| Performance & Quality | 20 | 7 | 3 | 30 |
| **Total** | **25** | **19** | **10** | **54** |

## Suggested order

1. ~~**CPP-C1**~~ — **done.** Stale reply corrupts the panel. User-visible, and the fix already
   exists ten lines away in `fetchComposition`.
2. ~~**CPP-C2**~~ — **done.** Live updates wipe the trail. User-visible; the trail feature
   currently cannot work for the selected train.
3. ~~**CPP-W1**~~ — **done.** One-line `j > i`, free 2× on the 60 s hitch.
4. ~~**CPP-W2**~~ — **done.** Startup metadata retry. Cheap, and removes a whole
   class of "why are the station names missing" reports.
5. ~~**CPP-W5** + **CPP-O1**~~ — **done** together — centralise `stationLabel`
   while extracting the shared service plumbing it lives in.
6. ~~**CPP-W6, CPP-W7, CPP-W8**~~ — **done** as one pass — all three are "validate
   remote data at the parse boundary".
7. ~~**CPP-W3, CPP-W9, CPP-W11**~~ — **done** as one pass — all three are "stop
   invalidating the whole model when a few fields changed".
8. The rest as convenient.

Findings below confidence 60 were suppressed entirely. No source files were
modified by the review.
