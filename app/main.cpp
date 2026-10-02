#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("HandWave"));

    QQmlApplicationEngine engine;
    // The HandWave QML module (Main.qml plus the C++ types) is compiled into
    // the executable's resources under qrc:/HandWave.
    engine.addImportPath(QStringLiteral("qrc:/"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/HandWave/Main.qml")));

    return app.exec();
}
