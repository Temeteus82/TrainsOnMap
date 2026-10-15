#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

#include <QMapLibre/Utils>

int main(int argc, char *argv[])
{
    // MapLibre renders with the one backend it was built for (OpenGL, Vulkan or
    // Metal), so Qt Quick must use the same API. RendererType values match
    // QSGRendererInterface::GraphicsApi. Must run before the QGuiApplication.
    QQuickWindow::setGraphicsApi(
        static_cast<QSGRendererInterface::GraphicsApi>(QMapLibre::supportedRendererType()));

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("TrainsOnMap"));
    QGuiApplication::setOrganizationName(QStringLiteral("TrainsOnMap"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/appicon.png")));

    QQmlApplicationEngine engine;
#ifdef TRAINSONMAP_MAPLIBRE_PREFIX
    // Linux build-tree run: MapLibre lives in its own prefix, not in Qt's.
    QCoreApplication::addLibraryPath(QStringLiteral(TRAINSONMAP_MAPLIBRE_PREFIX "/plugins"));
    engine.addImportPath(QStringLiteral(TRAINSONMAP_MAPLIBRE_PREFIX "/qml"));
#endif
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("TrainsOnMap", "Main");

    return app.exec();
}
