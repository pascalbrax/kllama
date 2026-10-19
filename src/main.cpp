#include "ollamaconnection.h"
#include "chatsession.h"
#include "markdownrenderer.h"
#include <QStandardPaths>
#include <QDir>
#include <QLockFile>
#include <QMessageBox>
#include <ki18n_version.h>
#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <KLocalizedQmlContext>
#include <KLocalizedString>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Kllama");
    QCoreApplication::setOrganizationDomain("kllama.org");
    QCoreApplication::setApplicationName("kllama");
    QCoreApplication::setApplicationVersion("0.3.0");
    QGuiApplication::setApplicationDisplayName("Kllama");
    QGuiApplication::setDesktopFileName("org.kllama.Kllama");
    KLocalizedString::setApplicationDomain("kllama");
    QGuiApplication::setWindowIcon(QIcon(":/kllama.svg"));
    const auto dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataDirectory);
    QLockFile lock(dataDirectory + "/session.lock");
    if (!lock.tryLock(0)) {
        QMessageBox::information(nullptr, "Kllama", i18n("Kllama is already running, or its data directory is not writable."));
        return 1;
    }
    OllamaConnection connection;
    ChatSession chat(&connection, dataDirectory);
    MarkdownRenderer markdown;
    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);
    engine.rootContext()->setContextProperty("ollama", &connection);
    engine.rootContext()->setContextProperty("chat", &chat);
    engine.rootContext()->setContextProperty("markdown", &markdown);
    engine.rootContext()->setContextProperty("appInfo", QVariantMap{
        {"version",QCoreApplication::applicationVersion()}, {"qtVersion",QString::fromLatin1(qVersion())},
        {"frameworksVersion",QStringLiteral(KI18N_VERSION_STRING)}, {"dataDirectory",dataDirectory}
    });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("org.kllama.app", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    if (!qEnvironmentVariableIsSet("KLLAMA_NO_AUTOCONNECT")) QTimer::singleShot(0, &connection, &OllamaConnection::refresh);
    if (app.arguments().contains("--smoke-test")) {
        // Exercise the settings popup as well as the initial shell.
        QTimer::singleShot(100, &engine, [&engine] {
            QMetaObject::invokeMethod(engine.rootObjects().first(), "openSettings");
        });
        QTimer::singleShot(350, &engine, [&engine] {
            QMetaObject::invokeMethod(engine.rootObjects().first(), "openAbout");
        });
        QTimer::singleShot(800, &app, &QCoreApplication::quit);
    }
    const int screenshotOption = app.arguments().indexOf("--screenshot");
    if (screenshotOption >= 0 && screenshotOption + 1 < app.arguments().size()) {
        const auto path = app.arguments().at(screenshotOption + 1);
        QTimer::singleShot(500, &engine, [&engine, path] {
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            if (!window || !window->grabWindow().save(path)) QCoreApplication::exit(2);
        });
        QTimer::singleShot(900, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
