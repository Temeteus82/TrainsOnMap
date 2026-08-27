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
    property bool refreshing: false
    property bool streamConnected: false
    property string streamStatus: ""
    property string punctuality: ""

    // Train-category filter (marker visibility); an unrecognised/empty category
    // (metadata not loaded yet) is always shown, so trains never vanish at startup.
    property bool showCommuter: true
    property bool showLongDistance: true
    property bool showCargo: true
    function categoryVisible(category) {
        if (category === "Commuter") return root.showCommuter
        if (category === "Long-distance") return root.showLongDistance
        if (category === "Cargo") return root.showCargo
        return true
    }

    // Track-category toggle: sidings/yards (non-main tracks) can be hidden to
    // declutter the map; running lines always show.
    property bool showSidings: true

    // Optional weather overlay (off by default): FMI open-data observations.
    property bool showWeather: false

    signal refreshRequested()

    radius: 12
    color: Theme.cardBg
    border.color: Theme.hairline
    border.width: 1
    implicitWidth: 268
    implicitHeight: layout.implicitHeight + 32
    // Don't run off the bottom on a short window: cap to the space below the
    // top margin (y) and let the content scroll (flick) when it doesn't fit.
    height: parent ? Math.min(implicitHeight, parent.height - y - 12) : implicitHeight

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

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: 16
        contentWidth: width
        contentHeight: layout.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        // Only scrolls when the card is height-capped on a short window.
        ScrollBar.vertical: ScrollBar {
            id: vbar
            contentItem: Rectangle {
                implicitWidth: 5
                radius: width / 2
                color: Theme.textMuted
                opacity: vbar.pressed ? 0.7 : (vbar.hovered ? 0.5 : 0.3)
                // U2-O5: gated like every other animation in the app.
                Behavior on opacity { enabled: !Theme.reducedMotion
                    NumberAnimation { duration: 120 } }
            }
        }

    ColumnLayout {
        id: layout
        // Leave a gutter for the vertical ScrollBar so its thumb doesn't sit on
        // top of edge-to-edge content (fillWidth rows, wrapped attribution text).
        width: flick.width - 12
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
                AppIcon {
                    anchors.centerIn: parent
                    name: "train"
                    color: Theme.accent
                    size: TypeScale.panelIconMd
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    text: qsTr("Trains on Map")
                    font.bold: true
                    font.pointSize: TypeScale.panelSubhead
                    color: Theme.textStrong
                }
                Label {
                    text: qsTr("Finland · Digitraffic")
                    font.pointSize: TypeScale.panelCaption
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
                id: liveDot
                Layout.preferredWidth: 10
                Layout.preferredHeight: 10
                radius: 5
                color: root.streamConnected ? Theme.liveOn : Theme.liveOff

                // W3: the pulse is non-essential motion — skip it when the user
                // has opted into reduced motion (and keep the dot fully opaque).
                SequentialAnimation on opacity {
                    id: livePulse
                    running: root.streamConnected && !Theme.reducedMotion
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.35; duration: 900; easing.type: Easing.InOutQuad }
                    NumberAnimation { to: 1.0;  duration: 900; easing.type: Easing.InOutQuad }
                }
                Binding {
                    target: liveDot; property: "opacity"; value: 1.0
                    when: !livePulse.running
                }
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("%1 live trains").arg(root.trainCount)
                font.pointSize: TypeScale.panelBody
                font.bold: true
                color: Theme.textStrong
            }

            Label {
                text: root.streamConnected ? qsTr("LIVE") : root.streamStatus
                font.pointSize: TypeScale.panelCaption
                font.bold: true
                color: root.streamConnected ? Theme.liveOnText : Theme.textMuted
            }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("%1 track segments").arg(root.trackCount)
            font.pointSize: TypeScale.panelBody
            color: Theme.textMuted
        }

        // Live on-time summary (per broad category), aggregated from the fleet's
        // /live-trains delay data. Empty until the first categories fetch lands.
        Label {
            Layout.fillWidth: true
            text: root.punctuality
            font.pointSize: TypeScale.panelCaption
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            visible: text.length > 0
        }

        Label {
            Layout.fillWidth: true
            text: root.statusText
            color: Theme.textMuted
            font.pointSize: TypeScale.panelBody
            wrapMode: Text.WordWrap
            visible: text.length > 0
        }

        // ---- Actions -------------------------------------------------------
        Button {
            id: refreshBtn
            Layout.fillWidth: true
            focusPolicy: Qt.StrongFocus
            // Doherty Threshold: a network round-trip can take well over 400 ms,
            // so say so instead of leaving the button looking inert.
            enabled: !root.refreshing
            text: root.refreshing ? qsTr("Refreshing…") : qsTr("Refresh")
            onClicked: root.refreshRequested()
            contentItem: Label {
                text: refreshBtn.text
                font.pointSize: TypeScale.panelBody
                font.bold: true
                color: Theme.accentText
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: 8
                implicitHeight: 44   // W-guide: 44 px desktop touch target (was 32)
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

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline }

        // ---- Train-type filter ---------------------------------------------
        Label {
            text: qsTr("Show trains")
            font.pointSize: TypeScale.panelCaption
            font.bold: true
            color: Theme.textMuted
        }

        ToggleRow {
            label: qsTr("Commuter")
            checked: root.showCommuter
            onToggled: root.showCommuter = !root.showCommuter
        }
        ToggleRow {
            label: qsTr("Long-distance")
            checked: root.showLongDistance
            onToggled: root.showLongDistance = !root.showLongDistance
        }
        ToggleRow {
            label: qsTr("Cargo")
            checked: root.showCargo
            onToggled: root.showCargo = !root.showCargo
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline }

        // ---- Track legend + siding toggle -----------------------------------
        Label {
            text: qsTr("Track legend")
            font.pointSize: TypeScale.panelCaption
            font.bold: true
            color: Theme.textMuted
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            RowLayout {
                spacing: 6
                Rectangle { width: 16; height: 3; radius: 1.5; color: Theme.railColor }
                Label { text: qsTr("Running line"); font.pointSize: TypeScale.panelCaption; color: Theme.textMuted }
            }
            RowLayout {
                spacing: 6
                Rectangle { width: 16; height: 3; radius: 1.5; color: Theme.railSidingColor }
                Label { text: qsTr("Siding"); font.pointSize: TypeScale.panelCaption; color: Theme.textMuted }
            }
        }

        ToggleRow {
            label: qsTr("Show sidings")
            checked: root.showSidings
            onToggled: root.showSidings = !root.showSidings
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline }

        // ---- Overlays -------------------------------------------------------
        Label {
            text: qsTr("Overlays")
            font.pointSize: TypeScale.panelCaption
            font.bold: true
            color: Theme.textMuted
        }

        ToggleRow {
            label: qsTr("Weather")
            checked: root.showWeather
            onToggled: root.showWeather = !root.showWeather
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("FMI weather stations (air °C).")
            font.pointSize: TypeScale.panelCaption
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            visible: root.showWeather
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline }

        // ---- Theme toggle (Auto follows the desktop colour scheme) ---------
        Label {
            text: qsTr("Appearance")
            font.pointSize: TypeScale.panelCaption
            font.bold: true
            color: Theme.textMuted
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32   // toward the 44 px desktop hit-target guideline
            radius: 8
            color: Theme.subtleHover
            border.color: Theme.controlOutline
            border.width: 1

            Row {
                anchors.fill: parent
                anchors.margins: 3

                Repeater {
                    model: 3

                    // W4: each segment is keyboard-focusable and operable (Tab to
                    // reach, Space/Enter to select) with a visible focus ring, and
                    // exposes itself to assistive tech as a radio button.
                    delegate: Item {
                        id: seg
                        required property int index
                        readonly property string mode: ["auto", "light", "dark"][index]
                        readonly property string label: [qsTr("Auto"), qsTr("Light"), qsTr("Dark")][index]
                        readonly property bool active: Theme.mode === mode
                        width: parent.width / 3
                        height: parent.height

                        activeFocusOnTab: true
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: label
                        Accessible.checkable: true
                        Accessible.checked: active
                        Keys.onPressed: (event) => {
                            if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                                    || event.key === Qt.Key_Enter) {
                                Theme.mode = seg.mode
                                event.accepted = true
                            }
                        }

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 1
                            radius: 6
                            color: seg.active ? Theme.accent : "transparent"
                        }
                        // Keyboard focus ring.
                        Rectangle {
                            anchors.fill: parent
                            radius: 6
                            color: "transparent"
                            border.color: Theme.focusRing
                            border.width: 2
                            visible: seg.activeFocus
                        }
                        Label {
                            anchors.centerIn: parent
                            text: seg.label
                            font.pointSize: TypeScale.panelCaption
                            font.bold: seg.active
                            color: seg.active ? Theme.accentText : Theme.textMuted
                        }
                        TapHandler { onTapped: Theme.mode = seg.mode }
                    }
                }
            }
        }

        // U2-W5: the reduced-motion flag gates six animations and persists via
        // QSettings, but had no control — turning it on meant editing the
        // registry by hand. Qt has no prefers-reduced-motion to inherit, so the
        // project-level setting has to be exposed here.
        ToggleRow {
            label: qsTr("Reduce motion")
            checked: Theme.reducedMotion
            onToggled: Theme.reducedMotion = !Theme.reducedMotion
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline }

        Label {
            Layout.fillWidth: true
            text: qsTr("Data © Fintraffic / Digitraffic (CC BY 4.0)\nMap © Esri, HERE, Garmin, © OpenStreetMap contributors")
            color: Theme.textMuted   // ≥ 4.5:1 on the card
            font.pointSize: TypeScale.panelCaption
            wrapMode: Text.WordWrap
        }
    }
    }
}
