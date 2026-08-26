pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import TrainsOnMap

/// Departure/arrival board for a clicked station. Bound to a StationBoardService.
Rectangle {
    id: root

    required property var service   // StationBoardService

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
                    Layout.fillWidth: true
                    text: root.service.stationName
                    font.bold: true
                    font.pointSize: TypeScale.panelTitle
                    color: Theme.textStrong
                    elide: Text.ElideRight
                }
                Label {
                    text: qsTr("Station board")
                    color: Theme.textMuted
                    font.pointSize: TypeScale.panelCaption
                }
            }
            // Close button — keyboard-focusable (matches the detail panel).
            Rectangle {
                id: closeBtn
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                radius: 6
                color: closeHover.hovered ? Theme.subtleHover : "transparent"
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Close")
                AppIcon {
                    anchors.centerIn: parent
                    name: "close"
                    color: Theme.textMuted
                    size: TypeScale.panelIconSm
                }
                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: "transparent"
                    border.color: Theme.focusRing
                    border.width: 2
                    visible: closeBtn.activeFocus
                }
                HoverHandler { id: closeHover }
                TapHandler { onTapped: root.service.clear() }
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                            || event.key === Qt.Key_Enter) {
                        root.service.clear()
                        event.accepted = true
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: root.service.status
            color: Theme.textMuted
            font.pointSize: TypeScale.panelCaption
            visible: text.length > 0
        }

        BusyIndicator {
            running: root.service.loading
            visible: running
            Layout.alignment: Qt.AlignHCenter
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline }

        // ---- Board -------------------------------------------------------
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.service.board
            spacing: 0
            ScrollBar.vertical: ScrollBar {
                id: vbar
                contentItem: Rectangle {
                    implicitWidth: 6
                    radius: width / 2
                    color: Theme.textMuted
                    opacity: vbar.pressed ? 0.75 : (vbar.hovered ? 0.55 : 0.35)
                    Behavior on opacity { NumberAnimation { duration: 120 } }
                }
            }

            delegate: Item {
                id: boardRow
                required property string trainLabel
                required property string destination
                required property string timeText
                required property string estimateText
                required property string track
                required property int delayMinutes
                required property bool cancelled
                required property bool arriving
                required property string causeText
                width: ListView.view.width
                height: rowLayout.implicitHeight + 12

                RowLayout {
                    id: rowLayout
                    anchors.fill: parent
                    anchors.leftMargin: 2
                    anchors.rightMargin: 12 + (vbar.visible ? vbar.width : 0)
                    spacing: 10

                    // Time (scheduled + live estimate)
                    ColumnLayout {
                        spacing: 0
                        Layout.preferredWidth: 50
                        Label {
                            text: boardRow.timeText
                            font.pointSize: TypeScale.panelBody
                            font.bold: true
                            font.strikeout: boardRow.cancelled
                            color: Theme.textStrong
                        }
                        Label {
                            text: boardRow.estimateText
                            visible: text.length > 0
                            font.pointSize: TypeScale.panelCaption
                            color: boardRow.delayMinutes > 0 ? Theme.delayLate : Theme.delayEarly
                        }
                    }

                    // Train label + destination
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            text: boardRow.trainLabel + (boardRow.arriving ? qsTr("  · arrives") : "")
                            font.pointSize: TypeScale.panelBody
                            font.bold: true
                            color: Theme.textStrong
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: boardRow.destination
                            font.pointSize: TypeScale.panelCaption
                            color: Theme.textMuted
                            elide: Text.ElideRight
                        }
                        // Delay cause (top-level category, e.g. "Onnettomuus"), when known.
                        Label {
                            Layout.fillWidth: true
                            text: boardRow.causeText
                            color: Theme.textMuted
                            font.pointSize: TypeScale.panelCaption
                            font.italic: true
                            // A detailed reason ("Liikenteenohjaus: Yhteyden odotus") does
                            // not fit this column on one line, and the longest ones need
                            // more than two, so wrap without a line cap and let the row grow.
                            wrapMode: Text.Wrap
                            visible: text.length > 0
                        }
                    }

                    // Track + delay
                    ColumnLayout {
                        spacing: 0
                        Layout.alignment: Qt.AlignRight
                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: boardRow.track.length > 0 ? qsTr("Trk %1").arg(boardRow.track) : ""
                            visible: text.length > 0
                            font.pointSize: TypeScale.panelCaption
                            color: Theme.textMuted
                        }
                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: boardRow.cancelled
                                  ? qsTr("Cancelled")
                                  : (boardRow.delayMinutes === 0
                                     ? "±0"
                                     : (boardRow.delayMinutes > 0 ? "+" : "") + boardRow.delayMinutes)
                            font.pointSize: TypeScale.panelCaption
                            font.bold: true
                            color: boardRow.cancelled || boardRow.delayMinutes > 0
                                   ? Theme.delayLate
                                   : (boardRow.delayMinutes < 0 ? Theme.delayEarly : Theme.textMuted)
                        }
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.hairline
                    opacity: 0.5
                }
            }
        }
    }
}
