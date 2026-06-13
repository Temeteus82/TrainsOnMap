import QtQuick
import QtLocation
import QtPositioning

/// Delegate for a single train inside a MapItemView. Model roles come from
/// TrainListModel: coordinate, trainNumber, speed, bearing.
MapQuickItem {
    id: marker

    required property var model
    property bool selected: false

    signal clicked(int trainNumber, string departureDate)

    coordinate: model.coordinate
    // Anchor the coordinate at the centre of the dot (the pill floats to its right).
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
                color: marker.model.speed > 1 ? "#d32f2f" : "#757575"   // moving vs stopped
                border.color: marker.selected ? "#1565c0" : "white"
                border.width: marker.selected ? 3 : 2
                rotation: marker.model.bearing      // only the dot/notch rotate to heading

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

            // Always-upright, high-contrast number label.
            Rectangle {
                id: pill
                anchors.verticalCenter: dot.verticalCenter
                radius: 3
                color: marker.selected ? "#1565c0" : Qt.rgba(1, 1, 1, 0.92)
                border.color: marker.selected ? "#0d3c75" : "#888888"
                border.width: 1
                width: numberLabel.implicitWidth + 8
                height: numberLabel.implicitHeight + 4

                Text {
                    id: numberLabel
                    anchors.centerIn: parent
                    text: marker.model.trainNumber
                    font.pixelSize: 11
                    font.bold: true
                    color: marker.selected ? "white" : "#1a1a1a"
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
