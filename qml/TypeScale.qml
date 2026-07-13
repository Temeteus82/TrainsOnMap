pragma Singleton

import QtQuick

/// App-wide type scale (W2 from the UI audit). One modular scale, four roles, so
/// the UI stops mixing ~6 ad-hoc sizes. Generated from base 13 with a minor-third
/// ratio (1.2): caption ≈ ms(-1), body = ms(0), subhead ≈ ms(1), title ≈ ms(2).
/// The base is deliberately compact (13, not the 16 desktop default) because the
/// content is a dense map HUD; `caption` (11) is the floor — nothing renders below
/// it, which also lifts the old 8–10 px labels. Values are consumed as
/// `font.pointSize`, not `pixelSize`, so text scales with the OS "Large text" /
/// accessibility DPI setting instead of staying fixed. Reference by role name,
/// never by a raw number, and recompute the whole set if base/ratio changes.
///
/// `panel*` is a second scale at 0.75×, used wherever the plain roles read too
/// large: the card-style overlay panels (InfoPanel/TrainDetailPanel/
/// StationBoardPanel) and the train marker badge text. The FMI weather chip
/// still uses the plain `caption` — untouched unless it's flagged too.
QtObject {
    readonly property real caption: 11   // metadata, labels, map badges
    readonly property real body:    13    // default reading text
    readonly property real subhead: 16    // card titles / section headings
    readonly property real title:   19    // primary panel heading

    // Icon glyph sizing, kept on the same scale so icons track the text.
    readonly property real iconSm:  14
    readonly property real iconMd:  18

    readonly property real panelScale: 0.75
    readonly property real panelCaption: caption * panelScale
    readonly property real panelBody:    body * panelScale
    readonly property real panelSubhead: subhead * panelScale
    readonly property real panelTitle:   title * panelScale
    readonly property real panelIconSm:  iconSm * panelScale
    readonly property real panelIconMd:  iconMd * panelScale
}
