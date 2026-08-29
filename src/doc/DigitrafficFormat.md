# DigitrafficFormat.h (`digitraffic` namespace)

## A. Overview

Header-only presentation helpers for the Digitraffic timetable fields that reach
the UI as strings. Two services render the same API data in different panels —
`StationBoardService` (the station departure board) and `TrainDetailsService`
(the per-train timetable) — and both need the same two conversions:

- an ISO8601 timestamp → the local `"HH:mm"` a row displays;
- a pair of delay-cause codes + the `/metadata/cause-category-codes` maps → the
  human-readable reason line.

Both services originally carried a private copy of each in their `.cpp`
anonymous namespace, and the copies drifted (see §D). This header is the single
definition; the services call into it and hold no formatting logic of their own.

Pure QtCore — no Qt Quick, no networking, no model types — so it is unit-tested
directly (`tests/tst_digitrafficformat.cpp`, ctest target `digitrafficformat`).

## B. Namespaces

`digitraffic` — matching the header-only convention already used by
`mqttwire` (`MqttCodec.h`) and `tm35fin` (`Projection.h`). All functions are
`inline`; there is no state and no corresponding `.cpp`.

## C. Functions

#### QDateTime parseIso(const QString &iso)

Parses a Digitraffic ISO8601 timestamp. Returns an invalid `QDateTime` when the
string does not parse.

Digitraffic emits fractional seconds on most timestamp fields
(`"2026-07-26T09:12:00.000Z"`) but not on all of them. A single
`Qt::ISODateWithMs` parse covers both shapes: when *parsing*, Qt treats the
fractional part as optional, so `Qt::ISODateWithMs` and `Qt::ISODate` accept
exactly the same set of strings — the two only differ in `toString()`.
`tst_digitrafficformat`'s `hhmmAcceptsBothIsoForms()` pins that assumption, so a
future change in Qt's parser surfaces as a test failure rather than as silently
blank times in the panels.

#### QString hhmm(const QString &iso)

The timestamp as local-time `"HH:mm"`. Empty in → empty out: a missing time is
the normal case for a field the API has no value for yet (e.g. no live estimate),
not an error, and the panels render the gap as blank. An unparseable string also
yields an empty string.

#### QString causeText(const QString &causeCode, const QString &causeDetailedCode, const QHash<QString, QString> &categoryNames, const QHash<QString, QString> &detailedCategoryNames)

Composes the delay reason a delayed row shows. A `timeTableRow`'s `causes[]`
entry carries a coarse `categoryCode` and an optional finer
`detailedCategoryCode`; both are resolved through the metadata maps that
`DigitrafficClient` exposes as `causeCategoryNames()` /
`detailedCauseCategoryNames()`.

Result:

| Input | Result |
|-------|--------|
| No `causeCode` | empty (the row is on time / has no recorded reason) |
| Category + known detail | `"Onnettomuus: Tasoristeysonnettomuus"` |
| Category, detail missing or unknown | `"Onnettomuus"` |
| Metadata not loaded yet | empty |

The last row is expected, not a hole: the metadata fetch lands asynchronously,
and both services re-run their rebuild on `causeCategoryNamesChanged`, so the
text fills in on the next pass.

## D. Rationale — why this is shared

The two copies this header replaced were behaviourally equivalent but written
differently, and the difference read as a bug: `StationBoardService::hhmm()` had
a second `fromString(iso, Qt::ISODate)` fallback that `TrainDetailsService`'s
copy lacked. A `qt-cpp-review` audit flagged that as a live defect — timestamps
without fractional seconds blanking in the detail panel but not on the board.
Investigation (a mutation test against the parser) showed the fallback could
never fire, so the two copies had in fact always agreed; the fallback was dead
code and is not carried forward. The dedup still stands on its own: the
cause-text composition genuinely was duplicated, and a single definition means
the next change to either conversion cannot land in only one panel.

## E. Dependencies

`QDateTime`, `QHash`, `QString` — QtCore only.

Consumed by `StationBoardService.cpp` and `TrainDetailsService.cpp`. Listed in
`CMakeLists.txt` alongside `Projection.h` / `MqttCodec.h` so it shows up in IDE
project trees.

## F. Usage Example

```cpp
#include "DigitrafficFormat.h"

// A board/timetable row, straight off the API JSON.
row.timeText = digitraffic::hhmm(r.value("scheduledTime").toString());
row.sortTime = digitraffic::parseIso(r.value("liveEstimateTime").toString());

// Resolved against DigitrafficClient's metadata maps. Both services reach
// these through FleetMetadata rather than passing the maps by hand.
row.causeText = m_meta.causeText(row.causeCode, row.causeDetailedCode);
row.destination = m_meta.stationLabel(code);
```
