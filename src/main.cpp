#include "backend.h"
#include "printportal.h"
#include "theme.h"
#include "translator.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTextStream>
#include <QTimer>

int main(int argc, char *argv[]) {
    const QString command = argc > 1 ? QString::fromLocal8Bit(argv[1]) : "open";
    const bool headless = command != "open" && command != "portal" && command != "print";
    if (headless || qEnvironmentVariableIsSet("ESYUPRINT_OFFSCREEN")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("QT_QPA_PLATFORMTHEME", "generic");
        qputenv("NO_AT_BRIDGE", "1");
    }
    QGuiApplication app(argc, argv);
    app.setApplicationName("esyuprint");
    app.setApplicationVersion("0.2.0");
    app.setDesktopFileName("esyuprint");
    app.setWindowIcon(QIcon(":/esyuprint.svg"));
    const auto arguments = app.arguments();
    if (command == "--version") {
        QTextStream(stdout) << app.applicationVersion() << '\n';
        return 0;
    }
    if (command == "--help" || command == "help") {
        QTextStream(stdout)
            << "esyuprint — Omarchy printer management\n\n"
               "esyuprint open [--demo] [--screenshot PATH]\n"
               "esyuprint print --demo [--screenshot PATH] Preview the themed print dialog\n"
               "esyuprint portal                          Desktop Print portal backend\n"
               "esyuprint query --json < request.json      Read-only JSON request\n"
               "esyuprint request --apply --json < request.json   Explicitly permit a mutation\n"
               "esyuprint compatibility --json            Check installed upstream APIs (read-only)\n"
               "esyuprint skill install                   Install the bundled agent skill\n\n"
               "JSON: {\"schema\":1,\"action\":\"overview\"}. Remote server and credentials go on stdin.\n"
               "CLI mutations need --apply. Portal printing requires confirmation in its print dialog.\n";
        return 0;
    }
    if (command == "skill" && arguments.mid(2) == QStringList{"install"}) {
        const QString directory = QDir::homePath() + "/.agents/skills/esyuprint-update";
        if (QFile::exists(directory + "/SKILL.md")) {
            QTextStream(stderr)
                << "An esyuprint-update skill already exists; review it before replacing it.\n";
            return 1;
        }
        QFile source(":/SKILL.md");
        if (!source.open(QIODevice::ReadOnly) || !QDir().mkpath(directory))
            return 1;
        QSaveFile target(directory + "/SKILL.md");
        if (!target.open(QIODevice::WriteOnly) || target.write(source.readAll()) < 0 || !target.commit())
            return 1;
        QTextStream(stdout) << directory << "/SKILL.md\n";
        return 0;
    }
    if (command == "query" || command == "request" || command == "compatibility") {
        QJsonObject request;
        if (command == "compatibility")
            request = {{"schema", 1}, {"action", "compatibility"}};
        else {
            QFile input;
            if (!input.open(stdin, QIODevice::ReadOnly))
                return 1;
            QJsonParseError parse;
            const auto document = QJsonDocument::fromJson(input.readAll(), &parse);
            if (parse.error != QJsonParseError::NoError || !document.isObject()) {
                QTextStream(stderr) << "Expected a JSON request object on stdin.\n";
                return 1;
            }
            request = document.object();
        }
        request["apply"] = command == "request" && arguments.contains("--apply");
        QProcess process;
        process.start("/usr/bin/python3", {"-I", "-c", QString::fromUtf8(Backend::bridgeSource())});
        if (!process.waitForStarted()) {
            QTextStream(stderr) << process.errorString() << '\n';
            return 1;
        }
        process.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
        process.closeWriteChannel();
        if (!process.waitForFinished(
                request.value("apply").toBool() || request.value("action") == "devices-authorized" ? 180000
                                                                                                   : 30000)) {
            process.kill();
            process.waitForFinished(1000);
            QTextStream(stderr) << "Request timed out. Check the target before retrying a mutation.\n";
            return 1;
        }
        QTextStream(stdout) << process.readAllStandardOutput();
        QTextStream(stderr) << process.readAllStandardError();
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 ? 0 : 1;
    }
    if (command != "open" && command != "portal" && command != "print") {
        QTextStream(stderr) << "Unknown command. See esyuprint --help.\n";
        return 1;
    }
    const int screenshotIndex = arguments.indexOf("--screenshot");
    if (screenshotIndex >= 0 &&
        (command == "portal" || !arguments.contains("--demo") || screenshotIndex + 1 >= arguments.size())) {
        QTextStream(stderr) << "Screenshots require --demo and an output path.\n";
        return 1;
    }
    QQuickStyle::setStyle("Material");
    Translator translator;
    app.installTranslator(&translator);
    Theme theme;
    std::unique_ptr<Backend> backend;
    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    if (command == "portal") {
        if (arguments.contains("--demo")) {
            QTextStream(stderr)
                << "Use print --demo for previews. The portal backend cannot run in demo mode.\n";
            return 1;
        }
        app.setQuitOnLastWindowClosed(false);
        PrintPortal portal(&engine);
        if (!portal.start()) {
            QTextStream(stderr) << "Cannot register the esyuprint Print portal on the session bus.\n";
            return 1;
        }
        return app.exec();
    }
    std::unique_ptr<PrintPortal> preview;
    QQuickWindow *window = nullptr;
    if (command == "print") {
        if (!arguments.contains("--demo")) {
            QTextStream(stderr)
                << "Use the desktop Print portal to print, or print --demo to preview the dialog.\n";
            return 1;
        }
        preview = std::make_unique<PrintPortal>(&engine, true);
        auto *request = new PrintRequest(preview.get(), {}, {}, "Demo application", "Example document.pdf",
                                         {}, {}, {{"has_current_page", true}, {"has_selected_pages", true}});
        request->activate();
        window = request->window();
    } else {
        backend = std::make_unique<Backend>(arguments.contains("--demo"));
        engine.rootContext()->setContextProperty("backend", backend.get());
        engine.load(QUrl("qrc:/Main.qml"));
        if (!engine.rootObjects().isEmpty())
            window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    }
    if (!window)
        return 1;
    if (screenshotIndex >= 0) {
        QTimer::singleShot(800, window, [window, &app, arguments, screenshotIndex] {
            const bool saved = window->grabWindow().save(arguments.at(screenshotIndex + 1));
            app.exit(saved ? 0 : 1);
        });
    }
    return app.exec();
}
