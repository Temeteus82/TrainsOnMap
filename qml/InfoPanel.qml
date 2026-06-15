import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

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

    // Palette
    readonly property color cardBg: Qt.rgba(1, 1, 1, 0.96)
    readonly property color hairline: "#e6e8ec"
    readonly property color textStrong: "#1a1d21"
    readonly property color textMuted: "#6b7280"
    readonly property color accent: "#1565c0"
    readonly property color focusRing: "#1565c0"
    readonly property color liveOn: "#18a957"
    readonly property color liveOff: "#b0b6be"

    radius: 12
    color: cardBg
    border.color: hairline
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
        color: Qt.rgba(0, 0, 0, 0.06)
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
                color: "#eaf1fb"
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
                    color: root.textStrong
                }
                Label {
                    text: qsTr("Finland · Digitraffic")
                    font.pixelSize: 11
                    color: root.textMuted
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.hairline }

        // ---- Live status ---------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Rectangle {
                Layout.preferredWidth: 10
                Layout.preferredHeight: 10
                radius: 5
                color: root.streamConnected ? root.liveOn : root.liveOff

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
                color: root.textStrong
            }

            Label {
                text: root.streamConnected ? qsTr("LIVE") : root.streamStatus
                font.pixelSize: 10
                font.bold: true
                color: root.streamConnected ? root.liveOn : root.textMuted
            }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("%1 track segments").arg(root.trackCount)
            font.pixelSize: 12
            color: root.textMuted
        }

        Label {
            Layout.fillWidth: true
            text: root.statusText
            color: root.textMuted
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
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 8
                    implicitHeight: 32
                    color: refreshBtn.down ? Qt.darker(root.accent, 1.15)
                                           : (refreshBtn.hovered ? Qt.lighter(root.accent, 1.08) : root.accent)

                    // Keyboard focus ring (sits in the card margin around the button).
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -4
                        radius: 12
                        color: "transparent"
                        border.color: root.focusRing
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
                    color: tracksBtn.enabled ? root.accent : root.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 8
                    implicitHeight: 32
                    color: tracksBtn.down ? "#e4e7ec" : (tracksBtn.hovered ? "#f1f3f6" : "transparent")
                    border.color: tracksBtn.visualFocus ? root.focusRing : root.hairline
                    border.width: tracksBtn.visualFocus ? 2 : 1
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.hairline }

        Label {
            Layout.fillWidth: true
            text: qsTr("Data © Fintraffic / Digitraffic (CC BY 4.0)\nMap © OpenStreetMap contributors, © CARTO")
            color: root.textMuted   // ≥ 4.5:1 on the card
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }
    }
}
