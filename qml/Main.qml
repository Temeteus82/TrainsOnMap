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
    // REST bootstraps the full set of trains and resyncs/prunes every 60 s;
    // MQTT streams live position deltas in between. active:true fetches the
    // bootstrap snapshot immediately and starts the resync timer.
    DigitrafficClient {
        id: trainClient
        active: true
    }

    DigitrafficMqttClient {
        id: trainStream
        model: trainClient.model
        active: true
    }

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
    // Light, OSM-based cartography (CARTO Positron) — like juliadata.fi's light
    // map, so the type-coloured trains and rails read clearly. Swap for
    // "dark_all" / "rastertiles/voyager" to retheme. CARTO renders OSM data.
    readonly property string basemapStyle: "light_all"

    Plugin {
        id: mapPlugin
        name: "osm"
        // Use only our custom tile host, not the bundled online provider list.
        PluginParameter { name: "osm.mapping.providersrepository.disabled"; value: true }
        PluginParameter {
            name: "osm.mapping.custom.host"   // Qt appends "%z/%x/%y.png"
            value: "https://a.basemaps.cartocdn.com/" + win.basemapStyle + "/"
        }
        PluginParameter {
            name: "osm.mapping.custom.mapcopyright"
            value: "© OpenStreetMap contributors, © CARTO"
        }
        PluginParameter { name: "osm.mapping.highdpi_tiles"; value: true }
        PluginParameter { name: "osm.useragent"; value: "TrainsOnMap/0.1 (Qt6 scaffolding)" }
    }

    Map {
        id: map
        anchors.fill: parent
        plugin: mapPlugin
        center: QtPositioning.coordinate(60.20, 24.94)  // Helsinki region (capital area)
        zoomLevel: 10.5
        copyrightsVisible: true
        color: "#e9eaec"          // neutral light backdrop shown while tiles load

        // The CARTO tiles arrive as the plugin's "custom" map type; activate it.
        function selectBasemap() {
            for (var i = 0; i < supportedMapTypes.length; ++i) {
                if (supportedMapTypes[i].style === MapType.CustomMap) {
                    activeMapType = supportedMapTypes[i];
                    return;
                }
            }
        }
        onSupportedMapTypesChanged: selectBasemap()

        // Debounce viewport changes before re-fetching track geometry.
        onCenterChanged: trackDebounce.restart()
        onZoomLevelChanged: trackDebounce.restart()
        Component.onCompleted: { selectBasemap(); win.autoLoadTracks(); }

        // Track geometry layer (drawn beneath the trains).
        MapItemView {
            model: trackService.model
            delegate: MapPolyline {
                required property var model
                line.width: 1.4
                line.color: "#b4b8bf"      // light grey rail, subtle on the light base
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
