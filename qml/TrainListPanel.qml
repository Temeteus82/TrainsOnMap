pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import TrainsOnMap

/// Searchable, keyboard-operable list of the live fleet.
///
/// This is the accessible counterpart to the map markers (UI audit U2-C1/U2-O1):
/// selecting a train is the app's primary action, and on the map it is reachable
/// only by pointer — an unlabelled MapQuickItem is nothing at all to a screen
/// reader, and a fleet that turns over every few seconds cannot carry a sane tab
/// order. Here the same action is a focusable list row with a name and a role,
/// and the same data is searchable rather than something you scan the map for.
///
/// Tab reaches the search field, then the list; arrows move the current row,
/// Enter/Space opens it. The row set and ordering come from TrainFilterModel.
Rectangle {
    id: root

    /// A TrainFilterModel wrapping TrainListModel.
    required property var model
    /// Total live trains, for the "N of M" header.
    property int totalCount: 0

    /// Emitted when a row is activated by pointer or keyboard.
    signal trainActivated(int trainNumber, string departureDate, var coordinate)

    /// Collapsed to just the header row. Main.qml drives this card's height off
    /// the flag, falling back to `implicitHeight` below so the map gets the space.
    property bool collapsed: false

    implicitWidth: 268
    // Only consulted while collapsed — expanded, Main.qml sets the height.
    implicitHeight: header.implicitHeight + 24
    color: Theme.cardBg
    border.color: Theme.hairline
    border.width: 1
    radius: 12

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            id: header
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: qsTr("Trains")
                font.bold: true
                font.pointSize: TypeScale.subhead
                color: Theme.textStrong
            }
            Label {
                text: root.model ? qsTr("%1 of %2").arg(root.model.count).arg(root.totalCount)
                                 : ""
                font.pointSize: TypeScale.caption
                color: Theme.textMuted
            }
            CollapseButton {
                collapsed: root.collapsed
                label: qsTr("train list")
                onToggled: root.collapsed = !root.collapsed
            }
        }

        // Search. Matches the number, type or line letter — i.e. what the map
        // badge shows, so the user can type what they just read off a marker.
        TextField {
            id: search
            Layout.fillWidth: true
            visible: !root.collapsed
            placeholderText: qsTr("Find a train — number, type, line")
            font.pointSize: TypeScale.body
            color: Theme.textStrong
            placeholderTextColor: Theme.textMuted
            Accessible.role: Accessible.EditableText
            Accessible.name: qsTr("Find a train")
            background: Rectangle {
                radius: 6
                color: Theme.subtleHover
                border.color: search.activeFocus ? Theme.focusRing : Theme.controlOutline
                border.width: search.activeFocus ? 2 : 1
            }
            onTextChanged: if (root.model) root.model.searchText = text
            // Down from the field steps into the list, so the whole panel is
            // operable without reaching for the mouse.
            Keys.onDownPressed: {
                list.forceActiveFocus()
                if (list.currentIndex < 0 && list.count > 0)
                    list.currentIndex = 0
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Hidden, not just clipped — an invisible item leaves the tab chain,
            // so a collapsed card can't hand focus to a row nobody can see.
            visible: !root.collapsed
            clip: true
            model: root.model
            currentIndex: -1
            activeFocusOnTab: true
            keyNavigationEnabled: true
            // Don't wrap: running off the end of a live list that reorders under
            // you is disorienting, and Home/End still jump.
            keyNavigationWraps: false
            boundsBehavior: Flickable.StopAtBounds

            Accessible.role: Accessible.List
            Accessible.name: qsTr("Live trains")

            ScrollBar.vertical: ScrollBar {
                id: vbar
                contentItem: Rectangle {
                    implicitWidth: 5
                    radius: width / 2
                    color: Theme.textMuted
                    opacity: vbar.pressed ? 0.7 : (vbar.hovered ? 0.5 : 0.3)
                    Behavior on opacity { enabled: !Theme.reducedMotion
                        NumberAnimation { duration: 120 } }
                }
            }

            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                        || event.key === Qt.Key_Space) {
                    if (list.currentItem)
                        list.currentItem.activate()
                    event.accepted = true
                }
            }

            delegate: Item {
                id: row
                required property int index
                required property var model
                width: ListView.view.width
                height: 40

                function activate() {
                    root.trainActivated(row.model.trainNumber, row.model.departureDate,
                                        row.model.coordinate)
                }

                readonly property bool current: row.ListView.isCurrentItem
                readonly property string label:
                    row.model.commuterLine && row.model.commuterLine.length > 0
                        ? row.model.commuterLine
                        : (row.model.trainType && row.model.trainType.length > 0
                           ? row.model.trainType + " " + row.model.trainNumber
                           : "" + row.model.trainNumber)

                // One assistive announcement carrying everything the marker
                // conveys visually: identity, speed, and delay state.
                Accessible.role: Accessible.Button
                Accessible.name: {
                    let s = row.label
                    if (row.model.speed > 0)
                        s += ", " + Math.round(row.model.speed) + " km/h"
                    else
                        s += ", " + qsTr("stopped")
                    if (row.model.delayMinutes > 0)
                        s += ", " + qsTr("%1 minutes late").arg(row.model.delayMinutes)
                    return s
                }
                Accessible.onPressAction: row.activate()

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: 6
                    color: row.current ? Theme.subtleHover
                                       : (hover.hovered ? Theme.subtleHover : "transparent")
                    border.color: row.current && list.activeFocus ? Theme.focusRing : "transparent"
                    border.width: 2
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8

                    // Type colour, so the list and the map read as one dataset.
                    Rectangle {
                        Layout.preferredWidth: 10
                        Layout.preferredHeight: 10
                        radius: 5
                        color: Theme.trainColorFor(row.model.trainType, row.model.category,
                                                   row.model.speed)
                    }

                    Label {
                        Layout.fillWidth: true
                        text: row.label
                        elide: Text.ElideRight
                        font.pointSize: TypeScale.body
                        color: Theme.textStrong
                    }

                    // Delay pairs colour with text, never colour alone.
                    Label {
                        visible: row.model.delayMinutes > 0
                        text: "+" + row.model.delayMinutes
                        font.pointSize: TypeScale.caption
                        color: row.model.delayMinutes >= 15 ? Theme.ringVeryLate : Theme.ringLate
                    }

                    Label {
                        text: row.model.speed > 0 ? Math.round(row.model.speed) + " km/h" : "—"
                        font.pointSize: TypeScale.caption
                        color: Theme.textMuted
                    }
                }

                HoverHandler { id: hover }
                TapHandler {
                    onTapped: {
                        list.currentIndex = row.index
                        row.activate()
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: list.count === 0
                text: root.totalCount === 0
                      ? qsTr("Waiting for live train data…")
                      : qsTr("No trains match this filter.")
                color: Theme.textMuted
                font.pointSize: TypeScale.caption
            }
        }
    }
}
