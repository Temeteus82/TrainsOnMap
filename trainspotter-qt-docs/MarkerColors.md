# TrainSpotter — Train Marker Colours

How each train's map badge gets its colour, and where the palette comes from.

TrainSpotter's marker colours are lifted **verbatim** from the official
[juliadata.fi map legend](https://juliadata.fi/map) — the reference Finnish-rail
tracking map the community already knows. Matching it means a train that's red on
juliadata is red here too. The legend states:

> *"Junien esittämisessä kartalla käytetään alla olevia värejä. Muut junat näkyvät
> mustalla."* — The following colours are used to display trains on the map. Other
> trains appear in black.

Hex values were read directly off the legend's `.train-color` swatch elements
(see [Refreshing the palette](#refreshing-the-palette)), **verified 2026-06-13**.

## At a glance

Colour is resolved by `trainType` first; anything unmatched drops to a
category/speed heuristic.

```
   trainType ──► exact juliadata colour  (IC, S, PYO, H, T, VET, …)
        │
        └─ (no match) ──► category ──► .cargo     ──► navy   #000077
                                  └──► .commuter  ──► teal   (SwiftUI .teal)
                                  └──► (other)    ──► speed > 0 ? orange : gray
```

## The palette

| `trainType` | Meaning | Colour | Hex | RGB (0–255) |
|---|---|---|---|---|
| `IC`, `IC2`¹ | InterCity | red | `#FF0000` | 255, 0, 0 |
| `S` | Pendolino (Sm3) | green | `#007700` | 0, 119, 0 |
| `PYO`, `P` | Night train / local | blue | `#0000FF` | 0, 0, 255 |
| `H`, `HDM`, `HSM`¹ | Express + diesel expresses | dark red | `#770000` | 119, 0, 0 |
| `HL` | Helsinki commuter (R, Z, G, U, O … lines) | dark green | `#004400` | 0, 68, 0 |
| `HV`, `MV` | (museum / shunting variants) | pink | `#FF006E` | 255, 0, 110 |
| `PAI` | — | teal | `#007070` | 0, 112, 112 |
| `SAA`, `VLI` | — | cyan-teal | `#009090` | 0, 144, 144 |
| `W` | — | light cyan | `#00B0B0` | 0, 176, 176 |
| `T` | Cargo (freight) | navy | `#000077` | 0, 0, 119 |
| `TYO` | Work / maintenance train | olive | `#7F6A00` | 127, 106, 0 |
| `VET` | Locomotive haul | purple | `#660066` | 102, 0, 102 |
| `VEV` | — | magenta | `#9E009E` | 158, 0, 158 |
| *(unmatched)* | — | black² | — | — |

¹ `IC2` and `HSM` are **not** enumerated by juliadata. They're grouped with their
closest listed sibling (`IC` → red, `HSM` → `H` dark red) so no live type renders
as an unstyled fallback.

² juliadata draws unmatched trains in black. TrainSpotter instead routes them
through the category/speed heuristic (navy for cargo, teal for commuter, else
orange when moving / gray when stopped) — so a moving train is never an
indistinct black dot.

## Implementation

Single source of truth: `trainMarkerColor(trainType:category:speed:)` in
`Views/TrainMarkerView.swift` (in the Swift app). It is shared by
**both** the map badge (`TrainMarkerView`) and the detail-panel header strip
(`TrainDetailPanel`), so the two can never diverge.

Colours are built from a private hex initialiser so the source reads as the exact
legend values rather than pre-divided doubles:

```swift
private extension Color {
    /// Build a Color from a 24-bit RGB hex literal, e.g. Color(rgb: 0x770000).
    init(rgb hex: Int) {
        self.init(
            red:   Double((hex >> 16) & 0xFF) / 255,
            green: Double((hex >>  8) & 0xFF) / 255,
            blue:  Double( hex        & 0xFF) / 255
        )
    }
}
```

`trainType` itself comes from the `TrainSummary` cache (`TrainStore.trainType(for:)`)
— the live positions endpoint doesn't carry it. Until that cache is populated, the
heuristic branch handles colouring. See [DataFlow.md](DataFlow.md) for how the
summary cache is built.

**Cross-midnight fallback.** The summary cache is built from `/trains/{today}` and
keyed by `trainNumber` only, so a train whose number isn't in *today's* schedule has
no entry — notably **cargo** runs (irregular numbers) and **night trains** (`PYO`)
that departed *yesterday* and are still running this morning. Those would otherwise
fall through to the orange/gray speed heuristic. To fix that, `trainType(for:)`,
`category(for:)`, and `commuterLineID(for:)` fall back to the bulk `/live-trains`
status poll (`trainStatuses` — already fetched every 120 s for the rings), which
lists the trains running *right now* with their real type/category/line regardless
of departure date. The fallback adds **no** extra request; the colour self-corrects
on the first status poll (kicked off immediately at startup, in parallel with the
position bootstrap).

## State rings

On top of the type-coloured fill, each marker carries a **status ring** conveying
the train's live state — mirroring juliadata's green/yellow/red ring + grey-marker
convention.

| State | Indicator | Condition |
|---|---|---|
| Ready to depart | green ring `#18A957` | on time (delay ≤ 0) **and stopped** (`speed == 0`) |
| Slightly late | amber ring `#F2A900` | delay 1–5 min |
| Very late | red ring `#E03131` | delay > 5 min |
| Stale / not running | **grey, dimmed badge** (no ring) | position > 5 min old & not flagged running, or `cancelled` / `runningCurrently == false` |
| Running on time | no ring | on time and moving — the normal case |
| Unknown | no ring | status cache not loaded yet, or train absent from `/live-trains` |

Green is **reserved for waiting/ready trains**, matching juliadata's *lähtövalmis*
(ready-to-depart) semantics — a normally-running on-time train carries no ring, so
the map isn't a wash of green. Lateness rings take precedence over green.

Modelled by `TrainRingState` (`Models/TrainDetails.swift` (in the Swift app));
ring colours are `TrainRingState.ringColor` in
`Views/TrainMarkerView.swift` (in the Swift app), next to the type
palette. The stale state greys the **whole badge** (juliadata's "harmaa" marker)
rather than drawing a grey ring.

### Where the state data comes from

The live position feed (`TrainLocation`) carries only `speed` + `timestamp` — **no
delay**. So delay comes from a second, slower bulk poll:

- `DigitrafficService.fetchRunningTrainStatuses()` hits the bulk `/live-trains`
  endpoint (one request, ~6 MB, gzip transparent via URLSession) and decodes a
  lean `TrainRunningStatus` per train: `currentDelayMinutes`, `cancelled`,
  `runningCurrently`. This is **not** a per-train detail fetch — it's the bulk
  endpoint, so it doesn't violate the "no eager per-train details" rule.
- `TrainStore.statusTask` polls it every **120 s** (`statusInterval`) — slower than
  the 60 s position resync, since delays drift gradually and the payload is large.
- `TrainStore.ringState(for:)` combines the cached status with the position's
  `timestamp` age (`stalePositionThreshold` = 300 s) to pick a `TrainRingState`.

Thresholds live as constants on `TrainStore` (`stalePositionThreshold`,
`veryLateThresholdMinutes`) and in `ringState(for:)`.

### Not ported

juliadata also shows a parenthesised number for stopped trains and red/yellow
number backgrounds for "watched"/pinned trains. TrainSpotter has no watch/pin
concept, and selection is shown via its own halo + scale, so those are omitted.

## Caveat: white text on the fill

juliadata uses bare dot markers; TrainSpotter overlays white **"TYPE NUMBER"**
text on the colour. The darker types (navy, maroon, purple, dark green, red,
green) have strong white-on-fill contrast. The lighter cyans — `W` (`#00B0B0`)
and `SAA`/`VLI` (`#009090`) — are weaker, but those types are rare. If they ever
become common, darken those two entries rather than abandoning the palette match.

## Refreshing the palette

Re-run if juliadata changes its legend:

1. Open <https://juliadata.fi/map> in Chrome.
2. In the console, read the swatch backgrounds off the legend DOM — each legend
   entry is a `.train-color-item` wrapping an inner `.train-color` div that holds
   the actual colour:

   ```js
   Object.fromEntries(
     [...document.querySelectorAll('.train-color-item')].map(it => {
       const sw = it.querySelector('.train-color');
       const m = getComputedStyle(sw).backgroundColor.match(/\d+/g);
       const hex = '#' + m.slice(0,3)
         .map(x => (+x).toString(16).padStart(2,'0')).join('');
       return [it.textContent.trim(), hex];
     })
   )
   ```

3. Update the `switch` in `trainMarkerColor(...)` and the table above.

---

*See also: the Swift app's `CLAUDE.md` → "Type-coded marker colours" for the
condensed architecture note, and [DataFlow.md](DataFlow.md) for how `trainType`
reaches the colour function.*
