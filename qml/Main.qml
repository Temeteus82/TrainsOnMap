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
        // Framed on southern Finland (the dense Helsinki–Turku–Tampere rail
        // triangle) on startup; zoom out to 4.0 to see the whole country.
        center: QtPositioning.coordinate(61.00, 24.50)
        zoomLevel: 7.0
        minimumZoomLevel: 4.0
        maximumZoomLevel: 18.0

        // Anchor point captured when a pinch starts, so the gesture zooms
        // around the fingers rather than the map centre.
        property geoCoordinate startCentroid
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
        Component.onCompleted: selectBasemap()

        // Rail geometry is held in memory; materialise only the segments in (a
        // padded) viewport so a pan doesn't reproject the whole ~10k-segment
        // network. toCoordinate() needs the map ready, so seed the first load
        // from mapReady, then refresh on a short debounce as the view changes.
        onMapReadyChanged: if (mapReady) refreshTracks()
        onVisibleRegionChanged: trackReloadTimer.restart()

        Timer {
            id: trackReloadTimer
            interval: 250        // settle delay: fire once the gesture stops
            onTriggered: map.refreshTracks()
        }

        // Filter the track model to the current viewport (+25% margin).
        function refreshTracks() {
            if (!map.mapReady || map.width <= 0 || map.height <= 0)
                return
            const nw = map.toCoordinate(Qt.point(0, 0), false)                  // NW corner
            const se = map.toCoordinate(Qt.point(map.width, map.height), false) // SE corner
            if (!nw.isValid || !se.isValid)
                return
            // Pad each side so a small pan doesn't expose unloaded edges before
            // the next debounce fires.
            const latPad = Math.abs(nw.latitude - se.latitude) * 0.25
            const lonPad = Math.abs(se.longitude - nw.longitude) * 0.25
            trackService.loadForBounds(nw.longitude - lonPad,   // west
                                       se.latitude  - latPad,   // south
                                       se.longitude + lonPad,   // east
                                       nw.latitude  + latPad)   // north
        }

        // Geometry is parsed on a worker thread; seed the first viewport load
        // once it lands (the map may become ready before or after this fires).
        Connections {
            target: trackService
            function onGeometryReady() { map.refreshTracks() }
        }

        // Track geometry layer (drawn beneath the trains).
        MapItemView {
            model: trackService.model
            delegate: MapPolyline {
                required property var model
                line.width: 2.2
                line.color: "#8c95a0"      // mid grey rail, legible at the overview zoom
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

        // ---- Pan / zoom input (Qt 6 needs explicit handlers) --------------
        // Drag to pan, wheel/trackpad to zoom, pinch to zoom; the map stays
        // north-up (no rotation handler). Keyboard +/- zoom in steps too.
        PinchHandler {
            id: pinch
            target: null
            onActiveChanged: if (active)
                map.startCentroid = map.toCoordinate(pinch.centroid.position, false)
            onScaleChanged: (delta) => {
                map.zoomLevel += Math.log2(delta)
                map.alignCoordinateToPoint(map.startCentroid, pinch.centroid.position)
            }
            grabPermissions: PointerHandler.TakeOverForbidden
        }
        WheelHandler {
            id: wheel
            // Magic Mouse / Wayland trackpads report as touchpads (QTBUG-87646).
            acceptedDevices: Qt.platform.pluginName === "cocoa"
                             || Qt.platform.pluginName === "wayland"
                             ? PointerDevice.Mouse | PointerDevice.TouchPad
                             : PointerDevice.Mouse
            rotationScale: 1 / 120
            property: "zoomLevel"
        }
        DragHandler {
            id: drag
            target: null
            onTranslationChanged: (delta) => map.pan(-delta.x, -delta.y)
        }
        Shortcut {
            enabled: map.zoomLevel < map.maximumZoomLevel
            sequence: StandardKey.ZoomIn
            onActivated: map.zoomLevel = Math.round(map.zoomLevel + 1)
        }
        Shortcut {
            enabled: map.zoomLevel > map.minimumZoomLevel
            sequence: StandardKey.ZoomOut
            onActivated: map.zoomLevel = Math.round(map.zoomLevel - 1)
        }
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
        statusText: trackService.status.length > 0 ? trackService.status : trainClient.status

        onRefreshRequested: trainClient.refresh()
        onLoadTracksRequested: map.refreshTracks()
    }

    // Timetable detail panel — only built once a train is picked, so its
    // subtree isn't constructed/compiled on the startup path.
    Loader {
        id: detailPanelLoader
        active: trainDetails.hasSelection
        width: 340
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 12
        sourceComponent: TrainDetailPanel {
            details: trainDetails
        }
    }
}
