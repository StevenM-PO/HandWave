#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("HandWave"));

    // Developer options, for checking the UI without touching the screen.
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption evalOption(QStringLiteral("eval"),
        QStringLiteral("Run a QML/JS expression in Main.qml's scope at startup "
                       "(e.g. \"pages.currentIndex = 1\")."),
        QStringLiteral("expression"));
    QCommandLineOption grabOption(QStringLiteral("grab"),
        QStringLiteral("Render the window off-screen to an image file, then exit."),
        QStringLiteral("file"));
    parser.addOptions({evalOption, grabOption});
    parser.process(app);

    QQmlApplicationEngine engine;
    // The HandWave QML module (QML files plus the C++ types) is compiled into
    // the executable's resources under qrc:/HandWave.
    engine.addImportPath(QStringLiteral("qrc:/"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/HandWave/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (parser.isSet(grabOption) && window)
        window->setVisible(false); // grabWindow() renders hidden windows off-screen

    if (parser.isSet(evalOption)) {
        QQmlExpression expression(qmlContext(window), window, parser.value(evalOption));
        expression.evaluate();
        if (expression.hasError())
            qWarning().noquote() << "--eval:" << expression.error().toString();
    }

    if (parser.isSet(grabOption) && window) {
        const QString file = parser.value(grabOption);
        // Give the drawing hand-offs (16 ms timers) a moment to settle first.
        QTimer::singleShot(500, &app, [window, file] {
            const bool ok = window->grabWindow().save(file);
            if (!ok)
                qWarning().noquote() << "--grab: could not save" << file;
            QCoreApplication::exit(ok ? 0 : 1);
        });
    }

    return app.exec();
}
