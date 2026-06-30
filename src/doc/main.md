# main.cpp — Application Entry Point

## A. Overview

`main.cpp` is the startup sequence for TrainsOnMap, a Qt 6 desktop application
that shows live Finnish train positions and rail-track geometry on a Qt Quick
map. The file is deliberately minimal: it creates the GUI application object, sets
application identity and the window icon, loads the root QML module, and starts
the event loop. **All** of the application's logic — the backend services, the
map, the panels — is declared in QML (`qml/Main.qml`) and in the C++ types
registered into the `TrainsOnMap` QML module; `main.cpp` only bootstraps that.

## B. Qt Application Setup

A **`QGuiApplication`** is instantiated (not `QApplication` — the UI is pure Qt
Quick with no Qt Widgets). Before the event loop starts it sets:

- `setApplicationName("TrainsOnMap")` and `setOrganizationName("TrainsOnMap")` —
  identity used by Qt for paths such as `QStandardPaths` (the QML basemap
  tile-cache directory derives from the generic cache location).
- `setWindowIcon(QIcon(":/appicon.png"))` — the runtime window/taskbar icon,
  loaded from the embedded `appicon` resource.

No high-DPI attributes are set explicitly (Qt 6 enables high-DPI scaling by
default).

## C. Command-Line Handling

None. The application takes no command-line options; `argc`/`argv` are passed to
`QGuiApplication` solely for Qt's own standard argument handling.

## D. Top-Level Object Creation

Only one C++ object is created here:

- **`QQmlApplicationEngine engine`** — the QML runtime that loads and executes the
  UI. Every other top-level object (the backend services `DigitrafficClient`,
  `DigitrafficMqttClient`, `TrackService`, `TrainDetailsService`; the map; the
  panels) is created **declaratively from QML** in `Main.qml`, not in C++. Those
  C++ types are reachable from QML because each is registered into the
  `TrainsOnMap` module via `QML_ELEMENT` and built by `qt_add_qml_module`.

## E. Wiring and Connections

- The engine's `objectCreationFailed` signal is connected to a lambda that calls
  `QCoreApplication::exit(-1)`, using a **queued** connection so the failure is
  handled cleanly from the event loop. This makes a QML load/instantiation failure
  terminate the process with a non-zero status instead of running a broken UI.
- `engine.loadFromModule("TrainsOnMap", "Main")` loads the `Main` component from
  the registered QML module — the modern module-based load (no file path / `qrc`
  URL). This is where `Main.qml` constructs and wires the backend services to the
  map and panels.

## F. Event Loop

The event loop is started with `app.exec()`, whose return value is returned from
`main()`. A normal quit returns 0; a QML object-creation failure routes through
the queued `exit(-1)` above.

## G. Dependencies

| Include | Provides |
|---------|----------|
| `<QGuiApplication>` | The GUI application object and event loop (`exec`), plus the application-name/icon setters. Qt6::Gui. |
| `<QIcon>` | The `QIcon` used for the window icon, loaded from the embedded resource. Qt6::Gui. |
| `<QQmlApplicationEngine>` | Loads and runs the QML UI; the `objectCreationFailed` signal and `loadFromModule`. Qt6::Qml. |

The bulk of the program's behaviour lives behind these few includes, in the QML
module the engine loads — see the per-class docs for the services that `Main.qml`
instantiates: `DigitrafficClient`, `DigitrafficMqttClient`, `TrackService`,
`TrainDetailsService`.
