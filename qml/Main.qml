pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtLocation
import QtPositioning
import QtCore

import TrainsOnMap

ApplicationWindow {
    id: win
    visible: true
    width: 1100
    height: 820
    title: qsTr("Trains on Map — Finland (Digitraffic)")
    color: Theme.windowBg

    // Per-style basemap tile-cache directory. Qt's OSM disk cache keys tiles by
    // map-type id only (the light and dark providers are both the one StreetMap
    // type), not by URL — so without separate directories the two styles share a
    // cache and serve each other's tiles after a theme flip (patchy dark/light
    // map).
    // Give each style its own directory to keep them isolated.
    function cacheDirFor(style) {
        // writableLocation() returns a file:// url; the OSM plugin wants a plain
        // absolute path. Strip the scheme (and the leading slash on Windows
        // drive paths: "/C:/…" -> "C:/…").
        let base = "" + StandardPaths.writableLocation(StandardPaths.GenericCacheLocation)
        if (base.startsWith("file://"))
            base = base.substring(7)
        if (base.length > 2 && base.charAt(0) === "/" && base.charAt(2) === ":")
            base = base.substring(1)
        return decodeURIComponent(base) + "/QtLocation/osm-" + style
    }

    // Tier-2 diagnostics for the selected train: { rawLat, rawLon, snapLat,
    // snapLon, offset, tunniste, onRoute }. Polled (the model exposes it via an
    // invokable, not a notifying role) while a train is selected, and drives the
    // route/raw-vs-snapped debug overlay and the detail-panel diagnostics line.
    property var selMatch: ({})
    // Fading breadcrumb trail: the selected train's snapped positions, appended
    // as selMatch is refreshed (below), capped so the trail stays short.
    property var trailPoints: []
    readonly property int maxTrailPoints: 8
    onSelMatchChanged: {
        if (selMatch.snapLat === undefined)
            return
        trailPoints = trailPoints.concat([QtPositioning.coordinate(selMatch.snapLat, selMatch.snapLon)])
                                  .slice(-maxTrailPoints)
    }
    Timer {
        running: trainDetails.hasSelection
        interval: 750
        repeat: true
        triggeredOnStart: true
        onTriggered: win.selMatch = trainClient.model.matchInfoFor(
                         trainDetails.trainNumber, trainDetails.departureDate)
    }
    // Reset/refresh selMatch the instant the selection changes. The Timer above
    // keeps running across a selection change (running stays true), so without
    // this the ring/connector/diagnostics would show the *previous* train's data
    // for up to 750 ms until the next tick (#9).
    Connections {
        target: trainDetails
        function onSelectionChanged() {
            win.trailPoints = []   // new/no selection: don't carry the old train's trail
            win.selMatch = trainDetails.hasSelection
                ? trainClient.model.matchInfoFor(trainDetails.trainNumber,
                                                 trainDetails.departureDate)
                : ({})
            // Pin the selected train's route so it isn't evicted from the route
            // cache (and its overlay/Tier-2 match lost) if the train drops out of
            // the live fleet while still selected (R7). Empty list unpins.
            trackService.pinRoute(trainDetails.hasSelection ? trainDetails.routeStations : [])
        }
        // routeStations resolves asynchronously after the timetable loads, so
        // (re)pin when it arrives or changes for the current selection.
        function onRouteStationsChanged() {
            if (trainDetails.hasSelection)
                trackService.pinRoute(trainDetails.routeStations)
        }
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
        fleet: trainClient       // shares its station code -> name map (one fetch)
    }

    StationBoardService {
        id: stationBoard
        fleet: trainClient       // reuses the station code -> name map for destinations
    }

    // Optional weather overlay (FMI open data, opendata.fmi.fi). Idle until the
    // sidebar toggle turns it on.
    FmiWeatherClient {
        id: fmiWeather
        active: panel.showWeather
    }

    // ---- Map ---------------------------------------------------------------
    // The Esri Gray Canvas basemap (light / dark) is chosen by Theme. The osm
    // plugin only resolves its providers at construction, so a theme flip
    // rebuilds the Plugin + Map via this Loader; the view (centre/zoom) is
    // preserved in the loader's saved* properties across the reload.
    Loader {
        id: mapLoader
        anchors.fill: parent
        sourceComponent: mapComponent

        // The opening view, and separately the live one. They have to be separate
        // properties: QtLocation's Map emits zoomLevelChanged/centerChanged during
        // its own construction, which ran *before* Component.onCompleted read the
        // saved values back — so a single set of properties was overwritten with
        // the map's defaults and the intended first-run view never happened (the
        // app opened at ~zoom 11 on a bare Helsinki suburb instead of the country).
        // `everBuilt` is what tells a cold start from the theme-driven rebuild the
        // saved values exist for.
        readonly property real initialCenterLat: 61.00   // southern Finland on first launch
        readonly property real initialCenterLon: 24.50
        readonly property real initialZoom: 7.0
        property bool everBuilt: false

        property real savedCenterLat: initialCenterLat
        property real savedCenterLon: initialCenterLon
        property real savedZoom: initialZoom

        // Rebuild the map when the resolved light/dark state changes so the
        // tiles re-fetch from the matching Esri service.
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
                // Point the OSM plugin at our own single-provider repository
                // rather than osm.mapping.custom.host. custom.host cannot express
                // this tile source: Qt builds the URL by naive concatenation,
                // appending "%z/%x/%y.png" to whatever the host string is, so
                // there is no way to get Esri's z/y/x order, its extensionless
                // path, or a trailing query string (a "?key=" would land in the
                // middle of the path). A repository manifest carries a full
                // UrlTemplate with %z/%y/%x placeholders and has none of those
                // limits. The manifest is embedded (qrc:), so nothing is fetched
                // over the network to resolve the provider.
                PluginParameter {
                    name: "osm.mapping.providersrepository.address"
                    value: Theme.basemapRepo
                }
                PluginParameter {
                    // Isolate the disk cache per basemap style — see cacheDirFor().
                    name: "osm.mapping.cache.directory"
                    value: win.cacheDirFor(Theme.basemapStyle)
                }
                // Note: no osm.mapping.highdpi_tiles here. With it enabled Qt
                // looks for a "street-hires" manifest instead of "street", and
                // Esri's MapServer has no @2x endpoint to point one at.
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

            // Ground resolution (metres/px) at this latitude and the map's current
            // zoom — standard spherical-Mercator formula for 256 px tiles. Used to
            // turn a screen-space declutter radius into a real-world one.
            function metersPerPixel(lat) {
                return 156543.03392 * Math.cos(lat * Math.PI / 180) / Math.pow(2, zoomLevel)
            }

            // The repository's "street" provider registers as StreetMap (not
            // CustomMap, which is what osm.mapping.custom.host used to produce);
            // activate it.
            function selectBasemap() {
                for (let i = 0; i < supportedMapTypes.length; ++i) {
                    if (supportedMapTypes[i].style === MapType.StreetMap) {
                        activeMapType = supportedMapTypes[i];
                        return;
                    }
                }
            }
            onSupportedMapTypesChanged: selectBasemap()
            Component.onCompleted: {
                // Imperative (non-binding) restore of the saved view, so panning
                // isn't fought by a center/zoomLevel binding.
                const restore = mapLoader.everBuilt   // a rebuild, not a cold start
                center = QtPositioning.coordinate(
                    restore ? mapLoader.savedCenterLat : mapLoader.initialCenterLat,
                    restore ? mapLoader.savedCenterLon : mapLoader.initialCenterLon)
                zoomLevel = restore ? mapLoader.savedZoom : mapLoader.initialZoom
                mapLoader.everBuilt = true
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

            // Track geometry layer (drawn beneath the trains).
            MapItemView {
                model: trackService.model
                delegate: MapPolyline {
                    required property var model
                    // Style by line category: running lines (paaraide) read as the
                    // network; sidings/yards recede as thinner, dimmer strands.
                    // Sidings can be hidden entirely via the legend toggle.
                    visible: model.mainTrack || panel.showSidings
                    line.width: model.mainTrack ? 2.2 : 1.3
                    line.color: model.mainTrack ? Theme.railColor : Theme.railSidingColor
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

            // Fading breadcrumb trail of the selected train's recent matched
            // positions (win.trailPoints, appended from the same 750 ms poll that
            // drives the route/connector overlay above).
            MapPolyline {
                visible: win.trailPoints.length > 1
                line.width: 3
                line.color: Theme.accent
                opacity: 0.35
                path: win.trailPoints
            }

            // Optional weather overlay (FMI): air-temperature chips, shown only
            // when the layer is enabled (model is empty otherwise) and zoomed in.
            MapItemView {
                model: fmiWeather.model
                delegate: MapQuickItem {
                    required property var model
                    visible: map.zoomLevel >= 8.0
                    coordinate: model.coordinate
                    anchorPoint.x: chip.width / 2
                    anchorPoint.y: chip.height / 2
                    sourceItem: Rectangle {
                        id: chip
                        radius: 3
                        color: Theme.cardBg
                        border.width: 1
                        border.color: model.tempC < 0 ? Theme.weatherCold : Theme.weatherWarm
                        implicitWidth: chipText.implicitWidth + 8
                        implicitHeight: chipText.implicitHeight + 3
                        Text {
                            id: chipText
                            anchors.centerIn: parent
                            text: model.tempText
                            font.pointSize: TypeScale.caption
                            color: Theme.textStrong
                        }
                    }
                }
            }

            // Clickable passenger-station layer (under the trains). Dots appear
            // once zoomed in enough to pick one out; clicking opens its board.
            MapItemView {
                model: trainClient.stations
                delegate: MapQuickItem {
                    required property var model
                    visible: map.zoomLevel >= 9.0
                    coordinate: model.coordinate
                    anchorPoint.x: 5
                    anchorPoint.y: 5
                    sourceItem: Rectangle {
                        width: 10; height: 10; radius: 5
                        color: Theme.cardBg
                        border.color: Theme.accent
                        border.width: 2
                        MouseArea {
                            anchors.fill: parent
                            // U2-W7: 10 px dot + 7 px per side = 24×24, the
                            // minimum WCAG 2.2 2.5.8 (AA) asks of a target.
                            anchors.margins: -7
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                stationBoard.show(model.code, model.name)
                                trainDetails.clear()   // right panel shows one thing
                            }
                        }
                    }
                }
            }

            // Live train layer.
            MapItemView {
                model: trainClient.model
                delegate: TrainMarker {
                    visible: panel.categoryVisible(model.category)
                    selected: trainDetails.hasSelection && trainDetails.trainNumber === model.trainNumber
                    // Dots only at country scale; reveal the text badges once
                    // zoomed in enough that they no longer collide into a blur.
                    // Density check on top: a terminus (e.g. Helsinki) can have
                    // several trains within a couple hundred metres of each other
                    // even at a zoom where labels are otherwise legible, so also
                    // drop to a dot if the nearest other train is closer than
                    // ~30 px on screen (nearestNeighborMeters is precomputed once
                    // per poll in TrainListModel; ponytail: 30 px is a tuned
                    // heuristic, not derived from the badge's actual size).
                    labelsVisible: map.zoomLevel >= 8.0
                                   && (model.nearestNeighborMeters < 0
                                       || model.nearestNeighborMeters
                                          > 30 * map.metersPerPixel(model.coordinate.latitude))
                    onClicked: (trainNumber, departureDate) => {
                        trainDetails.show(trainNumber, departureDate)
                        stationBoard.clear()   // right panel shows one thing
                    }
                }
            }

            // ---- Pan / zoom input (Qt 6 needs explicit handlers) --------------
            // Drag to pan, wheel/trackpad to zoom, pinch to zoom; the map stays
            // north-up (no rotation handler). Keyboard +/- zoom in steps too, and
            // arrow keys pan (W4 — markers themselves stay mouse-only, but the map
            // view is now fully keyboard-reachable).
            focus: true
            Keys.onPressed: (event) => {
                const step = 80   // px per key-repeat, matches a comfortable drag
                switch (event.key) {
                case Qt.Key_Left:  map.pan(-step, 0); event.accepted = true; break
                case Qt.Key_Right: map.pan(step, 0);  event.accepted = true; break
                case Qt.Key_Up:    map.pan(0, -step); event.accepted = true; break
                case Qt.Key_Down:  map.pan(0, step);  event.accepted = true; break
                }
            }
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
                sequences: [StandardKey.ZoomIn]
                onActivated: map.zoomLevel = Math.round(map.zoomLevel + 1)
            }
            Shortcut {
                enabled: map.zoomLevel > map.minimumZoomLevel
                sequences: [StandardKey.ZoomOut]
                onActivated: map.zoomLevel = Math.round(map.zoomLevel - 1)
            }
        }
    }

    // ---- Overlay UI --------------------------------------------------------

    // U2-O2: with an empty fleet, a dead network or an MQTT that never connected,
    // the map is blank tiles and the only signal was a muted line inside the
    // sidebar card. "Graceful Failure" wants the fallback stated where the user is
    // actually looking. The body reuses the client's own status string rather than
    // inventing a second error vocabulary that could disagree with the sidebar.
    Rectangle {
        id: emptyState
        // Only once a fetch has settled: `loading` covers the first poll, so this
        // never flashes over a map that is simply still filling.
        visible: trainClient.model.count === 0 && !trainClient.loading
        anchors.centerIn: parent
        width: Math.min(320, parent.width - 24)
        implicitHeight: emptyStateText.implicitHeight + 32
        height: implicitHeight
        radius: 12
        color: Theme.cardBg
        border.color: Theme.hairline
        border.width: 1

        Accessible.role: Accessible.AlertMessage
        Accessible.name: emptyStateTitle.text
        Accessible.description: emptyStateBody.text

        // Column, not ColumnLayout: this file has no QtQuick.Layouts import and two
        // stacked labels do not need one.
        Column {
            id: emptyStateText
            anchors.centerIn: parent
            width: parent.width - 32
            spacing: 6

            Label {
                id: emptyStateTitle
                width: parent.width
                text: qsTr("No trains to show")
                font.pointSize: TypeScale.subhead
                font.bold: true
                color: Theme.textStrong
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
            Label {
                id: emptyStateBody
                width: parent.width
                text: trainClient.status.length > 0
                      ? trainClient.status
                      : qsTr("Nothing is running right now. Use Refresh in the sidebar to try again.")
                font.pointSize: TypeScale.caption
                color: Theme.textMuted
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }
    }

    InfoPanel {
        id: panel
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12

        trainCount: trainClient.model.count
        trackCount: trackService.model.count
        refreshing: trainClient.loading
        streamConnected: trainStream.connected
        streamStatus: trainStream.status
        punctuality: trainClient.punctuality
        statusText: trackService.status.length > 0 ? trackService.status : trainClient.status
        mapZoom: mapLoader.savedZoom   // tracks the live zoom via onZoomLevelChanged

        onRefreshRequested: trainClient.refresh()
    }

    // Keyboard/assistive path to the app's primary action, and the only way to
    // *find* a named train (U2-C1 / U2-O1). Shares the sidebar's category
    // toggles so the list and the map always show the same fleet.
    TrainFilterModel {
        id: fleetFilter
        sourceModel: trainClient.model
        showCommuter: panel.showCommuter
        showLongDistance: panel.showLongDistance
        showCargo: panel.showCargo
    }

    TrainListPanel {
        id: trainListPanel
        anchors.left: parent.left
        anchors.top: panel.bottom
        anchors.leftMargin: 12
        anchors.topMargin: 10
        // No bottom anchor. Binding one to `undefined` does *not* clear it — the
        // card stayed full height — so the height is explicit instead: its own
        // header when collapsed, otherwise down to 12 px off the window bottom.
        // Same shape as InfoPanel's height cap.
        height: trainListPanel.collapsed
                ? trainListPanel.implicitHeight
                : Math.max(0, parent.height - trainListPanel.y - 12)

        model: fleetFilter
        totalCount: trainClient.model.count

        onTrainActivated: (trainNumber, departureDate, coordinate) => {
            trainDetails.show(trainNumber, departureDate)
            stationBoard.clear()          // the right slot shows one thing
            // Selecting a train you can't see is disorienting, so bring it into
            // view — the list is usable at any zoom, including the ones where
            // markers are unlabelled dots.
            if (mapLoader.item && coordinate && coordinate.isValid)
                mapLoader.item.center = coordinate
        }
    }

    // Timetable detail panel — only built once a train is picked (still true:
    // `everShown` latches true on first use so the item then persists, hidden,
    // instead of being torn down — that's what lets the close fade actually play
    // rather than popping the panel out instantly).
    Loader {
        id: detailPanelLoader
        property bool everShown: false
        active: trainDetails.hasSelection || everShown
        onActiveChanged: if (active) everShown = true
        opacity: trainDetails.hasSelection ? 1 : 0
        visible: opacity > 0
        // U2-W4: focus enters the panel when it opens, so Tab reaches its close
        // button instead of restarting at the top of the window — and goes back
        // to the train list when it closes, rather than nowhere.
        onVisibleChanged: {
            const t = visible ? (item as Item) : trainListPanel
            if (t)
                t.forceActiveFocus()
        }
        Behavior on opacity {
            enabled: !Theme.reducedMotion
            NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
        }
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

    // Station board — same right-side slot as the train detail panel; the two are
    // mutually exclusive (selecting a train clears the station board and vice
    // versa), so they never overlap. Same lazy-build-then-fade treatment as above.
    Loader {
        id: stationBoardLoader
        property bool everShown: false
        active: stationBoard.hasSelection || everShown
        onActiveChanged: if (active) everShown = true
        opacity: stationBoard.hasSelection ? 1 : 0
        visible: opacity > 0
        // U2-W4: same focus entry/return as the detail panel above.
        onVisibleChanged: {
            const t = visible ? (item as Item) : trainListPanel
            if (t)
                t.forceActiveFocus()
        }
        Behavior on opacity {
            enabled: !Theme.reducedMotion
            NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
        }
        width: 320
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 12
        sourceComponent: StationBoardPanel {
            service: stationBoard
        }
    }
}
