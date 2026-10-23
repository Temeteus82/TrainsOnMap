# UI audit — round 3 (2026-10-23)

Scope: the eleven files in `qml/` and the two vendored basemap styles
(`resources/styles/positron.json`, `dark-matter.json`). Checked against WCAG 2.2 AA,
the Laws of UX set, and the qt-ui-design checklist.

Round 2 closed every one of its findings. This round asks what has changed since
then: the basemap moved from Esri raster tiles to the MapLibre vector basemap
(CARTO Positron / Dark Matter, PR #81 onwards), the rail network became one
GeoJSON layer, and the basemap itself gained land, roads, airports and POI labels
(PR #94). Every map-drawn colour in `Theme` was last measured against a ground
that no longer exists.

Target profile: same as round 2 — desktop (Windows 11 primary; Linux verified),
Qt 6.11/6.12, resizable window (1100×820 default), ~60 cm, mouse + keyboard,
English UI over Finnish data, LTR.

IDs are prefixed `U3-`.

> **Status (2026-10-23): every finding is closed.** The four Criticals landed
> first (C4, C2, then C1 with C3), then all six Warnings and three
> Opportunities. W6 closed as *not a defect*, because its premise was wrong
> (see there). Everything was checked in the running app (MSVC build) in both
> themes. The contrast floors are now enforced by `scripts/check_contrast.py`
> (ctest `contrast`).
>
> **One correction to this document:** W5 said Dark Matter's labels all pass.
> They don't. Sea and lake-line names, suburb names, major road names, park and
> stadium names, and two country-name stops measure 1.1–4.0:1. The original
> check sampled only a few layers. The fix covers every label in both styles.

## How the map was measured

Contrast ratios use the WCAG 2 relative-luminance formula. A marker or line on the
map is measured against two things: the **dominant ground**, and the **worst
ground** it can plausibly sit on.

| | Light (Positron) | Dark (Dark Matter) |
|---|---|---|
| Dominant ground (style `background`) | `#fafaf8` | `#0e0e0e` |
| Water | `#d4dadc` | `#2c353c` |
| Worst common ground | road casing `#d0d0d0` | motorway fill `#494949` |

Most of the area is the dominant ground, so it decides pass or fail. The
worst-ground figure is reported where it matters: roads are thin lines a marker
crosses, not ground a marker sits on.

Where a token is drawn with `opacity`, it is blended over the ground before
measuring. Round 2 skipped that blending step, and that is where one of the
Criticals below comes from.

Today's in-app captures are dark-theme only (MSVC build, Helsinki-Vantaa and
Hämeenlinna). The light-theme findings come from measurement; they were not
re-checked on screen in this round.

---

## Critical

### U3-C1 — Stale trains fade below 3:1 in both themes — **Fixed**

`TrainMarker.qml:172` dims a stale train to `opacity: 0.55` on top of the grey
fill `Theme.trainStale`. Round 2 lifted that grey to 3.22:1, but it measured the
grey at full opacity. What actually reaches the screen is the blended colour:

| Stale marker | Light | Dark |
|---|---|---|
| `trainStale` at opacity 1.0 | 3.30:1 | 7.31:1 |
| **at the shipped 0.55** | **1.81:1** | **2.97:1** |
| Against the rail it sits on | 1.41:1 | 1.20:1 |

At zoom levels where markers are bare 14 px dots, a stale train is close to
invisible in light mode. A dot's fill is the only signal it has, so this fails
WCAG 1.4.11 (Non-text Contrast, AA).

There is a second, quieter failure. Stale is shown **only** by greyness and
dimming, which means colour is its only carrier (WCAG 1.4.1, Use of Color, Level
A). Every other marker state pairs colour with text or shape. Late trains get a
ring plus "+N min"; suspect positions get a ring of their own. Stale does not.

The suspect dimming (`0.72`) has the same effect, only milder. A light-cyan `W`
train at 0.72 measures 2.01:1.

**Fix.**
- Stop dimming the fill with `opacity`.
- Give stale a shape cue: draw the capsule hollow, with a `Theme.cardBg` fill and
  a 2 px `trainStale` border. A hollow dot reads as "not live" without relying on
  colour, and its border clears 3:1 in both themes.
- Mark suspect positions with their ring only, which already exists. Drop the
  dimming.

**Fixed** as proposed.
- A stale capsule is now hollow: `Theme.cardBg` fill inside a 2 px
  `Theme.trainStale` ring, with labels in `Theme.textMuted`.
- The marker no longer sets `opacity` for stale or suspect, and the arrow lost
  its extra stale dimming.
- On screen, the hollow grey rings read clearly in both themes at the
  country-scale view, where the dimmed dots used to disappear.

One thing to watch: a hollow stale dot is close in shape to a station dot
(`cardBg` fill, accent ring, from z9). They differ in size (14 vs 10 px) and
ring colour (grey vs accent). In practice the stale dot also sits on a train's
position rather than at a station. Revisit this if users confuse the two.

### U3-C2 — The train list colours its delay text with map-ring tokens — **Fixed**

`TrainListPanel.qml` paints the "+N" delay label in `Theme.ringLate` or
`Theme.ringVeryLate`. Those tokens were tuned as **3:1 graphics on the map**.
Here they are 9 pt **text** on the card:

| Token as text on the light card | Ratio | Required |
|---|---|---|
| `ringLate` `#A87200` | **4.11:1** | 4.5:1 (WCAG 1.4.3) |
| `ringVeryLate` `#E03131` | **4.47:1** | 4.5:1 |

Dark mode passes (8.25 and 5.52).

The thresholds also disagree with the map. The list shows amber "+N" for **any**
delay above 0. On the map, amber means 5–14 minutes late and smaller delays get
no ring. A 2-minute delay is therefore amber in the list and unmarked on the map.
That is the same colour carrying two meanings on one screen (§1.4).

**Fix.**
- Use the text token `Theme.delayLate` (5.57:1), which the station board and
  timetable already use for the same meaning.
- If the amber/red distinction should survive in the list, add text-grade
  siblings: `ringLateText` `#946500` measures 5.05:1. Apply the map's 5- and
  15-minute thresholds to them.

**Fixed** with both halves.
- The "+N" label takes its colour from the row's `ringState`, the same role the
  marker ring reads. That gives `Theme.delayLate` for red, the new
  `Theme.ringLateText` (`#946500` light / `#F2A900` dark) for amber, and
  `Theme.textMuted` otherwise.
- Amber and red therefore mean exactly what they mean on the map. That includes
  the passenger-only rule: a late cargo train shows "+N" in muted text, just as
  its marker carries no ring.

### U3-C3 — Three light-palette dots fall under 3:1 on Positron — **Fixed**

Round 2 kept the light marker palette deliberately: matching the juliadata.fi
legend is a project value. It measured only the hues that failed on Esri's grey
ground. On Positron's near-white ground, three more fail as dots:

| Marker | Fill | On `#fafaf8` |
|---|---|---|
| `W` | `#00B0B0` | **2.56:1** |
| Commuter fallback | `#30B0C7` | **2.46:1** |
| Moving, type unknown | `#FF9500` | **2.10:1** |
| Stopped, type unknown | `#8E8E93` | 3.12:1 (passes, narrowly) |

The capsule's hairline edge does not rescue them. In light mode it is
`Qt.rgba(0, 0, 0, 0.28)`, which blends to **1.99:1** against the ground. That edge
is also one of the two colour literals left outside `Theme`
(`TrainMarker.qml:220`, with the arrow stroke at `:310`).

**Fix, keeping the palette.** Strengthen the edge instead of changing the hues.
- Move the edge into `Theme` as `markerEdge`.
- Set it to black at 0.55 alpha in light mode, which measures **4.71:1**.
- The fill hues stay exactly as juliadata.fi has them. The dot gains a boundary
  that clears 1.4.11 on any ground, and that also fixes C1's light-mode case if
  the hollow-stale design above is not taken.

**Fixed** as proposed.
- New `Theme.markerEdge`: black at 0.55 in light mode, 4.71:1 on `#fafaf8`. Dark
  mode keeps white at 0.30.
- It drives both the capsule border and the arrow stroke, so the last two colour
  literals outside `Theme` are gone. In dark mode the arrow stroke goes from 0.35
  to 0.30 white, so that one value covers both.
- Every fill hue is unchanged.

### U3-C4 — Map attribution is not visible — **Fixed**

CARTO's basemap terms and OpenStreetMap's ODbL both require a prominent credit.
`Main.qml:207` sets `copyrightsVisible: true`, but the MapLibre plugin draws no
copyright notice: the vector-basemap spike recorded that it shows none, and none
of today's captures has one. The only credit in the app is the attribution line
inside **Legend & settings**, which U2-W10 folded away and closed by default.

So on a fresh launch, no attribution is visible anywhere. This is a licence
problem rather than a WCAG one. It is in this section because it has to be fixed
before anything else ships.

**Fix.** Add a small persistent label at the map's bottom-right corner reading
"© CARTO © OpenStreetMap": caption size, `Theme.textMuted` on a `Theme.cardBg`
chip. Keep the full credit line in the settings group.

**Fixed** in `Main.qml`.
- A persistent chip reads "© CARTO © OpenStreetMap contributors" at the map's
  bottom-right.
- When the train detail panel or station board is open, the chip steps left of
  it instead of disappearing underneath.
- The fuller credit, which includes Digitraffic, stays in Legend & settings.

---

## Warnings

### U3-W1 — Rails are drawn twice from zoom 13 — **Fixed**

Positron and Dark Matter each draw their own railway lines from z13: `rail` and
`rail_dash`, plus `tunnel_rail` and `tunnel_rail_dash`. The app draws
`rails-line` from its own network on top. The two geometries come from different
sources and do not line up, so close in, tracks appear doubled and slightly
offset. This is visible around Helsinki-Vantaa in today's captures.

It also blurs the one thing the map is for. Trains snap to the app's network, so
a second parallel line next to it raises the question of which line the train
is actually on.

**Fix.** Remove those four layers from both styles. That is a text patch of the
kind made in PR #94, and nothing else reads them.

**Fixed** as proposed: both styles go from 99 layers to 95. On screen, close
in, the network is a single line again.

### U3-W2 — Switching theme still rebuilds the whole map — **Fixed**

`Main.qml:134-140` switches theme by destroying and recreating the `Map` through
the `Loader`, together with the `savedCenter*`/`savedZoom`/`everBuilt` logic that
carries the view across. That design was forced by the old `osm` plugin. The
vector-basemap spike measured an **877 ms GUI-thread stall** during this rebuild
on Windows, with no progress indication. That breaks the Doherty Threshold: no
feedback within 400 ms.

The same spike verified the alternative on real hardware. Register both styles
with the plugin and switch `activeMapType`. The custom rails layer is re-applied
on the new style automatically (`qgeomap.cpp:467`), there is no rebuild, and the
stall drops to the 392 ms startup figure.

**Fix.** Pass both style URLs to `maplibre.map.styles` and bind
`activeMapType` to `Theme.isDark`. Then delete the `Loader`, the `saved*`
properties and `everBuilt`. The result is net-negative code and keeps the
round-2 first-run fix.

**Fixed** as proposed.
- The plugin takes both styles as a list of `{url, name}` objects ("light",
  "dark").
- A `Binding` sets `activeMapType` to the type whose name matches
  `Theme.isDark`. Its value falls back to the current type until the plugin
  has registered the styles. Without that fallback, the binding logged a
  conversion warning at startup.
- The `Loader`, `saved*`, `everBuilt` and the theme `Connections` are gone. The
  `Map` now sits directly in the window.
- The opening view is two lines in `Component.onCompleted`.
- On screen, flipping Dark → Light → Dark keeps the view, the rails layer
  survives each switch, and its paint follows the new theme colours.

### U3-W3 — The selected train's route overlay is under 3:1 — **Fixed**

`Main.qml:237-248` draws the selected train's resolved route in `Theme.accent` at
`opacity: 0.45`. Once the colour is blended, the overlay is **2.00:1** on the
light ground and **2.26:1** on the dark one. Next to the train itself, the route
is the main thing selecting a train shows on the map, so 1.4.11 applies.

**Fix.** Raise the opacity to 0.7 (3.12:1 / 3.87:1), or draw the route opaque at
a narrower width. The breadcrumb trail (0.35) is meant to fade and can stay as
it is.

**Fixed** with opacity 0.7. The contrast check measures it on every run.

### U3-W4 — Theme's map-contrast notes describe a basemap that is gone — **Fixed**

`Theme.qml:81-117` and `:193-195` quote every map ratio against Esri Light Gray
`#efefef` / Dark Gray `#474749`, and explain the dark values as lifts for that
mid-grey ground. The real grounds are now `#fafaf8` and `#0e0e0e`.

The dark-mode notes are now wrong in the reassuring direction: all dark tokens
measure 4.3:1 or better on the new ground. The light-mode notes are what hid
C1 and C3.

Round 2's main structural win was that a theme pass is a one-file edit. That only
works if the numbers in that one file are true.

**Fix.**
- Re-measure and rewrite the comments against the CARTO grounds.
- Better, add the ratio table as a test that fails when a map token drops under
  3:1. Two grounds times about 25 tokens is a small pure-Python script, much like
  the measurement used for this document.

**Fixed** with both halves.
- **The comments.** The map section of `Theme.qml` now quotes its ratios against
  `#0e0e0e` / `#fafaf8` and says where they come from.
- **The check.** `scripts/check_contrast.py` reads the tokens out of `Theme.qml`
  and the grounds out of the two style files, so it can't go stale the way the
  comments did. It runs as ctest `contrast`, registered only when a Python
  interpreter is found. It enforces:
  - eight map-drawn tokens and the route overlay at ≥ 3:1 against the ground;
  - six text tokens at ≥ 4.5:1 on the card;
  - every basemap label at ≥ 4.5:1 against its own ground.
- **What it caught straight away.** The dark `markerEdge` from C3 (white at
  0.30) measured 2.64:1. It went to 0.35, which is 3.18:1 and is also the
  arrow stroke's old value.

### U3-W5 — Basemap labels miss 4.5:1 in light mode — **Fixed**

The basemap carries real text: place names, road names and, since PR #94, POIs
and airport names. In Positron:

| Label | Colour | On `#fafaf8` |
|---|---|---|
| POI names (`poi_major`) | `#8a8f96` | **3.12:1** |
| Airport names (`aerodrome_label`) | `#7b8490` | **3.62:1** |
| Road names (CARTO) | `#838383` | **3.63:1** |
| Village names (CARTO) | `#697b89` | **4.19:1** |

Dark Matter passes every one (5.0–6.8). The two layers added in PR #94 are the
cheapest fixes: `#6b7079` measures 4.76:1. CARTO's own label colours are a
deliberate low-key design, so darkening them is a style decision. They are still
text at 8–10 px.

**Fix.** Set POI and airport labels to `#6b7079` in `positron.json`. Decide
whether to darken CARTO's road and place colours to the same floor.

**Fixed for every label in both styles**, not just the two layers from PR #94.
- **How.** Each failing `text-color` (including each stop of a zoom-stop
  colour) is mixed toward black (Positron) or white (Dark Matter) in 1% steps
  until it clears 4.6:1 against its own ground. Water labels are measured
  against water. The step keeps each colour's hue and moves it only as far as
  it has to.
- **How many.** 32 colour values changed in Positron and 10 in Dark Matter.
  This includes the Dark Matter failures this document originally missed (see
  the correction at the top).
- **The extremes.** Positron's sea names went from 1.46:1 to 4.58:1. Dark
  Matter's major road names went from 1.65:1 to 4.62:1.
- CARTO's design is still recognisably low-key, just readable.

### U3-W6 — Map text does not follow the type scale or OS text size — **Not a defect**

`TypeScale` works in `pointSize` so that panel text grows with the OS
"Large text" setting. Basemap labels are fixed pixel sizes in the style JSON:
CARTO uses 8–13 px, and PR #94's labels 9–12 px. Train badges follow the OS
setting and the town and street names beneath them do not, so under that setting
they drift apart.

This is a Warning rather than a failure. WCAG 1.4.4 (Resize Text) allows map
imagery to rely on zoom, and the map zooms. But it is the one place where the
app's own type system has no say.

**Fix, if wanted.** Scale `text-size` by the factor the OS applies to
`pointSize`. The styles are local files, so the app could rewrite their
`text-size` values when it loads them. I haven't checked whether the MapLibre
Qt bindings offer a runtime multiplier that would avoid the rewrite.

**Closed: the premise was wrong.** The scale `pointSize` follows is the
display's scaling (logical DPI). The MapLibre plugin hands the renderer that
same factor: `window->devicePixelRatio()` in `src/location/qgeomap.cpp:93-99`
of maplibre-native-qt. So at 125% or 150% display scaling, the basemap labels
grow with the panels; they do not drift apart. The one setting this does not
cover is Windows' separate "Make text bigger" slider. That was not tested here,
and it is a question about Qt Quick generally, not about the map.

---

## Opportunities

### U3-O1 — Make the dot-mode marker target 24 px — **Fixed**

In dot mode the marker's `MouseArea` is the 14×14 capsule. That is below the
24×24 of WCAG 2.5.8, which round 2 fixed for station dots. It is not a failure:
2.5.8 exempts a target whose function is available elsewhere, and the train list
does that. But it is a small fix: give the `MouseArea` `anchors.margins: -5`
while `labelsVisible` is false.

**Fixed** exactly as written.

### U3-O2 — Rails get thinner than roads as you zoom in — **Fixed**

`rails-line` uses fixed widths: 2.2 px for running lines, 1.3 px for sidings.
PR #94's road widths grow with zoom, so from about z14 a minor street is 4–12 px
and a primary road 6–16 px, while the rail stays 2.2 px. Close in, the subject of
the app is visually weaker than the streets around it.

An interpolated width, such as `["interpolate", ["linear"], ["zoom"], 8, 2.2,
16, 5]` for running lines, keeps the rails dominant at every scale.

**Fixed** with that expression: running lines go from 2.2 to 5 px and sidings
from 1.3 to 2.5 px between z8 and z16. The main/siding split is still driven
by `paaraide`.

### U3-O3 — POIs have no category cue — **Fixed**

PR #94 made POIs text-only, because CARTO's sprite contains a single circle icon.
A hospital and a museum therefore look identical. A small app-owned sprite would
add category icons. The MapLibre QML `Style` can register images alongside the
rails source, so the icons need not come from CARTO. Low priority: the labels are
background detail.

**Fixed** with nine app-owned SVG icons in `resources/poi/`: health, culture,
worship, education, lodging, shop, transport, sport and civic.
- **What they look like.** Each is a white glyph on a `#6b7079` disc, which
  clears 3:1 on both grounds (4.8:1 light, 3.7:1 dark), so one set serves
  both themes.
- **How they get to the map.** They are embedded as `:/poi/*.svg` and
  registered from `Main.qml` with `ImageParameter`. The plugin loads them
  through `QImage`, so the `qsvg` image plugin that already ships with the app
  handles them.
- **How the style uses them.** `poi_major` picks an icon with a `match` on the
  POI class. Each icon is drawn at 30 px and shown at `icon-size: 0.5`, so it
  stays sharp on a 2× display. The name sits beneath the icon and is optional,
  so when labels collide the icon survives and the name drops.
- **Why nine explicit parameters.** `Style` takes its parameters as plain
  children, so a `Repeater`, which only creates Items, cannot generate them.
- Verified on screen: the culture icon over a gallery and the shop icon at a
  supermarket.

---

## What the audit did not find

Recorded so a later pass does not re-derive it:

- **Every round-2 fix is still in the code** (checked by reading, not by
  re-testing each one). That covers focus entry and return on the
  panels, Escape, the train list's keyboard and screen-reader path, the
  focus-scrolling sidebar, reduced motion (including a gated `CollapseButton`
  chevron), `controlOutline`, and the first-run view.
- **Panel text contrast is unchanged.** The cards did not move, so round 2's
  card measurements stand.
- **The dark marker palette is now comfortable.** Every dark marker token
  measures 4.3:1 or better on Dark Matter's ground. The lifts made for Esri's
  mid-grey dark ground now have plenty of margin.
- **Station dots, weather chips and the empty-state card** all sit on
  `Theme.cardBg` or carry an accent ring that clears 3:1 (5.50 light / 6.83
  dark).
- **Hardcoded colours.** Two `Qt.rgba` literals remain outside `Theme`, both in
  `TrainMarker`, and C3 moves them. There are no hex literals outside `Theme`.
- **The basemap work from this week holds up at the levels checked.** The land
  tints stay subtle, and every dark-theme marker clears 3:1 against the forest
  green.
- **Sections 3 (AI) and 4 (embedded) do not apply.**

---

## Suggested order

1. ~~**U3-C4**~~ (attribution): done.
2. ~~**U3-C2**~~ (list delay text): done.
3. ~~**U3-C1 + U3-C3**~~: done together. As it turned out, the hollow stale
   capsule uses `trainStale` for its ring rather than `markerEdge`; the two fixes
   share only the marker's edge code.
4. ~~**U3-W1 + U3-W5**~~: done, in one style pass.
5. ~~**U3-W4**~~: done. Its check caught one more failure on its first run, the
   dark `markerEdge`.
6. ~~**U3-W2**~~: done; it removed the `Loader` and saved-view code.
7. ~~**U3-W3**, then the Opportunities~~: done. **U3-W6** closed as not a
   defect.
