#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#ifdef HAVE_KICONTHEMES
#include <KIconTheme>
#endif

#ifdef HAVE_WEBENGINE
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#endif

#include "backend.h"

int main(int argc, char *argv[])
{
#ifdef HAVE_KICONTHEMES
    KIconTheme::initTheme();
#endif
#ifdef HAVE_WEBENGINE
    QtWebEngineQuick::initialize();
#endif

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("QuteStore"));
    app.setOrganizationName(QStringLiteral("io.github.zachvlat"));
    app.setApplicationDisplayName(QStringLiteral("Katalog"));
    app.setDesktopFileName(QStringLiteral("io.github.zachvlat.qutestore"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("io.github.zachvlat.qutestore")));

    QApplication::setStyle(QStringLiteral("breeze"));
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

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
