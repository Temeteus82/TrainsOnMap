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

    /// Current map zoom, so the card can say what is being withheld at this scale
    /// (U2-W9). 0 while the map is still loading, which reads as "not zoomed in"
    /// — the hint is then correct anyway.
    property real mapZoom: 0
    /// The zoom at which train labels and weather chips appear; station dots come
    /// one level later. Kept here next to the hint that names them, and asserted
    /// against Main.qml's thresholds by the hint text itself.
    readonly property real detailZoom: 8.0

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

    /// Collapsed to just the header row. The left column carries this card *and*
    /// the train list, and this one is seven groups tall (U2-W10) — on a short
    /// window that is most of the screen spent on controls you set once.
    property bool collapsed: false

    /// U2-W10, the within-card half: live status and the train filters change or
    /// get used constantly, while the legend, overlays, appearance and attribution
    /// are read once and then compete for the same permanent space. Those four are
    /// folded behind one disclosure row, closed by default — the card-level
    /// collapse above is all-or-nothing and takes the live status with it, which is
    /// the part worth watching.
    property bool settingsCollapsed: true

    radius: 12
    color: Theme.cardBg
    border.color: Theme.hairline
    border.width: 1
    implicitWidth: 268
    // Collapsing drops the Flickable out of `shell` entirely (layouts skip
    // invisible items), so the header height is all that's left — no separate
    // collapsed-height branch needed here.
    implicitHeight: shell.implicitHeight + 32
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

    ColumnLayout {
        id: shell
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // ---- Header (always visible; carries the collapse control) ---------
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
                    size: TypeScale.iconMd
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    text: qsTr("Trains on Map")
                    font.bold: true
                    font.pointSize: TypeScale.subhead
                    color: Theme.textStrong
                }
                Label {
                    text: qsTr("Finland · Digitraffic")
                    font.pointSize: TypeScale.caption
                    color: Theme.textMuted
                }
            }

            CollapseButton {
                collapsed: root.collapsed
                label: qsTr("controls")
                onToggled: root.collapsed = !root.collapsed
            }
        }

    Flickable {
        id: flick
        Layout.fillWidth: true
        Layout.fillHeight: true
        // Carries the natural height so the card sizes to its content when there
        // is room; `fillHeight` lets it shrink and scroll when there isn't.
        Layout.preferredHeight: layout.implicitHeight
        visible: !root.collapsed
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

        // U2-W3: Tab walks the whole column even when the card is height-capped
        // and scrolling, so focus could land on an off-screen row with no visible
        // ring (WCAG 2.2 2.4.11 Focus Not Obscured). One hook here covers every
        // focusable row, present and future, instead of a handler per row.
        readonly property Item focusedItem: root.Window.activeFocusItem
        onFocusedItemChanged: {
            let a = focusedItem
            while (a && a !== layout)
                a = a.parent
            if (!a)
                return   // focus is somewhere else in the window
            const top = layout.mapFromItem(focusedItem, 0, 0).y
            const bottom = top + focusedItem.height
            if (top < flick.contentY)
                flick.contentY = Math.max(0, top - 4)
            else if (bottom > flick.contentY + flick.height)
                flick.contentY = Math.min(flick.contentHeight - flick.height,
                                          bottom - flick.height + 4)
        }

    ColumnLayout {
        id: layout
        // Leave a gutter for the vertical ScrollBar so its thumb doesn't sit on
        // top of edge-to-edge content (fillWidth rows, wrapped attribution text).
        width: flick.width - 12
        spacing: 12

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
                font.pointSize: TypeScale.body
                font.bold: true
                color: Theme.textStrong
            }

            Label {
                text: root.streamConnected ? qsTr("LIVE") : root.streamStatus
                font.pointSize: TypeScale.caption
                font.bold: true
                color: root.streamConnected ? Theme.liveOnText : Theme.textMuted
            }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("%1 track segments").arg(root.trackCount)
            font.pointSize: TypeScale.body
            color: Theme.textMuted
        }

        // Live on-time summary (per broad category), aggregated from the fleet's
        // /live-trains delay data. Empty until the first categories fetch lands.
        Label {
            Layout.fillWidth: true
            text: root.punctuality
            font.pointSize: TypeScale.caption
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            visible: text.length > 0
        }

        Label {
            Layout.fillWidth: true
            text: root.statusText
            color: Theme.textMuted
            font.pointSize: TypeScale.body
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
                font.pointSize: TypeScale.body
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

        // U2-W9: at the opening zoom the map is a scatter of anonymous dots —
        // labels, station dots and weather chips are all above the threshold — with
        // nothing saying so. One line that removes itself once it stops being true.
        Label {
            Layout.fillWidth: true
            visible: root.mapZoom > 0 && root.mapZoom < root.detailZoom
            text: qsTr("Zoom in for train labels, stations and weather.")
            font.pointSize: TypeScale.caption
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline }

        // ---- Train-type filter ---------------------------------------------
        Label {
            Layout.fillWidth: true
            text: qsTr("Show trains")
            wrapMode: Text.Wrap   // U2-W6: expand, don't clip
            font.pointSize: TypeScale.caption
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

        // ---- Legend & settings (set-once; folded behind one row, U2-W10) ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                Layout.fillWidth: true
                text: qsTr("Legend & settings")
                font.pointSize: TypeScale.caption
                font.bold: true
                color: Theme.textMuted
            }
            CollapseButton {
                collapsed: root.settingsCollapsed
                label: qsTr("legend and settings")
                onToggled: root.settingsCollapsed = !root.settingsCollapsed
            }
        }

        // `visible: false` (not clipping) so the whole group leaves the tab chain
        // as well as the view — the mechanism the card-level collapse already
        // proved, reused per group. A layout skips invisible items, so the card
        // shrinks to fit.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 12
            visible: !root.settingsCollapsed

            // ---- Track legend + siding toggle -----------------------------------
            Label {
                Layout.fillWidth: true
                text: qsTr("Track legend")
                wrapMode: Text.Wrap   // U2-W6: expand, don't clip
                font.pointSize: TypeScale.caption
                font.bold: true
                color: Theme.textMuted
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 16

                RowLayout {
                    spacing: 6
                    Rectangle { Layout.preferredWidth: 16; Layout.preferredHeight: 3;
                                radius: 1.5; color: Theme.railColor }
                    Label { text: qsTr("Running line"); font.pointSize: TypeScale.caption; color: Theme.textMuted }
                }
                RowLayout {
                    spacing: 6
                    Rectangle { Layout.preferredWidth: 16; Layout.preferredHeight: 3;
                                radius: 1.5; color: Theme.railSidingColor }
                    Label { text: qsTr("Siding"); font.pointSize: TypeScale.caption; color: Theme.textMuted }
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
                Layout.fillWidth: true
                text: qsTr("Overlays")
                wrapMode: Text.Wrap   // U2-W6: expand, don't clip
                font.pointSize: TypeScale.caption
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
                font.pointSize: TypeScale.caption
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                visible: root.showWeather
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline }

            // ---- Theme toggle (Auto follows the desktop colour scheme) ---------
            Label {
                Layout.fillWidth: true
                text: qsTr("Appearance")
                wrapMode: Text.Wrap   // U2-W6: expand, don't clip
                font.pointSize: TypeScale.caption
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
                                font.pointSize: TypeScale.caption
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
                font.pointSize: TypeScale.caption
                wrapMode: Text.WordWrap
            }
        }
    }
    }
    }
}
