import QtQuick

import TrainsOnMap

/// Disclosure caret for the left-column cards. Same keyboard/assistive pattern as
/// `ToggleRow` and the Appearance segments: Tab to reach, Space/Enter to operate,
/// a visible focus ring, and a name that says what the press will do.
///
/// The caret points down when collapsed (press to open) and up when expanded,
/// which is the accordion convention every desktop user already has.
Item {
    id: root

    property bool collapsed: false
    /// Names the thing being collapsed, for the assistive announcement.
    property string label: ""

    signal toggled()

    implicitWidth: 24
    implicitHeight: 24

    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: collapsed ? qsTr("Expand %1").arg(label)
                               : qsTr("Collapse %1").arg(label)
    Accessible.onPressAction: root.toggled()

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            root.toggled()
            event.accepted = true
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: hover.hovered ? Theme.subtleHover : "transparent"
    }

    AppIcon {
        anchors.centerIn: parent
        name: "chevron"
        color: Theme.textMuted
        size: TypeScale.iconSm
        rotation: root.collapsed ? 0 : 180
        Behavior on rotation {
            enabled: !Theme.reducedMotion
            NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
        }
    }

    // Keyboard focus ring.
    Rectangle {
        anchors.fill: parent
        radius: 6
        color: "transparent"
        border.color: Theme.focusRing
        border.width: 2
        visible: root.activeFocus
    }

    HoverHandler { id: hover }
    TapHandler { onTapped: root.toggled() }
}
