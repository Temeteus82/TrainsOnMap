# 3.22 for the tests' ENVIRONMENT_MODIFICATION property (see the Tests section).
cmake_minimum_required(VERSION 3.22)

project(TrainsOnMap
    VERSION 0.1.0
    DESCRIPTION "Live Finnish train positions and track geometry on a Qt6 map"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Windows.h defines min/max as macros, which then eat any unparenthesised
# std::min/std::max in a TU that pulls it in (directly or through a Qt private
# header). Suppressing the macros once here covers every call site and anything
# added later — the alternative is remembering to write (std::min)(a, b) forever.
# WIN32_LEAN_AND_MEAN comes along for the ride: it drops winsock1, which Qt's
# networking would otherwise clash with.
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Floor is 6.10, not QtLocation/QtPositioning's 6.5+: TimetableModel uses
# QRangeModel (QtCore, since 6.10).
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Qml Quick Network Positioning Location WebSockets Concurrent Test)

qt_standard_project_setup(REQUIRES 6.10)

# QML files live in a subdirectory; opt in to the modern qmldir handling so a
# qmldir is generated for each QML dir (QTP0004, Qt 6.8+). Guard on the Qt
# policy var — if(POLICY ...) only matches CMake policies, never Qt ones.
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_executable(TrainsOnMap
    src/main.cpp
    src/DigitrafficClient.h     src/DigitrafficClient.cpp
    src/TrainListModel.h        src/TrainListModel.cpp
    src/TrackService.h          src/TrackService.cpp
    src/RailGraph.h             src/RailGraph.cpp
    src/TrackListModel.h        src/TrackListModel.cpp
    src/TrainDetailsService.h   src/TrainDetailsService.cpp
    src/TimetableModel.h        src/TimetableModel.cpp
    src/TimetableFilterModel.h  src/TimetableFilterModel.cpp
    src/TrainFilterModel.h      src/TrainFilterModel.cpp
    src/CompositionModel.h      src/CompositionModel.cpp
    src/DigitrafficMqttClient.h src/DigitrafficMqttClient.cpp
    src/StationListModel.h      src/StationListModel.cpp
    src/StationBoardModel.h     src/StationBoardModel.cpp
    src/StationBoardService.h   src/StationBoardService.cpp
    src/WeatherStationModel.h   src/WeatherStationModel.cpp
    src/FmiWeatherClient.h      src/FmiWeatherClient.cpp
    src/Projection.h
    src/MqttCodec.h
    src/DigitrafficFormat.h
    src/NetworkDiagnostics.h
)

# The theme palette and the type scale are QML singletons (one shared instance
# app-wide).
set_source_files_properties(qml/Theme.qml     PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/TypeScale.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(TrainsOnMap
    URI TrainsOnMap
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/TrainMarker.qml
        qml/InfoPanel.qml
        qml/TrainDetailPanel.qml
        qml/StationBoardPanel.qml
        qml/TrainListPanel.qml
        qml/ToggleRow.qml
        qml/Theme.qml
        qml/TypeScale.qml
        qml/AppIcon.qml
)

# Pre-baked national rail network (scripts/bake_rails.py), embedded so the app
# never fetches track geometry over the network at launch. Already compressed
# (qCompress container), loaded at startup by TrackService.
qt_add_resources(TrainsOnMap "raildata"
    PREFIX "/data"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    FILES resources/rails.geojson.qz)

# Application icon, embedded for the runtime window/taskbar icon (set via
# QGuiApplication::setWindowIcon in main.cpp -> ":/appicon.png"). Regenerate the
# raster assets from the SVG master with scripts/make_icons.sh.
qt_add_resources(TrainsOnMap "appicon"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon"
    FILES resources/icon/appicon.png)

# Windows: embed the multi-size .ico as the executable's shell icon. The .rc is
# configured with an absolute path so windres resolves it from any working dir.
if(WIN32)
    set(TRAINSONMAP_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/appicon.ico")
    configure_file(resources/icon/appicon.rc.in
                   "${CMAKE_CURRENT_BINARY_DIR}/appicon.rc" @ONLY)
    target_sources(TrainsOnMap PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/appicon.rc")
endif()

# macOS: ship the .icns as a bundle resource (regenerate from the SVG master with
# scripts/make_icns.sh) and point the generated Info.plist at it. The bundle
# metadata (identifier/version/copyright) below feeds Qt's default Info.plist
# template; MACOSX_BUNDLE is enabled on the target so an .app is produced.
if(APPLE)
    set(MACOSX_BUNDLE_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/appicon.icns")
    set_source_files_properties("${MACOSX_BUNDLE_ICNS}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(TrainsOnMap PRIVATE "${MACOSX_BUNDLE_ICNS}")
endif()

# So the generated QML type-registration unit can find the class headers
# (it probes them via __has_include(<Header.h>)).
target_include_directories(TrainsOnMap PRIVATE src)

target_link_libraries(TrainsOnMap PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Network
    Qt6::Positioning
    Qt6::Location
    Qt6::WebSockets
    Qt6::Concurrent
)

# Place the executable under build/bin/. This also avoids a name clash on
# case-sensitive/extension-less platforms (Linux, macOS): qt_add_qml_module
# creates a "TrainsOnMap" *directory* in the build root for the QML module, which
# would collide with an extension-less "TrainsOnMap" executable emitted there.
# (Windows is unaffected — it emits TrainsOnMap.exe.)
set_target_properties(TrainsOnMap PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    # GUI-subsystem app in Release (no stray console window on launch); Debug
    # keeps the console attached so qDebug/log output is visible while developing.
    WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>
    # macOS: build a relocatable TrainsOnMap.app. MACOSX_BUNDLE is a no-op off Apple,
    # so plain ON is safe cross-platform (a genex here would defeat TARGET_BUNDLE_DIR).
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "TrainsOnMap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.trainsonmap.TrainsOnMap"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "appicon.icns"
    MACOSX_BUNDLE_COPYRIGHT "© 2025 TrainsOnMap"
)

# On Windows, copy every Qt runtime DLL and plugin the app needs (Qt core libs,
# the "windows" platform plugin, the TLS backend for the wss:// MQTT feed, and
# the QML / QtLocation geoservices / QtPositioning plugins) next to the .exe in
# bin/ as a post-build step, so the freshly built app runs without a Qt on PATH.
# windeployqt scans the QML sources (--qmldir) to resolve the QML import plugins.
if(WIN32)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET TrainsOnMap POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --compiler-runtime
                    --qmldir "${CMAKE_SOURCE_DIR}/qml"
                    "$<TARGET_FILE:TrainsOnMap>"
            COMMENT "windeployqt: bundling Qt DLLs and QML/geoservices/TLS plugins next to the .exe"
            VERBATIM)
    else()
        message(WARNING "windeployqt not found near ${_qt_bin_dir}; "
                        "the built .exe will need Qt DLLs on PATH to run.")
    endif()
endif()

# On macOS, copy the Qt frameworks and plugins the app needs (QML imports, the
# QtLocation geoservices + QtPositioning plugins, the TLS backend for the wss://
# MQTT feed) into TrainsOnMap.app/Contents/{Frameworks,PlugIns} as a post-build
# step, so the bundle runs standalone without a Qt install. macdeployqt scans the
# QML sources (-qmldir) to resolve the QML import plugins.
if(APPLE)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${_qt_bin_dir}")
    if(MACDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET TrainsOnMap POST_BUILD
            COMMAND "${MACDEPLOYQT_EXECUTABLE}"
                    "$<TARGET_BUNDLE_DIR:TrainsOnMap>"
                    "-qmldir=${CMAKE_SOURCE_DIR}/qml"
                    -no-strip
            COMMENT "macdeployqt: bundling Qt frameworks and QML/geoservices/TLS plugins into the .app"
            VERBATIM)
    else()
        message(WARNING "macdeployqt not found near ${_qt_bin_dir}; "
                        "the built .app will need a Qt install to run.")
    endif()

    # `cmake --build --preset macos-clang --target dmg` wraps the deployed bundle
    # into a distributable TrainsOnMap-<version>.dmg (with the usual drag-to-
    # /Applications layout). Depends on the app target so the bundle is built and
    # macdeployqt has run first. Not part of ALL — invoke it explicitly.
    set(TRAINSONMAP_DMG "${CMAKE_BINARY_DIR}/bin/TrainsOnMap-${PROJECT_VERSION}.dmg")
    add_custom_target(dmg
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/dmg-staging"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/dmg-staging"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "$<TARGET_BUNDLE_DIR:TrainsOnMap>"
                "${CMAKE_BINARY_DIR}/dmg-staging/TrainsOnMap.app"
        COMMAND ${CMAKE_COMMAND} -E create_symlink /Applications
                "${CMAKE_BINARY_DIR}/dmg-staging/Applications"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${TRAINSONMAP_DMG}"
        COMMAND hdiutil create -volname "TrainsOnMap"
                -srcfolder "${CMAKE_BINARY_DIR}/dmg-staging"
                -ov -format UDZO "${TRAINSONMAP_DMG}"
        DEPENDS TrainsOnMap
        COMMENT "Packaging ${TRAINSONMAP_DMG}"
        VERBATIM)
endif()

install(TARGETS TrainsOnMap
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin)

# ---- Tests (Tier-2 routing core) ---------------------------------------------
# RailGraph is pure (QtCore + QtPositioning), so it's unit-tested directly with
# QtTest — both on a synthetic fixture network and a smoke test over the real
# baked blob. Run with: ctest --test-dir build/<preset> --output-on-failure
enable_testing()
qt_add_executable(tst_railgraph
    tests/tst_railgraph.cpp
    src/RailGraph.h src/RailGraph.cpp
    src/Projection.h)
target_include_directories(tst_railgraph PRIVATE src)
target_link_libraries(tst_railgraph PRIVATE Qt6::Core Qt6::Positioning Qt6::Test)
target_compile_definitions(tst_railgraph PRIVATE
    RAILS_BLOB="${CMAKE_CURRENT_SOURCE_DIR}/resources/rails.geojson.qz")
add_test(NAME railgraph COMMAND tst_railgraph)

# TimetableModel is a QRangeModel subclass (Qt 6.10+); this pins the role table
# auto-derived from TimetableStop's Q_PROPERTYs to the names the QML detail panel
# binds to, plus the setStops()/clear()/count contract. Links Qt6::Qml for the
# QtQmlIntegration header (the QML_ELEMENT macros in TimetableModel.h).
qt_add_executable(tst_timetablemodel
    tests/tst_timetablemodel.cpp
    src/TimetableModel.h src/TimetableModel.cpp)
target_include_directories(tst_timetablemodel PRIVATE src)
target_link_libraries(tst_timetablemodel PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
add_test(NAME timetablemodel COMMAND tst_timetablemodel)

# TimetableFilterModel is the proxy that makes the timetable ListView virtualise by
# dropping passing points from the row set. Pinned against the real TimetableModel
# so the cached "stopping" role stays tied to the QRangeModel-derived role table.
qt_add_executable(tst_timetablefilter
    tests/tst_timetablefilter.cpp
    src/TimetableFilterModel.h src/TimetableFilterModel.cpp
    src/TimetableModel.h src/TimetableModel.cpp)
target_include_directories(tst_timetablefilter PRIVATE src)
target_link_libraries(tst_timetablefilter PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
add_test(NAME timetablefilter COMMAND tst_timetablefilter)

# TrainFilterModel backs the sidebar train list — the keyboard/assistive path to
# selecting a train (UI audit U2-C1). Pinned against the real TrainListModel: the
# proxy resolves its role numbers from the source's roleNames(), so a stub with a
# different role table would exercise the wrong code. TrainListModel pulls in
# TrackService (the matcher) and thus RailGraph, even though the filter never
# matches a fix.
qt_add_executable(tst_trainfilter
    tests/tst_trainfilter.cpp
    src/TrainFilterModel.h src/TrainFilterModel.cpp
    src/TrainListModel.h src/TrainListModel.cpp
    src/TrackService.h src/TrackService.cpp
    src/TrackListModel.h src/TrackListModel.cpp
    src/RailGraph.h src/RailGraph.cpp)
target_include_directories(tst_trainfilter PRIVATE src)
target_link_libraries(tst_trainfilter PRIVATE
    Qt6::Core Qt6::Qml Qt6::Positioning Qt6::Concurrent Qt6::Test)
add_test(NAME trainfilter COMMAND tst_trainfilter)

# CompositionModel is the same QRangeModel idiom (Qt 6.10+) for the carriage strip;
# this pins the role table auto-derived from CompositionVehicle's Q_PROPERTYs — the
# QStringList `amenities` role included — to the names the QML carriage delegate binds
# to, plus the setVehicles()/clear()/count contract.
qt_add_executable(tst_compositionmodel
    tests/tst_compositionmodel.cpp
    src/CompositionModel.h src/CompositionModel.cpp)
target_include_directories(tst_compositionmodel PRIVATE src)
target_link_libraries(tst_compositionmodel PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
add_test(NAME compositionmodel COMMAND tst_compositionmodel)

# DigitrafficFormat is header-only and pure QtCore — the timestamp and delay-cause
# formatting the station board and the train detail panel both render. Pinned
# because these started as per-service copies that silently drifted apart.
qt_add_executable(tst_digitrafficformat
    tests/tst_digitrafficformat.cpp
    src/DigitrafficFormat.h)
target_include_directories(tst_digitrafficformat PRIVATE src)
target_link_libraries(tst_digitrafficformat PRIVATE Qt6::Core Qt6::Test)
add_test(NAME digitrafficformat COMMAND tst_digitrafficformat)

# Only the app gets windeployqt'd, so on Windows the test exes have no Qt DLLs
# beside them and ctest fails them all with 0xc0000135 unless the kit happens to
# be on PATH. Prepend it for every registered test rather than deploying each
# one; reading the DIRECTORY TESTS property covers tests added later too.
if(WIN32)
    file(TO_NATIVE_PATH "${_qt_bin_dir}" _qt_bin_dir_native)
    get_property(_all_tests DIRECTORY PROPERTY TESTS)
    set_tests_properties(${_all_tests} PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir_native}")
endif()
