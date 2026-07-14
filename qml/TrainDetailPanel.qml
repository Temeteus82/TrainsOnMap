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

    // Guards the one-shot auto-scroll to the current position: we re-focus the
    // timetable when a *new* train's stops load, but not on every live update.
    property int autoScrolledTrain: -1

    // Distinct dot colour per carriage amenity (see CompositionVehicle.amenities).
    function amenityColor(a) {
        switch (a) {
        case "Catering":   return "#e08a3c"   // café/restaurant car
        case "Accessible": return Theme.accent
        case "Family":     return "#3fae6b"   // play area
        case "Pet":        return "#caa23a"
        }
        return Theme.textMuted
    }

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
                    font.pointSize: TypeScale.panelTitle
                    color: Theme.textStrong
                }
                Label {
                    text: root.details.subtitle
                    color: Theme.textMuted
                    font.pointSize: TypeScale.panelBody
                    visible: text.length > 0
                }
            }
            Label {
                text: qsTr("CANCELLED")
                color: "white"
                padding: 4
                font.pointSize: TypeScale.panelCaption
                font.bold: true
                background: Rectangle { color: Theme.cancelledBg; radius: 4 }
                visible: root.details.cancelled
            }
            // Close button — keyboard-focusable (W4) with a drawn icon (O3).
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
            font.pointSize: TypeScale.panelCaption
            visible: text.length > 0
        }

        // Tier-2 map-match diagnostics: which track the live fix snapped to, how
        // far the raw fix was, whether it matched the scheduled route, and the
        // reported GPS accuracy (#1 — surfaces the quality the marker flags).
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textMuted
            font.pointSize: TypeScale.panelCaption
            visible: root.matchInfo
                     && ((root.matchInfo.offset !== undefined && root.matchInfo.offset >= 0)
                         || (root.matchInfo.accuracy !== undefined && root.matchInfo.accuracy >= 0))
            text: {
                if (!root.matchInfo)
                    return ""
                var parts = []
                if (root.matchInfo.offset !== undefined && root.matchInfo.offset >= 0) {
                    var where = root.matchInfo.onRoute ? qsTr("on route") : qsTr("nearest track")
                    parts.push(where + " · " + qsTr("%1 m off").arg(Math.round(root.matchInfo.offset)))
                }
                if (root.matchInfo.accuracy !== undefined && root.matchInfo.accuracy >= 0)
                    parts.push(qsTr("±%1 m GPS").arg(root.matchInfo.accuracy))
                if (root.matchInfo.tunniste && root.matchInfo.tunniste.length > 0)
                    parts.push(root.matchInfo.tunniste)
                return parts.join(" · ")
            }
        }

        BusyIndicator {
            running: root.details.loading
            visible: running
            Layout.alignment: Qt.AlignHCenter
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline }

        // ---- Carriage order ----------------------------------------------
        // Locomotive + wagons in physical order (front of train at the left),
        // each wagon labelled with its passenger-facing car number; hover a car
        // for its type and amenities. Hidden until the composition arrives (and
        // for runs with no stock data, e.g. most commuter/freight services).
        ColumnLayout {
            id: compositionCol
            Layout.fillWidth: true
            visible: root.details.hasComposition
            spacing: 4

            Label {
                text: qsTr("Carriage order")
                font.bold: true
                font.pointSize: TypeScale.panelBody
                color: Theme.textStrong
            }
            Label {
                text: root.details.compositionLeg
                visible: text.length > 0
                color: Theme.textMuted
                font.pointSize: TypeScale.panelCaption
            }

            // Carriage strip. Cars size up to fill the width on one row, then wrap
            // onto balanced extra rows once they'd drop below a readable minimum —
            // so a 6-car IC and a 13-car night train both stay legible without a
            // horizontal scrollbar. Row-major fill keeps the front of the train at
            // the top-left, reading left-to-right then down.
            Grid {
                id: carGrid
                Layout.alignment: Qt.AlignHCenter

                readonly property real gap: 4
                readonly property real minCarW: 40       // readability floor before wrapping
                readonly property real maxCarW: 80       // don't bloat a 2-3 car consist
                readonly property real cellH: 56
                readonly property real availW: compositionCol.width
                readonly property int total: root.details.composition.count
                // Fit as many as the width allows at the minimum size, then split
                // evenly so the last row isn't a lonely straggler (7 -> 4+3, not 6+1).
                readonly property int perRowMax: Math.max(1, Math.floor((availW + gap) / (minCarW + gap)))
                readonly property int rowCount: Math.max(1, Math.ceil(total / perRowMax))
                readonly property int perRow: Math.max(1, Math.ceil(total / rowCount))
                readonly property real cellW: Math.max(minCarW,
                        Math.min(maxCarW, (availW - (perRow - 1) * gap) / perRow))

                columns: perRow
                columnSpacing: gap
                rowSpacing: gap

                Repeater {
                    model: root.details.composition

                    delegate: Item {
                        id: car
                        required property int position
                        required property bool locomotive
                        required property string label
                        required property string vehicleType
                        required property string powerType
                        required property var amenities

                        width: carGrid.cellW
                        height: carGrid.cellH

                        // Tab-reachable so the tooltip's detail (type/power/amenities)
                        // has a keyboard path too, not just hover (§1.3).
                        activeFocusOnTab: true

                        // Expose the same detail the hover tooltip shows to assistive
                        // tech, so type/power/amenities aren't pointer-only (W: §1.3).
                        Accessible.role: Accessible.StaticText
                        Accessible.name: car.locomotive ? qsTr("Locomotive")
                                                         : qsTr("Car %1").arg(car.label)
                        Accessible.description: [car.vehicleType, car.powerType,
                                                 car.amenities.join(", ")].filter(s => s.length > 0).join(" · ")

                        Rectangle {
                            id: carBody
                            anchors.fill: parent
                            anchors.topMargin: 2
                            anchors.bottomMargin: 2
                            radius: 5
                            // Contain an oversized label (e.g. a long car number under
                            // OS "Large text" scaling) instead of letting it spill into
                            // neighbouring cells — the fixed cellW/cellH don't grow with it.
                            clip: true
                            color: car.locomotive ? Theme.subtlePress : Theme.iconBadgeBg
                            border.color: (carHover.hovered || car.activeFocus) ? Theme.accent : Theme.hairline
                            border.width: car.activeFocus ? 2 : 1

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 3
                                spacing: 1

                                // Car number (wagon) or locomotive icon.
                                Item {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    AppIcon {
                                        anchors.centerIn: parent
                                        visible: car.locomotive
                                        name: "train"
                                        color: Theme.textStrong
                                        size: TypeScale.panelIconSm
                                    }
                                    Label {
                                        anchors.centerIn: parent
                                        width: parent.width
                                        horizontalAlignment: Text.AlignHCenter
                                        elide: Text.ElideRight
                                        visible: !car.locomotive
                                        text: car.label
                                        font.bold: true
                                        font.pointSize: TypeScale.panelBody
                                        color: Theme.textStrong
                                    }
                                }

                                // Vehicle type code (e.g. "Ed", "Sr2").
                                Label {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: car.vehicleType
                                    font.pointSize: TypeScale.panelCaption
                                    color: Theme.textMuted
                                    elide: Text.ElideRight
                                    visible: text.length > 0
                                }

                                // Amenity dots (catering / accessible / family / pet).
                                RowLayout {
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.bottomMargin: 1
                                    spacing: 2
                                    visible: car.amenities.length > 0
                                    Repeater {
                                        model: car.amenities
                                        // A colour-only dot fails colour-blind /
                                        // low-vision users who can't hover for the
                                        // tooltip; the amenity's own initial (C/A/F/P)
                                        // makes each badge distinguishable without
                                        // relying on hue at all.
                                        delegate: Rectangle {
                                            id: badge
                                            required property string modelData
                                            implicitWidth: 12
                                            implicitHeight: 12
                                            radius: height / 2
                                            color: root.amenityColor(modelData)
                                            Text {
                                                anchors.centerIn: parent
                                                text: badge.modelData.charAt(0)
                                                font.pointSize: TypeScale.panelCaption
                                                font.bold: true
                                                color: Theme.inkFor(badge.color)
                                            }
                                        }
                                    }
                                }
                            }

                            HoverHandler { id: carHover }
                            ToolTip.visible: carHover.hovered || car.activeFocus
                            ToolTip.text: car.locomotive
                                ? (car.vehicleType + (car.powerType.length > 0
                                                      ? " · " + car.powerType : ""))
                                : (qsTr("Car %1").arg(car.label)
                                   + (car.vehicleType.length > 0 ? " · " + car.vehicleType : "")
                                   + (car.amenities.length > 0 ? "\n" + car.amenities.join(", ") : ""))
                        }
                    }
                }
            }

            Label {
                text: root.details.compositionSummary
                visible: text.length > 0
                color: Theme.textMuted
                font.pointSize: TypeScale.panelCaption
            }
            Label {
                text: qsTr("Consist changes en route")
                visible: root.details.compositionSectionCount > 1
                color: Theme.textMuted
                font.pointSize: TypeScale.panelCaption
                font.italic: true
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline }
        }

        // ---- Journey progress (booked stops the train has left) ----------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: root.details.model.totalStops > 0
            Label {
                text: qsTr("%1 of %2 stops").arg(root.details.model.passedStops)
                                            .arg(root.details.model.totalStops)
                color: Theme.textMuted
                font.pointSize: TypeScale.panelCaption
            }
            Rectangle {   // progress track
                Layout.fillWidth: true
                Layout.preferredHeight: 4
                radius: 2
                color: Theme.hairline
                Rectangle {   // filled portion
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.width * (root.details.model.totalStops > 0
                           ? root.details.model.passedStops / root.details.model.totalStops : 0)
                    radius: 2
                    color: Theme.accent
                }
            }
        }

        // ---- Show-all toggle (reveals passed-through timing points) -------
        // Keyboard-focusable checkbox (W4) with a drawn tick (O3).
        Item {
            id: allToggle
            Layout.fillWidth: true
            implicitHeight: 32   // toward the 44 px desktop hit-target guideline
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
                    font.pointSize: TypeScale.panelBody
                    color: Theme.textStrong
                }
            }
            TapHandler { onTapped: allToggle.checked = !allToggle.checked }
        }

        // ---- Timetable ---------------------------------------------------
        // Filter passed-through points out of the row set (rather than hiding
        // them with zero-height delegates) so the ListView below virtualises:
        // it then builds only the booked stops it shows, not every timing point
        // on the route. `showAll` follows the "Show all timing points" toggle.
        TimetableFilterModel {
            id: timetableModel
            sourceModel: root.details.model
            showAll: allToggle.checked
        }

        ListView {
            id: stopsList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: timetableModel
            spacing: 0
            // Theme the scrollbar to the dark panel: the native style renders a
            // light handle that clashes with the card. A thin muted-ink handle
            // that fades in on hover/press reads correctly in both light and dark.
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

            // When a newly selected train's stops load, scroll the timetable so
            // the NEXT stop sits at the top (current position in view, upcoming
            // route below). Once per train — live updates won't yank the view.
            Connections {
                target: root.details.model
                function onProgressChanged() {
                    if (root.details.trainNumber === root.autoScrolledTrain)
                        return
                    const src = root.details.model.nextStopRow
                    if (src < 0)
                        return
                    const proxyRow = timetableModel.proxyRowForSource(src)
                    if (proxyRow < 0)
                        return
                    root.autoScrolledTrain = root.details.trainNumber
                    // Defer so the view has laid out rows after the model reset.
                    Qt.callLater(() => stopsList.positionViewAtIndex(proxyRow, ListView.Beginning))
                }
            }

            delegate: ItemDelegate {
                id: stopRow
                width: ListView.view.width
                height: rowLayout.implicitHeight + 12
                clip: true

                // The native Controls style paints ItemDelegate's background from
                // the light system palette (white), which hid the theme-coloured
                // (near-white) row text in dark mode. Drive it from the app theme
                // instead so the dark card shows through and text stays legible.
                background: Rectangle {
                    color: stopRow.isNext ? Theme.subtleHover
                                          : (stopRow.hovered ? Theme.subtleHover : "transparent")
                    // Accent stripe marking the next booked stop the train will reach.
                    Rectangle {
                        visible: stopRow.isNext
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 3
                        color: Theme.accent
                    }
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
                required property bool passed
                required property bool isNext
                required property string causeText

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
                    // Dim stops the train has already left (journey progress).
                    opacity: stopRow.passed ? 0.45 : 1.0

                    // Station + track / "passing"
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Label {
                                text: stopRow.stationName
                                font.pointSize: TypeScale.panelBody
                                font.strikeout: stopRow.cancelled
                                font.bold: stopRow.isNext
                                color: stopRow.stopping ? Theme.textStrong : Theme.textMuted
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            // "NEXT" pill on the next booked stop.
                            Rectangle {
                                visible: stopRow.isNext
                                radius: height / 2
                                color: Theme.accent
                                Layout.preferredHeight: nextLabel.implicitHeight + 2
                                Layout.preferredWidth: nextLabel.implicitWidth + 12
                                Label {
                                    id: nextLabel
                                    anchors.centerIn: parent
                                    text: qsTr("NEXT")
                                    color: Theme.accentText
                                    font.pointSize: TypeScale.panelCaption
                                    font.bold: true
                                }
                            }
                        }
                        Label {
                            text: stopRow.stopping
                                  ? (stopRow.track.length > 0 ? qsTr("Track %1").arg(stopRow.track) : "")
                                  : qsTr("passing")
                            color: Theme.textMuted
                            font.pointSize: TypeScale.panelCaption
                            font.italic: !stopRow.stopping
                            visible: text.length > 0
                        }
                        // Delay cause (top-level category, e.g. "Onnettomuus"), when known.
                        Label {
                            Layout.fillWidth: true
                            text: stopRow.causeText
                            color: Theme.textMuted
                            font.pointSize: TypeScale.panelCaption
                            font.italic: true
                            elide: Text.ElideRight
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
                            font.pointSize: TypeScale.panelBody
                            color: Theme.textStrong
                            visible: stopRow.scheduledArrival.length > 0
                            text: stopRow.estimatedArrival.length > 0
                                  ? qsTr("arr %1 → %2").arg(stopRow.scheduledArrival).arg(stopRow.estimatedArrival)
                                  : qsTr("arr %1").arg(stopRow.scheduledArrival)
                        }
                        Label {
                            Layout.alignment: Qt.AlignRight
                            font.pointSize: TypeScale.panelBody
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
                        font.pointSize: TypeScale.panelBody
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
                        font.pointSize: TypeScale.panelBody
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
