import QtQuick
import QtLocation
import QtPositioning

/// Delegate for a single train inside a MapItemView. Model roles come from
/// TrainListModel: coordinate, trainNumber, speed, bearing, trainType, category.
///
/// Colours follow juliadata.fi (verified against the live map): long-distance
/// is split by train type (S = green, IC = red, PYO = navy); commuter = green,
/// cargo/freight = navy, everything else (locomotive / shunting / unknown) = grey.
MapQuickItem {
    id: marker

    required property var model
    property bool selected: false

    signal clicked(int trainNumber, string departureDate)

    function colorFor(type, category) {
        switch (type) {            // long-distance types get their own colour
        case "S":   return "#33B24A";   // Pendolino — green
        case "IC":  return "#E0312A";   // InterCity — red
        case "PYO": return "#2f4cc8";   // night train — navy (brightened for dark base)
        }
        switch (category) {        // fall back to the broad class
        case "Commuter":      return "#33B24A";   // green
        case "Long-distance": return "#E0312A";   // other long-distance ~ red
        case "Cargo":         return "#2f4cc8";   // freight — navy
        }
        return "#9E9E9E";          // locomotive / shunting / unknown
    }

    readonly property color trainColor: colorFor(model.trainType, model.category)

    coordinate: model.coordinate
    // Anchor the coordinate at the centre of the dot (the label floats right).
    anchorPoint.x: dot.width / 2
    anchorPoint.y: dot.height / 2

    sourceItem: Item {
        width: row.width
        height: row.height

        Row {
            id: row
            spacing: 3

            Rectangle {
                id: dot
                width: 15
                height: 15
                radius: 7.5
                color: marker.trainColor
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

            // Outlined, category-coloured number — readable on dark or light.
            Text {
                anchors.verticalCenter: dot.verticalCenter
                text: marker.model.trainNumber
                font.pixelSize: 11
                font.bold: true
                color: marker.trainColor
                style: Text.Outline
                styleColor: Qt.rgba(1, 1, 1, 0.85)   // light halo for the light base
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
