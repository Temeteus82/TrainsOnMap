cmake_minimum_required(VERSION 3.21)

project(TrainsOnMap
    VERSION 0.1.0
    DESCRIPTION "Live Finnish train positions and track geometry on a Qt6 map"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# QtLocation/QtPositioning require 6.5+. Qml/Quick give us the Qt Quick map UI;
# Network is used by the Digitraffic REST/GeoJSON clients.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick Network Positioning Location WebSockets Concurrent Test)

qt_standard_project_setup(REQUIRES 6.5)

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
    src/TrackMatcher.h
    src/RailGraph.h             src/RailGraph.cpp
    src/TrackListModel.h        src/TrackListModel.cpp
    src/TrainDetailsService.h   src/TrainDetailsService.cpp
    src/TimetableModel.h        src/TimetableModel.cpp
    src/DigitrafficMqttClient.h src/DigitrafficMqttClient.cpp
    src/Projection.h
    src/MqttCodec.h
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
    MACOSX_BUNDLE OFF
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

install(TARGETS TrainsOnMap
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin)

# ---- Tests (Tier-2 routing core) ---------------------------------------------
# RailGraph is pure (QtCore + QtPositioning), so it's unit-tested directly with
# QtTest — both on a synthetic fixture network and a smoke test over the real
# baked blob. Run with: ctest --test-dir build/<preset> --output-on-failure
# (the test exe needs the Qt kit's bin on PATH for its DLLs).
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
