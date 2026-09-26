#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
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
