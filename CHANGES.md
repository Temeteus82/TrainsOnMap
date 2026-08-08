# Changes

Work log for the live-train pipeline, organised by category. Tick items as they
land; keep open items under **Known issues / follow-ups**. Each push should
leave this file describing what that push contains.

Legend: ✨ feature · 🐛 bug fix · ♻️ change/refactor · ✅ verification · 📋 follow-up

---

## `setSourceModel` cached the filter role too late

The one item the `tst_timetablefilter` PR left open, now confirmed as a real
defect rather than a theoretical one, and fixed.

### 🐛 The role was cached after the base class had already emitted
- [x] `TimetableFilterModel::setSourceModel` delegated to
      `QSortFilterProxyModel::setSourceModel` *first* and cached the `"stopping"`
      role number afterwards. The base emits `modelReset` from inside its own
      `endResetModel()`, and the proxy builds its row mapping lazily on the first
      query after that — so a client already attached to the proxy filters
      through `m_stoppingRole == -1`, takes the fail-open branch, and the
      unfiltered mapping it builds is *kept*. Every passing point stayed visible
      until something else invalidated the filter. In the app that client is the
      timetable `ListView`, because `sourceModel` is a QML binding.
- [x] Fix is the ordering: cache the role, then delegate. One line moved, plus a
      comment saying why the order is load-bearing so it doesn't get "tidied"
      back. `src/doc/TimetableFilterModel.md` updated to match — it documented
      the old sequence.

### ✅ Verification
- [x] Confirmed as a live defect *before* the fix, not assumed: the new
      `cachesStoppingRoleBeforeBaseReset` slot queries `rowCount()` from a
      `Qt::DirectConnection` `modelReset` handler and failed against the old code
      with `Actual (rowsDuringReset): 3` against `Expected: 2` — all three rows
      unfiltered, exactly the predicted fail-open.
- [x] Clean `windows-llvm` build, no warnings; `ctest` 5/5, `tst_timetablefilter`
      10/10. Reverting the ordering fails the new slot again.
- [x] No sibling callers to fix: `TimetableFilterModel` is the only
      `QSortFilterProxyModel` in `src/`, so the root cause is fixed once.

---

## `tst_timetablefilter` — the last untested model

`TimetableFilterModel` was the one model in `src/` with no test. It is also the
one whose value is invisible to row-count assertions, so the test is built around
what a regression would actually break.

### ✅ New `tests/tst_timetablefilter.cpp` (ctest `timetablefilter`)
- [x] Six slots over the real `TimetableModel` as source (not a stub — the
      proxy's cached `"stopping"` role is tied to the QRangeModel-derived role
      table, so a stub would test the wrong thing): passing points dropped when
      collapsed, `showAll` reveals them, roles pass through, `proxyRowForSource`
      maps the NEXT-stop row across the filter's row shift, re-filtering after a
      source reset (the live-MQTT `setStops` path), and the fail-open branch when
      the source has no `"stopping"` role at all.
- [x] The toggle is pinned by its **signal protocol**, not just its row count:
      `rowsInserted`/`rowsRemoved`/`modelReset` spies assert PSL enters and leaves
      at proxy row 1 with zero resets. A reset produces the identical row set, so
      row counts cannot see the difference — but it discards the ListView's
      instantiated delegates and scroll position, which is the entire reason this
      proxy exists instead of zero-height delegates.
- [x] Role coverage names the 12 `required property` roles the
      `TrainDetailPanel.qml` delegate declares (a missing one hard-errors at
      runtime). Comparing `proxy.roleNames()` to `source.roleNames()` was tried
      first and dropped: `QAbstractProxyModel` overrides `roleNames()` to forward
      to the source, so that assertion holds for *any* implementation.

### ✅ Verification
- [x] Clean `windows-llvm` build, no warnings; `ctest` 5/5.
- [x] Discriminating power checked by mutation, per the house rule: replacing
      `beginFilterChange`/`endFilterChange(Rows)` with a full model reset fails
      the test, as does gutting `filterAcceptsRow`, stubbing the cached
      `"stopping"` role to −1, or dropping a `Q_PROPERTY` from `TimetableStop`.
      One mutation did *not* fail — deleting the range guard in
      `proxyRowForSource` — because `index()` on an out-of-range row already
      returns an invalid index whose `row()` is −1. That guard is redundant
      rather than untested, and was left alone.

### 📋 Left open
- [x] `TimetableFilterModel::setSourceModel` caches the `"stopping"` role *after*
      delegating to the base, whose `endResetModel()` therefore fires while the
      role is still −1. **Confirmed real and fixed** — see "`setSourceModel`
      cached the filter role too late" above.
- [ ] `roleFor()` now exists three times across `tests/` with two different
      bodies. Harmless (role names are unique per metaobject) but it is the drift
      pattern `tst_digitrafficformat` was written to stop; needs a decision on a
      shared test header versus keeping every target standalone.

---

## Shared Digitraffic formatting helpers + `qt-cpp-review` follow-ups

Seven items from the `qt-cpp-review` audit's Left-open list below. One of them
turned out not to be a real defect — see the correction.

### ♻️ One definition of the formatting both timetable panels use
- [x] New header-only `src/DigitrafficFormat.h` (`digitraffic` namespace, the
      same idiom as `MqttCodec.h` / `Projection.h`) holding `parseIso()`,
      `hhmm()` and `causeText()`. `StationBoardService` and
      `TrainDetailsService` each carried a private copy of the timestamp
      formatter, and the causeCode→causeText composition was duplicated
      verbatim between `rebuildBoard()` and `rebuildStops()`; both services now
      call the shared helpers and hold no formatting logic of their own.
      `StationBoardService`'s separate ad-hoc `sortTime` parse folds into
      `parseIso()` too. Documented in `src/doc/DigitrafficFormat.md`.

### ✅ The audit's `hhmm()` divergence was a false alarm — corrected
- [x] The audit recorded `TrainDetailsService::hhmm()` as missing
      `StationBoardService::hhmm()`'s `Qt::ISODate` fallback, so a timestamp
      without fractional seconds would silently blank in the detail panel while
      still rendering on the station board. **Not reproducible.** When
      *parsing*, `Qt::ISODateWithMs` treats the fractional part as optional and
      accepts exactly the same strings as `Qt::ISODate` — the two enums differ
      only in `toString()`. Verified two ways: deleting the fallback changed no
      test outcome (mutation test), and a direct probe over eight timestamp
      shapes had both enums accepting all eight. So the two copies had always
      agreed and the fallback was dead code; it is not carried into the shared
      helper. `tst_digitrafficformat` now pins the both-forms behaviour, so a
      future change in Qt's parser fails the build instead of silently blanking
      times in the panels.

### 🐛 A truncated FMI response replaced the weather overlay with partial data
- [x] `FmiWeatherClient::handleData()` never checked
      `QXmlStreamReader::hasError()` after its parse loop, so a truncated or
      malformed WFS response pushed whatever records happened to parse first
      into the model as if it were a complete national overlay. It now bails on
      a parse error — keeping the previous complete overlay, since the 10-minute
      poll retries anyway — and emits a `qWarning` naming the failing line and
      reason. That is the first diagnostic logging in `src/`; there was no
      `qWarning`/`qDebug` anywhere in the C++ layer before this.

### 🐛 MQTT packet identifiers wrapped through 0
- [x] `m_packetId` (`quint16`, incremented at three bare `m_packetId++` call
      sites) wrapped to 0 after 65535 SUBSCRIBE/UNSUBSCRIBEs, and 0 is not a
      valid packet identifier in MQTT 3.1.1. All three sites now go through a
      `nextPacketId()` helper that wraps back to 1. Reachable only after a very
      long session of train selections, but free to fix; the helper preserves
      the existing id sequence (first SUBSCRIBE still uses 1).

### ♻️ The timetable rebuild copied the stop vector three times
- [x] `TimetableModel::setStops()` now takes its vector **by value** and
      `std::move()`s it into the backing container, and
      `TrainDetailsService::rebuildStops()` moves its resolved vector in — one
      copy per rebuild instead of three. This path runs on every live MQTT
      update for the currently selected train. `src/doc/TimetableModel.md`
      updated for the new signature. Left `StationBoardModel::setRows()` on the
      const-ref signature deliberately: the board rebuilds once per fetch, not
      per live update, so the same change there would be churn without a reason.

### 🐛 A TLS failure was undiagnosable on all five network clients
- [x] None of the four `QNetworkAccessManager` owners nor
      `DigitrafficMqttClient`'s `QWebSocket` handled `sslErrors`, so a
      certificate problem surfaced only as a generic "request failed" /
      "Socket error" with no reason attached. New header-only
      `src/NetworkDiagnostics.h` (`netdiag` namespace) wires one logging handler
      per manager; the WebSocket's differently-shaped signal is inlined at its
      single call site so Qt WebSockets stays out of a header that four
      non-WebSocket translation units include. Guarded on `QT_CONFIG(ssl)` so an
      SSL-less Qt still compiles.

      Deliberately **no** `ignoreSslErrors()` anywhere: these are public,
      credential-free endpoints, but silently accepting a bad certificate would
      turn a diagnostics gap into a real vulnerability. The request still fails
      exactly as before — only the reason is now recoverable.

### ♻️ `TrainDetailsService` dereferenced `m_fleet` where its sibling guards
- [x] `onStationNames()` / `onCauseCategoryNames()` dereferenced `m_fleet`
      unconditionally while the near-identical `StationBoardService` methods
      guard with `if (m_fleet)`. Not reachable today (both are only invoked from
      inside `setFleet`'s own `if (m_fleet)` branch, or by a signal from a live
      fleet), so this is drift between two sibling classes rather than a live
      bug — but it is the kind of drift that becomes a crash the first time
      someone adds another caller. Guarded to match.

### ✅ Verification
- [x] Clean `macos-clang` rebuild (`--clean-first`), zero compiler warnings;
      `ctest` 4/4 — the new `digitrafficformat` target joins railgraph /
      timetablemodel / compositionmodel. The new test's discriminating power was
      itself checked by mutation, which is how the false alarm above surfaced.
- [x] The TLS handler was exercised end-to-end against a known bad-certificate
      host (`self-signed.badssl.com`) with a throwaway probe: it logged
      *"TLS error for https://self-signed.badssl.com/: The certificate is
      self-signed, and untrusted"* and the request still failed with
      `QNetworkReply::SslHandshakeFailedError` — confirming the handler both
      fires and does not suppress the failure. `QT_FEATURE_ssl` is 1 in this
      Qt 6.11.1 build, so the guarded code is compiled in rather than stubbed.
- [x] Not eyeballed in the running app: every remaining change is either
      behaviour-preserving or on an error path (malformed XML, 65535-message
      wrap, bad certificate) that normal use doesn't reach.

---

## `qt-cpp-review` audit — src/ codebase scan

Read-only review (lint + 6 parallel deep-analysis agents) over all of `src/`.
No code changed. Model contracts, ownership/lifecycle, and thread safety came
back clean; open items below are from API/correctness, error handling, and
performance/quality.

### 📋 Left open
The first seven items below are now closed — see "Shared Digitraffic formatting
helpers + `qt-cpp-review` follow-ups" above, which also records why the `hhmm()`
one was a false alarm. The rest are still open.

- [x] `StationBoardService::rebuildBoard()` and `TrainDetailsService::rebuildStops()`
      duplicate the causeCode→causeText composition logic (added by the two
      delay-cause sessions). Extract one shared helper
      `(causeCode, causeDetailedCode, categoryNames, detailedCategoryNames) -> QString`.
- [x] `TrainDetailsService::hhmm()` (`TrainDetailsService.cpp:25-33`) lacks the
      `Qt::ISODate` fallback that `StationBoardService::hhmm()`
      (`StationBoardService.cpp:21-29`) has — a timestamp missing fractional
      seconds silently blanks in the train-detail panel but still renders on
      the station board. Extract one shared timestamp-parsing helper.
      **Premise disproved:** the two parse modes are identical when parsing, so
      the panels never disagreed. Helper extracted anyway; fallback dropped.
- [x] `TimetableModel::setStops()` + `TrainDetailsService::rebuildStops()`
      copy the stop vector 3x per rebuild (fires on every live MQTT update for
      the selected train) instead of moving. `TimetableModel.cpp`'s
      `m_stops = rows;` should be `m_stops = std::move(rows);`, and
      `rebuildStops()` should move `resolved` into `setStops()`.
- [x] `FmiWeatherClient::handleData()` (`FmiWeatherClient.cpp:81-111`) never
      checks `QXmlStreamReader::hasError()` after the parse loop — a
      truncated/malformed response silently pushes partial data to the model
      with no error signal.
- [x] `m_packetId` in `DigitrafficMqttClient` (`quint16`, unguarded increment)
      wraps to 0 after 65535 SUBSCRIBE/UNSUBSCRIBE calls, which MQTT 3.1.1
      treats as an invalid packet id. Cheap fix: `if (++m_packetId == 0) m_packetId = 1;`.
- [x] `DigitrafficMqttClient`'s `QWebSocket` connection (`DigitrafficMqttClient.cpp:24-33`)
      has no `sslErrors` handler (the four `QNetworkAccessManager` owners have
      the same gap — low severity, public credential-free endpoints, but no
      diagnostic detail on a cert failure today).
- [x] `TrainDetailsService::onStationNames()`/`onCauseCategoryNames()`
      dereference `m_fleet` unconditionally; the near-identical
      `StationBoardService` methods guard with `if (m_fleet)`. Not currently
      exploitable, but the two sibling classes have drifted — add the guard
      for consistency.
- [ ] Six bare `std::min`/`std::max` calls in `TrackService.cpp:102-112` (plus
      one each in `DigitrafficClient.cpp:174` and `TrainListModel.cpp:354`)
      aren't parenthesis-protected against the Windows.h macro clash — matters
      once the `windows-msvc`/`windows-llvm` presets are actually built.
- [ ] Two unscoped enums without an explicit underlying type
      (`TrackListModel.h:26`, `TrainListModel.h:100`) risk a BiC break; add
      `: int` to both.
- [ ] `DigitrafficClient`'s `stationNames()`/`causeCategoryNames()`/
      `detailedCauseCategoryNames()` (`DigitrafficClient.h:69,75,82`) return
      `QHash` by value, copied on every call — likely negligible given call
      frequency, worth a `const&` pass only if profiling ever shows otherwise.
- [ ] `TrainListModel`/`TrackListModel`'s hand-written `roleNames()` rebuild
      the role hash on every call instead of caching it — likely harmless
      since Qt/QML calls it once per view attachment, but cheap to cache.

Not left open (verified non-issues during the review): the lint pass's five
`QString::arg()` "%2 but only 1 .arg()" hits are all false positives (chained
`.arg().arg()` or the two-arg `arg(a, b)` overload, which the single-line
scanner doesn't parse); `TrainListModel.cpp:91`'s `default:` in a role
`switch` was traced and all 14 roles are in fact handled; the pervasive
direct-brace-init style (`Type var{args};`) is this codebase's established
idiom, not a defect.

---

## `qt-ui-design` audit — Warning findings

The five Warning-tier findings from the same audit pass.

### 🐛 Carriage cells were pointer-only despite carrying rich `Accessible.description`
- [x] Each carriage cell (`TrainDetailPanel.qml`) now sets `activeFocusOnTab: true`
      and shows the same accent focus border used elsewhere (thicker when
      focused) instead of only on hover; `ToolTip.visible` now also triggers on
      `car.activeFocus`, so the type/power/amenity detail has a keyboard path,
      not just a mouse one. Verified by tabbing through an IC train's carriage
      strip: each cell gets a visible focus ring and its own tooltip in order.

### ♻️ Compact toggle rows and close buttons sat below the 44 px desktop guideline
- [x] Bumped `ToggleRow` (24→32 px), `TrainDetailPanel`'s "Show all timing
      points" checkbox (24→32 px), both panels' close buttons (26→32 px), and
      `InfoPanel`'s Appearance segmented control (30→32 px) — a step toward the
      44 px guideline without doubling the sidebar's height (five toggles stack
      per screen).

### ♻️ Carriage grid's fixed-pixel cells didn't defend against text overflow
- [x] Added `clip: true` to the carriage cell body and `elide: Text.ElideRight`
      to the car-number label, so a label that outgrows its cell (e.g. under
      OS "Large text" scaling) is contained/truncated instead of spilling into
      neighbouring cells. Left the compact `panelBody`/`TypeScale` base as-is —
      that's a documented, deliberate "dense HUD" choice, not a bug.

### 🐛 Reduced-motion opt-out had three gaps
- [x] `Theme.reducedMotion` gated the LIVE pulse, marker glide, and direction
      arrow, but not `TrainMarker`'s selection-scale bump or the two side
      panels' opacity fades (`Main.qml`) — all three now check it too.

### ✨ No feedback while a refresh is in flight
- [x] `DigitrafficClient` gained a `loading` property (`Q_PROPERTY(bool
      loading …)`), true from the moment `refresh()` issues its position +
      category requests until both replies land (a paired increment/decrement
      counter, `adjustPending()`, so the two in-flight requests don't
      double-fire the signal). The sidebar's Refresh button now reads
      "Refreshing…" and disables itself for the round-trip instead of sitting
      inert (Doherty Threshold). Verified live: clicking Refresh flips the
      button text immediately and it reverts once the fetch completes.

### 📋 Left open
- [ ] Map markers and station dots remain pointer-only (Tab-reaching an
      individual train/station to open its panel). This is a bigger,
      previously-declined scope item — the project's own history notes it
      "needs a train list view, out of scope now that the list-dependent
      feature ideas were declined" — so it's left as a known limitation rather
      than silently re-opening that decision.

### ✅ Verification
- [x] Clean `windows-llvm` build; `ctest` 3/3. All five fixes eyeballed live
      (see notes above); no regressions in the sidebar's visual density.

---

## `qt-ui-design` audit — Critical findings (ToggleRow icon, amenity colour)

Two Critical findings from a fresh `qt-ui-design` audit pass over the QML surface.

### 🐛 `ToggleRow` checkmark regressed to hardcoded `pixelSize`
- [x] Every label in the app was migrated to `font.pointSize` (so text follows
      the OS "Large text" scale) except `ToggleRow`'s checkmark, which still
      drew a raw `"✓"` glyph at `font.pixelSize: 11` — the sole `pixelSize`
      survivor in the whole `qml/` tree, and inconsistent with the `AppIcon`
      component used for every other check/close glyph. `ToggleRow` backs the
      sidebar's Commuter/Long-distance/Cargo/Siding/Weather toggles. Swapped
      for `AppIcon { name: "check"; size: TypeScale.panelIconSm }`
      (`ToggleRow.qml`), matching `TrainDetailPanel`'s own checkbox.

### 🐛 Carriage amenity dots: colour was the sole carrier, hover the only disclosure
- [x] The catering/accessible/family/pet dots on each carriage cell
      (`TrainDetailPanel.qml`) were bare colour-only circles, with the meaning
      exposed only via a hover `ToolTip` (pointer) or `Accessible.description`
      (screen reader) — a sighted, keyboard-only user had no way to tell them
      apart (WCAG 1.4.1 Use of Color). Each badge now shows the amenity
      string's own first letter (C/A/F/P) so the dots are distinguishable
      without relying on hue at all. Ink colour picked via a new
      `Theme.inkFor(fill)` — the WCAG relative-luminance formula extracted
      from `TrainMarker`'s marker-label ink picker so both call sites share
      one implementation; `TrainMarker` keeps its local wrapper only for its
      saturated-red special case before delegating to `Theme.inkFor()`.

### ✅ Verification
- [x] Clean `windows-llvm` build; `ctest` 3/3. Both fixes eyeballed live:
      confirmed the sidebar toggles render a crisp check icon, and confirmed
      an IC train's carriage strip shows legible white "P"/"C" and dark
      "A"/"F" letters on their respective amenity-colour badges.

---

## Sidebar polish — scrollbar overlap, duplicate track count

Two UI issues spotted by the user while eyeballing the running app.

### 🐛 Sidebar ScrollBar overlapped text
- [x] `InfoPanel`'s `ColumnLayout` filled the full `Flickable` width, leaving no
      gutter for the `ScrollBar` docked on the right edge — invisible while the
      panel's content fit without scrolling, but the recent `panel*` type-scale
      pass added enough rows that it now does, and the thumb drew on top of the
      last few pixels of every `fillWidth` row (worst on the wrapped attribution
      text). Reserved a 12 px gutter (`width: flick.width - 12`) so the thumb no
      longer sits on top of content (`InfoPanel.qml`).

### 🐛 Track-segment count shown twice, and disagreeing
- [x] The sidebar showed **two** track-segment counts that didn't match: the
      live viewport-filtered count (`trackCount`, e.g. "2514 track segments")
      next to a one-time "N track segments ready" status line reporting the
      *total* baked network (e.g. "4934"). Root cause: `TrackService` set that
      status once after the initial load and never cleared it, permanently
      blocking `statusText`'s intended fallback to the live train-fetch status
      (`trackService.status.length > 0 ? trackService.status : trainClient.status`
      in `Main.qml`). Cleared the status on success instead of reporting the
      total (`TrackService.cpp`) — the live count already conveys what's
      rendered, and the status line now correctly shows the live train-fetch
      status ("N trains • updated HH:MM:SS") once tracks finish loading.

### ✅ Verification
- [x] Clean `windows-llvm` build. Both fixes eyeballed live: shrank the running
      app's window to force the sidebar into scroll mode and confirmed the
      thumb no longer overlaps any row (including the wrapped attribution
      text); confirmed the sidebar now shows a single track count and the
      status line falls through to the live train-fetch status after load.

---

## Weather overlay switched to FMI open data

The road-weather proxy is replaced with real weather observations.

### ♻️ FMI observations replace the Fintraffic road-weather proxy
- [x] New `FmiWeatherClient` fetches `opendata.fmi.fi` WFS
      (`fmi::observations::weather::simple`, `parameters=temperature`, whole-country
      bbox, last-30-min window) and keeps each station's latest reading. The simple
      format is flat XML (`BsWfsElement`: coordinate + time + value inline), parsed
      with `QXmlStreamReader` — one request, so the old two-stage
      station-metadata + data fetch collapses into one. `NaN` values (missing
      sensor) are skipped. Poll every 10 min (FMI's reporting cadence).
- [x] `RoadWeatherClient` deleted; `WeatherPoint` drops the unused `name` role
      (the map chips never showed it and FMI simple has no station names).
      Sidebar toggle relabelled "Weather" (`showWeather`) — no proxy disclaimer
      needed now that it's actual weather-station data.

### ✅ Verification
- [x] Clean `windows-llvm` build; `ctest` 3/3. The client's exact query verified
      against live FMI data (~190 stations return temperature records).
      Chip rendering on the running map not yet eyeballed (same delegate as
      before, only the model source changed).

---

## Panel/marker type scale + crisper small text

### 🐛 Side-panel and marker text too large
- [x] `TypeScale` gained a `panel*` role family (`panelCaption`/`panelBody`/
      `panelSubhead`/`panelTitle` + matching icon sizes) at a fixed 0.75×
      `panelScale`, alongside the existing plain roles. `InfoPanel`,
      `TrainDetailPanel`, `StationBoardPanel`, `ToggleRow`, and the train
      marker badge text (`TrainMarker.qml`) now reference the `panel*` roles;
      the FMI weather map chip still uses the plain scale (untouched — not
      flagged).

### 🐛 Blurry small text on train markers
- [x] Shrinking the marker text to `panelCaption` (8.25 pt) exposed the
      softness of Qt Quick's default `Text.QtRendering` (GPU distance-field
      glyphs) at small sizes. Set `renderType: Text.NativeRendering` on the
      three `TrainMarker.qml` labels (badge, speed, delay) — platform-hinted
      rasterisation, crisp at small sizes, and fine here since this text is
      never transformed/scaled (NativeRendering's one real limitation).

### ✅ Verification
- [x] Clean `linux-release` build after each step; `ctest` 3/3; app launched
      (persistent, not just a smoke-timeout) and eyeballed live by the user,
      who confirmed the result looks good.

---

## Marker declutter near busy termini (e.g. Helsinki)

### ✨ Density-aware label suppression
- [x] Qt Location has no built-in marker clustering (`MapItemView` is a plain
      model→delegate repeater — checked against the Qt 6.11 docs), so this is
      app logic. `TrainListModel` now precomputes `nearestNeighborMeters` for
      every row once per REST snapshot (O(n²) over the live fleet, but only
      every 60 s, so trivially cheap); MQTT single-train upserts don't
      recompute it since a terminus is essentially stationary between polls.
- [x] `Main.qml`'s train delegate combines that with a zoom/latitude-derived
      ground resolution (`map.metersPerPixel`, standard spherical-Mercator
      formula) so `labelsVisible` drops to a bare dot whenever another train
      is closer than ~30 px on screen — on top of the existing zoom < 8
      country-scale collapse. A stationary cluster at a terminus platform now
      reads as dots instead of overlapping text badges.

### ✅ Verification
- [x] Clean `linux-release` build; app launches with no QML warnings; `ctest`
      3/3. Not yet eyeballed against a live cluster of trains at Helsinki
      (needs the app running against live Digitraffic data at the right time
      of day).

---

## Feature batch — station board, punctuality stats, road-weather overlay

Three of the deferred feature ideas, built API-first (endpoints verified against
live Digitraffic before coding).

### ✨ #1 Station departure board
- [x] Passenger stations render as clickable dots (`StationListModel`, populated
      from the existing /metadata/stations fetch — passenger stations only, shown
      at zoom ≥ 9). Clicking one opens a board panel.
- [x] `StationBoardService` fetches `/live-trains/station/{code}` (same train-object
      shape the app already parses) and turns each calling train into a board row
      (`StationBoardModel`): time + live estimate, destination, track, delay,
      arriving/departing. `StationBoardPanel.qml` lists them, sorted by time.
- [x] Shares the right-side slot with the train detail panel — selecting a station
      clears any train selection and vice versa, so they never overlap.

### ✨ #5 Punctuality stats
- [x] No stats endpoint exists, so it's aggregated **client-side** from the
      `/live-trains` delay data already polled each cycle (`DigitrafficClient::`
      `recomputePunctuality`): % on time (≤5 min) per broad category, shown as a
      sidebar caption line. No extra request.

### ✨ #4 Road-weather overlay (labelled as road, not rail)
- [x] The rail API publishes **no** weather; this uses Fintraffic **road** weather
      (`tie.digitraffic.fi`) as a nearby-conditions proxy, clearly labelled as such.
      `RoadWeatherClient` fetches station coords once + air temperature (`ILMA`
      sensor) on a slow timer; `WeatherStationModel` feeds a map layer of
      temperature chips. Off by default (idle, no traffic) behind a sidebar
      "Road weather" toggle.

### ✅ Verification
- [x] Clean `linux-release` build; app launches with no QML warnings; `ctest` 3/3.
      Endpoints (`/live-trains/station`, road weather stations + data) verified
      against live data before implementation. Interactive click/toggle paths not
      yet eyeballed on the running map.

---

## UI-audit accessibility fixes — marker contrast, type scale, a11y, panel overflow

Four `qt-ui-design` audit findings, applied to the QML surface (no C++). PR #41.

### 🐛 Marker label contrast (WCAG)
- [x] `TrainMarker` capsule labels hardcoded white ink, which fell below 4.5:1 on the
      light train hues (cyan/orange/pink/stale-grey). New `inkFor()` picks black or
      white by true sRGB relative luminance — per train type, mirroring
      `Theme.accentText` — and `labelInk` drives the badge, km/h and `+N min` labels.

### ♻️ Type scale + accessibility polish
- [x] Capsule labels routed through `TypeScale.caption` (were raw 9/11 px; the 9 px
      sub-labels sat below the scale's documented 11 px floor).
- [x] Carriage cells expose `Accessible.name`/`description` (type · power · amenities)
      so that detail is reachable without the hover tooltip (`TrainDetailPanel.qml`).
- [x] `InfoPanel` wraps its content in a `Flickable` and caps its height to the space
      below the top margin, so a short window scrolls the card (themed scrollbar)
      instead of clipping the lower sections off-screen (`InfoPanel.qml`).

### ✅ Verification
- [x] Clean `linux-release` build (qmlcachegen validates the QML); smoke-launched with
      an empty log (no QML warnings). Deferred: keyboard-reachable markers and sidebar
      cursor consistency (audit #3/#6) — see **Still open** below.

---

## Track-category styling + themed timetable scrollbar

PR #38. Running lines vs sidings now read distinctly, and the timetable scrollbar
matches the dark panel.

### ✨ Track categories by `paaraide`
- [x] `RailGraph`/`TrackService` carry each segment's `paaraide` main-track flag
      through to a `mainTrack` role on `TrackListModel`; the `Main.qml` track delegate
      draws running lines bolder/opaque (`Theme.railColor`, width 2.2) and sidings
      thinner/dimmer (`Theme.railSidingColor`, width 1.3). Sidings hide via the legend
      toggle.

### ✨ Themed timetable scrollbar
- [x] The timetable `ScrollBar` gets a thin muted-ink handle that fades in on
      hover/press, reading correctly on the dark card (`TrainDetailPanel.qml`).

---

## Map/legend polish — filters, cause text, breadcrumb trail

Four small features, each reusing an existing service/pattern rather than
adding new architecture.

### ✨ Track-category legend + siding toggle
- [x] `InfoPanel` legend swatches (`Theme.railColor` / `railSidingColor`) plus a
      "Show sidings" toggle (`showSidings`); the track `MapPolyline` delegate in
      `Main.qml` hides non-`mainTrack` segments when it's off.

### ✨ Train-type filter
- [x] `InfoPanel` gained `showCommuter` / `showLongDistance` / `showCargo`
      toggles + `categoryVisible(category)`; the `TrainMarker` delegate binds
      `visible` to it. An unrecognised/empty category (metadata not loaded yet)
      always shows, so trains never vanish at startup.
- [x] New shared `ToggleRow.qml` (labelled checkbox, keyboard-focusable with a
      focus ring, mirrors the Appearance segment's accessibility pattern) —
      4 call sites (siding + 3 category toggles) justified factoring it out.

### ✨ Delay-cause text on the timetable
- [x] `DigitrafficClient` fetches `/metadata/cause-category-codes` once
      (mirrors the existing one-shot station-names fetch) and exposes
      `causeCategoryNames()` (categoryCode -> Finnish name).
- [x] `TrainDetailsService::buildStops()` captures each stop's top-level cause
      `categoryCode` (departure preferred over arrival, like `delayMinutes`);
      `rebuildStops()` resolves it to `TimetableStop::causeText`, shown as an
      italic caption line under the track/passing label in
      `TrainDetailPanel.qml`. Only the top-level code is resolved (not
      `detailedCategoryCode`/`thirdCategoryCode`) — coarser, but one fetch.

### ✨ Breadcrumb trail for the selected train
- [x] `Main.qml` appends the selected train's snapped position (from the
      existing `matchInfoFor()` 750 ms poll) to `win.trailPoints` (capped at 8),
      reset on selection change, rendered as a fading `MapPolyline`. No new
      C++/model state — reuses the same diagnostics feed as the route/connector
      overlay.

---

## Track accuracy — Tier 2 (topology-routed map matching)

Route-constrained map matching: markers now follow each train's *scheduled* path
through double-track and junctions, and a stopped train pins to its booked
platform track. Plan + findings in `docs/track-accuracy-tier2-plan.md`.

### ✨ Re-bake with identity + topology + station crosswalk
- [x] **Schema-v2 blob** (`scripts/bake_rails.py`): keeps per-track `tunniste`,
      `paaraide`, `kaupallinenNumero`, `seuraavatRaiteet`, `viereisetRaiteet`,
      `rautatieliikennepaikat`, `ratakmvalit` (was geometry-only), and bakes a
      station crosswalk (`stations`, `operatingPoints`). `schemaVersion: 2` lets
      the loader reject old blobs. 4936 tracks, 557 stations → **2.6 MB (+9 %)**.
- [x] **Crosswalk join verified** — `uicKoodi` over the union of
      `rautatieliikennepaikat` + `liikennepaikanosat` (parts), matched to
      `/metadata/stations.stationUICCode`: **214/215 passenger stations resolve**
      (only the parts layer makes hubs like Pasila resolvable; parent-op fallback
      for tracks). `lyhenne`/coarse-op `uicKoodi` do **not** join.

### ✨ Derived-graph routing + route-constrained matching
- [x] **`RailGraph`** (new pure, unit-testable core): parses the v2 blob, builds a
      routing graph from **geometry endpoints + `viereisetRaiteet`** (because
      `seuraavatRaiteet` is empty in the live data — 38 edges nationally), and
      provides Dijkstra route resolution, chainage-windowed projection, and
      platform snapping. `TrackService` owns it and derives render segments from it.
- [x] **Off-thread route precompute** (`TrackService::precomputeRoutes`): resolves
      each train's station sequence to a chainage-parameterised polyline in a
      `QtConcurrent` task, deduped + memoised; a 60 s refresh only adds new routes.
- [x] **Route-constrained match** (`TrainListModel::applyOne`): snaps to the
      nearest point on the train's route within a `speed·Δt` window (1-D chainage
      carried across fixes for continuity), platform-snaps a stopped train to its
      `commercialTrack`, and falls back to the Tier-1 nearest+heading matcher when
      no route is resolved or the fix is off the booked route.

### ✅ Validation
- [x] **Unit tests** (`tests/tst_railgraph.cpp`, QtTest + CTest): fixture parse,
      endpoint-graph route stitching, chainage projection, platform snapping, plus
      a real-blob smoke test (HKI→PSL→TPE resolves; on-route point projects <1 m).
      **9/9 pass.**
- [x] **Debug overlay** (`Main.qml`): the selected train's resolved route polyline,
      a raw→snapped connector, a ring at the raw fix; the detail panel shows
      `on route / nearest track · N m off · <tunniste>`.
- [x] Builds clean (`windows-llvm`); app runs with live data (REST+MQTT+routing) —
      no crashes/warnings.
- [ ] 📋 Live on-map eyeballing of parallel-track/junction/platform behaviour
      (inherently visual; not fully judgeable from unit tests).

---

## Dark mode, map panning & UI-audit polish

A map-interaction fix, full light/dark/auto theming, three dark-mode/launch bug
fixes, and the remaining `qt-ui-design` audit items.

### 🐛 Bug fixes
- [x] **Horizontal panning was throttled** — `Map.center` was two-way bound to the
      loader's `savedCenter*` (the binding read center *from* them while
      `onCenterChanged` wrote them *back*), so every `map.pan()` was re-asserted by
      the binding and horizontal drags barely moved. Center/zoom are now restored
      imperatively once in `Component.onCompleted`; the handlers only write back for
      theme-reload persistence (`Main.qml`).
- [x] **No console window on launch** — the target is a GUI-subsystem app in
      Release (`WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>`); Debug keeps the console
      attached for `qDebug`/logs (`CMakeLists.txt`). Verified PE subsystem = GUI.
- [x] **Timetable unreadable in dark mode** — the native-styled `ItemDelegate`
      painted a white system-palette background, hiding the theme-coloured
      (near-white) text. Its background is now driven by `Theme`
      (`TrainDetailPanel.qml`).
- [x] **Delay badges hidden under the scrollbar** — the timetable row reserves the
      vertical `ScrollBar` width on the right, and a conflicting `verticalCenter`
      anchor was removed (`TrainDetailPanel.qml`).

### ✨ / ♻️ UI audit — Criticals (C1–C3) + Warnings/Opportunities (W1–W4, O1–O3)
- [x] **C1** Theme-aware markers: dark hues lifted for legibility on the dark
      basemap, light dot stroke in dark mode, theme-aware label/km-h colours.
- [x] **C2** `Theme.accentText` is dark ink on the light dark-mode accent (white
      on it was ~2.8:1 → now ~6.4:1).
- [x] **C3** marker text labels gate on `zoomLevel >= 8.0` (dots only at country
      scale) — fixes the overlap clutter.
- [x] **W1** km/h marker sub-label 8 px → 11 px.
- [x] **W2** new `TypeScale` singleton (caption/body/subhead/title) replaces ~6
      ad-hoc font sizes across the panels (sizes are px — OS font-scale TODO below).
- [x] **W3** reduced-motion opt-out (`Theme.reducedMotion`, persisted via
      `QtCore.Settings`); the LIVE pulse gates on it.
- [x] **W4** Appearance segments, the detail-panel close button, and the show-all
      checkbox are keyboard-focusable (Tab + Space/Enter) with focus rings and
      `Accessible` roles.
- [x] **O1** marker labels sit on a semi-opaque themed pill (was a 1 px outline).
- [x] **O2** selection is a haloed accent double-ring that reads on blue/navy dots.
- [x] **O3** new `AppIcon` (Canvas line-art, theme-recolourable) replaces the
      emoji glyphs (train / close / check). New singletons registered in CMake:
      `TypeScale.qml`, `AppIcon.qml`.

### ✅ Verification
- [x] Clean `windows-llvm` Release build; no QML errors on startup. Screenshot-
      verified: horizontal + vertical pan, dark-mode markers and timetable, delay
      badges clear of the scrollbar, and a console-free GUI launch.

---

## Accessibility — UI audit critical fixes

From the `qt-ui-design` audit. The three **Critical** (WCAG / core-law) findings:

### 🐛 / ♻️ Fixes
- [x] **Keyboard focus invisible** — the custom-styled sidebar buttons dropped
      the style's focus ring. Added `focusPolicy: Qt.StrongFocus` and a
      `visualFocus`-driven focus ring (accent outline) on *Refresh* and
      *Load tracks* (`InfoPanel.qml`).
- [x] **Lateness was colour-only** — the marker late ring (amber/red) is now
      paired with a textual `+N min` badge so the state is legible without colour
      perception (colour-blind safety). Added a `delayMinutes` model role
      (`TrainListModel`) and the badge (`TrainMarker.qml`).
- [x] **Sub-4.5:1 text contrast** — raised failing greys to ≥ 4.5:1: sidebar
      attribution `#9aa0a6`→`#6b7280`; detail-panel status `#888888`, track
      sub-label and neutral delay `#999999` → `#5f6671`
      (`InfoPanel.qml`, `TrainDetailPanel.qml`).

### ✅ Verification
- [x] Builds clean; app loads with no QML errors. Screenshot-verified an amber
      late train rendering both the ring **and** a matching "+5 min" badge.

---

## Map framing & sidebar polish

### ✨ Features
- [x] Sidebar card (`InfoPanel`) redesigned: rounded 12 px card with a soft drop
      shadow, icon badge + title/subtitle header, hairline dividers, a pulsing
      green **LIVE** indicator paired with the live-train count, and flat
      primary/outlined action buttons (custom-styled, `QtQuick.Controls.Basic`).

### 🐛 Bug fixes
- [x] **Map was not interactive** — Qt 6 removed `MapGestureArea`, so the `Map`
      had no pan/zoom at all (the old "enabled by default" comment was wrong).
      Added explicit `DragHandler` (pan), `WheelHandler` (wheel/trackpad zoom),
      `PinchHandler` (pinch zoom, north-up — no rotation), and `StandardKey`
      zoom-in/out shortcuts (`Main.qml`).

### ♻️ Changes
- [x] Map now opens framed on **southern Finland** (the Helsinki–Turku–Tampere
      rail triangle, centre 61.0 °N, 24.5 °E, zoom 7.0) instead of the capital
      area; clamped to `minimumZoomLevel` 4.0 (zoom out for the whole country) /
      `maximumZoomLevel` 18.0 (`Main.qml`).
- [x] The whole pre-baked rail network (4936 segments) is now rendered: loaded
      once at startup via `trackService.load()` and held as a static model, so
      panning/zooming no longer rebuilds polyline delegates. Dropped the
      per-viewport `loadForBounds` fetch, the `trackZoomThreshold` gate, and the
      debounce timer (`Main.qml`). Cost: ~+110 MB RSS over the filtered view.
- [x] Rail stroke bumped to width 2.2, colour `#8c95a0` (was 1.4 / `#b4b8bf`) so
      the network reads clearly at the overview zoom (`Main.qml`).

### ✅ Verification
- [x] Visual: southern Finland in view, full rail network drawn as legible grey
      lines with trains on top; redesigned sidebar shows live count, LIVE pulse,
      restyled buttons, and "4936 track segments". Wheel-zoom confirmed working.
- [x] Measured: ~400 MB RSS, ~44 % of a core idle (the idle CPU is dominated by
      the live train markers, not the static rail polylines).

---

## Status-ring rules (refinement)

### ♻️ Changes
- [x] Late rings now have a clear two-tier threshold: **amber at 5–14 min late,
      red at 15+ min** (nothing under 5 min). Constants `kLateMinutes`/
      `kVeryLateMinutes` (`TrainListModel`).
- [x] Delay rings apply to **scheduled passenger trains only** — Cargo,
      Locomotive, Shunting and On-track-machines carry no ring. Green "ready" ring
      additionally requires a genuinely on-time (`delay <= 0`), stopped train.

### ✅ Verification
- [x] Visual: green rings only on stopped trains, red rings on very-late ICs.
- [x] Data cross-check vs live `/live-trains`: amber tier populated, all
      non-passenger categories excluded.

---

## Live-train pipeline (REST + MQTT → map)

### ✨ Features
- [x] Marker colours: full juliadata.fi palette keyed on `trainType`, with a
      category/speed fallback (`TrainMarker.qml`).
- [x] Badge labels: commuter line letter → `"TYPE NUMBER"` → bare number, plus a
      `km/h` sub-line while moving (`TrainMarker.qml`).
- [x] `commuterLine` model role, sourced from `/live-trains`
      (`TrainListModel`, `DigitrafficClient`).
- [x] Status rings: green (ready/stopped on time) · amber (1–5 min late) · red
      (>5 min) · grey-dimmed (stale / cancelled / not running). Computed in
      `TrainListModel::ringStateFor()` from live status + position age/speed.
- [x] `TrainStatus` (delay / cancelled / running) parsed from `/live-trains`
      (`DigitrafficClient::handleCategories`).

### 🐛 Bug fixes
- [x] **REST metadata never applied** — manual `Accept-Encoding: gzip` disabled
      Qt's transparent decompression, so every REST reply parsed as raw gzip and
      failed silently (markers stayed orange/bare-number, no rings). Removed the
      manual header in `DigitrafficClient`, `TrainDetailsService`,
      `TrackService`; Qt now negotiates + inflates automatically.
- [x] **Rail tracks failed to load** — infra-api rejected fractional bbox
      coordinates (`400 "Coordinate must be an integer number"`). Now floor/ceil
      to whole metres (`TrackService::loadForBounds`). 529 segments load.

### ♻️ Changes / refactors
- [x] `TrainListModel` now has a single timestamp-guarded `applyOne()` funnel for
      both REST and MQTT updates (was two separate paths).
- [x] REST snapshot **merges + prunes** (120 s grace) instead of
      `beginResetModel()` — no marker flicker, MQTT positions not clobbered.
- [x] Bearing history (`previousPositions`) garbage-collected to live trains.
- [x] REST poll cadence 5 s → 60 s resync (`DigitrafficClient`); MQTT carries
      live deltas in between.
- [x] Resync actually enabled — `DigitrafficClient.active: true` in `Main.qml`
      (was `false`, so nothing pruned after the bootstrap).
- [x] Rail lines restyled to thin light grey (`#b4b8bf`, width 1.4) so trains
      read as the focal layer on the light base (`Main.qml`).

### ✅ Verification
- [x] Builds and links (via `-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=build/bin`, see
      follow-up below).
- [x] Ran the app and screenshot-verified: type colours, labels, km/h, status
      rings, live MQTT, and 529 rail segments on the CARTO light base.

---

## Build & tooling

### ✨ Features
- [x] `CMakePresets.json` pins the toolchain per OS — **Clang** on Linux/macOS,
      **MSVC 2022** (VS 17 generator, x64) on Windows — each building into its own
      `build/<preset>/` folder, with the executable under `bin/` for clarity.
- [x] Windows: `windeployqt` post-build step bundles the Qt DLLs and required
      plugins (windows platform, TLS backend for `wss://`, QML / QtLocation
      geoservices / QtPositioning) next to the `.exe`, plus `--compiler-runtime`,
      so a fresh build runs without Qt on `PATH` (`CMakeLists.txt`).

### ♻️ Changes
- [x] **CMake exe/module name collision** (fixed) — default `cmake --build build`
      used to fail at link (`cannot open output file TrainsOnMap: Is a directory`)
      because the extension-less executable collided with the `TrainsOnMap/` QML
      module directory on Linux/macOS. Fixed by emitting the executable to a
      `bin/` subfolder via `RUNTIME_OUTPUT_DIRECTORY` (`CMakeLists.txt`).
- [x] README **Build & run** rewritten around the presets, with per-OS paths
      (`build/<preset>/bin/...`).

### ✅ Verification
- [x] `cmake --preset linux-clang` + `cmake --build --preset linux-clang`
      configures with Clang 22 and builds; the Clang binary launches and renders
      trains, rings, and rail tracks.
- [x] Windows build verified on Windows 11 + Qt 6.11.1 (2026-06-15): clean
      from-scratch configure + Release build with the **MSVC** preset (VS 2022
      Build Tools v17.14, `Visual Studio 17 2022` generator) and with a **MinGW**
      kit (Qt's bundled GCC 13 + Ninja), both exit 0 and emit `TrainsOnMap.exe`.
      The `windeployqt` post-build step runs and bundles the Qt DLLs/plugins.
      Launching the deployed `.exe` standalone not yet confirmed.

---

## Rail geometry (offline)

### ✨ Features
- [x] Rail network is **pre-baked and shipped in the repo** instead of fetched on
      every launch. `scripts/bake_rails.py` tiles the national network from the
      infra-api, de-dupes by `tunniste`, strips to geometry-only, and writes a
      compressed snapshot (`resources/rails.geojson.qz`).
- [x] The snapshot is embedded as a Qt resource and loaded once at startup;
      `TrackService` now projects it to WGS84 in memory and `loadForBounds()`
      filters to the viewport — no network calls for tracks (`TrackService`,
      `CMakeLists.txt`).

### ♻️ Changes
- [x] `TrackService` no longer uses `QNetworkAccessManager` / the infra-api
      endpoint; the live-fetch + bbox-request path is removed.

### 📝 Notes
- Container is Qt's `qCompress` (zlib) format, not a literal `.gz`, so it loads
  with pure Qt Core (`qUncompress`) — no `find_package(ZLIB)` or build-time
  gunzip, keeping the MSVC 2022 build dependency-free. Re-bake with
  `python3 scripts/bake_rails.py` when the rail topology changes.

---

## Simplification pass (repo-wide over-engineering audit)

### ♻️ Changes
- [x] **`TrackMatcher` interface folded away** — it had exactly one implementation
      (`TrackService`), and `DigitrafficClient` already coupled to `TrackService*`
      directly. The `TrackMatch` / `RouteMatchRequest` value types moved into
      `TrackService.h`; `TrainListModel` now holds a forward-declared
      `const TrackService *` (`TrackMatcher.h` deleted).
- [x] **`/metadata/stations` fetched once, not twice** — `DigitrafficClient` now
      also parses station names from its one-shot stations fetch and exposes them
      via `stationNames()`; `TrainDetailsService` consumes them through its new
      `fleet` property (wired in `Main.qml`) instead of issuing its own request.
- [x] **Dead code deleted** — unused `tm35fin::fromWgs84()` (forward projection,
      no callers) and `TrainListModel::nearestRouteStation()` (line-for-line
      duplicate of `nearestRouteStationCode()`; the one call site now looks the
      code up in `m_stationCoords`).
- [x] **Baked blob slimmed** — `ratakmvalit` (~0.5 MB of linear referencing) and
      the crosswalk diagnostics (`uic`/`opOid`/`match`/`distM`) are no longer
      baked; `RailGraph::loadFromJson` never read them. Raw JSON 8.3 → 7.7 MB
      (crosswalk resolution stats are still printed at bake time).

---

## 📋 Known issues / follow-ups
- [ ] `/trains/{date}` summary cache not added — `/live-trains` already supplies
      type/category/line for currently-running trains (covers cross-midnight), so
      it's unnecessary for now. Revisit only if a type source independent of
      "currently running" is needed.

### Feature ideas
- [x] **Station departure board** — done: clickable station layer +
      `StationBoardService`/`StationBoardModel` + `StationBoardPanel` over
      `/live-trains/station/{code}` (see the feature batch section above).
- [x] ~~**Favourite/pinned trains**~~ — declined, not appealing.
- [x] ~~**"Nearest trains to me"**~~ — declined, not appealing.
- [x] **Rail-weather overlay** — done: first as a Fintraffic **road**-weather
      proxy (the rail API publishes no weather), since replaced by real FMI
      open-data observations (`FmiWeatherClient`, see the FMI section above).
- [x] **On-time / punctuality stats** — done: there is no statistics endpoint, so
      it's aggregated client-side from the `/live-trains` delay data
      (`DigitrafficClient::recomputePunctuality`), no new fetch.

### UI design audit (qt-ui-design)
All audit findings — Criticals **and** the Warnings/Opportunities below — are now
implemented (see "Dark mode, map panning & UI-audit polish" above).

**✅ Done**
- [x] `TypeScale` singleton (modular scale) replaced the hardcoded font sizes.
- [x] `Theme` singleton of role-based colour tokens — drives light/dark/auto and
      reconciles the previously-divergent semantic colours.
- [x] Marker label declutter — text hidden below zoom 8 (dots only).
- [x] Reduced-motion opt-out for the LIVE pulse (`Theme.reducedMotion`).
- [x] Dark theme for overlays — basemap + all overlay colours follow `Theme`.

**📋 Still open**
- [x] ~~Type sizes are px, not `pointSize`.~~ Done — every `TypeScale`-driven
      label now sets `font.pointSize` instead of `pixelSize`, so text follows
      the OS "Large text" / accessibility DPI setting.
- [x] ~~Sidebar redundant affordances.~~ Done — dropped the "Load tracks"
      button (auto-refresh on pan/zoom already covered it, so it was a no-op)
      and stopped `TrackService::loadForBounds` from restating the viewport
      count into `status`, which duplicated the dedicated track-count label.
- [x] ~~Map markers are click-only; no keyboard pan.~~ Partially done — the map
      itself now pans with the arrow keys (existing Ctrl+/- already zoomed).
      Individual train markers still aren't Tab-reachable one-by-one; doing
      that properly needs a train list view, which is out of scope now that
      the list-dependent feature ideas below were declined.
- [x] ~~Action buttons are 32 px tall.~~ Done — Refresh is now 44 px
      (the only action button left after "Load tracks" was removed).
- [x] ~~`TrainDetailPanel` only toggles `visible`.~~ Done — both right-side
      panels (train detail + station board) now fade over 220 ms; the loaded
      item is kept around (hidden) after first use instead of being torn down,
      so the closing fade has something to animate.
- [x] ~~Timetable `ScrollBar` styling needs refinement to match the dark panel.~~
      Done — themed handle (PR #38).
- [x] ~~Dark basemap tile cache: stale `light_all` tiles linger after switching
      theme.~~ Done — `Main.qml` gives each basemap style its own cache directory
      (`cacheDirFor`).
