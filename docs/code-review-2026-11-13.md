# Full code review, round 3: `src/`, `qml/`, `tests/`, build (2026-11-13)

A manual read of every file in `src/`, `qml/` and `tests/`, plus `CMakeLists.txt`, `CMakePresets.json` and `scripts/bake_rails.py`, on a clean `main` at `39c4d43`. It follows [the 2026-09-25 C++ review](cpp-review-2026-09-25.md) and, unlike the two C++ rounds, covers the QML as well.

IDs use an `R3-` prefix so they don't collide with the `CPP-*`, `CPP2-*` and `U*-*` tags already quoted in source comments.

**How far each finding was checked.** Every finding was traced through the source. None was reproduced in the running app. The 11 existing tests pass on the existing `build/windows-msvc` Release build, which was not rebuilt for this review. Findings that rest on an assumption about the live feed say so.

**Suggested fix order:** R3-1 first (visible on every train, once a minute), then R3-2 to R3-5 (small QML fixes), then the MQTT group already queued from round 2, then the rest.

## Status

| Finding | Status |
|---|---|
| R3-1 | Fixed: equal timestamps are dropped and a sub-metre move keeps the previous bearing. |
| R3-2 to R3-7 | Fixed. R3-6 refreshes every 45 s. None of the QML ones was exercised by hand in the running app. |
| CPP2-I1, I5, I6, I10 + `MqttCodec` test | Fixed. `tst_mqttcodec` covers the codec; the client's watchdog, backoff and SUBACK handling have no test and were not tried against the live broker. |
| CPP2-I2, I3, I7, I8 | Fixed. I3 is a `qWarning` on each path; `FmiWeatherClient` still has no error flag. |
| CPP2-I4 | Fixed by moving the punctuality line to the ring's boundary: under 5 min is on time, and the label now reads "<5m". |
| R3-L1 to R3-L7 | Fixed. R3-L1 has a `tst_railgraph` case. |
| R3-L8 | Open. The app needs a source-built QMapLibre to configure, so CI needs either that or a tests-only configure option first. The preset text that claimed CI exists is corrected. |
| R3-L9 | Fixed for the tests: they share one static library. The app still compiles its own copy, because `qt_add_qml_module` registers the `QML_ELEMENT` types from the target that compiles them. |

---

## New findings, most severe first

### R3-1: Direction arrows swing to north once a minute
`src/TrainListModel.cpp:127-132`, `:274-278`; `qml/TrainMarker.qml:285-294`

The staleness guard in `applyOne` only drops a fix that is strictly older than the row (`train.timestamp < row timestamp`). The 60 s REST resync therefore re-applies every fix whose timestamp equals the last MQTT one. The coordinate is unchanged, so `bearingFor` sees a move under 1 m and returns 0. `TrainMarker` shows the arrow whenever `speed > 0` and rotates it to `model.bearing`, so a moving train's arrow points north until its next MQTT fix arrives.

On the Tier-1 path it can be worse than 0: the re-applied fix has no heading (the move is under `kHeadingMinMoveMeters`), so the matcher may pick a different parallel track than the heading-aware match did, and the bearing becomes the sideways azimuth between the two.

**Assumption:** REST `train-locations/latest` and the MQTT stream carry the same `timestamp` for the same fix. Not checked against the live feed.

**Fix idea:** drop equal timestamps too (`<=`), and keep the row's previous bearing when the move is under 1 m. The first half also skips re-matching most of the fleet on every resync. `tst_trainlistmodel.cpp:225` pins "0 for sub-metre jitter", so that assertion changes with the fix.

### R3-2: Swapping the right-hand panel leaves focus on the train list
`qml/Main.qml:569-573`, `:600-604`

Each Loader's `onVisibleChanged` sends focus to `trainListPanel` when it hides. On a train-to-station swap (or the reverse) the new panel becomes visible first and takes focus, then the closing panel finishes its 220 ms fade, turns invisible, and pulls focus out to the train list. With reduced motion the two happen synchronously in the same order, with the same result. This undoes U2-W4 and U2-O4 for the swap case: Tab no longer starts inside the open panel, and the focus-move announcement goes to the list.

**Fix idea:** on hide, return focus to the list only when the other panel is not showing.

### R3-3: Selection highlight ignores the departure date
`qml/Main.qml:342`

`selected` compares `trainDetails.trainNumber === model.trainNumber` only. Two runs of the same number under different departure dates, the case `TrainKey` exists for, both render as selected.

**Fix idea:** also compare `departureDate`.

### R3-4: The breadcrumb trail is mostly duplicates
`qml/Main.qml:28-41`

The 750 ms Timer assigns a fresh object to `selMatch` on every tick, and `onSelMatchChanged` appends the snapped point every time, whether or not it moved. The 8-point cap therefore spans about 6 seconds, which is one or two distinct positions, so the trail is close to invisible.

**Fix idea:** skip the append when the snapped point equals the last one in `trailPoints`.

### R3-5: Reopening the same train does not scroll to the next stop
`qml/TrainDetailPanel.qml:22`, `:485-493`

`autoScrolledTrain` is keyed on the train number and never reset. Closing the panel and reopening the same train, or selecting the same number under another date, resets the list to the top and skips the auto-scroll.

**Fix idea:** reset `autoScrolledTrain` when the selection is cleared or changes, or key it on number plus date.

### R3-6: The station board never refreshes
`src/StationBoardService.cpp:66-99`; `qml/StationBoardPanel.qml`

`show()` is a one-shot fetch and there is no timer in the service or the panel. An open board's times, estimates and delays stay as they were at the click until the station is clicked again.

**Fix idea:** re-issue the request on a slow timer (30 to 60 s) while `hasSelection` is true, through the existing request-generation guard, without clearing the rows first.

### R3-7: Commuter rows in the train list show only the line letter
`qml/TrainListPanel.qml:153-158`

A commuter train's row label is just the line letter ("R"), with no number. A busy line yields a dozen identical rows, and searching by number returns a row that does not show the number that was typed. The accessible name has the same gap.

**Fix idea:** show the number next to the letter in the list ("R 9123"); the map badge can stay as it is.

---

## Still open from round 2

Checked against the current source; all unchanged.

- **CPP2-I1:** any SUBACK sets the status to "Live (MQTT)" without reading the return code (`src/DigitrafficMqttClient.cpp:238-240`).
- **CPP2-I5:** no PINGRESP or CONNACK watchdog (`src/DigitrafficMqttClient.cpp:59-60`, `:249-251`).
- **CPP2-I6:** fixed 5 s reconnect, no backoff (`src/DigitrafficMqttClient.cpp:26`, `:169-177`).
- **CPP2-I10:** a remaining-length field with all four continuation bits set is accepted (`src/MqttCodec.h:51`). `MqttCodec` still has no unit test.
- **CPP2-I2:** `recomputeNearestNeighbors()` emits `dataChanged` over every row on every snapshot (`src/TrainListModel.cpp:499-500`).
- **CPP2-I3:** network failures on the weather, category and metadata fetches are silent.
- **CPP2-I4:** a train exactly 5 minutes late counts as on time in punctuality (`src/DigitrafficClient.cpp:488`) but gets an amber ring (`src/TrainListModel.cpp:611`).
- **CPP2-I7:** the route key string is rebuilt on every on-route fix (`src/TrackService.cpp:232`).
- **CPP2-I8:** the two services pick actual and estimated time in opposite order (`src/TrainDetailsService.cpp:23-25` against `src/StationBoardService.cpp:175-177`).

---

## Lower priority

### R3-L1: A dead-reckoned chainage overshoot is not self-correcting (lower confidence)
`src/TrainListModel.cpp:400-402`; `src/RailGraph.cpp:389-411`

After a Tier-2 miss the carried chainage is advanced by speed × Δt, capped at 20 km. On the next fix, `projectOntoRoute` searches the window and then only forward of `prevChainage - 150 m`. That forward search is valid whenever any route segment lies ahead, so the global fallback on line 411 is reached only when the chainage is past the end of the route. A train that actually advanced less than the estimate (it stopped during the gap) sits behind the carried chainage and stays on Tier-1 matching until it physically catches up. The comment at `TrainListModel.cpp:49-50` calls over-shooting self-correcting. Not reproduced.

### R3-L2: `compositionLeg` is not re-resolved when station names load late
`src/TrainDetailsService.cpp:371-377`

The "Helsinki → Joensuu" leg is resolved once, when the composition reply lands. `onStationNames()` rebuilds the stops but not this string, so it stays as codes. Same class as CPP2-W2.

### R3-L3: Status names the wrong missing metadata
`src/DigitrafficClient.cpp:449-451`

`metadataIncomplete()` is true when any of the three metadata fetches is unloaded, but the status always says "station names unavailable", including when only the cause-category maps are missing.

### R3-L4: One hardcoded time format left
`src/TrainDetailsService.cpp:302`

The live status uses `"HH:mm:ss"`; every other time in the app goes through the locale (U2-W6).

### R3-L5: The theme choice is not persisted
`qml/Theme.qml:15-25`

Only `reducedMotion` is aliased into `Settings`. `mode` resets to "auto" on every launch.

### R3-L6: User-agent version drift
`src/DigitrafficFormat.h:29`; `CMakeLists.txt:5`

`kUserAgent` says `TrainsOnMap/0.1`; the project is 0.1.1. The comment asks for the two to be kept in step by hand; a compile definition from `PROJECT_VERSION` would do it.

### R3-L7: Stale comments
- `src/RailGraph.h:68-70` says an internal gap aborts the whole route. `routePath` keeps the routable prefix (R6).
- `scripts/bake_rails.py` says the WGS84 GeoJSON is "Not embedded in the app yet". `CMakeLists.txt:93` embeds it.
- `CMakeLists.txt:223` says the QMapLibre DLL bundling is untested on Windows.
- `CMakePresets.json` says the official kits are "what CI ... use"; the repo has no CI.

### R3-L8: No CI
There is no workflow in the repo, so the 11 tests and the contrast check run only when someone runs `ctest` locally.

### R3-L9: Every test target recompiles its sources
`CMakeLists.txt:328-474`

Each test executable lists the sources it needs, so `RailGraph.cpp` is compiled seven times and `TrainListModel.cpp` and `TrackService.cpp` six. One static library shared by the app and the tests would shorten the build and the source lists.

---

## Checked and clean

- Every `QNetworkReply` is `deleteLater`'d on every path, and the request-generation guards in `TrainDetailsService` and `StationBoardService` drop superseded replies before they touch `loading`.
- Model begin/end pairs balance in `TrainListModel` and the `QRangeModel` subclasses; both proxies cache their role numbers before delegating `setSourceModel`.
- The only background work is the two `QtConcurrent::run` jobs in `TrackService`; `RailGraph` is immutable after load and results are applied on the GUI thread.
- Remote strings are validated before they reach a URL path or an MQTT topic filter (`isStationShortCode`, `isDepartureDate`), and coordinates are bbox-checked at every parse boundary.
- No secrets, keys or tokens in the repo or the vendored map styles.
