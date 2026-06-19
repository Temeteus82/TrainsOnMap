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

    // Tier-2 map-match diagnostics for this train: { offset, tunniste, onRoute }.
    // Defaulted so the panel works if the host doesn't supply it.
    property var matchInfo: ({})

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
                    font.pixelSize: TypeScale.title
                    color: Theme.textStrong
                }
                Label {
                    text: root.details.subtitle
                    color: Theme.textMuted
                    font.pixelSize: TypeScale.body
                    visible: text.length > 0
                }
            }
            Label {
                text: qsTr("CANCELLED")
                color: "white"
                padding: 4
                font.pixelSize: TypeScale.caption
                font.bold: true
                background: Rectangle { color: Theme.cancelledBg; radius: 4 }
                visible: root.details.cancelled
            }
            // Close button — keyboard-focusable (W4) with a drawn icon (O3).
            Rectangle {
                id: closeBtn
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                radius: 6
                color: closeHover.hovered ? Theme.subtleHover : "transparent"
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Close")
                AppIcon {
                    anchors.centerIn: parent
                    name: "close"
                    color: Theme.textMuted
                    size: TypeScale.iconSm
                }
                // Keyboard focus ring.
                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: "transparent"
                    border.color: Theme.focusRing
                    border.width: 2
                    visible: closeBtn.activeFocus
                }
                HoverHandler { id: closeHover }
                TapHandler { onTapped: root.details.clear() }
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                            || event.key === Qt.Key_Enter) {
                        root.details.clear()
                        event.accepted = true
                    }
                }
            }
        }

        Label {
            text: root.details.status
            color: Theme.textMuted
            font.pixelSize: TypeScale.caption
            visible: text.length > 0
        }

        // Tier-2 map-match diagnostics: which track the live fix snapped to, how
        // far the raw fix was, and whether it matched the scheduled route.
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textMuted
            font.pixelSize: TypeScale.caption
            visible: root.matchInfo && root.matchInfo.offset !== undefined
                     && root.matchInfo.offset >= 0
            text: {
                if (!root.matchInfo || root.matchInfo.offset === undefined)
                    return ""
                var s = root.matchInfo.onRoute ? qsTr("on route") : qsTr("nearest track")
                s += " · " + qsTr("%1 m off").arg(Math.round(root.matchInfo.offset))
                if (root.matchInfo.tunniste && root.matchInfo.tunniste.length > 0)
                    s += " · " + root.matchInfo.tunniste
                return s
            }
        }

        BusyIndicator {
            running: root.details.loading
            visible: running
            Layout.alignment: Qt.AlignHCenter
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline }

        // ---- Show-all toggle (reveals passed-through timing points) -------
        // Keyboard-focusable checkbox (W4) with a drawn tick (O3).
        Item {
            id: allToggle
            Layout.fillWidth: true
            implicitHeight: 24
            property bool checked: false

            activeFocusOnTab: true
            Accessible.role: Accessible.CheckBox
            Accessible.name: qsTr("Show all timing points")
            Accessible.checkable: true
            Accessible.checked: checked
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                        || event.key === Qt.Key_Enter) {
                    allToggle.checked = !allToggle.checked
                    event.accepted = true
                }
            }

            RowLayout {
                anchors.fill: parent
                spacing: 7

                Rectangle {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    radius: 4
                    color: allToggle.checked ? Theme.accent : "transparent"
                    border.color: allToggle.activeFocus ? Theme.focusRing
                                  : (allToggle.checked ? Theme.accent : Theme.hairline)
                    border.width: allToggle.activeFocus ? 2 : 1.5
                    AppIcon {
                        anchors.centerIn: parent
                        name: "check"
                        color: Theme.accentText
                        size: 12
                        visible: allToggle.checked
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Show all timing points")
                    font.pixelSize: TypeScale.body
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
            ScrollBar.vertical: ScrollBar { id: vbar }

            delegate: ItemDelegate {
                id: stopRow
                width: ListView.view.width
                clip: true

                // The native Controls style paints ItemDelegate's background from
                // the light system palette (white), which hid the theme-coloured
                // (near-white) row text in dark mode. Drive it from the app theme
                // instead so the dark card shows through and text stays legible.
                background: Rectangle {
                    color: stopRow.hovered ? Theme.subtleHover : "transparent"
                }

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
                    // Reserve the scrollbar's width on the right so the (rightmost)
                    // delay-badge column isn't hidden under the overlaid ScrollBar.
                    anchors.rightMargin: 12 + (vbar.visible ? vbar.width : 0)
                    spacing: 10

                    // Station + track / "passing"
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            text: stopRow.stationName
                            font.pixelSize: TypeScale.body
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
                            font.pixelSize: TypeScale.caption
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
                            font.pixelSize: TypeScale.body
                            color: Theme.textStrong
                            visible: stopRow.scheduledArrival.length > 0
                            text: stopRow.estimatedArrival.length > 0
                                  ? qsTr("arr %1 → %2").arg(stopRow.scheduledArrival).arg(stopRow.estimatedArrival)
                                  : qsTr("arr %1").arg(stopRow.scheduledArrival)
                        }
                        Label {
                            Layout.alignment: Qt.AlignRight
                            font.pixelSize: TypeScale.body
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
                        font.pixelSize: TypeScale.body
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
                        font.pixelSize: TypeScale.body
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
