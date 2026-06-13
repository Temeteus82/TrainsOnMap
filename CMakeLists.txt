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
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick Network Positioning Location WebSockets)

qt_standard_project_setup(REQUIRES 6.5)

# QML files live in a subdirectory; opt in to the modern qmldir handling.
if(POLICY QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_executable(TrainsOnMap
    src/main.cpp
    src/DigitrafficClient.h     src/DigitrafficClient.cpp
    src/TrainListModel.h        src/TrainListModel.cpp
    src/TrackService.h          src/TrackService.cpp
    src/TrackListModel.h        src/TrackListModel.cpp
    src/TrainDetailsService.h   src/TrainDetailsService.cpp
    src/TimetableModel.h        src/TimetableModel.cpp
    src/DigitrafficMqttClient.h src/DigitrafficMqttClient.cpp
    src/Projection.h
    src/MqttCodec.h
)

qt_add_qml_module(TrainsOnMap
    URI TrainsOnMap
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/TrainMarker.qml
        qml/InfoPanel.qml
        qml/TrainDetailPanel.qml
)

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
)

# Place the executable in a predictable spot.
set_target_properties(TrainsOnMap PROPERTIES
    WIN32_EXECUTABLE OFF        # keep the console attached for log output during development
    MACOSX_BUNDLE OFF
)

install(TARGETS TrainsOnMap
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin)
