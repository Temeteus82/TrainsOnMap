import QtQuick
import QtQuick.Controls
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

    radius: 8
    color: Qt.rgba(1, 1, 1, 0.92)
    border.color: "#cccccc"
    border.width: 1
    implicitWidth: layout.implicitWidth + 24
    implicitHeight: layout.implicitHeight + 24

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Label {
            text: qsTr("🚆 Trains on Map — Finland")
            font.bold: true
            font.pixelSize: 15
        }

        RowLayout {
            spacing: 6
            Rectangle {
                width: 9; height: 9; radius: 4.5
                anchors.verticalCenter: parent.verticalCenter
                color: root.streamConnected ? "#2e7d32" : "#bbbbbb"
            }
            Label {
                text: root.streamConnected
                      ? qsTr("Live trains: %1").arg(root.trainCount)
                      : qsTr("Live trains: %1  (%2)").arg(root.trainCount).arg(root.streamStatus)
            }
        }

        Label {
            text: qsTr("Track segments: %1").arg(root.trackCount)
            color: "#555555"
        }

        Label {
            text: root.statusText
            color: "#555555"
            wrapMode: Text.WordWrap
            Layout.maximumWidth: 260
            visible: text.length > 0
        }

        RowLayout {
            spacing: 8
            Button {
                text: qsTr("Refresh trains")
                onClicked: root.refreshRequested()
            }
            Button {
                text: root.tracksLoading ? qsTr("Loading…") : qsTr("Load tracks in view")
                enabled: !root.tracksLoading
                onClicked: root.loadTracksRequested()
            }
        }

        Label {
            text: qsTr("Data © Fintraffic / Digitraffic (CC BY 4.0)\nMap © OpenStreetMap contributors")
            color: "#888888"
            font.pixelSize: 10
        }
    }
}
