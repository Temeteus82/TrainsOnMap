import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import TrainsOnMap

/// A single labelled checkbox row, keyboard-focusable with a visible focus ring
/// (same accessibility pattern as InfoPanel's Appearance segments). Used for the
/// map's show/hide toggles (track legend, train-category filters).
Item {
    id: root

    property bool checked: false
    property string label: ""

    signal toggled()

    Layout.fillWidth: true
    implicitHeight: 24

    activeFocusOnTab: true
    Accessible.role: Accessible.CheckBox
    Accessible.name: label
    Accessible.checkable: true
    Accessible.checked: checked

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            root.toggled()
            event.accepted = true
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 8

        Rectangle {
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            radius: 3
            color: root.checked ? Theme.accent : "transparent"
            border.color: root.checked ? Theme.accent : Theme.hairline
            border.width: 1.5

            Text {
                anchors.centerIn: parent
                visible: root.checked
                text: "✓"
                color: Theme.accentText
                font.pixelSize: 11
                font.bold: true
            }
        }

        Label {
            Layout.fillWidth: true
            text: root.label
            font.pointSize: TypeScale.panelBody
            color: Theme.textStrong
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: 6
        color: "transparent"
        border.color: Theme.focusRing
        border.width: 2
        visible: root.activeFocus
    }

    TapHandler { onTapped: root.toggled() }
}
