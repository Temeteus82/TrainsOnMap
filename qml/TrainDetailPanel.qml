pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import TrainsOnMap

/// Slide-in panel showing a single train's timetable.
Rectangle {
    id: root

    // Bound to a TrainDetailsService instance from Main.qml.
    required property var details

    color: Theme.cardBg
    border.color: Theme.hairline
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
                    color: Theme.textStrong
                }
                Label {
                    text: root.details.subtitle
                    color: Theme.textMuted
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
                background: Rectangle { color: Theme.cancelledBg; radius: 4 }
                visible: root.details.cancelled
            }
            Rectangle {
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                radius: 6
                color: closeHover.hovered ? Theme.subtleHover : "transparent"
                Label {
                    anchors.centerIn: parent
                    text: "✕"
                    font.pixelSize: 14
                    color: Theme.textMuted
                }
                HoverHandler { id: closeHover }
                TapHandler { onTapped: root.details.clear() }
            }
        }

        Label {
            text: root.details.status
            color: Theme.textMuted
            font.pixelSize: 11
            visible: text.length > 0
        }

        BusyIndicator {
            running: root.details.loading
            visible: running
            Layout.alignment: Qt.AlignHCenter
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline }

        // ---- Show-all toggle (reveals passed-through timing points) -------
        Item {
            id: allToggle
            Layout.fillWidth: true
            implicitHeight: 24
            property bool checked: false

            RowLayout {
                anchors.fill: parent
                spacing: 7

                Rectangle {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    radius: 4
                    color: allToggle.checked ? Theme.accent : "transparent"
                    border.color: allToggle.checked ? Theme.accent : Theme.hairline
                    border.width: 1.5
                    Label {
                        anchors.centerIn: parent
                        text: "✓"
                        font.pixelSize: 11
                        font.bold: true
                        color: Theme.accentText
                        visible: allToggle.checked
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Show all timing points")
                    font.pixelSize: 12
                    color: Theme.textStrong
                }
            }
            TapHandler { onTapped: allToggle.checked = !allToggle.checked }
        }

        // ---- Timetable ---------------------------------------------------
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.details.model
            spacing: 0
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: stopRow
                width: ListView.view.width
                clip: true

                required property string stationName
                required property bool cancelled
                required property string track
                required property string scheduledArrival
                required property string estimatedArrival
                required property string scheduledDeparture
                required property string estimatedDeparture
                required property int delayMinutes
                required property bool stopping

                // Passed-through points are hidden until the user opts in.
                visible: stopping || allToggle.checked
                height: visible ? rowLayout.implicitHeight + 12 : 0

                // One compact time for a passing point (departure preferred).
                readonly property string passTime: scheduledDeparture.length > 0 ? scheduledDeparture
                                                                                 : scheduledArrival
                readonly property string passEst: estimatedDeparture.length > 0 ? estimatedDeparture
                                                                                : estimatedArrival

                RowLayout {
                    id: rowLayout
                    anchors.fill: parent
                    anchors.leftMargin: stopRow.stopping ? 6 : 18   // indent passed points
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10

                    // Station + track / "passing"
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            text: stopRow.stationName
                            font.pixelSize: stopRow.stopping ? 13 : 12
                            font.strikeout: stopRow.cancelled
                            color: stopRow.stopping ? Theme.textStrong : Theme.textMuted
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: stopRow.stopping
                                  ? (stopRow.track.length > 0 ? qsTr("Track %1").arg(stopRow.track) : "")
                                  : qsTr("passing")
                            color: Theme.textMuted
                            font.pixelSize: 10
                            font.italic: !stopRow.stopping
                            visible: text.length > 0
                        }
                    }

                    // Arrival / departure times (booked stops)
                    ColumnLayout {
                        spacing: 0
                        Layout.alignment: Qt.AlignRight
                        visible: stopRow.stopping
                        Label {
                            Layout.alignment: Qt.AlignRight
                            font.pixelSize: 12
                            color: Theme.textStrong
                            visible: stopRow.scheduledArrival.length > 0
                            text: stopRow.estimatedArrival.length > 0
                                  ? qsTr("arr %1 → %2").arg(stopRow.scheduledArrival).arg(stopRow.estimatedArrival)
                                  : qsTr("arr %1").arg(stopRow.scheduledArrival)
                        }
                        Label {
                            Layout.alignment: Qt.AlignRight
                            font.pixelSize: 12
                            color: Theme.textStrong
                            visible: stopRow.scheduledDeparture.length > 0
                            text: stopRow.estimatedDeparture.length > 0
                                  ? qsTr("dep %1 → %2").arg(stopRow.scheduledDeparture).arg(stopRow.estimatedDeparture)
                                  : qsTr("dep %1").arg(stopRow.scheduledDeparture)
                        }
                    }

                    // Single pass-through time
                    Label {
                        Layout.alignment: Qt.AlignRight
                        visible: !stopRow.stopping && stopRow.passTime.length > 0
                        font.pixelSize: 12
                        color: Theme.textMuted
                        text: stopRow.passEst.length > 0
                              ? qsTr("%1 → %2").arg(stopRow.passTime).arg(stopRow.passEst)
                              : stopRow.passTime
                    }

                    // Delay badge (booked stops only)
                    Label {
                        Layout.alignment: Qt.AlignRight
                        Layout.preferredWidth: 42
                        horizontalAlignment: Text.AlignRight
                        font.pixelSize: 12
                        font.bold: true
                        visible: stopRow.stopping
                        color: stopRow.delayMinutes > 0 ? Theme.delayLate
                                                        : (stopRow.delayMinutes < 0 ? Theme.delayEarly : Theme.textMuted)
                        text: stopRow.delayMinutes === 0
                              ? "±0"
                              : (stopRow.delayMinutes > 0 ? "+" : "") + stopRow.delayMinutes
                    }
                }
            }
        }
    }
}
