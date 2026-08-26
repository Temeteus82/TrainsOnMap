# UI audit — round 2 (2026-08-26)

> **Status:** U2-W1, U2-C2, and U2-C3 were fixed on 2026-08-26 and are
> marked *Fixed* below. Everything else is open.

Scope: the nine files in `qml/`, audited against WCAG 2.2 AA, the Laws of UX
set, and the qt-ui-design checklist (typography, motion, keyboard/multi-input,
semantic colour, localisation).

Target profile assumed: Windows 11 desktop, Qt 6.10, resizable window (1100×820
default), ~60 cm viewing distance, mouse + keyboard, English UI over Finnish
data, LTR.

IDs in this document are prefixed `U2-` so they don't collide with the `W2`/`W3`/
`W4`/`O3` codes from the first UI audit that are already cited in source comments.

Contrast ratios below were computed with the WCAG 2 relative-luminance formula
against `Theme.cardBg` resolved over its backdrop (`#fefefe` light, `#1c1f24`
dark) and against the CARTO basemaps (`#f7f7f5` Positron, `#1b1b1b` Dark Matter).

---

## Critical

### U2-C1 — The app's primary action has no keyboard or assistive path

Selecting a train (`TrainMarker.qml:333`) and opening a station board
(`Main.qml:380`) are the two things this application exists to do, and both are
reachable only with a mouse. Neither delegate sets `activeFocusOnTab`, an
`Accessible.role`, or a key handler, and there is no list view or search that
offers the same reachability another way. The map view itself was made keyboard
operable in the previous round (arrows pan, `StandardKey.ZoomIn/Out`), and
`Main.qml:410` records the gap explicitly — but panning to a train you cannot
then select does not close it.

- Violates WCAG 2.1.1 Keyboard (Level A) and 4.1.2 Name, Role, Value (Level A).
- Violates the multi-input rule in §1.3: every interactive element must be
  reachable by pointer **and** keyboard **and**, where Qt exposes it, screen
  reader.
- A screen reader currently perceives the fleet as nothing at all: the entire
  live dataset is unlabelled `MapQuickItem` geometry.

**Fix.** The cheapest complete fix is also the one that closes U2-O1: add a
focusable train list (see Opportunities) and let the map stay a visualisation of
it. If markers must carry the interaction themselves, they need
`activeFocusOnTab`, `Accessible.role: Accessible.Button`, an `Accessible.name`
built from the badge label + speed + delay, and `Keys.onPressed` for
Space/Enter — plus a defined tab order across a set that changes every few
seconds, which is the reason the list is the better answer.

### U2-C2 — The "LIVE" indicator text fails contrast in light mode — **Fixed**

`InfoPanel.qml:164` paints the `LIVE` label in `Theme.liveOn` (`#18a957`,
`Theme.qml:54`) on the card.

| Pair | Ratio | Required |
|---|---|---|
| `#18a957` on light card | **3.04:1** | 4.5:1 (WCAG 1.4.3, AA) |
| `#18a957` on dark card | 5.39:1 | pass |

The label is `TypeScale.panelCaption` — 8.25 pt, nowhere near the "large text"
exemption. Dark mode passes; light mode is the failure.

Related, same block: the disconnected dot `Theme.liveOff` (`#b0b6be`) scores
**2.03:1** on the light card, under the 3:1 that WCAG 1.4.11 asks of a graphical
object carrying state. It is partly rescued by the adjacent `streamStatus` text,
which is `Theme.textMuted` and passes.

**Fixed** by splitting the token. `Theme.liveOn` (`#18a957`) still tints the
dot, which only has to clear 1.4.11's 3:1; the label now reads
`Theme.liveOnText`, which is `#0f7a3d` in light mode (**5.38:1**) and the
unchanged green in dark, where it never failed.

`liveOff` was fixed in **both** branches, not just light: sizing the change
showed the dark grey `#5c636c` at **2.72:1**, also under 3:1 — the original audit
only measured the light one. Now `#868d97` light (3.32:1) / `#767d88` dark
(3.98:1).

### U2-C3 — Unchecked controls have no perceptible boundary — **Fixed**

`Theme.hairline` is used both as a 1 px divider (correct — decorative) and as
the *only* boundary of unchecked interactive controls:

- `ToggleRow.qml:45` — all five sidebar checkboxes
- `TrainDetailPanel.qml:405` — "Show all timing points"
- `InfoPanel.qml:329` — the Appearance segmented control's container

| Pair | Ratio | Required |
|---|---|---|
| `#e6e8ec` on light card | **1.22:1** | 3:1 (WCAG 1.4.11, AA) |
| `#34373d` on dark card | **1.38:1** | 3:1 |

An unchecked checkbox is therefore, in both themes, an empty patch of card. The
checked state is fine (accent fill, 5.7:1) — which means the control reads as
*present* only when it is already on, exactly inverting the affordance.

**Fixed** with a distinct `Theme.controlOutline`, applied at all three sites;
`hairline` stays a divider.

The shipped values are darker than the ones proposed above. A control boundary
has to clear 3:1 against whatever sits on *both* sides of it, and the Appearance
control is filled with `Theme.subtleHover`, not the card — against which
`#8e959f` measured only 2.72:1. Final: **`#7e858f` light** (3.69:1 vs card,
3.35:1 vs `subtleHover`) and **`#767d88` dark** (3.98:1 / 3.47:1).

### U2-C4 — Map-layer colours fail non-text contrast, worst in dark mode

`TrainMarker.colorFor()` is a single hardcoded light-mode palette (the
juliadata.fi parity set) with no dark variant, painted over a basemap that does
flip. Measured against CARTO Dark Matter:

| Marker | Colour | On dark map | On light map |
|---|---|---|---|
| Cargo / `T` | `#000077` | **1.04:1** | 15.4:1 |
| Night / `PYO`,`P` | `#0000FF` | **2.00:1** | 8.0:1 |
| Helsinki commuter `HL` | `#004400` | **1.50:1** | 10.6:1 |
| Stale (any type) | `#9AA0A6` | 3.9:1 | **2.46:1** |
| IC | `#FF0000` | 5.3:1 | 3.73:1 |

Cargo trains on the dark basemap are, to within a rounding error, invisible: the
only thing separating them from the tiles is the 1 px 30 %-white hairline on the
capsule (`TrainMarker.qml`, `capsule.border.color`). The label ink is computed
correctly by `inkFor()`, so *labelled* markers are still readable — but below
zoom 8 the capsule collapses to a bare 14 px dot with no text at all, which is
precisely where the fill is the only signal.

Lower-severity, same cause — the rail layer against its basemap:
`Theme.railColor` 2.83:1 light, `Theme.railSidingColor` 1.66:1 light / 1.75:1
dark. Sidings are deliberately recessive and have a legend entry, so this is a
judgement call rather than a defect; the running line at 2.83:1 is marginal
enough to be worth nudging.

**Fix.** Give the marker palette a dark-mode branch in `Theme` (the natural home
for it, per U2-W8) rather than in `TrainMarker`. A per-colour minimum-luminance
lift is enough: `#000077` → about `#5b5be0` (3.30:1) on dark. The stale grey
needs the opposite treatment in light mode.

---

## Warnings

### U2-W1 — Two Qt Quick Controls styles in one application — **Fixed**

`InfoPanel.qml` and `ToggleRow.qml` import `QtQuick.Controls.Basic`
(compile-time selection). `Main.qml`, `TrainDetailPanel.qml`, and
`StationBoardPanel.qml` import `QtQuick.Controls` (run-time selection), and
`main.cpp` never calls `QQuickStyle::setStyle()`, so on Windows those files
resolve to the native **Windows** style. The Qt documentation on this is
unambiguous: never explicitly import two different styles in one application,
because theming — fonts and palettes — will not behave as expected.

The symptoms are already in the tree, each patched locally:

- `TrainDetailPanel.qml` re-templates `ItemDelegate.background` because the
  native style painted it white from the *system* palette, hiding near-white
  text in dark mode.
- Three files re-template `ScrollBar.contentItem` for the same reason.
- `BusyIndicator` in both panels is left at whatever the resolved style draws,
  which is not the app's accent.

This is the highest-leverage structural fix in the list: it removes three
existing workarounds rather than adding code.

**Fixed** by importing `QtQuick.Controls.Basic` in all five files —
compile-time style selection, so the QML compiler can generate C++ for bindings
on Controls types and the `QtQuick.Controls` plugin drops out of the deployment.
`main.cpp` needs no `QQuickStyle` call, and a stray `QT_QUICK_CONTROLS_STYLE` in
the environment can no longer change the app's appearance, because compile-time
selection wins.

The three "workarounds" were **kept**, contrary to what this entry first
suggested. Each does real work beyond papering over the style conflict: the
`ItemDelegate` background also carries the hover fill and the next-stop accent
stripe, and the `ScrollBar` templates give a thin themed handle that no stock
style provides. What changed is the comment above the `ItemDelegate` override,
which blamed the "native" style — Basic drives the same properties from the
system palette, so the override is still needed, for a slightly different
reason.

Two things do change visually and are worth an eyeball: `BusyIndicator` and
`ToolTip` in the two panels now render Basic rather than native Windows.

### U2-W2 — The `panel*` scale breaks its own stated floor

`TypeScale.qml` documents `caption` (11) as the floor: "nothing renders below
it". `panelScale: 0.75` then puts most of the application below it:

| Role | Value | ≈ px @96 dpi | Where |
|---|---|---|---|
| `panelCaption` | 8.25 pt | ~11 px | marker badges, all panel metadata, cause text, legend |
| `panelBody` | 9.75 pt | ~13 px | panel reading text, timetable rows, station board |
| `panelSubhead` | 12 pt | ~16 px | InfoPanel title |
| `panelTitle` | 14.25 pt | ~19 px | panel headings |

§1.2 sets 16 px as the desktop body minimum; the panels read at ~13 px and their
metadata at ~11 px. The compact 13 pt base was a deliberate, defensible choice
for a dense map HUD — multiplying it again by 0.75 is what pushes it past the
line. It also doubles the scale from four active roles to eight, and produces
two different sizes for the same visual role: the FMI weather chip uses
`TypeScale.caption` (11 pt) while the train marker badge beside it uses
`panelCaption` (8.25 pt), and both are chips floating on the map.

**Fix.** Either fold the panel scale back into the main one (lower the base to
11–12 and delete `panelScale`), or keep two scales but stop them overlapping —
one map-chip size, one panel size. The floor should be a real floor.

### U2-W3 — Keyboard focus can scroll out of view in the sidebar

`InfoPanel.qml` caps its height on a short window and flicks. Tab order walks
the whole `ColumnLayout` regardless of what is scrolled into view, and nothing
adjusts `flick.contentY` on focus — so on an 800 px-tall window, tabbing to the
Weather toggle or the Appearance segments moves focus to an off-screen control
with no visible indicator.

Violates WCAG 2.2 2.4.11 Focus Not Obscured (Minimum), Level AA — new in 2.2,
so it would not have been caught by a 2.1 pass.

**Fix.** An `onActiveFocusChanged` handler on the focusable rows that scrolls
the focused item's `y` into `[contentY, contentY + height]`.

### U2-W4 — No Escape, and focus never enters an opened panel

`grep` finds zero `Key_Escape` handlers in `qml/`. Opening a train detail or
station board panel does not move focus into it, and closing it does not restore
focus to anything. The close buttons are focusable, but only after tabbing
through everything ahead of them.

Not a focus trap (the panels aren't modal), so not a WCAG failure — but Escape
closes a transient panel in every application a user has ever used (Jakob's Law),
and §1.3 asks for a keyboard-accessible exit from any overlay.

**Fix.** `Shortcut { sequence: StandardKey.Cancel }` on each panel calling
`clear()`; `forceActiveFocus()` on the panel when it becomes visible.

### U2-W5 — `Theme.reducedMotion` cannot be turned on from the UI

The flag is honoured in six places (marker glide, selection scale, arrow orbit,
live-dot pulse, both panel fades) and persisted through `QSettings` — and there
is no control anywhere that sets it. Turning motion off currently requires
editing the registry by hand.

§1.1 requires the project-level reduced-motion setting to be *exposed*, since Qt
has no `prefers-reduced-motion` equivalent to inherit from. The plumbing is the
hard part and it is already done.

**Fix.** One `ToggleRow` in the Appearance group:
`ToggleRow { label: qsTr("Reduce motion"); checked: Theme.reducedMotion; onToggled: Theme.reducedMotion = !Theme.reducedMotion }`.

### U2-W6 — Localisation is scaffolded but not wired, and will clip when it is

48 `qsTr()` calls across the QML; zero `.ts` files; no `qt_add_translations()`
in `CMakeLists.txt`. Nothing is translatable today despite the markup.

Two things will break the moment it is:

- **Times are hardcoded, not locale-aware.** `DigitrafficFormat.h:42` formats
  `"HH:mm"` and `DigitrafficClient.cpp:372` formats `"HH:mm:ss"`. §1.5 says
  never hardcode a format string. Finland is 24-hour so this is invisible
  locally and wrong everywhere else. The good news is that the first is a single
  shared helper — a one-line change to
  `QLocale::system().toString(dt, QLocale::ShortFormat)`.
- **Containers can't absorb the expansion.** `InfoPanel` is a fixed
  `implicitWidth: 268`, and `ToggleRow`'s label has neither `wrapMode` nor
  `elide` — so at the typical 30–40 % expansion, or under the OS "Large text"
  setting, "Long-distance" and "Show sidings" clip silently rather than wrap or
  ellipsise.

Also worth a decision rather than a fix: station names and delay-cause
categories arrive from Digitraffic in Finnish (`"Onnettomuus"`) regardless of UI
language, so a translated build is bilingual by construction. That may be
perfectly acceptable — it is worth saying so deliberately.

### U2-W7 — Station-dot hit target is 22×22

`Main.qml:376` draws a 10 px dot; `Main.qml:382` enlarges the `MouseArea` by
6 px per side, giving 22×22. WCAG 2.2 2.5.8 Target Size (Minimum), Level AA,
requires 24×24. Two pixels short, one character to fix (`margins: -7`).

Everything else clears 2.5.8: the 32 px toggles and close buttons, and the
Appearance segments at roughly 74×26. They sit below the 44 px comfort target
the source comments aim at, but that is a recommendation, not the AA line, and
the sidebar density argument for 32 is reasonable.

### U2-W8 — Colours defined outside the token system

`Theme` is a well-built token set, and then a meaningful amount of colour is
declared outside it with no dark-mode branch:

| Colour | Location |
|---|---|
| Amenity dots `#e08a3c`, `#3fae6b`, `#caa23a` | `TrainDetailPanel.qml:33-38` |
| Weather chip borders `#5b9bf3`, `#e08a3c` | `Main.qml`, FMI delegate |
| Delay rings `#F2A900`, `#E03131` | `TrainMarker.qml` |
| The whole train palette | `TrainMarker.colorFor()` |
| Suspect-ring greys `#b9bec6` / `#5f6368` | `TrainMarker.qml` (this one *is* theme-aware, inline) |

`#5b9bf3` in the weather chip is literally `Theme.accent`'s dark-mode value
pasted into a light-mode context. §1.4 asks for role-based tokens precisely so
that a dark-mode pass is a single-file edit; today it is a grep.

**Fix.** Move them into `Theme` as named roles (`amenityCatering`, `ringLate`,
`ringVeryLate`, `weatherCold`/`weatherWarm`, the train palette as a function).
U2-C4's dark-mode lift then has somewhere to live.

### U2-W9 — The first-run view hides most of the application

The app opens at zoom 7 (`Main.qml`, `savedZoom: 7.0`). At that zoom:

- train labels are hidden (threshold ≥ 8)
- station dots are hidden (≥ 9)
- weather chips are hidden (≥ 8)
- track geometry is culled to the viewport

So the first thing a new user sees is a scatter of anonymous 14 px coloured
dots, with nothing indicating that zooming in reveals labels, clickable
stations, and the rail network. Wayfinding and Affordance both ask that the user
know what is available and where.

**Fix.** Either open at zoom 8, or add a zoom-driven hint line in the sidebar
("Zoom in for train labels and stations") that disappears past the threshold —
the sidebar already has the plumbing for conditional text.

### U2-W10 — The sidebar carries seven groups permanently

`InfoPanel` stacks: identity header, live status, two counts, punctuality, status
text, Refresh, three train-type filters, track legend, siding toggle, weather
toggle + note, a three-way Appearance control, and attribution — eight
interactive controls in one always-open card. That is at or past Miller's ~7
chunks, and Hick's Law applies to a control set the user reads on every glance.

The asymmetry is that live status changes every second while Legend, Overlays,
and Appearance are set-once settings, and they compete for the same permanent
space.

**Fix.** Progressive disclosure: keep the header, live status, and train filters
open; collapse Legend / Overlays / Appearance / attribution behind a single
disclosure row. The card already flicks, so the mechanism is half-built.

---

## Opportunities

### U2-O1 — A train list would close the keyboard gap and the findability gap at once

There is currently no way to find a specific train. To locate IC 967 you scan
the map visually — Recognition over Recall inverted. A searchable, filterable
list in the left sidebar would simultaneously: give U2-C1 its keyboard path,
give assistive tech a navigable representation of the fleet, give the type
filters a visible consequence, and give the map a textual counterpart for the
zoom levels where labels are hidden (U2-W9). Highest value-per-line item in this
audit.

### U2-O2 — No empty or error state for the map

If the fleet is empty, the network is down, or MQTT never connects, the map is
blank tiles and the only signal is a muted status line inside the sidebar card.
An overlay on the map itself — where the user is looking — would do better, and
"Graceful Failure" wants a stated fallback, not silence.

### U2-O3 — The arrow animations exceed the motion budget

`TrainMarker.qml` animates the direction arrow's `x`, `y`, and `rotation` at
600 ms. §1.1 caps UI motion at 400 ms for full-screen transitions and treats
anything past 500 ms as reading broken; a small element's budget is 100–150 ms.
300 ms would still glide.

The 1000 ms `CoordinateAnimation` is a different case and is fine as-is: it
represents real-world motion paced to the data cadence, not a UI transition.

### U2-O4 — Panel opening is not announced

A panel fading in on the right is a silent event for a screen-reader user, and
there is no `Accessible` live-region equivalent. Pairs naturally with the
focus-management fix in U2-W4.

### U2-O5 — ScrollBar fade Behaviors are not gated on reduced motion

Three files animate the scrollbar handle's opacity for 120 ms without checking
`Theme.reducedMotion`, unlike every other animation in the app. Trivial, but the
gating is otherwise complete enough that the exception stands out.

---

## What the audit did not find

Recorded so a later pass doesn't re-derive it:

- **Text contrast in the panels is sound.** `textMuted` measures 5.74:1 light /
  6.98:1 dark, `accent` 5.70/5.84, `delayLate` 5.57, `delayEarly` 5.08 — the
  comments claiming ≥ 4.5:1 hold up. The `accentText` inversion for dark mode
  (6.35:1) is correctly reasoned.
- **Colour is never the sole carrier of state.** Delay pairs colour with `+N`
  text; lateness pairs the ring with a text badge; amenity dots carry an
  initial; cancelled pairs a red pill with the word. This is done thoroughly.
- **`inkFor()` is right, including the red-hue exception.** The WCAG 2 luminance
  formula genuinely under-weights red, and the documented workaround for
  `#FF0000`/`#FF006E` is the correct call.
- **Reduced motion is honoured everywhere that matters** (see U2-W5 for the
  missing control, U2-O5 for the one gap).
- **Latency is communicated.** The Refresh button states "Refreshing…" rather
  than going inert, satisfying the Doherty Threshold.
- **Section 3 (AI-specific UX) does not apply** — no AI features in this
  application.
- **Section 4 (embedded/MCU) does not apply** — desktop target with a GPU.
- **RTL is not required** for a Finnish-railway application; `QLocale`
  formatting (U2-W6) still is.

---

## Suggested order

1. ~~**U2-W1** (single style)~~ — done.
2. ~~**U2-C2, U2-C3**~~ — done.
3. **U2-C4 + U2-W8** — move the stray colours into `Theme`, then give the marker
   palette its dark branch. Same edit, done once.
4. **U2-W5, U2-W7, U2-O5** — one-liners.
5. **U2-C1 / U2-O1** — the train list. The largest item, and the one that closes
   the Level A failure.
6. **U2-W3, U2-W4** — focus management, best done alongside the list.
7. **U2-W2** (type scale), **U2-W6** (localisation), **U2-W9/W10** (sidebar) —
   each is a deliberate design decision to make rather than a bug to fix.
