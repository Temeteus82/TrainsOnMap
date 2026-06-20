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
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("TrainsOnMap", "Main");

    return app.exec();
}
