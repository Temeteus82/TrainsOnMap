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
    readonly property string basemapStyle: isDark ? "dark_all" : "light_all"
    readonly property color railColor: isDark ? "#5a6470" : "#8c95a0"        // running lines
    readonly property color railSidingColor: isDark ? "#3d444e" : "#bcc3cb"  // yards / sidings

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
