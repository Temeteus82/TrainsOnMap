import QtQuick
import QtLocation

import TrainsOnMap

/// Delegate for a single train inside a MapItemView. Model roles come from
/// TrainListModel: coordinate, trainNumber, speed, bearing, trainType, category,
/// commuterLine.
///
/// Colours are the juliadata.fi palette, keyed on trainType, with a
/// category/speed fallback for types the legend doesn't enumerate (so a moving
/// train is never a bland grey dot). See trainspotter-qt-docs/MarkerColors.Qt.md.
MapQuickItem {
    id: marker

    required property var model
    property bool selected: false
    // Show the text badges only when zoomed in enough to read them; at country
    // scale the labels collide into an unreadable mass, so we render dots only.
    property bool labelsVisible: true

    signal clicked(int trainNumber, string departureDate)

    // The juliadata palette is tuned for the light basemap. On the dark basemap
    // the darkest hues (navy cargo, dark-red/green, purple) become illegible as
    // 11 px text, so lift their lightness in dark mode while keeping the hue
    // identity. Achromatic greys (the stopped/unknown fallbacks) get a fixed
    // light ink instead, since Qt.hsla() can't reconstruct a hueless colour.
    function legibleInk(c) {
        if (!Theme.isDark)
            return c
        if (c.hslSaturation < 0.15)
            return "#d6dae0"
        return Qt.hsla(c.hslHue, Math.min(c.hslSaturation, 0.85),
                       Math.max(c.hslLightness, 0.62), 1)
    }
    // O1: a semi-opaque rounded "pill" sits behind the label so it stays legible
    // on any basemap tile — more robust than the old thin 1 px text outline, which
    // could wash out where glyph and tile were close in tone.
    readonly property color pillBg:     Theme.isDark ? Qt.rgba(0.08, 0.09, 0.11, 0.82)
                                                     : Qt.rgba(1, 1, 1, 0.82)
    readonly property color pillBorder: Theme.isDark ? Qt.rgba(1, 1, 1, 0.12)
                                                     : Qt.rgba(0, 0, 0, 0.12)

    // trainType -> juliadata fill colour; unmatched types fall to category/speed.
    function colorFor(type, category, speed) {
        switch (type) {
        case "IC":  case "IC2":               return "#FF0000";   // InterCity — red
        case "S":                             return "#007700";   // Pendolino — green
        case "PYO": case "P":                 return "#0000FF";   // night / local — blue
        case "H":   case "HDM": case "HSM":   return "#770000";   // express / diesel — dark red
        case "HL":                            return "#004400";   // Helsinki commuter — dark green
        case "HV":  case "MV":                return "#FF006E";   // museum / shunting — pink
        case "PAI":                           return "#007070";   // teal
        case "SAA": case "VLI":               return "#009090";   // cyan-teal
        case "W":                             return "#00B0B0";   // light cyan
        case "T":                             return "#000077";   // cargo — navy
        case "TYO":                           return "#7F6A00";   // work / maintenance — olive
        case "VET":                           return "#660066";   // locomotive haul — purple
        case "VEV":                           return "#9E009E";   // magenta
        }
        switch (category) {        // unmatched type: fall back to the broad class
        case "Cargo":    return "#000077";   // navy
        case "Commuter": return "#30B0C7";   // teal
        }
        return speed > 0 ? "#FF9500" : "#8E8E93";   // moving = orange, stopped = grey
    }

    // Badge text: a commuter line letter if any, else "TYPE NUMBER", else number.
    function labelFor(line, type, number) {
        if (line && line.length > 0)
            return line;                       // "R", "Z", "U", …
        if (type && type.length > 0)
            return type + " " + number;        // "IC 967", "T 5280"
        return "" + number;                    // "967" until the type cache loads
    }

    readonly property color trainColor: colorFor(model.trainType, model.category, model.speed)
    readonly property string badgeLabel: labelFor(model.commuterLine, model.trainType, model.trainNumber)

    // Live status ring (passenger trains only): green = ready/stopped on time,
    // amber = 5–14 min late, red = 15+ min late, stale = greyed/dimmed,
    // none = running on time / under 5 min late / cargo & special trains.
    readonly property bool stale: model.ringState === "stale"
    // Late tiers also carry a textual "+N min" badge so the state is legible
    // without relying on the ring colour alone (colour-blind safety).
    readonly property bool late: model.ringState === "amber" || model.ringState === "red"
    readonly property color ringColor: {
        switch (model.ringState) {
        case "green": return "#18A957";
        case "amber": return "#F2A900";
        case "red":   return "#E03131";
        default:      return "transparent";
        }
    }
    // Greyed when stale, else the type colour.
    readonly property color dotColor: stale ? "#9AA0A6" : trainColor

    coordinate: model.coordinate
    // Anchor the coordinate at the centre of the dot (the label floats right).
    anchorPoint.x: dotGroup.width / 2
    anchorPoint.y: dotGroup.height / 2

    sourceItem: Item {
        width: row.width
        height: row.height
        opacity: marker.stale ? 0.5 : 1.0   // dim a stale / not-running train

        Row {
            id: row
            spacing: 3

            // Dot + (optional) status ring. The ring stays upright; only the dot
            // and its heading notch rotate.
            Item {
                id: dotGroup
                width: 21
                height: 21

                // Status ring drawn behind the dot when one applies.
                Rectangle {
                    anchors.centerIn: parent
                    width: 21
                    height: 21
                    radius: 10.5
                    color: "transparent"
                    border.width: 3
                    border.color: marker.ringColor
                    visible: marker.ringColor.a > 0
                }

                // O2: selection is a haloed accent ring (accent outer edge + a
                // light/dark inner halo) — reads on any dot colour, including the
                // blue/navy types the old single accent border blended into.
                Rectangle {
                    anchors.centerIn: parent
                    width: 21
                    height: 21
                    radius: 10.5
                    color: "transparent"
                    border.width: 5
                    border.color: Theme.isDark ? "#0c0e12" : "white"
                    visible: marker.selected
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 21
                    height: 21
                    radius: 10.5
                    color: "transparent"
                    border.width: 2.5
                    border.color: Theme.accent
                    visible: marker.selected
                }

                Rectangle {
                    id: dot
                    anchors.centerIn: parent
                    width: 15
                    height: 15
                    radius: 7.5
                    color: marker.dotColor
                    // Light stroke in dark mode so dark dots separate from dark tiles.
                    border.color: Theme.isDark ? "#cdd2da" : "#10141a"
                    border.width: 1
                    rotation: marker.model.bearing      // dot/notch rotate to heading

                    // Small notch indicating heading.
                    Rectangle {
                        width: 2
                        height: 7
                        color: "white"
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.topMargin: 1
                    }
                }
            }

            // Type-coloured label ("IC 967" / line letter) plus a km/h sub-line
            // while moving, on a semi-opaque pill (O1) so it reads on any tile.
            Rectangle {
                anchors.verticalCenter: dotGroup.verticalCenter
                visible: marker.labelsVisible
                radius: 5
                color: marker.pillBg
                border.color: marker.pillBorder
                border.width: 1
                implicitWidth: labelCol.implicitWidth + 12
                implicitHeight: labelCol.implicitHeight + 6

                Column {
                    id: labelCol
                    anchors.centerIn: parent
                    spacing: 0

                    Text {
                        text: marker.badgeLabel
                        font.pixelSize: 12
                        font.bold: true
                        color: marker.legibleInk(marker.trainColor)
                    }

                    Text {
                        visible: marker.model.speed > 0
                        text: Math.round(marker.model.speed) + " km/h"
                        font.pixelSize: 11   // W1: was 8 px (below the legibility floor)
                        color: Theme.isDark ? "#c9ced6" : "#3a3f47"
                    }

                    // Lateness as text (paired with the ring colour, not colour alone).
                    Text {
                        visible: marker.late
                        text: qsTr("+%1 min").arg(marker.model.delayMinutes)
                        font.pixelSize: 11
                        font.bold: true
                        color: marker.ringColor
                    }
                }
            }
        }

        // Whole marker (dot + number) is clickable.
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: marker.clicked(marker.model.trainNumber, marker.model.departureDate)
        }
    }
}
