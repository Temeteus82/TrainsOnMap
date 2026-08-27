# Train marker colours & labels (Qt)

How each train's map badge gets its **colour** and its **label text** in the Qt app,
and how to wire both into the model. Both are driven by the same `(trainType,
category, commuterLineID, speed)` data, so they live together here. The palette is
identical to the shipping Swift app — it's lifted verbatim from
the official [juliadata.fi](https://juliadata.fi/map) map legend, so a train that's red
there is red here. This file is the Qt-side companion to the canonical
[`MarkerColors.md`](MarkerColors.md) (which holds the palette provenance and the
"refresh from the juliadata DOM" procedure — not repeated here).

The resolver is a **pure function of `(trainType, category, speed)`**; port it verbatim
from the Swift `trainMarkerColor(...)`. The only wrinkle is that the *position* feed
carries no type, so colour pulls in one extra data source — see
[§ Where `trainType` comes from](#where-traintype-comes-from).

> ## ⚠️ Historical — palette accurate, placement and thresholds are not
>
> This planned the port; the port is this repo, TrainsOnMap. **The palette table
> below is correct** and is what ships, `#30B0C7` commuter fallback included — but
> it lives in `colorFor()` in [`qml/TrainMarker.qml`](../qml/TrainMarker.qml), *not*
> in a C++ `fillColorRole`/`badgeLabelRole` as § Wiring into the model prescribes.
> That was a measured decision: two profiler runs put the marker bindings at
> ~0.005 ms/frame and declined the move, which would couple the model to the UI
> palette for no gain.
>
> Two further corrections: the **status-ring thresholds** below are stale — shipping
> values are amber ≥ 5 min, red ≥ 15 min, and **rings are drawn on scheduled
> passenger trains only** (cargo, locomotive, shunting and on-track machines carry
> none). And § Where `trainType` comes from describes a `/trains/{date}` cache with
> a `/live-trains` fallback; the app polls **`/live-trains` alone**, so there is no
> cross-midnight gap to fall back from. Full list:
> [`README.Qt.md`](README.Qt.md).

---

## The palette (trainType → fill)

| trainType | Meaning | Hex | | trainType | Meaning | Hex |
|---|---|---|---|---|---|---|
| `IC`, `IC2`¹ | InterCity | `#FF0000` | | `SAA`, `VLI` | — | `#009090` |
| `S` | Pendolino | `#007700` | | `W` | — | `#00B0B0` |
| `PYO`, `P` | Night / local | `#0000FF` | | `T` | Cargo (freight) | `#000077` |
| `H`, `HDM`, `HSM`¹ | Express + diesel | `#770000` | | `TYO` | Work / maintenance | `#7F6A00` |
| `HL` | Helsinki commuter | `#004400` | | `VET` | Locomotive haul | `#660066` |
| `HV`, `MV` | museum / shunting | `#FF006E` | | `VEV` | — | `#9E009E` |
| `PAI` | — | `#007070` | | *(unmatched)* | — | heuristic² |

¹ `IC2` and `HSM` aren't enumerated by juliadata — group them with their closest
listed sibling (`IC` → red, `HSM` → `H` dark red) so no live type renders unstyled.

² **Unmatched fallback** (juliadata draws these black; we route them so a moving train
is never an indistinct dot):
- `category == Cargo` → navy `#000077`
- `category == Commuter` → teal `#30B0C7` *(≈ SwiftUI `.teal`; tune to taste)*
- else → `speed > 0 ? orange #FF9500 : grey #8E8E93` *(≈ SwiftUI `.orange` / `.gray`)*

---

## Resolver (sketch)

A direct port — a type→colour table, then the category/speed fallback:

```cpp
// illustrative; not a drop-in
QColor trainFillColor(const QString &type, TrainCategory cat, int speed) {
    static const QHash<QString, QColor> byType = {
        {"IC",  QColor("#FF0000")}, {"IC2", QColor("#FF0000")}, {"S",   QColor("#007700")},
        {"PYO", QColor("#0000FF")}, {"P",   QColor("#0000FF")}, {"H",   QColor("#770000")},
        {"HDM", QColor("#770000")}, {"HSM", QColor("#770000")}, {"HL",  QColor("#004400")},
        {"HV",  QColor("#FF006E")}, {"MV",  QColor("#FF006E")}, {"PAI", QColor("#007070")},
        {"SAA", QColor("#009090")}, {"VLI", QColor("#009090")}, {"W",   QColor("#00B0B0")},
        {"T",   QColor("#000077")}, {"TYO", QColor("#7F6A00")}, {"VET", QColor("#660066")},
        {"VEV", QColor("#9E009E")},
    };
    if (const auto it = byType.constFind(type); it != byType.cend()) return *it;
    if (cat == TrainCategory::Cargo)    return QColor("#000077");
    if (cat == TrainCategory::Commuter) return QColor("#30B0C7");
    return speed > 0 ? QColor("#FF9500") : QColor("#8E8E93");
}
```

`QColor("#RRGGBB")` keeps the source reading as the exact legend hex and is
QML-friendly (the model can hand the same value straight to a delegate's `color`).

---

## Where `trainType` comes from

The position feed (`/train-locations/latest/` and the MQTT firehose) carries **only**
number / coordinate / speed / timestamp — **no type**. So both the colour *and* the
badge label (below) force one second source beyond the position pipeline:

- **`/trains/{today}`** — fetched once at startup into a `QHash<int, TrainSummary>`
  cache (`trainNumber → trainType, trainCategory, commuterLineID`). Refresh it on the
  **midnight rollover** (compare today's date to the date the cache was loaded for).
- **Fallback to `/live-trains`** when the summary cache misses. This is the
  **cross-midnight fix**: the summary cache is keyed by `trainNumber` for *today's*
  schedule, so a train running across midnight — cross-midnight **cargo** (irregular
  numbers) and **night trains** (`PYO`) that departed *yesterday* — isn't in it and
  would mis-colour as the orange/grey heuristic. `/live-trains` lists currently-running
  trains with their real type regardless of departure date, so a `trainType(number)`
  that reads `summary ?? status` resolves them correctly.

Wire both as caches refreshed on `QTimer`s (the `/live-trains` poll also feeds the
status rings below). For the fetch mechanics see
[`TrainDataWiring.Qt.md`](TrainDataWiring.Qt.md); for the full rationale see
[`MarkerColors.md`](MarkerColors.md) § Cross-midnight fallback.

---

## Badge label

The text on the coloured badge follows the same precedence as the Swift
`TrainMarkerView.badgeLabel`, driven by the same `commuterLineID` / `trainType` data as
the colour:

| Priority | When | Label | Example |
|---|---|---|---|
| 1 | `commuterLineID` present **and non-empty** | the line letter alone | `R`, `L`, `Z`, `U` |
| 2 | otherwise, `trainType` known | `"{trainType} {trainNumber}"` | `IC 967`, `S 29`, `T 5280` |
| 3 | type unknown (cache not loaded yet) | bare `"{trainNumber}"` | `967` |

Below it, a secondary line shows **`"{speed} km/h"`** only when `speed > 0` — a
stationary train shows no speed line. (`displayName` = the train number as a string.)

> **Empty-string guard.** The API returns `commuterLineID == ""` (an empty string, not
> null) for many non-commuter trains, so test `!isEmpty()` — **not** just non-null.
> A bare null check would give an IC train an empty line letter and drop its
> `"IC 967"` text. Same trap the Swift app calls out.

```cpp
// illustrative; not a drop-in
QString badgeLabel(const QString &commuterLineID, const QString &trainType, int number) {
    if (!commuterLineID.isEmpty()) return commuterLineID;                                  // "R", "Z", …
    if (!trainType.isEmpty())      return QStringLiteral("%1 %2").arg(trainType).arg(number); // "IC 967"
    return QString::number(number);                                                        // "967"
}
```

`trainType` / `commuterLineID` resolve from the same `summary ?? liveStatus` lookup as
the colour (see above), so cross-midnight cargo and night trains keep their prefix
(`T 5280`, `PYO 266`) instead of dropping to a bare number.

---

## Status rings (optional, on top of the fill)

A ring conveys live delay/staleness — green ready / amber late / red very late / grey
stale. These need the delay + running flags from the `/live-trains` poll, so they're
optional until you add that endpoint:

| State | Indicator | Condition | |
|---|---|---|---|
| Ready to depart | green ring `#18A957` | on time **and** stopped (`speed == 0`) | ships as described |
| Slightly late | amber ring `#F2A900` | ~~delay 1–5 min~~ → **delay ≥ 5 min** | threshold raised |
| Very late | red ring `#E03131` | ~~delay > 5 min~~ → **delay ≥ 15 min** | threshold raised |
| Stale / not running | **dim the whole badge** to grey `#9AA0A6` (no ring) | position > 5 min old & not flagged running, or cancelled / not running | ships as described |
| Running on time | no ring | the normal case — keeps the map from being a wash of green | ships as described |

The shipped thresholds are `kLateMinutes` / `kVeryLateMinutes` in
[`src/TrainListModel.cpp`](../src/TrainListModel.cpp); the 1–5 / >5 min pair above
lit up too much of the map against live data. Rings are additionally restricted to
**scheduled passenger trains** — cargo, locomotive, shunting and on-track machines
never carry one — and the green "ready" ring further requires a genuinely on-time
(`delay <= 0`) stopped train.

Green is reserved for waiting/ready trains (juliadata's *lähtövalmis*); lateness rings
take precedence over green.

---

## Wiring into the model

- Add a **`fillColorRole`** (`QColor`) and a **`badgeLabelRole`** (`QString`) to
  `TrainListModel`. Compute each lazily in `data()` (or cache them on the row in
  `apply()`) via `trainFillColor(...)` and `badgeLabel(...)`. Keep `speedRole` separate
  so the delegate can show/hide the `"{speed} km/h"` line on `speed > 0`.
- **Re-evaluate on type arrival.** When the summary or `/live-trains` cache lands, emit
  `dataChanged(0, rowCount-1, { fillColorRole, badgeLabelRole })` (or per affected row)
  so badges that bootstrapped on the heuristic pick up their real colour **and** their
  proper prefix. This is why a cross-midnight cargo train flips from a grey/orange
  `"5280"` to a navy `"T 5280"` a second after launch.
- **QML delegate:** bind directly — `Rectangle { color: model.fillColor }` with a
  white `Label { text: model.badgeLabel }` on top, and a second `Label` for km/h
  shown `visible: model.speed > 0`.

---

## Caveat — white text on the fill

juliadata uses bare dots; TrainSpotter overlays **white** `"TYPE NUMBER"` text on the
colour. The darker types have strong contrast; the light cyans — `W` (`#00B0B0`) and
`SAA`/`VLI` (`#009090`) — are weaker. Acceptable since those types are rare; darken
those two entries if they ever become common rather than abandoning the palette match.

> **Resolved in TrainsOnMap.** The shipping Qt app no longer hardcodes white: the
> `TrainMarker` delegate picks black *or* white ink per fill by WCAG relative
> luminance (`inkFor()` → `labelInk`), so the light cyans/orange/pink/stale-grey all
> clear 4.5:1 without darkening the palette. The advice above is the pre-fix rationale;
> keep it only as context for the palette-match tradeoff.

---

*Canonical palette + provenance + refresh procedure: [`MarkerColors.md`](MarkerColors.md)
(Swift app). Fetch wiring: [`TrainDataWiring.Qt.md`](TrainDataWiring.Qt.md).
Architecture: [`DataFlow.Qt.md`](DataFlow.Qt.md).*
