import QtQuick
import QtQuick.Controls
import QtLocation
import QtPositioning

import TrainsOnMap

ApplicationWindow {
    id: win
    visible: true
    width: 1100
    height: 820
    title: qsTr("Trains on Map — Finland (Digitraffic)")

    // ---- Backend services (C++) -------------------------------------------
    // REST seeds the full set of trains once; MQTT then streams live updates.
    DigitrafficClient {
        id: trainClient
        active: false
    }

    DigitrafficMqttClient {
        id: trainStream
        model: trainClient.model
        active: true
    }

    Component.onCompleted: trainClient.refresh()   // initial snapshot

    TrackService {
        id: trackService
    }

    TrainDetailsService {
        id: trainDetails
        stream: trainStream      // live timetable updates for the selected train
    }

    // Below this zoom the viewport covers too much of the network to fetch.
    readonly property real trackZoomThreshold: 9.0

    // Fetch only the tracks within the current viewport.
    function loadVisibleTracks() {
        const tl = map.toCoordinate(Qt.point(0, 0));
        const br = map.toCoordinate(Qt.point(map.width, map.height));
        trackService.loadForBounds(tl.longitude, br.latitude, br.longitude, tl.latitude);
    }

    // Auto-load tracks for the visible area once zoomed in enough.
    function autoLoadTracks() {
        if (map.zoomLevel >= win.trackZoomThreshold)
            win.loadVisibleTracks();
    }

    // ---- Map ---------------------------------------------------------------
    Plugin {
        id: mapPlugin
        name: "osm"             // OpenStreetMap tiles, bundled with Qt Location
    }

    Map {
        id: map
        anchors.fill: parent
        plugin: mapPlugin
        center: QtPositioning.coordinate(62.8, 25.7)   // central Finland
        zoomLevel: 5.4
        copyrightsVisible: true

        // Debounce viewport changes before re-fetching track geometry.
        onCenterChanged: trackDebounce.restart()
        onZoomLevelChanged: trackDebounce.restart()
        Component.onCompleted: win.autoLoadTracks()

        // Track geometry layer (drawn beneath the trains).
        MapItemView {
            model: trackService.model
            delegate: MapPolyline {
                required property var model
                line.width: 2
                line.color: "#5b6bb5"
                path: model.path
            }
        }

        // Live train layer.
        MapItemView {
            model: trainClient.model
            delegate: TrainMarker {
                selected: trainDetails.hasSelection && trainDetails.trainNumber === model.trainNumber
                onClicked: (trainNumber, departureDate) => trainDetails.show(trainNumber, departureDate)
            }
        }

        // Standard pan/zoom gestures are enabled by default on Map.
    }

    Timer {
        id: trackDebounce
        interval: 500
        onTriggered: win.autoLoadTracks()
    }

    // ---- Overlay UI --------------------------------------------------------
    InfoPanel {
        id: panel
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12

        trainCount: trainClient.model.count
        trackCount: trackService.model.count
        tracksLoading: trackService.loading
        streamConnected: trainStream.connected
        streamStatus: trainStream.status
        statusText: map.zoomLevel < win.trackZoomThreshold && trackService.model.count === 0
                    ? qsTr("Zoom in to load track geometry")
                    : (trackService.status.length > 0 ? trackService.status : trainClient.status)

        onRefreshRequested: trainClient.refresh()
        onLoadTracksRequested: win.loadVisibleTracks()
    }

    // Timetable detail panel — slides in from the right when a train is picked.
    TrainDetailPanel {
        id: detailPanel
        details: trainDetails
        visible: trainDetails.hasSelection
        width: 340
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 12
    }
}
