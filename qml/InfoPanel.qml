import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import TrainsOnMap

/// Floating status/control card overlaid on the map.
Rectangle {
    id: root

    property string statusText: ""
    property int trainCount: 0
    property int trackCount: 0
    property bool tracksLoading: false
    property bool streamConnected: false
    property string streamStatus: ""

    signal refreshRequested()
    signal loadTracksRequested()

    radius: 12
    color: Theme.cardBg
    border.color: Theme.hairline
    border.width: 1
    implicitWidth: 268
    implicitHeight: layout.implicitHeight + 32

    // Soft drop shadow for a floating-card feel (drawn behind the card).
    Rectangle {
        z: -1
        anchors.fill: parent
        anchors.topMargin: 2
        anchors.leftMargin: 1
        anchors.rightMargin: -1
        radius: root.radius
        color: Theme.shadow
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // ---- Header --------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                radius: 8
                color: Theme.iconBadgeBg
                Label {
                    anchors.centerIn: parent
                    text: "🚆"
                    font.pixelSize: 16
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    text: qsTr("Trains on Map")
                    font.bold: true
                    font.pixelSize: 15
                    color: Theme.textStrong
                }
                Label {
                    text: qsTr("Finland · Digitraffic")
                    font.pixelSize: 11
                    color: Theme.textMuted
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline }

        // ---- Live status ---------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Rectangle {
                Layout.preferredWidth: 10
                Layout.preferredHeight: 10
                radius: 5
                color: root.streamConnected ? Theme.liveOn : Theme.liveOff

                SequentialAnimation on opacity {
                    running: root.streamConnected
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.35; duration: 900; easing.type: Easing.InOutQuad }
                    NumberAnimation { to: 1.0;  duration: 900; easing.type: Easing.InOutQuad }
                }
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("%1 live trains").arg(root.trainCount)
                font.pixelSize: 13
                font.bold: true
                color: Theme.textStrong
            }

            Label {
                text: root.streamConnected ? qsTr("LIVE") : root.streamStatus
                font.pixelSize: 10
                font.bold: true
                color: root.streamConnected ? Theme.liveOn : Theme.textMuted
            }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("%1 track segments").arg(root.trackCount)
            font.pixelSize: 12
            color: Theme.textMuted
        }

        Label {
            Layout.fillWidth: true
            text: root.statusText
            color: Theme.textMuted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            visible: text.length > 0
        }

        // ---- Actions -------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                id: refreshBtn
                Layout.fillWidth: true
                focusPolicy: Qt.StrongFocus
                text: qsTr("Refresh")
                onClicked: root.refreshRequested()
                contentItem: Label {
                    text: refreshBtn.text
                    font.pixelSize: 12
                    font.bold: true
                    color: Theme.onAccent
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 8
                    implicitHeight: 32
                    color: refreshBtn.down ? Qt.darker(Theme.accent, 1.15)
                                           : (refreshBtn.hovered ? Qt.lighter(Theme.accent, 1.08) : Theme.accent)

                    // Keyboard focus ring (sits in the card margin around the button).
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -4
                        radius: 12
                        color: "transparent"
                        border.color: Theme.focusRing
                        border.width: 2
                        visible: refreshBtn.visualFocus
                    }
                }
            }

            Button {
                id: tracksBtn
                Layout.fillWidth: true
                focusPolicy: Qt.StrongFocus
                enabled: !root.tracksLoading
                text: root.tracksLoading ? qsTr("Loading…") : qsTr("Load tracks")
                onClicked: root.loadTracksRequested()
                contentItem: Label {
                    text: tracksBtn.text
                    font.pixelSize: 12
                    font.bold: true
                    color: tracksBtn.enabled ? Theme.accent : Theme.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 8
                    implicitHeight: 32
                    color: tracksBtn.down ? Theme.subtlePress : (tracksBtn.hovered ? Theme.subtleHover : "transparent")
                    border.color: tracksBtn.visualFocus ? Theme.focusRing : Theme.hairline
                    border.width: tracksBtn.visualFocus ? 2 : 1
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline }

        // ---- Theme toggle (Auto follows the desktop colour scheme) ---------
        Label {
            text: qsTr("Appearance")
            font.pixelSize: 10
            font.bold: true
            color: Theme.textMuted
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            radius: 8
            color: Theme.subtleHover
            border.color: Theme.hairline
            border.width: 1

            Row {
                anchors.fill: parent
                anchors.margins: 3

                Repeater {
                    model: 3

                    delegate: Item {
                        id: seg
                        required property int index
                        readonly property string mode: ["auto", "light", "dark"][index]
                        readonly property string label: [qsTr("Auto"), qsTr("Light"), qsTr("Dark")][index]
                        readonly property bool active: Theme.mode === mode
                        width: parent.width / 3
                        height: parent.height

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 1
                            radius: 6
                            color: seg.active ? Theme.accent : "transparent"
                        }
                        Label {
                            anchors.centerIn: parent
                            text: seg.label
                            font.pixelSize: 11
                            font.bold: seg.active
                            color: seg.active ? Theme.onAccent : Theme.textMuted
                        }
                        TapHandler { onTapped: Theme.mode = seg.mode }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline }

        Label {
            Layout.fillWidth: true
            text: qsTr("Data © Fintraffic / Digitraffic (CC BY 4.0)\nMap © OpenStreetMap contributors, © CARTO")
            color: Theme.textMuted   // ≥ 4.5:1 on the card
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }
    }
}
