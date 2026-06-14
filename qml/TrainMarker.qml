import QtQuick
import QtLocation
import QtPositioning

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

    signal clicked(int trainNumber, string departureDate)

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
                    visible: marker.ringColor != "transparent"
                }

                Rectangle {
                    id: dot
                    anchors.centerIn: parent
                    width: 15
                    height: 15
                    radius: 7.5
                    color: marker.dotColor
                    border.color: marker.selected ? "#1565c0" : "#10141a"
                    border.width: marker.selected ? 3 : 1
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

            // Outlined, type-coloured label ("IC 967" / line letter) plus a
            // km/h sub-line while moving — readable on the light base.
            Column {
                anchors.verticalCenter: dotGroup.verticalCenter
                spacing: 0

                Text {
                    text: marker.badgeLabel
                    font.pixelSize: 11
                    font.bold: true
                    color: marker.trainColor
                    style: Text.Outline
                    styleColor: Qt.rgba(1, 1, 1, 0.85)   // light halo for the light base
                }

                Text {
                    visible: marker.model.speed > 0
                    text: Math.round(marker.model.speed) + " km/h"
                    font.pixelSize: 8
                    color: "#3a3f47"
                    style: Text.Outline
                    styleColor: Qt.rgba(1, 1, 1, 0.85)
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
