import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/// Slide-in panel showing a single train's timetable.
Rectangle {
    id: root

    // Bound to a TrainDetailsService instance from Main.qml.
    required property var details

    color: Qt.rgba(1, 1, 1, 0.97)
    border.color: "#cccccc"
    border.width: 1
    radius: 8

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 8

        // ---- Header ------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: root.details.title
                    font.bold: true
                    font.pixelSize: 17
                }
                Label {
                    text: root.details.subtitle
                    color: "#666666"
                    font.pixelSize: 12
                    visible: text.length > 0
                }
            }
            Label {
                text: qsTr("CANCELLED")
                color: "white"
                padding: 4
                font.pixelSize: 11
                font.bold: true
                background: Rectangle { color: "#c62828"; radius: 4 }
                visible: root.details.cancelled
            }
            ToolButton {
                text: "✕"
                onClicked: root.details.clear()
            }
        }

        Label {
            text: root.details.status
            color: "#5f6671"   // ≥ 4.5:1 on the panel
            font.pixelSize: 11
            visible: text.length > 0
        }

        BusyIndicator {
            running: root.details.loading
            visible: running
            Layout.alignment: Qt.AlignHCenter
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#eeeeee" }

        // ---- Timetable ---------------------------------------------------
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.details.model
            spacing: 2
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                width: ListView.view.width
                height: rowLayout.implicitHeight + 12

                RowLayout {
                    id: rowLayout
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10

                    // Station + track
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            text: model.stationName
                            font.pixelSize: 13
                            font.strikeout: model.cancelled
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: model.track.length > 0 ? qsTr("Track %1").arg(model.track) : ""
                            color: "#5f6671"   // ≥ 4.5:1 on the panel
                            font.pixelSize: 10
                            visible: text.length > 0
                        }
                    }

                    // Arrival / departure times
                    ColumnLayout {
                        spacing: 0
                        Layout.alignment: Qt.AlignRight
                        Label {
                            Layout.alignment: Qt.AlignRight
                            font.pixelSize: 12
                            visible: model.scheduledArrival.length > 0
                            text: model.estimatedArrival.length > 0
                                  ? qsTr("arr %1 → %2").arg(model.scheduledArrival).arg(model.estimatedArrival)
                                  : qsTr("arr %1").arg(model.scheduledArrival)
                        }
                        Label {
                            Layout.alignment: Qt.AlignRight
                            font.pixelSize: 12
                            visible: model.scheduledDeparture.length > 0
                            text: model.estimatedDeparture.length > 0
                                  ? qsTr("dep %1 → %2").arg(model.scheduledDeparture).arg(model.estimatedDeparture)
                                  : qsTr("dep %1").arg(model.scheduledDeparture)
                        }
                    }

                    // Delay badge
                    Label {
                        Layout.alignment: Qt.AlignRight
                        Layout.preferredWidth: 42
                        horizontalAlignment: Text.AlignRight
                        font.pixelSize: 12
                        font.bold: true
                        color: model.delayMinutes > 0 ? "#c62828"
                                                      : (model.delayMinutes < 0 ? "#2e7d32" : "#5f6671")
                        text: model.delayMinutes === 0
                              ? "±0"
                              : (model.delayMinutes > 0 ? "+" : "") + model.delayMinutes
                    }
                }
            }
        }
    }
}
