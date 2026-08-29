pragma Singleton

import QtQuick

/// App-wide type scale (W2 from the first UI audit, U2-W2 from the second). One
/// scale, four roles, so the UI stops mixing ad-hoc sizes. Values are consumed as
/// `font.pointSize`, not `pixelSize`, so text scales with the OS "Large text" /
/// accessibility DPI setting instead of staying fixed. Reference by role name,
/// never by a raw number.
///
/// `caption` (9) is the floor: nothing renders below it. That is literally true —
/// U2-W2 found it wasn't. A second `panel*` scale at 0.75× used to sit under this
/// one for the card overlays, which is where nearly all the app's text lives, so
/// the real floor was 8.25 pt and the real body 9.75 pt: the stated floor was a
/// fiction, one visual role (a chip floating on the map) had two different sizes
/// depending on whether it was the FMI weather chip or a train marker badge, and
/// four roles had become eight. The panel scale is gone; these four are the whole
/// set.
///
/// **These are hand-tuned, not generated.** Earlier revisions claimed a minor-
/// third scale from a base, which stopped being true the moment the sizes were
/// checked against the running app: a 1.2 ratio steps 9 → 11 → 13 → 16, and the
/// gaps are too coarse for a dense HUD — the step the panels actually wanted sat
/// between two of them. Bases 12 and 11 were both tried and both read a point
/// heavy in the overlay cards. So the numbers below are what looks right on
/// screen, and `body` (10) is one point off `caption` deliberately: hierarchy in
/// the cards comes from weight and colour (`textStrong` vs `textMuted`, bold
/// headers), not from size alone.
///
/// That puts `body` under the 16 px @96 dpi desktop minimum §1.2 asks for. It is
/// a knowing trade of that guideline for density, the same one the original
/// 13-base made — the difference is that the floor no longer lies about where it
/// is. Change these by looking at the app, not by re-deriving a ratio, and resist
/// adding a fifth role: a `dense` half-step lived here for exactly one revision
/// before `body` came down to meet it.

QtObject {
    readonly property real caption:  9   // metadata, labels, map badges — the floor
    readonly property real body:    10    // default reading text
    readonly property real subhead: 12    // card titles / section headings
    readonly property real title:   15    // primary panel heading

    // Icon glyph sizing. These are px (AppIcon is a Canvas on a 24 px grid), not
    // pt, so they don't track the text through the OS DPI setting — they're sized
    // to sit correctly next to it at 100%.
    readonly property real iconSm:  11
    readonly property real iconMd:  14
}
