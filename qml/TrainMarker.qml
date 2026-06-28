import QtQuick
import QtQuick.Shapes
import QtLocation
import QtPositioning

import TrainsOnMap

/// Delegate for a single train inside a MapItemView. Model roles come from
/// TrainListModel: coordinate, trainNumber, speed, bearing, trainType, category,
/// commuterLine.
///
/// Visual style mirrors the TrainSpotter (Swift/MapKit) app: a filled, type-
/// coloured capsule with white "TYPE NUMBER" + km/h text, and a separate
/// direction arrow that orbits the capsule to the heading side (apex outward),
/// fading with speed and gliding along the shortest arc on a turn.
///
/// Colours are the juliadata.fi palette, keyed on trainType, with a
/// category/speed fallback for types the legend doesn't enumerate (so a moving
/// train is never a bland grey dot). See trainspotter-qt-docs/MarkerColors.Qt.md.
MapQuickItem {
    id: marker

    required property var model
    property bool selected: false
    // Show the text capsule only when zoomed in enough to read it; at country
    // scale the labels collide into an unreadable mass, so we render dots only.
    property bool labelsVisible: true

    signal clicked(int trainNumber, string departureDate)

    // trainType -> juliadata fill colour; unmatched types fall to category/speed.
    function colorFor(type, category, speed) {
        switch (type) {
        case "IC":  case "IC2":               return "#FF0000";   // InterCity — red
        case "S":                             return "#007700";   // Pendolino — green
        case "PYO": case "P":                 return "#0000FF";   // night / local — blue
        case "H":   case "HDM": case "HSM":   return "#770000";   // express / diesel — dark red
        case "HL":                            return "#004400";   // Helsinki commuter — dark green
        case "HV":  case "MV":                return "#FF006E";   // museum / shunting — pink
        case "PAI":                           return "#007070";   // teal
        case "SAA": case "VLI":               return "#009090";   // cyan-teal
        case "W":                             return "#00B0B0";   // light cyan
        case "T":                             return "#000077";   // cargo — navy
        case "TYO":                           return "#7F6A00";   // work / maintenance — olive
        case "VET":                           return "#660066";   // locomotive haul — purple
        case "VEV":                           return "#9E009E";   // magenta
        }
        switch (category) {        // unmatched type: fall back to the broad class
        case "Cargo":    return "#000077";   // navy
        case "Commuter": return "#30B0C7";   // teal
        }
        return speed > 0 ? "#FF9500" : "#8E8E93";   // moving = orange, stopped = grey
    }

    // Badge text: a commuter line letter if any, else "TYPE NUMBER", else number.
    function labelFor(line, type, number) {
        if (line && line.length > 0)
            return line;                       // "R", "Z", "U", …
        if (type && type.length > 0)
            return type + " " + number;        // "IC 967", "T 5280"
        return "" + number;                    // "967" until the type cache loads
    }

    readonly property color trainColor: colorFor(model.trainType, model.category, model.speed)
    readonly property string badgeLabel: labelFor(model.commuterLine, model.trainType, model.trainNumber)

    // Live status ring (passenger trains only). Mirroring TrainSpotter, only the
    // late tiers ring the capsule: amber = 5–14 min late, red = 15+ min late.
    // A "stale" train is greyed/dimmed instead of ringed.
    readonly property bool stale: model.ringState === "stale"
    // Late tiers also carry a textual "+N min" badge so the state is legible
    // without relying on the ring colour alone (colour-blind safety).
    readonly property bool late: model.ringState === "amber" || model.ringState === "red"
    readonly property color ringColor: {
        switch (model.ringState) {
        case "amber": return "#F2A900";
        case "red":   return "#E03131";
        default:      return "transparent";
        }
    }
    // Greyed when stale, else the type colour. Drives the capsule fill + arrow.
    readonly property color dotColor: stale ? "#9AA0A6" : trainColor

    // ---- Direction-arrow geometry (TrainSpotter parity) ------------------
    readonly property real arrowW: 11
    readonly property real arrowH: 8
    // Gap between the capsule edge and the arrow's centre as it orbits.
    readonly property real arrowGap: 2.5 + arrowH / 2
    // Hidden at a standstill; tapers from 0.35 up to fully opaque at 50 km/h,
    // dimmed to match a stale capsule.
    readonly property real arrowOpacity: {
        if (model.speed <= 0)
            return 0
        const t = Math.min(Math.max(model.speed / 50, 0), 1)
        const base = 0.35 + 0.65 * t
        return stale ? base * 0.55 : base
    }

    // Position-quality flag (#1). A fix with poor GPS accuracy, or one that sits
    // far enough off any rail that it was kept raw (not snapped, offset beyond the
    // ~150 m snap-accept), is an approximate position. Flag it so the marker isn't
    // read as a precise location when it's really a guess; the exact figures show
    // in the detail panel. accuracy/trackOffsetMeters are -1 when unknown.
    readonly property int accuracyMeters: model.accuracy
    readonly property real offsetMeters: model.trackOffsetMeters
    readonly property bool suspect: accuracyMeters > 100 || offsetMeters > 150

    // Glide along the rail between the (roughly periodic) position fixes instead
    // of teleporting on each one. Successive fixes are already snapped onto the
    // rail, so a short tween between two of them tracks the line closely; a fresh
    // fix simply retargets the animation in flight. Honour the reduced-motion
    // opt-out — then the coordinate snaps straight to each fix with no animation.
    //
    // Beyond maxGlideMeters the tween is disabled so the marker snaps instantly:
    // a CoordinateAnimation interpolates a straight chord, so gliding a multi-km
    // re-acquire (a tunnel GPS blackout) would visibly drag the dot across open
    // terrain off the rails. The threshold sits above any legitimate single-step
    // move — a 60 s REST resync at line speed (~200 km/h) is ~3.3 km — so routine
    // motion and the brief startup Tier-1->Tier-2 convergence still glide smoothly;
    // only a genuine multi-km teleport snaps.
    readonly property real maxGlideMeters: 4000

    // Decide glide-vs-snap once per incoming fix, not on every animation step.
    // The guard must NOT depend on the live (animating) `coordinate`: binding it
    // into Behavior.enabled re-evaluates distanceTo() every frame for every
    // gliding marker (the dominant binding churn in the profiler trace). Instead
    // mirror the model fix through `fix`, and on each change measure the jump from
    // the marker's current position to the new fix, latch the flag, THEN apply the
    // move — so the Behavior reads an already-settled `enabled` for this
    // transition. distanceTo() now runs once per fix rather than once per frame.
    // The isValid guard makes a marker entering the fleet appear at its position
    // rather than glide in from the invalid (0,0) default.
    readonly property var fix: model.coordinate
    property bool glideEnabled: false
    onFixChanged: {
        glideEnabled = !Theme.reducedMotion
                       && coordinate.isValid
                       && coordinate.distanceTo(fix) < maxGlideMeters
        coordinate = fix
    }

    Behavior on coordinate {
        enabled: marker.glideEnabled
        CoordinateAnimation {
            duration: 1000
            easing.type: Easing.Linear
        }
    }

    // Anchor the coordinate at the centre of the capsule (the train's location).
    anchorPoint.x: content.width / 2
    anchorPoint.y: content.height / 2

    sourceItem: Item {
        id: content
        width: capsule.width
        height: capsule.height

        // Selection emphasis: scale the whole badge up around its centre — the
        // anchored coordinate (= capsule centre) stays pinned, so the marker
        // grows in place. Mirrors TrainSpotter's 1.15× selected scale.
        transformOrigin: Item.Center
        scale: marker.selected ? 1.15 : 1.0
        Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

        // Dim a stale / not-running train most; a suspect (low-confidence
        // position) train a little, so it reads as present-but-approximate (#1).
        opacity: marker.stale ? 0.55 : (marker.suspect ? 0.72 : 1.0)

        // Selection halo: a translucent capsule of the train colour sitting just
        // outside the fill (TrainSpotter parity).
        Rectangle {
            anchors.centerIn: capsule
            width: capsule.width + 14
            height: capsule.height + 14
            radius: height / 2
            color: marker.dotColor
            opacity: 0.30
            visible: marker.selected
        }

        // Status ring (amber / red lateness), drawn just outside the capsule.
        Rectangle {
            anchors.centerIn: capsule
            width: capsule.width + 6
            height: capsule.height + 6
            radius: height / 2
            color: "transparent"
            border.width: 2.5
            border.color: marker.ringColor
            visible: marker.ringColor.a > 0
        }

        // Position-quality outline (#1): a thin neutral ring marks a suspect
        // (poor-accuracy / off-rail) fix. Neutral grey on purpose, so it never
        // reads as one of the coloured delay rings.
        Rectangle {
            anchors.centerIn: capsule
            width: capsule.width + 9
            height: capsule.height + 9
            radius: height / 2
            color: "transparent"
            border.width: 1.5
            border.color: Theme.isDark ? "#b9bec6" : "#5f6368"
            opacity: 0.85
            visible: marker.suspect
        }

        // The capsule: type-coloured fill, white text. Collapses to a small dot
        // when labels are hidden (country scale).
        Rectangle {
            id: capsule
            radius: height / 2
            color: marker.dotColor
            // Hairline edge so the capsule separates from a same-hued tile.
            border.color: Theme.isDark ? Qt.rgba(1, 1, 1, 0.30) : Qt.rgba(0, 0, 0, 0.28)
            border.width: 1
            implicitWidth: marker.labelsVisible ? labelCol.implicitWidth + 14 : 14
            implicitHeight: marker.labelsVisible ? labelCol.implicitHeight + 6 : 14
            width: implicitWidth
            height: implicitHeight

            Column {
                id: labelCol
                anchors.centerIn: parent
                spacing: 0
                visible: marker.labelsVisible

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: marker.badgeLabel
                    font.pixelSize: 11
                    font.bold: true
                    color: "white"
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: marker.model.speed > 0
                    text: Math.round(marker.model.speed) + " km/h"
                    font.pixelSize: 9
                    color: Qt.rgba(1, 1, 1, 0.9)
                }

                // Lateness as text (paired with the ring colour, not colour alone).
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: marker.late
                    text: qsTr("+%1 min").arg(marker.model.delayMinutes)
                    font.pixelSize: 9
                    font.bold: true
                    color: "white"
                }
            }
        }

        // Direction arrow — a triangle (apex = travel direction) orbiting the
        // capsule to the heading side. Both its orbit position and its rotation
        // glide along the shortest arc on a turn (TrainSpotter parity).
        Shape {
            id: arrow
            width: marker.arrowW
            height: marker.arrowH
            antialiasing: true
            visible: marker.model.speed > 0
            opacity: marker.arrowOpacity

            readonly property real rad: marker.model.bearing * Math.PI / 180
            readonly property real cx: capsule.width / 2
            readonly property real cy: capsule.height / 2
            // North (0°) → top, clockwise. Item space is y-down, so north is −y.
            x: cx + (cx + marker.arrowGap) * Math.sin(rad) - width / 2
            y: cy - (cy + marker.arrowGap) * Math.cos(rad) - height / 2
            rotation: marker.model.bearing
            transformOrigin: Item.Center

            Behavior on x { enabled: !Theme.reducedMotion
                NumberAnimation { duration: 600; easing.type: Easing.InOutQuad } }
            Behavior on y { enabled: !Theme.reducedMotion
                NumberAnimation { duration: 600; easing.type: Easing.InOutQuad } }
            Behavior on rotation { enabled: !Theme.reducedMotion
                RotationAnimation { duration: 600; direction: RotationAnimation.Shortest
                    easing.type: Easing.InOutQuad } }

            ShapePath {
                fillColor: marker.dotColor
                strokeColor: Theme.isDark ? Qt.rgba(1, 1, 1, 0.35) : Qt.rgba(0, 0, 0, 0.30)
                strokeWidth: 0.75
                startX: arrow.width / 2; startY: 0
                PathLine { x: 0;            y: arrow.height }
                PathLine { x: arrow.width;  y: arrow.height }
                PathLine { x: arrow.width / 2; y: 0 }
            }
        }

        // Whole marker (capsule + arrow) is clickable.
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: marker.clicked(marker.model.trainNumber, marker.model.departureDate)
        }
    }
}
