pragma Singleton

import QtQuick
import QtCore

/// App-wide palette + theme state. `mode` is "auto" (follow the desktop colour
/// scheme), "light", or "dark"; `isDark` resolves it against the system setting.
/// All UI surfaces and the map basemap read their colours from here so a single
/// switch retheme the whole app.
QtObject {
    id: theme

    // "auto" | "light" | "dark". Set from the sidebar toggle; "auto" tracks the
    // desktop via QStyleHints::colorScheme (Qt 6.8+).
    property string mode: "auto"

    // W3 (UI audit): reduced-motion opt-out. Qt has no `prefers-reduced-motion`
    // equivalent, so expose a project-level flag instead; non-essential animations
    // (e.g. the live-dot pulse) gate on this. Persisted via QSettings so it sticks
    // across launches and can be flipped without a rebuild.
    property bool reducedMotion: false
    property Settings _settings: Settings {
        category: "Appearance"
        property alias reducedMotion: theme.reducedMotion
    }

    readonly property bool systemDark: Application.styleHints.colorScheme === Qt.ColorScheme.Dark
    readonly property bool isDark: mode === "dark" || (mode === "auto" && systemDark)

    // ---- Surfaces ---------------------------------------------------------
    readonly property color windowBg:  isDark ? "#15171b" : "#e9eaec"   // map placeholder backdrop
    readonly property color cardBg:    isDark ? Qt.rgba(0.11, 0.12, 0.14, 0.97)
                                              : Qt.rgba(1, 1, 1, 0.96)
    readonly property color hairline:  isDark ? "#34373d" : "#e6e8ec"
    // Boundary of an *interactive* control (unchecked checkbox, segmented-control
    // frame). Deliberately not `hairline`: that's a divider, and at 1.2:1 (light)
    // / 1.4:1 (dark) against the card it left an unchecked control with no
    // perceptible edge — the affordance appeared only once the control was
    // already on. WCAG 1.4.11 wants >= 3:1, and a control boundary has to clear
    // it against whatever sits on *both* sides: these do, against the card
    // (3.7 / 4.0) and against `subtleHover`, which fills the segmented control
    // (3.4 / 3.5).
    readonly property color controlOutline: isDark ? "#767d88" : "#7e858f"
    readonly property color shadow:    isDark ? Qt.rgba(0, 0, 0, 0.45) : Qt.rgba(0, 0, 0, 0.06)

    // ---- Text -------------------------------------------------------------
    readonly property color textStrong: isDark ? "#f2f4f7" : "#1a1d21"
    readonly property color textMuted:  isDark ? "#a3a9b2" : "#5f6671"  // both ≥ 4.5:1 on cardBg

    // ---- Accents ----------------------------------------------------------
    readonly property color accent:      isDark ? "#5b9bf3" : "#1565c0"
    // NB: don't name this "onAccent" — QML reads an on<Capital> identifier as a
    // signal handler, not a property, and the file fails to compile.
    // Dark mode's accent (#5b9bf3) is too light to carry white text (~2.8:1), so
    // use a near-black ink on it there (~6.4:1); light mode keeps white on #1565c0.
    readonly property color accentText:  isDark ? "#15171b" : "white"
    readonly property color focusRing:   isDark ? "#7eb0f6" : "#1565c0"
    readonly property color iconBadgeBg: isDark ? "#1d2c40" : "#eaf1fb"
    readonly property color subtleHover: isDark ? "#262a30" : "#f1f3f6"
    readonly property color subtlePress: isDark ? "#30353c" : "#e4e7ec"

    // ---- Live indicator ---------------------------------------------------
    // `liveOn` tints the 10 px connected dot, which only has to clear the 3:1
    // WCAG 1.4.11 asks of a graphical object. The same green as *text* measured
    // 3.04:1 on the light card — under 1.4.3's 4.5:1 — so the "LIVE" label reads
    // from `liveOnText` instead: same hue, darkened to 5.4:1. Dark mode never
    // failed (5.4:1), so it keeps one green for both roles.
    readonly property color liveOn:     "#18a957"
    readonly property color liveOnText: isDark ? "#18a957" : "#0f7a3d"
    // Disconnected dot. The old greys (#b0b6be / #5c636c) sat at 2.0:1 / 2.7:1 on
    // the card, under 1.4.11's 3:1 for a state-carrying graphic; these clear it
    // in both themes.
    readonly property color liveOff:    isDark ? "#767d88" : "#868d97"

    // ---- Delay semantics --------------------------------------------------
    readonly property color delayLate:   isDark ? "#ef6b6b" : "#c62828"
    readonly property color delayEarly:  isDark ? "#6bbf73" : "#2e7d32"
    readonly property color cancelledBg: isDark ? "#b53030" : "#c62828"

    // ---- Map --------------------------------------------------------------
    // Esri Gray Canvas, described by a provider manifest embedded in resources
    // (see CMakeLists.txt "basemap"). basemapStyle only names the disk cache
    // directory now — it deliberately no longer matches the old CARTO
    // "light_all"/"dark_all" ids, so a stale cache of watermarked CARTO tiles is
    // never served to a build that has moved on.
    readonly property string basemapStyle: isDark ? "esri-dark-gray" : "esri-light-gray"
    readonly property url basemapRepo: isDark ? "qrc:/basemap/dark/" : "qrc:/basemap/light/"
    // Track strands. Both were well under 1.4.11's 3:1 on the Esri ground
    // (running 1.54 / siding 1.06 dark, 2.81 / 1.55 light) and were lifted to
    // clear it. On the dark ground they had to go *lighter*: #474749 is midtone
    // enough that even pure black only reaches 2.27:1, so darkening a siding to
    // make it recede is not available there.
    //
    // The pair also has to stay told apart from each other. On the map that is
    // reinforced by width (2.2 px running vs 1.3 px siding, Main.qml), but the
    // sidebar legend draws both as identical 3 px swatches, so colour carries it
    // alone there — hence the >= 1.4:1 sibling separation the marker palette
    // uses, which puts the running line at ~4.2:1 and the siding at ~3.0:1.
    readonly property color railColor: isDark ? "#A8B0B9" : "#69727F"        // running lines  4.2 / 4.2
    readonly property color railSidingColor: isDark ? "#8994A3" : "#7E8B9B"  // yards / sidings 3.0 / 3.0

    // ---- Map overlay semantics (U2-W8) ------------------------------------
    // Everything below used to be a hex literal sitting in TrainMarker.qml,
    // Main.qml or TrainDetailPanel.qml with no dark-mode branch — which made a
    // theme pass a grep instead of a one-file edit, and is how the marker
    // palette ended up light-only over a basemap that flips (U2-C4).
    //
    // Ratios quoted are against the basemap the thing is drawn on (Esri Light
    // Gray #efefef / Dark Gray #474749) or, for the amenity badges, the card.
    // WCAG 1.4.11 wants >= 3:1 for a graphic that carries state.
    //
    // NB the dark ground got much lighter when the basemap moved off CARTO
    // (#1b1b1b -> #474749), so every dark-branch ratio below is roughly halved
    // from what it measured on Dark Matter. The map-drawn tokens were re-checked
    // against the new ground; the on-card badges are unaffected.

    // Delay rings. Amber measured 1.87:1 on Positron — it is paired with a
    // "+N min" text badge so colour was never the sole carrier, but the ring
    // itself still has to clear 3:1, hence the darkened light-mode value.
    readonly property color ringLate:     isDark ? "#F2A900" : "#A87200"  // 8.6 / 3.9
    // Dark value lifted #F25555 -> #F36868 when the basemap moved to Esri Dark
    // Gray (#474749): the old red measured 3.1:1 on CARTO Dark Matter's #1b1b1b
    // but only 2.74:1 on the lighter Esri ground, under 1.4.11's 3:1.
    readonly property color ringVeryLate: isDark ? "#F36868" : "#E03131"  // 3.1 / 4.2
    readonly property color ringReady:    liveOn
    // A fix the matcher could not place on the network: a neutral outline.
    readonly property color ringSuspect:  isDark ? "#b9bec6" : "#5f6368"

    // Weather chip borders (FMI overlay). Light mode was using dark mode's
    // accent (#5b9bf3) at 2.6:1 on Positron.
    readonly property color weatherCold: isDark ? "#5b9bf3" : "#2E77D8"   // 6.1 / 4.1
    readonly property color weatherWarm: isDark ? "#e08a3c" : "#B36A22"   // 6.5 / 4.2

    // Carriage amenity badges, drawn on the card. Each also carries its own
    // initial (C/A/F/P), so these are reinforcement, not the sole signal.
    readonly property color amenityCatering:   isDark ? "#e08a3c" : "#B36A22"  // 6.2 / 4.2
    readonly property color amenityAccessible: accent
    readonly property color amenityFamily:     isDark ? "#3fae6b" : "#2F8850"  // 5.9 / 4.4
    readonly property color amenityPet:        isDark ? "#caa23a" : "#9E7C1E"  // 6.9 / 3.9

    // ---- Train marker palette (U2-C4) -------------------------------------
    // The light branch is the juliadata.fi legend verbatim — matching that map
    // is a deliberate project value, so it is not touched.
    //
    // The dark branch exists because those hues were authored for a light
    // basemap: cargo navy #000077 measures 1.04:1 on Dark Matter, i.e.
    // invisible, and below zoom 8 the capsule collapses to a bare dot where the
    // fill is the only signal. Every dark value clears 3:1 there.
    //
    // The lift is hand-tuned rather than a mechanical luminance raise: raising
    // each colour to the 3:1 floor independently collapses S onto HL, T onto
    // PYO and VET onto VEV — same-hue pairs that the light palette separates by
    // lightness. Each pair is kept >= 1.4:1 apart from its sibling so the
    // type-coding survives the theme.
    function trainColorFor(type, category, speed) {
        if (isDark) {
            switch (type) {
            case "IC":  case "IC2":               return "#FF3B3B"   // InterCity — red
            case "S":                             return "#00B050"   // Pendolino — green
            case "PYO": case "P":                 return "#8A8AFF"   // night / local — blue
            case "H":   case "HDM": case "HSM":   return "#C25450"   // express / diesel — brick
            case "HL":                            return "#5FD16B"   // Helsinki commuter — light green
            case "HV":  case "MV":                return "#FF006E"   // museum / shunting — pink
            case "PAI":                           return "#00A0A0"   // teal
            case "SAA": case "VLI":               return "#00C4C4"   // cyan-teal
            case "W":                             return "#5FE0E0"   // light cyan
            case "T":                             return "#4A6BFF"   // cargo — blue
            case "TYO":                           return "#B99A18"   // work / maintenance — olive
            case "VET":                           return "#A64DE0"   // locomotive haul — violet
            case "VEV":                           return "#E86AE8"   // magenta
            }
            switch (category) {
            case "Cargo":    return "#4A6BFF"
            case "Commuter": return "#30B0C7"
            }
            return speed > 0 ? "#FF9500" : "#8E8E93"
        }
        switch (type) {
        case "IC":  case "IC2":               return "#FF0000"   // InterCity — red
        case "S":                             return "#007700"   // Pendolino — green
        case "PYO": case "P":                 return "#0000FF"   // night / local — blue
        case "H":   case "HDM": case "HSM":   return "#770000"   // express / diesel — dark red
        case "HL":                            return "#004400"   // Helsinki commuter — dark green
        case "HV":  case "MV":                return "#FF006E"   // museum / shunting — pink
        case "PAI":                           return "#007070"   // teal
        case "SAA": case "VLI":               return "#009090"   // cyan-teal
        case "W":                             return "#00B0B0"   // light cyan
        case "T":                             return "#000077"   // cargo — navy
        case "TYO":                           return "#7F6A00"   // work / maintenance — olive
        case "VET":                           return "#660066"   // locomotive haul — purple
        case "VEV":                           return "#9E009E"   // magenta
        }
        switch (category) {        // unmatched type: fall back to the broad class
        case "Cargo":    return "#000077"
        case "Commuter": return "#30B0C7"
        }
        return speed > 0 ? "#FF9500" : "#8E8E93"   // moving = orange, stopped = grey
    }

    // A stale train greys out entirely (juliadata's "harmaa" marker). #9AA0A6
    // measured 2.46:1 on Positron, so light mode gets a darkened grey.
    readonly property color trainStale: isDark ? "#9AA0A6" : "#848B92"   // 6.5 / 3.2

    // Pick black or white ink for text/glyphs on a data-driven background colour,
    // whichever gives the most contrast (WCAG 2 relative luminance). Shared by any
    // surface that colours its own fill at runtime (train markers, amenity badges)
    // instead of picking from Theme's own fixed light/dark pairs.
    function inkFor(fill) {
        const lin = (c) => c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4)
        const L = 0.2126 * lin(fill.r) + 0.7152 * lin(fill.g) + 0.0722 * lin(fill.b)
        return (L + 0.05) / 0.05 >= 1.05 / (L + 0.05) ? "#15171b" : "white"
    }
}
