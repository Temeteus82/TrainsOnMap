# Qt6 C++ review, round 2: `src/` + `tests/` (2026-09-25)

This is the output of `/qt-cpp-review` over every C++ file in `src/` and `tests/` (49 files, 8,244 lines) on a clean `main` at `9f4acd5`. It follows [the 2026-08-28 review](cpp-review-2026-08-28.md), and every finding from that round is closed. The run used deterministic lint (60+ rules), then six parallel deep-analysis passes: model contracts, ownership/lifecycle, thread safety, API & C++ correctness, error handling, and performance & quality.

IDs use a `CPP2-` prefix so they don't collide with the round-1 `CPP-*` / `C1` tags already quoted in source comments.

Findings are ranked most severe first. Everything above the investigation targets was checked against the source before it was written down. Several of those findings were raised independently by two or more of the six passes. The investigation targets were not checked, and each one says why.

**All lint hits are noise.** None of the 103 raw hits survive: 49 are false positives and 54 are style-only. They are listed under [What the review did not find](#what-the-review-did-not-find) so nobody re-litigates them next round.

**Suggested fix order:** fix **CPP2-C1** and **CPP2-C2** first, since both are user-visible. They share a root cause with **CPP2-I9**, so fix all three in one pass. Then do **CPP2-W1** and **CPP2-W2**, then the rest.

## Status (2026-09-25)

| Finding | Status |
|---|---|
| CPP2-C1, CPP2-C2, CPP2-I9 | **Fixed** in #83: a request-generation counter in both services; stops cleared before `selectionChanged`. New `tst_stationboardservice` added. |
| CPP2-W1 | **Fixed** in #84: the position poll now uses `parseArray`, and a parse failure refreshes the rings. |
| CPP2-W2 | **Fixed** in #85: board destinations are resolved in `rebuildBoard()` and rebuilt on `stationNamesChanged`. |
| CPP2-W3, CPP2-W4 | **Fixed** in #86: `m_previous` and the unreachable no-grid scans are deleted. The W3 fix idea below said `tst_trainlistmodel` covered bearing. It did not, so #86 adds `bearingFollowsSuccessiveFixes`. |
| CPP2-I1, I5, I6, I10 + `MqttCodec` test | **Next.** These are the MQTT robustness targets. None has been checked against the source yet. |
| CPP2-I2, I3, I4, I7, I8 and the items below the target cap | Open. |

---

## Critical: user-visible defects

### CPP2-C1: A superseded station-board reply turns off the spinner for the current request
`src/StationBoardService.cpp:107`

`handleReply` calls `setLoading(false)` on line 107. The stale-reply check (`code != m_stationCode || !m_hasSelection`) comes after it, on line 109. The sequence goes wrong like this:

1. Click station A, then station B, before A's reply lands.
2. A's reply clears `loading` and is then dropped.
3. B's request is still in flight, but the panel shows B's empty board with the status "Loading board…" and no busy indicator.
4. Nothing raises `loading` again before B's reply lands.

`show(A)`, `clear()`, `show(A)` also passes the code check, so the first reply renders early.

This is the same bug as round-1 **CPP-C1**, which was fixed in `TrainDetailsService.cpp:210-222` by filtering stale replies in the `finished` lambda before anything touches `loading`. `StationBoardService` never got the same fix.

Four of the six passes (ownership, error handling, API, performance) reported this independently.

**Fix idea:** check for staleness before touching `loading`, in the connect lambda the way `TrainDetailsService::show` does. To also handle repeated clicks on the same station, tag each request with a generation counter that `show()` and `clear()` increment. That counter also closes CPP2-I9. `tst_traindetailsservice` already has the `FakeNetwork` out-of-order pattern for pinning this with a test (select A, select B, land A, assert still loading).

### CPP2-C2: Selecting a new train pins and draws the previous train's route
`src/TrainDetailsService.cpp:197-199`, with `qml/Main.qml:66-82` and `:319`

`show()` emits `selectionChanged` on line 197, and only then runs `m_stops.clear()` and `m_model->clear()`. Two things read `trainDetails.routeStations` on that signal while `m_stops` still holds the **old** train's stops:

- the `onSelectionChanged` handler (`Main.qml:75`), which calls `trackService.pinRoute(...)`
- the `routeOverlay.path` binding (`:319`)

The new selection therefore pins and draws the old route. The only thing that corrects it is `applyTrainObject` when the timetable arrives: it compares against the now-empty list and emits `routeStationsChanged`.

If the timetable request fails or returns an empty array (the `handleTrain` early returns at `:240-247`), nothing emits. The old route then stays drawn under the new header, and stays pinned in `TrackService`, which means it is exempt from eviction.

**Fix idea:** clear `m_stops` and the model before emitting `selectionChanged`. Emit `routeStationsChanged` whenever the canonical route goes from non-empty to empty, as `clear()` already does. You can pin this with a test by failing the timetable reply after a previous selection had stops, then asserting that `routeStations()` is empty.

---

## Warnings: real defects, lower blast radius

### CPP2-W1: Position-poll parse failures give a misleading status and skip the ring refresh
`src/DigitrafficClient.cpp:421-427` (compare `:412-418`)

This is the one reply handler that still calls `QJsonDocument::fromJson` inline. Round-1 **CPP-W13** moved every other handler onto `digitraffic::parseArray`, but not this one, even though `train-locations/latest` is the most important endpoint. That causes three problems:

1. Valid JSON with a non-array root produces the status `"Unexpected response (no error occurred)"`, because `perr.errorString()` is used while `perr.error == NoError`.
2. Nothing is logged, which is the gap CPP-W13 closed everywhere else.
3. The network-error branch deliberately calls `m_model->refreshRingStates()`. The round-1 I2 comment explains why: "a failed poll is exactly when the markers most need re-evaluating". The parse-failure branch returns without that call, so an HTTP 200 with an HTML or truncated body leaves every marker's ring claiming the position is fresh for as long as the bad responses continue.

The error-handling, API and performance passes all reported this.

**Fix idea:** call `parseArray(body, "train-locations/latest")`, use a fixed failure status string, and run every non-success exit through `refreshRingStates()`.

### CPP2-W2: Station-board destinations stay as raw codes when station names load late
`src/StationBoardService.cpp:163`, `:53-63`, `:209-216`

`row.destination` is resolved through `m_meta.stationLabel()` once, when the reply is parsed. After that:

- `onStationNames()` refreshes only the panel title.
- `rebuildBoard()` re-resolves `causeText` but not the destination.

Suppose `/metadata/stations` fails at startup, so the retry backs off for up to about 8 minutes (`kMetadataBackoffMax`). A board opened in that window shows every destination as a code. When `stationNamesChanged` fires, only the header updates, and the codes stay until the user selects the station again. `TrainDetailsService::rebuildStops()` doesn't have this problem because it re-resolves `stationName` on every rebuild.

**Fix idea:** keep the destination code in the row, the same way `causeCode` is kept. Resolve the name in `rebuildBoard()`, and call `rebuildBoard()` from `onStationNames()` when rows exist.

### CPP2-W3: `m_previous` duplicates each row's coordinate
`src/TrainListModel.h:251`; `src/TrainListModel.cpp:127-137, 222-223, 255-256, 349`

`bearingFor()` stores `matched.coordinate` in `m_previous[key]`, and the same statement pair (`:439-440`, `:457-458`) writes the same value to `row.pos`. Both early returns, the staleness check and the teleport guard, skip both writes. So for every live key, `m_previous[key]` always equals `m_rows[m_indexByKey[key]].pos.coordinate`.

The copy costs a second `TrainKey` hash insert on every MQTT message and an extra lookup in `applyOne`. It also needs two garbage-collection loops (`updateTrains`, `pruneAbandonedRows`) whose only job is to keep it bounded.

**Fix idea:** read the previous coordinate from the row `applyOne` has already found, then delete `m_previous` and both GC loops. `tst_trainlistmodel` covers bearing, so a green run is enough proof.

### CPP2-W4: Unreachable "no grid" fallback branches and a comment that isn't true
`src/TrackService.cpp:269-271, 393-397, 606-614`

`RailGraph::loadFromJson` drops every track with fewer than 2 points (`RailGraph.cpp:134`), so `loadNetwork()` never skips a track. That has two consequences: segment id always equals `trackIndex`, and the grid is non-empty whenever the graph is.

- The linear-scan `else` in `matchToNetwork` can't run, because an empty graph already returns at `:324`.
- The `else` in `loadForBounds` iterates an empty `m_all`.
- The comment "segment ids are NOT parallel to the track indices" is false.

**Fix idea:** delete both branches and fix the comment. The `Segment::trackIndex` indirection could go too, but it's harmless.

---

## Investigation targets

These were not checked against the source. Each one is listed with its confidence out of 100, what is still unknown, and how to confirm it.

### CPP2-I1: SUBACK return codes are ignored (75)
`src/DigitrafficMqttClient.cpp:238-240`

Any SUBACK sets the status to "Live (MQTT)". The return-code byte is never read, and 0x80 means failure (MQTT 3.1.1 §3.9.3). If the broker refuses `train-locations/#`, the status says Live while no PUBLISH ever arrives. The train-topic SUBACK also overwrites the status.
**Unverified:** we don't know whether the Digitraffic broker ever refuses these filters.
**To check:** subscribe to a bogus filter and look at the payload byte.
**Fix idea:** treat a trailing 0x80 as "subscription refused".

### CPP2-I2: Nearest-neighbour recompute emits `dataChanged` over every row on every snapshot (75)
`src/TrainListModel.cpp:510-511`

`recomputeNearestNeighbors()` emits one `dataChanged` over rows 0 to n-1 on every REST snapshot, whether or not any value changed. That contradicts the change-only `emitChangedRuns` approach from round-1 CPP-W9. On Qt 6.11, `QSortFilterProxyModel::_q_sourceDataChanged` does not return early based on the role list, so `TrainFilterModel` re-filters and re-sorts the whole fleet every poll. The declutter binding at `Main.qml:438` also re-runs.
**Unverified:** the cost hasn't been measured.
**To check:** run the `qt-qml-profiler` skill across a poll.
**Fix idea:** compare each row's old and new value (with a small tolerance) and emit through `emitChangedRuns`.

### CPP2-I3: Silent failure paths on the weather, category and metadata fetches (75)
`src/FmiWeatherClient.cpp:80-81`; `src/DigitrafficClient.cpp:141-142, 253-254, 313-314, 352-353`

Network failures on these paths return with no log line and no status: the FMI weather feed, the `/live-trains` category poll, and the three metadata fetches. `parseArray` logs parse failures only, not transport errors. When offline, delays, colours and punctuality go stale without any warning, and a weather outage can't be seen at all because `FmiWeatherClient` has no status property.
**Unverified:** staying quiet may be deliberate for background feeds.
**Fix idea:** add one `qWarning` with `errorString()` on each path, and consider giving `FmiWeatherClient` an error flag.

### CPP2-I4: Two different "late" thresholds (72)
`src/DigitrafficClient.cpp:488` vs `src/TrainListModel.cpp:26, 622`

Checked in the code: the punctuality tally counts `delayMinutes <= 5` as on time, while the ring turns amber at `>= kLateMinutes` (5). A train exactly 5 minutes late gets an amber "late" ring but counts as on time in the punctuality line.
**Unverified:** the "≤5m" label suggests the punctuality boundary may be intentional, following the usual 5-minute punctuality definition.
**Fix idea:** pick one convention and share one named constant.

### CPP2-I5: No MQTT liveness or CONNACK watchdog (70)
`src/DigitrafficMqttClient.cpp:59-60, 249-251`

PINGREQ is sent every 30 s, but a missing PINGRESP is never noticed. On a half-open TCP connection (a NAT drop or a Wi-Fi roam), the status stays "Live (MQTT)" until TCP retransmission gives up, which is about 15 minutes on Linux. A broker that accepts the WebSocket but never sends CONNACK leaves the status stuck on "Authenticating…". This also covers the lint's ERR-5 hit on the WebSocket open request.
**Unverified:** the 60 s REST resync hides most of the effect.
**To check:** drop the network with `iptables` while the stream is up.
**Fix idea:** track the time of the last received packet. On each ping tick, if nothing has arrived for about 1.5 × keepalive, `abort()` the socket, which triggers the existing reconnect. Arm the same deadline when CONNECT is sent.

### CPP2-I6: MQTT reconnect has no backoff (70)
`src/DigitrafficMqttClient.cpp:26, 173-176, 233-236`

A broker refusal, the malformed-stream path and socket errors all end up in `onSocketDisconnected`, which restarts the same fixed 5 s timer. A persistent refusal therefore causes a TLS and WebSocket handshake every 5 s, forever. `DigitrafficClient` already has a doubling metadata backoff.
**Fix idea:** double the interval on each failure up to a 2–5 min cap, and reset it on a successful CONNACK.

### CPP2-I7: Route key string rebuilt on every MQTT fix (70)
`src/TrackService.cpp:433`; `src/TrainListModel.cpp:377-389`

Every on-route position update calls `RailGraph::routeKey(req.stationCodes)`, which joins tens of station codes into a new string before the `m_routePolys` lookup. The route only changes when `/live-trains` changes it.
**Unverified:** the cost hasn't been measured.
**Fix idea:** compute the key once when the `TrainRoute` is built in `handleCategories`, and pass it through `RouteMatchRequest`.

### CPP2-I8: The two services pick the live time in opposite order (65)
`src/TrainDetailsService.cpp:20-28` (`estimate`) vs `src/StationBoardService.cpp:166-169`

TrainDetailsService prefers `actualTime` over `liveEstimateTime`, and StationBoardService does the reverse. The comment at `StationBoardService.cpp:178` says the cause extraction is the "same extraction as TrainDetailsService::buildStops", so the two were meant to match.
**Unverified:** this only matters if Digitraffic keeps `liveEstimateTime` after `actualTime` is recorded.
**Fix idea:** move "best known time for a row" and "first cause of a row" into `DigitrafficFormat.h` next to `localTime` and `causeText`.

### CPP2-I9: `TrainDetailsService` matches replies by run, not by request (65)
`src/TrainDetailsService.cpp:216-222, 307-313`

Replies are matched by `(trainNumber, departureDate)`. Two quick taps on the same train, or `show(A)`, `clear()`, `show(A)`, let the first reply clear the spinner while the second is still in flight, and the timetable is applied twice.
**Unverified:** the final state ends up correct.
**Fix idea:** the same generation counter as CPP2-C1, so do them together.

### CPP2-I10: A malformed MQTT remaining-length field is accepted (65)
`src/MqttCodec.h:51`

If all 4 varint bytes have the continuation bit set, the function returns true instead of reporting a protocol error. The value stays in bounds, so there's no overflow and no out-of-bounds read. But the 5th byte is read as the start of the body, the stream loses sync, and garbage packets can be dispatched. MQTT 3.1.1 §2.2.3 says the connection should be closed.
**Unverified:** in practice the broker is behind TLS, so the risk is low.
**Fix idea:** add a third "malformed" result and take the existing drop-and-reconnect path. `MqttCodec` has **no unit test**; a small one covering the varint edge cases and short or oversized `topicLen` would pin this down.

### Below the 10-target cap (60–65), not investigated

- **`TrainFilterModel` calls `beginFilterChange()` after the state change** (`src/TrainFilterModel.cpp:50-87`). This was checked: each setter assigns its member first, then calls `refilter()`, which runs begin and end back to back. Qt's docs say to call `beginFilterChange()` before the parameters change, which is what `TimetableFilterModel::setShowAll` does. There's no effect today because the mapping already exists.
- **Links between services are raw non-owning pointers.** These are `m_stream`, `m_fleet`, `m_matcher` and `m_model` across `TrainDetailsService.h:123-124`, `StationBoardService.h:73`, `FleetMetadata.h:60`, `DigitrafficClient.h:149`, `TrainListModel.h:259` and `DigitrafficMqttClient.h:143`. They are safe only because of how `Main.qml:89-121` lays out the app-lifetime siblings. Using `QPointer` would be a one-line change per member, and the existing null checks would then cover destruction automatically.
- **Per-train type, category, line and status live in four parallel `TrainKey` hashes** in both the client and the model. Each poll's insert forces a detach. One `QHash<TrainKey, TrainMeta>` would avoid that.
- **No incoming WebSocket size cap.** `kMaxPacketBytes` is checked only after QWebSocket has buffered the whole message. `setMaxAllowedIncomingMessageSize`/`FrameSize` would move the cap earlier.
- **Roles that can't change are sent on every MQTT upsert** (`TrainListModel.cpp:448-450`): `DepartureDateRole`, which is part of the row key, and `TimestampRole`, which no QML reads.
- **`parseTrainLocation` bypasses `digitraffic::parseIso`** (`TrainListModel.h:89-90`). Behaviour is the same today; it's just another private copy.
- **Minor.** `applyOne` looks up `m_indexByKey` three times (`:286, :341, :434`). `StationBoardService.cpp:16-17` has an empty anonymous namespace. The three `fetchX`/`handleX` metadata pairs in `DigitrafficClient.cpp:236-371` are near-copies of each other.

---

## What the review did not find

**Thread safety is still clean, with zero findings.** The only background work is the two `QtConcurrent::run` jobs in `TrackService.cpp`, and both are unchanged since round 1:

- `loadNetwork` is static.
- The precompute captures a `shared_ptr` copy and does no `this` capture.
- `m_precomputing` allows one batch at a time.
- Results are applied in the child `QFutureWatcher::finished` on the GUI thread.

`RailGraph` still has no `mutable` members, `const_cast` or function-local statics. The round-1 invariant ("RailGraph must stay immutable after load") still holds. `mutable m_boxed` (`TrackService.h:177`) is touched only on the GUI thread.

**The MQTT codec has no memory-safety bug.** The remaining-length varint is capped at 4 bytes, so `1 + 4 + 268,435,455` fits in an `int`. `kMaxPacketBytes` is checked before any wait for the body. `parsePublish` bounds-checks `topicLen` and the QoS packet id. The cursor erase is clamped. CPP2-I10 is a protocol-conformance gap, not an out-of-bounds read.

**Model contracts are clean.** In `TrainListModel` and `TrackListModel`:

- Every begin/end pair balances.
- `data()` handles all 14 roles in `roleNames()`.
- Every `dataChanged` names its roles.
- `countChanged` fires on every structural change.
- `TrackListModel` passes `QAbstractItemModelTester`.

`TimetableModel` updates in place when the station sequence is unchanged and resets otherwise. Both proxies read only through `data()`/`index()`. QML maps source `nextStopRow` through `proxyRowForSource` before `positionViewAtIndex`.

**Also checked and clean:**

- Every `QNetworkReply` is `deleteLater`'d on every path, including error, parse failure, stale drop and `!m_active`.
- Every `finished` lambda passes `this` as its context object.
- No reply pointer is kept in a member.
- Every `new` has a parent.
- There's no `Q_ASSERT` anywhere.
- `main.cpp` has no context properties or C++ singletons, and the engine is destroyed before `app`.
- The API side is clean: no get-prefixed getters, every `Q_PROPERTY` has NOTIFY or CONSTANT, enums have an explicit underlying type and a trailing comma, and every `.first()`/`.last()`/`at()` is guarded.
- There's no `QRegularExpression` anywhere, and both `roleNames()` use a `static const` table.

### Suppressed lint hits: all 103

These were checked against the source; do not re-file them.

| Rule | Count | Why suppressed |
|---|---|---|
| `ERR-9` | 24 | Every endpoint is `https://`/`wss://`, and SSL errors are logged but never ignored. Leaving `sslErrors` unconnected is the **secure** default (same reasoning as round 1). |
| `HDR-3` | 10 | `CMakeLists.txt:20` already sets `add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)` on Windows. |
| `ERR-3` | 5 | Every handler checks `reply->error()` a few lines before `readAll()` (`DigitrafficClient.cpp:412`, `FmiWeatherClient.cpp:80`, `StationBoardService.cpp:111`, `TrainDetailsService.cpp:238`, `:320`). |
| `ERR-6` | 4 | These use chained `.arg().arg()` or the multi-arg `.arg(a, b)`. |
| `LCY-1` | 1 | `DigitrafficClient.cpp:421`: `deleteLater()` is the first statement of `handleReply` (`:409`). |
| `MDL-7` | 1 | `data()` switches on `int role`, where `default:` is required. |
| `ERR-5` | 1 | This is the `QWebSocket` handshake request, where `setTransferTimeout` does not apply. The liveness gap is CPP2-I5. |
| `DEP-11` | 1 | `DigitrafficClient.cpp:447` formats **local** wall-clock time for display (U2-W6); UTC would be wrong. |
| `ERR-1` | 1 | `tst_railgraph.cpp:396` is wrapped in `QVERIFY`. |
| `ERR-2` | 1 | `tst_traindetailsservice.cpp:65` parses the test's own fixture. |
| `DEP-10` | 38 | Style only, almost all `QSignalSpy::count()` in tests. |
| `VAR-3` | 13 | Style only (`QJsonParseError perr{}` and similar). |
| `TMO-1` | 1 | `kFullCategoriesIntervalMs` is a named `qint64` constant; `std::chrono` would be style only. |
| `DEP-7` | 1 | `qMin` in `tests/FakeNetwork.h:74`; style only. |
| `DEP-13` | 1 | `QChar` in `DigitrafficFormat.h:56`; style only. |

---

## Summary

| Category | Lint | Deep | Investigate | Total |
|---|---|---|---|---|
| Model Contracts | 0 | 0 | 1 | 1 |
| Ownership & Lifecycle | 0 | 1 | 1 | 2 |
| Thread Safety | 0 | 0 | 0 | 0 |
| API & C++ Correctness | 0 | 2 | 1 | 3 |
| Error Handling | 0 | 1 | 5 | 6 |
| Performance & Quality | 0 | 2 | 2 | 4 |
| **Total** | **0** | **6** | **10** | **16** |

Findings raised by more than one pass are counted once, in the category of their primary owner.

## Suggested order

1. **CPP2-C1 + CPP2-I9 + CPP2-C2.** These are the stale-async-state bugs. One request-generation counter pattern covers both services. Pin each with a `FakeNetwork` out-of-order test.
2. **CPP2-W1.** Move the last hand-parsing handler onto `parseArray` and refresh the rings on failure.
3. **CPP2-W2.** Resolve board destinations on rebuild.
4. **CPP2-W3, CPP2-W4.** Pure deletion.
5. **CPP2-I1, I5, I6, I10.** These are all MQTT robustness and can go in one pass together with the missing `MqttCodec` unit test.
6. The rest, as time allows.

Findings below confidence 60 were suppressed entirely. The review did not modify any source files.
