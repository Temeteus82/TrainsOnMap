pragma ComponentBehavior: Bound

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
    color: Theme.windowBg

    // Ask the (Loader-hosted) map to reload tracks for the current viewport.
    // Routed as a signal so the call lives inside the Map's typed scope rather
    // than reaching through the loosely-typed Loader.item.
    signal requestTrackReload()

    // Tier-2 diagnostics for the selected train: { rawLat, rawLon, snapLat,
    // snapLon, offset, tunniste, onRoute }. Polled (the model exposes it via an
    // invokable, not a notifying role) while a train is selected, and drives the
    // route/raw-vs-snapped debug overlay and the detail-panel diagnostics line.
    property var selMatch: ({})
    Timer {
        running: trainDetails.hasSelection
        interval: 750
        repeat: true
        triggeredOnStart: true
        onTriggered: win.selMatch = trainClient.model.matchInfoFor(
                         trainDetails.trainNumber, trainDetails.departureDate)
    }

    // ---- Backend services (C++) -------------------------------------------
    // REST bootstraps the full set of trains and resyncs/prunes every 60 s;
    // MQTT streams live position deltas in between. active:true fetches the
    // bootstrap snapshot immediately and starts the resync timer.
    DigitrafficClient {
        id: trainClient
        active: true
        matcher: trackService   // snap/flag GPS fixes against the rail network
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
    // The CARTO basemap (light_all / dark_all) is chosen by Theme. The osm
    // plugin only reads its tile host at construction, so a theme flip rebuilds
    // the Plugin + Map via this Loader; the view (centre/zoom) is preserved in
    // the loader's saved* properties across the reload.
    Loader {
        id: mapLoader
        anchors.fill: parent
        sourceComponent: mapComponent

        property real savedCenterLat: 61.00   // southern Finland on first launch
        property real savedCenterLon: 24.50
        property real savedZoom: 7.0

        // Rebuild the map when the resolved light/dark state changes so the
        // tiles re-fetch from the matching CARTO host.
        Connections {
            target: Theme
            function onIsDarkChanged() {
                mapLoader.active = false
                mapLoader.active = true
            }
        }
    }

    Component {
        id: mapComponent

        Map {
            id: map
            plugin: Plugin {
                name: "osm"
                // Use only our custom tile host, not the bundled provider list.
                PluginParameter { name: "osm.mapping.providersrepository.disabled"; value: true }
                PluginParameter {
                    name: "osm.mapping.custom.host"   // Qt appends "%z/%x/%y.png"
                    value: "https://a.basemaps.cartocdn.com/" + Theme.basemapStyle + "/"
                }
                PluginParameter {
                    name: "osm.mapping.custom.mapcopyright"
                    value: "© OpenStreetMap contributors, © CARTO"
                }
                PluginParameter { name: "osm.mapping.highdpi_tiles"; value: true }
                PluginParameter { name: "osm.useragent"; value: "TrainsOnMap/0.1 (Qt6 scaffolding)" }
            }

            // The starting view is restored from the loader's saved* values in
            // Component.onCompleted. We deliberately do NOT bind center/zoomLevel
            // to those values: a live binding re-asserts the view on every
            // savedCenter* write below, which fights map.pan() on each drag event
            // and throttled horizontal panning to a crawl. Initialise imperatively
            // once, then only write the saved* values back (see handlers below).
            minimumZoomLevel: 4.0
            maximumZoomLevel: 18.0

            // Persist the view so a theme-driven reload restores it.
            onCenterChanged: {
                mapLoader.savedCenterLat = center.latitude
                mapLoader.savedCenterLon = center.longitude
            }
            onZoomLevelChanged: mapLoader.savedZoom = zoomLevel

            // Anchor point captured when a pinch starts, so the gesture zooms
            // around the fingers rather than the map centre.
            property geoCoordinate startCentroid
            copyrightsVisible: true
            color: Theme.windowBg     // neutral backdrop shown while tiles load

            // The CARTO tiles arrive as the plugin's "custom" map type; activate it.
            function selectBasemap() {
                for (let i = 0; i < supportedMapTypes.length; ++i) {
                    if (supportedMapTypes[i].style === MapType.CustomMap) {
                        activeMapType = supportedMapTypes[i];
                        return;
                    }
                }
            }
            onSupportedMapTypesChanged: selectBasemap()
            Component.onCompleted: {
                // Imperative (non-binding) restore of the saved view, so panning
                // isn't fought by a center/zoomLevel binding.
                center = QtPositioning.coordinate(mapLoader.savedCenterLat,
                                                  mapLoader.savedCenterLon)
                zoomLevel = mapLoader.savedZoom
                selectBasemap()
            }

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

            // "Load tracks" button (relayed via win) → refresh this viewport.
            Connections {
                target: win
                function onRequestTrackReload() { map.refreshTracks() }
            }

            // Track geometry layer (drawn beneath the trains).
            MapItemView {
                model: trackService.model
                delegate: MapPolyline {
                    required property var model
                    line.width: 2.2
                    line.color: Theme.railColor    // legible on either basemap
                    path: model.path
                }
            }

            // ---- Tier-2 debug overlay (selected train) -----------------------
            // Re-evaluate the route binding when a precompute finishes resolving.
            property int routeTick: 0
            Connections {
                target: trackService
                function onRoutesReady() { map.routeTick++ }
            }

            // The train's resolved route polyline (derived-graph path), accent-tinted.
            MapPolyline {
                id: routeOverlay
                visible: trainDetails.hasSelection && path.length > 1
                line.width: 4
                line.color: Theme.accent
                opacity: 0.45
                path: {
                    map.routeTick   // dependency: refresh when routes finish resolving
                    return trainDetails.hasSelection
                        ? trackService.routePolyline(trainDetails.routeStations) : []
                }
            }

            // Connector from the raw GPS fix to the snapped position.
            MapPolyline {
                visible: trainDetails.hasSelection && win.selMatch.rawLat !== undefined
                         && win.selMatch.snapLat !== undefined
                line.width: 1.5
                line.color: Theme.accent
                opacity: 0.8
                path: visible
                      ? [QtPositioning.coordinate(win.selMatch.rawLat, win.selMatch.rawLon),
                         QtPositioning.coordinate(win.selMatch.snapLat, win.selMatch.snapLon)]
                      : []
            }

            // The raw (unsnapped) fix as a small hollow ring.
            MapQuickItem {
                visible: trainDetails.hasSelection && win.selMatch.rawLat !== undefined
                coordinate: visible
                            ? QtPositioning.coordinate(win.selMatch.rawLat, win.selMatch.rawLon)
                            : QtPositioning.coordinate(0, 0)
                anchorPoint.x: 6
                anchorPoint.y: 6
                sourceItem: Rectangle {
                    width: 12; height: 12; radius: 6
                    color: "transparent"
                    border.color: Theme.accent
                    border.width: 2
                }
            }

            // Live train layer.
            MapItemView {
                model: trainClient.model
                delegate: TrainMarker {
                    selected: trainDetails.hasSelection && trainDetails.trainNumber === model.trainNumber
                    // Dots only at country scale; reveal the text badges once
                    // zoomed in enough that they no longer collide into a blur.
                    labelsVisible: map.zoomLevel >= 8.0
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
        onLoadTracksRequested: win.requestTrackReload()
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
            matchInfo: win.selMatch
        }
    }
}
