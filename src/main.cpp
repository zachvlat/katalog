#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include "backend.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("QuteStore"));
    app.setOrganizationName(QStringLiteral("io.github.zachvlat"));
    app.setApplicationDisplayName(QStringLiteral("Qute Store"));
    app.setDesktopFileName(QStringLiteral("io.github.zachvlat.qutestore"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("io.github.zachvlat.qutestore")));

    QQmlApplicationEngine engine;
    Backend backend;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    const QUrl url(QStringLiteral("qrc:/qml/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
                         if (!obj && url == objUrl)
                             QCoreApplication::exit(-1);
                     }, Qt::QueuedConnection);
    engine.load(url);
    return app.exec();
}
