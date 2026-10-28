#include "backend.h"
#include "printjob.h"
#include "printportal.h"
#include "theme.h"
#include "translator.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPdfWriter>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <sys/file.h>

static void writeFile(const QString &path, const QByteArray &data) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        qFatal("Cannot write fixture");
}

static QQuickItem *findVisual(QQuickItem *root, const QString &name) {
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *found = findVisual(child, name))
            return found;
    return nullptr;
}

struct BackendHarness {
    QTemporaryDir directory;
    QFile gate{directory.filePath("gate")};
    QJsonObject responses;
    Backend backend{false, nullptr, false,
                    QString("import fcntl,json,sys\n"
                            "request=json.load(sys.stdin)\n"
                            "with open('%1/gate','r') as gate: fcntl.flock(gate,fcntl.LOCK_SH)\n"
                            "data=json.load(open('%1/responses.json'))[request['action']]\n"
                            "print(json.dumps({'schema':1,'ok':True,'data':data}))\n")
                        .arg(directory.path())
                        .toUtf8()};
    BackendHarness() {
        if (!gate.open(QIODevice::ReadWrite))
            qFatal("Cannot open backend fixture gate");
        QFile demo(":/demo.json");
        if (!demo.open(QIODevice::ReadOnly))
            qFatal("Cannot read backend fixture");
        const auto fixture = QJsonDocument::fromJson(demo.readAll()).object();
        responses = {{"overview", fixture.value("snapshot")}, {"details", fixture.value("details")}};
        save();
        backend.refresh();
        if (!QTest::qWaitFor(
                [this] { return backend.details().value("name") == "Studio_Printer" && !backend.busy(); },
                3000))
            qFatal("Backend fixture failed to load");
    }
    ~BackendHarness() { release(); }
    void save() { writeFile(directory.filePath("responses.json"), QJsonDocument(responses).toJson()); }
    void block() {
        if (::flock(gate.handle(), LOCK_EX | LOCK_NB) != 0)
            qFatal("Cannot lock fixture gate");
    }
    void release() { ::flock(gate.handle(), LOCK_UN); }
};

struct PrintHarness {
    QTemporaryDir directory;
    Theme theme{directory.path()};
    Translator translator{directory.filePath("language")};
    QQmlEngine engine;
    PrintPortal portal{&engine, true};
    QString connectionName = "spool-client-" + QUuid::createUuid().toString(QUuid::Id128);
    QDBusConnection client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, connectionName);
    bool ready;
    PrintHarness(bool expose = false, bool demo = true) : portal(&engine, demo) {
        if (!qEnvironmentVariableIsSet("ESYUPRINT_PRIVATE_TEST_BUS"))
            qFatal("Use bin/test: portal tests require an isolated D-Bus session.");
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("translator", &translator);
        ready = !expose || (portal.start() && client.registerService("org.freedesktop.portal.Desktop"));
    }
    ~PrintHarness() {
        client.unregisterService("org.freedesktop.portal.Desktop");
        QDBusConnection::disconnectFromBus(connectionName);
    }
    QQuickWindow *window() {
        for (auto *request : portal.findChildren<PrintRequest *>())
            if (request->window() && request->window()->isVisible())
                return request->window();
        return nullptr;
    }
    PrintRequest *showPrintDialog(const QString &title = "Example document.pdf",
                                  const QVariantMap &options = {}) {
        auto *request = new PrintRequest(&portal, {}, {}, {}, title, {}, {}, options);
        request->activate();
        QCoreApplication::processEvents();
        return request;
    }
    QDBusPendingCall prepare(const QString &app = "org.example.Test", const QString &suffix = "one",
                             const QVariantMap &settings = {}) {
        QVariantMap initial{{"n-copies", "2"}};
        for (auto it = settings.begin(); it != settings.end(); ++it)
            initial[it.key()] = it.value();
        auto message = QDBusMessage::createMethodCall("org.freedesktop.impl.portal.desktop.esyuprint",
                                                      "/org/freedesktop/portal/desktop",
                                                      "org.freedesktop.impl.portal.Print", "PreparePrint");
        message << QDBusObjectPath("/org/freedesktop/portal/desktop/request/test/" + suffix) << app
                << QString() << QString("Test document") << initial << QVariantMap{} << QVariantMap{};
        return client.asyncCall(message);
    }
    QDBusPendingCall print(const QDBusUnixFileDescriptor &fd, uint token, const QString &app,
                           const QString &suffix) {
        auto message = QDBusMessage::createMethodCall("org.freedesktop.impl.portal.desktop.esyuprint",
                                                      "/org/freedesktop/portal/desktop",
                                                      "org.freedesktop.impl.portal.Print", "Print");
        message << QDBusObjectPath("/org/freedesktop/portal/desktop/request/test/" + suffix) << app
                << QString() << QString("Test document") << QVariant::fromValue(fd)
                << QVariantMap{{"token", token}};
        return client.asyncCall(message);
    }
};

class PortalResponse : public QObject {
    Q_OBJECT
  public slots:
    void response(uint code, const QVariantMap &results) { emit received(code, results); }
  signals:
    void received(uint code, const QVariantMap &results);
};

struct DesktopPortalHarness {
    PrintHarness print;
    QProcess frontend;
    QProcess application;
    QString requestPath;
    PortalResponse response;
    explicit DesktopPortalHarness(bool demo = true) : print(false, demo) {
        const auto root = print.directory.path();
        QDir().mkpath(root + "/portals");
        QDir().mkpath(root + "/config/xdg-desktop-portal");
        QDir().mkpath(root + "/runtime");
        QFile::setPermissions(root + "/runtime", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        writeFile(root + "/portals/esyuprint.portal",
                  "[portal]\nDBusName=org.freedesktop.impl.portal.desktop."
                  "esyuprint\nInterfaces=org.freedesktop.impl.portal.Print;\n");
        writeFile(root + "/portals/portals.conf", "[preferred]\ndefault=esyuprint\n");
        if (!print.portal.start())
            qFatal("Cannot start Spool on the private test bus");
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("XDG_DESKTOP_PORTAL_DIR", root + "/portals");
        env.insert("XDG_CURRENT_DESKTOP", "SpoolTest");
        env.insert("XDG_CONFIG_HOME", root + "/config");
        env.insert("XDG_DATA_HOME", root + "/data");
        env.insert("XDG_CACHE_HOME", root + "/cache");
        env.insert("XDG_STATE_HOME", root + "/state");
        env.insert("XDG_RUNTIME_DIR", root + "/runtime");
        frontend.setProcessEnvironment(env);
        frontend.start("/usr/lib/xdg-desktop-portal", {"--verbose"});
        if (!QTest::qWaitFor(
                [this] {
                    return print.client.interface()
                               ->isServiceRegistered("org.freedesktop.portal.Desktop")
                               .value() ||
                           frontend.state() == QProcess::NotRunning;
                },
                5000) ||
            frontend.state() == QProcess::NotRunning)
            qFatal("Desktop portal failed to start: %s", frontend.readAllStandardError().constData());
        QString sender = print.client.baseService().mid(1);
        sender.replace('.', '_');
        requestPath = "/org/freedesktop/portal/desktop/request/" + sender + "/spooltest";
        if (!print.client.connect("org.freedesktop.portal.Desktop", requestPath,
                                  "org.freedesktop.portal.Request", "Response", &response,
                                  SLOT(response(uint, QVariantMap))))
            qFatal("Cannot subscribe to the public print request");
    }
    ~DesktopPortalHarness() {
        if (application.state() != QProcess::NotRunning) {
            application.kill();
            application.waitForFinished(1000);
        }
        frontend.terminate();
        if (!frontend.waitForFinished(3000)) {
            frontend.kill();
            frontend.waitForFinished(1000);
        }
    }
    QDBusPendingCall prepare(const QVariantMap &settings = {}) {
        auto call = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop",
                                                   "/org/freedesktop/portal/desktop",
                                                   "org.freedesktop.portal.Print", "PreparePrint");
        call << QString() << QString("Public portal integration test") << settings << QVariantMap{}
             << QVariantMap{{"handle_token", "spooltest"}};
        return print.client.asyncCall(call);
    }
};

class SpoolTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { QQuickStyle::setStyle("Material"); }
    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QColor("#FFD60A"));
    }
    void themeReadsCompletePalette() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        writeFile(dir.filePath("theme/colors.toml"),
                  "background='#eeeeee'\nforeground='#111111'\naccent='#225588'\n");
        Theme theme(dir.path());
        QCOMPARE(theme.background(), QColor("#eeeeee"));
    }
    void themeRejectsPartialPalette() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        writeFile(dir.filePath("theme/colors.toml"), "accent='invalid'\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QColor("#FFD60A"));
    }
    void themeFollowsReplacement() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        const auto path = dir.filePath("theme/colors.toml");
        writeFile(path, "background='#111111'\nforeground='#eeeeee'\naccent='#112233'\n");
        Theme theme(dir.path());
        QFile::remove(path);
        writeFile(path, "background='#111111'\nforeground='#eeeeee'\naccent='#abcdef'\n");
        QTRY_COMPARE(theme.accent(), QColor("#abcdef"));
    }
    void translatorRegionalFallback() {
        QTemporaryDir dir;
        writeFile(dir.filePath("language"), "nl-BE\n");
        writeFile(dir.filePath("nl.json"), "{\"Save\":\"Opslaan\"}");
        Translator translator(dir.filePath("language"), dir.path());
        QCOMPARE(translator.translate("Main", "Save"), QString("Opslaan"));
    }
    void translatorCanSwitchFromEnglishAfterInstallation() {
        QTemporaryDir dir;
        writeFile(dir.filePath("language"), "en\n");
        writeFile(dir.filePath("nl.json"), "{\"Save\":\"Opslaan\"}");
        Translator translator(dir.filePath("language"), dir.path());
        qApp->installTranslator(&translator);
        writeFile(dir.filePath("language"), "nl\n");
        QTRY_COMPARE(QCoreApplication::translate("Main", "Save"), QString("Opslaan"));
    }
    void demoNeverStagesRealChanges() {
        Backend backend(true);
        backend.stage("delete");
        QCOMPARE(backend.pending(), QVariantMap{});
    }
    void demoSelectionIsPreserved() {
        Backend backend(true);
        backend.select("Office_Laser");
        QCOMPARE(backend.selected(), QString("Office_Laser"));
    }
    void backgroundRefreshKeepsTheInterfaceInteractive() {
        BackendHarness harness;
        harness.block();

        harness.backend.refresh(true);

        QCOMPARE(harness.backend.busy(), false);
    }
    void actionsCanBeStagedDuringBackgroundRefresh() {
        BackendHarness harness;
        harness.block();
        harness.backend.refresh(true);

        harness.backend.stage("info", {{"value", "Edited description"}});

        QCOMPARE(harness.backend.pending().value("action").toString(), QString("info"));
    }
    void confirmedActionsAreNotDroppedDuringBackgroundRefresh() {
        BackendHarness harness;
        harness.responses["info"] = QJsonObject{};
        harness.save();
        harness.block();
        harness.backend.refresh(true);
        harness.backend.stage("info", {{"value", "Edited description"}});

        harness.backend.confirm();
        harness.release();

        QTRY_COMPARE(harness.backend.notice(), QString("Completed: info"));
    }
    void completedEditsKeepTheLastGoodViewWhileRefreshing() {
        BackendHarness harness;
        harness.responses["info"] = QJsonObject{};
        harness.save();
        bool cleared = false;
        connect(&harness.backend, &Backend::detailsChanged, this,
                [&] { cleared |= harness.backend.details().isEmpty(); });
        harness.backend.stage("info", {{"value", "Edited description"}});

        harness.backend.confirm();
        if (!QTest::qWaitFor([&] { return harness.backend.notice() == "Completed: info"; }, 3000))
            QFAIL("Fixture edit did not complete");

        QCOMPARE(cleared, false);
    }
    void manualRefreshDoesNotMoveHeaderControls() {
        QTest::failOnWarning(QRegularExpression(".*"));
        BackendHarness backend;
        PrintHarness harness;
        harness.engine.rootContext()->setContextProperty("backend", &backend.backend);
        QQmlComponent component(&harness.engine, QUrl("qrc:/Main.qml"));
        std::unique_ptr<QQuickWindow> window(qobject_cast<QQuickWindow *>(component.create()));
        if (!window)
            QFAIL("Manager failed to load");
        QCoreApplication::processEvents();
        auto *refresh = findVisual(window->contentItem(), "refreshButton");
        if (!refresh)
            QFAIL("Refresh button missing");
        const auto before = refresh->mapToScene(QPointF());
        backend.block();

        backend.backend.refresh();
        QSignalSpy frame(window.get(), &QQuickWindow::afterAnimating);
        window->requestUpdate();
        if (frame.isEmpty() && !frame.wait(3000))
            QFAIL("Manager did not render");

        QCOMPARE(refresh->mapToScene(QPointF()), before);
    }
    void printerStatusUpdatesKeepExistingDelegates() {
        QTest::failOnWarning(QRegularExpression(".*"));
        BackendHarness backend;
        PrintHarness harness;
        harness.engine.rootContext()->setContextProperty("backend", &backend.backend);
        QQmlComponent component(&harness.engine, QUrl("qrc:/Main.qml"));
        std::unique_ptr<QQuickWindow> window(qobject_cast<QQuickWindow *>(component.create()));
        if (!window)
            QFAIL("Manager failed to load");
        QCoreApplication::processEvents();
        QPointer<QQuickItem> before = findVisual(window->contentItem(), "printer-Studio_Printer");
        if (!before)
            QFAIL("Printer delegate missing");
        auto overview = backend.responses.value("overview").toObject();
        auto printers = overview.value("printers").toArray();
        auto printer = printers.at(0).toObject();
        printer["state"] = 4;
        printers[0] = printer;
        overview["printers"] = printers;
        backend.responses["overview"] = overview;
        backend.save();

        backend.backend.refresh(true);
        if (!QTest::qWaitFor(
                [&] {
                    return backend.backend.snapshot()
                               .value("printers")
                               .toList()
                               .first()
                               .toMap()
                               .value("state") == 4;
                },
                3000))
            QFAIL("Printer status did not update");

        QCOMPARE(findVisual(window->contentItem(), "printer-Studio_Printer"), before.data());
    }
    void supplyUpdatesKeepExistingDelegates() {
        QTest::failOnWarning(QRegularExpression(".*"));
        BackendHarness backend;
        PrintHarness harness;
        harness.engine.rootContext()->setContextProperty("backend", &backend.backend);
        QQmlComponent component(&harness.engine, QUrl("qrc:/Main.qml"));
        std::unique_ptr<QQuickWindow> window(qobject_cast<QQuickWindow *>(component.create()));
        if (!window)
            QFAIL("Manager failed to load");
        QCoreApplication::processEvents();
        QPointer<QQuickItem> before = findVisual(window->contentItem(), "supply-Black ink");
        if (!before)
            QFAIL("Supply delegate missing");
        auto details = backend.responses.value("details").toObject();
        auto supplies = details.value("supplies").toArray();
        auto supply = supplies.at(0).toObject();
        supply["level"] = 81;
        supplies[0] = supply;
        details["supplies"] = supplies;
        backend.responses["details"] = details;
        backend.save();

        backend.backend.refresh(true);
        if (!QTest::qWaitFor(
                [&] {
                    return backend.backend.details()
                               .value("supplies")
                               .toList()
                               .first()
                               .toMap()
                               .value("level") == 81;
                },
                3000))
            QFAIL("Supply level did not update");

        QCOMPARE(findVisual(window->contentItem(), "supply-Black ink"), before.data());
    }
    void updatingAnotherOptionPreservesAnUnsavedChoice() {
        QTest::failOnWarning(QRegularExpression(".*"));
        BackendHarness backend;
        PrintHarness harness;
        harness.engine.rootContext()->setContextProperty("backend", &backend.backend);
        QQmlComponent component(&harness.engine, QUrl("qrc:/Main.qml"));
        std::unique_ptr<QQuickWindow> window(qobject_cast<QQuickWindow *>(component.create()));
        if (!window)
            QFAIL("Manager failed to load");
        window->setProperty("page", 1);
        QCoreApplication::processEvents();
        QPointer<QQuickItem> picker = findVisual(window->contentItem(), "optionPicker-media");
        if (!picker)
            QFAIL("Paper option missing");
        picker->setProperty("currentIndex", 1);
        auto details = backend.responses.value("details").toObject();
        auto options = details.value("options").toArray();
        auto sides = options.at(1).toObject();
        sides["value"] = "two-sided-long-edge";
        options[1] = sides;
        details["options"] = options;
        backend.responses["details"] = details;
        backend.save();

        backend.backend.refresh(true);
        if (!QTest::qWaitFor(
                [&] {
                    return backend.backend.options().at(1).toMap().value("value") == "two-sided-long-edge";
                },
                3000))
            QFAIL("Other option did not update");

        QCOMPARE(picker ? picker->property("currentValue").toString() : QString(),
                 QString("na_letter_8.5x11in"));
    }
    void rowUpdatesPreserveIdentityAcrossReordering() {
        RowModel model("name");
        model.update({QVariantMap{{"name", "A"}}, QVariantMap{{"name", "B"}}});
        const QPersistentModelIndex original(model.index(0));

        model.update({QVariantMap{{"name", "B"}}, QVariantMap{{"name", "A"}, {"state", 4}}});

        QCOMPARE(original.data(Qt::UserRole).toMap(), (QVariantMap{{"name", "A"}, {"state", 4}}));
    }
    void unchangedRowsDoNotNotifyViews() {
        RowModel model("name");
        const QVariantList rows{QVariantMap{{"name", "A"}}};
        model.update(rows);
        QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);

        model.update(rows);

        QCOMPARE(changes.count(), 0);
    }
    void readOnlyCliIgnoresInputApplyFlag() {
        QProcess process;
        process.start(QDir::currentPath() + "/../esyuprint", {"query", "--json"});
        if (!process.waitForStarted())
            QFAIL("Build spool before its CLI integration tests");
        process.write(R"({"schema":1,"action":"delete","name":"NeverContactAnyServer","apply":true})");
        process.closeWriteChannel();
        if (!process.waitForFinished(5000))
            QFAIL("CLI request hung");
        const auto response = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
        QCOMPARE(response.value("error").toString(),
                 QString("Mutation refused. Explicit confirmation (apply: true) is required."));
    }
    void managerExitsWithoutDanglingQmlBindings() {
        QTemporaryDir directory;
        QProcess process;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("ESYUPRINT_OFFSCREEN", "1");
        process.setProcessEnvironment(env);

        process.start(QDir::currentPath() + "/../esyuprint",
                      {"open", "--demo", "--screenshot", directory.filePath("manager.png")});
        if (!process.waitForFinished(5000) || process.exitCode() != 0)
            QFAIL(qPrintable(QString::fromUtf8(process.readAllStandardError())));

        QCOMPARE(process.readAllStandardError(), QByteArray{});
    }
    void qmlWindowLoadsAndNavigates_data() {
        QTest::addColumn<int>("targetPage");
        QTest::addColumn<bool>("light");
        QTest::addColumn<QSize>("windowSize");
        for (int page = 0; page < 7; ++page)
            for (bool light : {false, true})
                for (const auto &size : {QSize(1180, 790), QSize(820, 580), QSize(640, 420)})
                    QTest::newRow(qPrintable(
                        QString("%1-%2-%3x%4").arg(page).arg(light).arg(size.width()).arg(size.height())))
                        << page << light << size;
    }
    void qmlWindowLoadsAndNavigates() {
        QFETCH(int, targetPage);
        QFETCH(bool, light);
        QFETCH(QSize, windowSize);
        QTest::failOnWarning(QRegularExpression(".*"));
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        writeFile(dir.filePath("theme/colors.toml"),
                  light ? "background='#f4f4ee'\nforeground='#222222'\naccent='#266488'\n"
                        : "background='#18181b'\nforeground='#eeeeee'\naccent='#FFD60A'\n");
        writeFile(dir.filePath("language"), light ? "nl\n" : "en\n");
        Theme theme(dir.path());
        Translator translator(dir.filePath("language"));
        qApp->installTranslator(&translator);
        Backend backend(true);
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError> &errors) {
            for (const auto &error : errors)
                warnings << error.toString();
        });
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.load(QUrl("qrc:/Main.qml"));
        if (engine.rootObjects().isEmpty())
            QFAIL(qPrintable(warnings.join('\n')));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(windowSize);
        QCoreApplication::processEvents();
        auto *button = findVisual(window->contentItem(), "tab" + QString::number(targetPage));
        if (!button)
            QFAIL("Missing navigation button");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        QCoreApplication::processEvents();
        const auto screenshots = qEnvironmentVariable("ESYUPRINT_TEST_SCREENSHOTS");
        if (!screenshots.isEmpty()) {
            if (!window->grabWindow().save(screenshots + QString("/spool-page-%1-%2-%3x%4.png")
                                                             .arg(targetPage)
                                                             .arg(light)
                                                             .arg(windowSize.width())
                                                             .arg(windowSize.height())))
                QFAIL("Cannot save UI screenshot");
        }
        QCOMPARE(window->property("page").toInt(), targetPage);
    }
    void printerFilterUsesCompactHeight() {
        QTest::failOnWarning(QRegularExpression(".*"));
        QTemporaryDir dir;
        Theme theme(dir.path());
        Translator translator(dir.filePath("language"));
        Backend backend(true);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.load(QUrl("qrc:/Main.qml"));
        if (engine.rootObjects().isEmpty())
            QFAIL("QML failed to load");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QCoreApplication::processEvents();
        auto *filter = findVisual(window->contentItem(), "printerFilter");
        if (!filter)
            QFAIL("Filter not found");
        QCOMPARE(filter->height(), 28.0);
    }
    void overviewBottomActionsCanBeScrolledIntoView() {
        QTest::failOnWarning(QRegularExpression(".*"));
        PrintHarness harness;
        Backend backend(true);
        harness.engine.rootContext()->setContextProperty("backend", &backend);
        QQmlComponent component(&harness.engine, QUrl("qrc:/Main.qml"));
        std::unique_ptr<QQuickWindow> window(qobject_cast<QQuickWindow *>(component.create()));
        if (!window)
            QFAIL("Main window failed to load");
        window->resize(640, 420);
        QCoreApplication::processEvents();
        auto *scroll = findVisual(window->contentItem(), "pageScroller");
        auto *remove = findVisual(window->contentItem(), "removePrinterButton");
        if (!scroll || !remove)
            QFAIL("Missing scrollable controls");
        auto *viewport = scroll->property("contentItem").value<QQuickItem *>();
        viewport->setProperty("contentY",
                              qMax(0.0, viewport->property("contentHeight").toDouble() - viewport->height()));
        QCoreApplication::processEvents();
        QCOMPARE(viewport->mapRectToScene(QRectF(0, 0, viewport->width(), viewport->height()))
                     .contains(remove->mapRectToScene(QRectF(0, 0, remove->width(), remove->height()))),
                 true);
    }
    void printRangesAreConvertedToPortalZeroBasedPages() {
        PrintHarness harness;
        PrintRequest request(&harness.portal, {}, {}, {}, {}, {{"print-pages", "ranges"}}, {}, {});
        QString ranges;
        const auto error = PrintRequest::validateSettings(request.settings(), request.pageSetup(), {},
                                                          "1-3,5,8-10", &ranges);
        if (!error.isEmpty())
            QFAIL(qPrintable(error));
        QCOMPARE(ranges, QString("0-2,4,7-9"));
    }
    void invalidPrintRangesAreRejected_data() {
        QTest::addColumn<QString>("ranges");
        for (const QString value : {"0", "3-1", "", "1,,3", "x", "99999999999999999999999999"})
            QTest::newRow(qPrintable(value)) << value;
    }
    void invalidPrintRangesAreRejected() {
        QFETCH(QString, ranges);
        PrintHarness harness;
        PrintRequest request(&harness.portal, {}, {}, {}, {}, {{"print-pages", "ranges"}}, {}, {});
        QCOMPARE(
            PrintRequest::validateSettings(request.settings(), request.pageSetup(), {}, ranges).isEmpty(),
            false);
    }
    void currentPageNeedsApplicationCapability() {
        PrintHarness harness;
        PrintRequest request(&harness.portal, {}, {}, {}, {}, {{"print-pages", "current"}}, {}, {});
        QCOMPARE(PrintRequest::validateSettings(request.settings(), request.pageSetup(), {}, {}),
                 QString("This application did not provide a current page."));
    }
    void outputPathsPreserveReservedCharacters() {
        PrintHarness harness;
        PrintRequest request(&harness.portal, {}, {}, {}, {}, {}, {}, {});
        request.setOutputPath("/tmp/report #1 %.pdf");
        QCOMPARE(request.outputPath(), QString("/tmp/report #1 %.pdf"));
    }
    void printDialogKeepsButtonsVisibleAtSmallSizes_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("small") << QSize(560, 420);
        QTest::newRow("normal") << QSize(780, 620);
    }
    void printDialogHonorsPortalModality_data() {
        QTest::addColumn<QVariantMap>("options");
        QTest::addColumn<Qt::WindowModality>("modality");
        QTest::newRow("default-modal") << QVariantMap{} << Qt::ApplicationModal;
        QTest::newRow("explicit-modal") << QVariantMap{{"modal", true}} << Qt::ApplicationModal;
        QTest::newRow("non-modal") << QVariantMap{{"modal", false}} << Qt::NonModal;
    }
    void printDialogHonorsPortalModality() {
        QFETCH(QVariantMap, options);
        QFETCH(Qt::WindowModality, modality);
        QTest::failOnWarning(QRegularExpression(".*"));
        PrintHarness harness;

        auto *request = harness.showPrintDialog("Example.pdf", options);
        if (!request->window())
            QFAIL("Print dialog failed to load");

        QCOMPARE(request->window()->modality(), modality);
    }
    void printDialogUsesTheNativeDialogWindowType() {
        QTest::failOnWarning(QRegularExpression(".*"));
        PrintHarness harness;

        auto *request = harness.showPrintDialog();
        if (!request->window())
            QFAIL("Print dialog failed to load");

        QCOMPARE(request->window()->type(), Qt::Dialog);
    }
    void printDialogKeepsButtonsVisibleAtSmallSizes() {
        QFETCH(QSize, size);
        QTest::failOnWarning(QRegularExpression(".*"));
        PrintHarness harness;
        auto *request = new PrintRequest(&harness.portal, {}, {}, {}, "Demo document.pdf", {}, {}, {});
        request->activate();
        auto *window = request->window();
        if (!window)
            QFAIL("Print dialog failed to load");
        window->resize(size);
        QCoreApplication::processEvents();
        auto *button = findVisual(window->contentItem(), "printAcceptButton");
        if (!button)
            QFAIL("Print button missing");
        const auto screenshots = qEnvironmentVariable("ESYUPRINT_TEST_SCREENSHOTS");
        if (!screenshots.isEmpty())
            window->grabWindow().save(screenshots +
                                      QString("/spool-print-%1x%2.png").arg(size.width()).arg(size.height()));
        QCOMPARE(QRectF(0, 0, window->width(), window->height())
                     .contains(button->mapRectToScene(QRectF(0, 0, button->width(), button->height()))),
                 true);
    }
    void printDialogHidesGenericTitles_data() {
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("expected");
        QTest::newRow("qt-mnemonic") << QString("&Print…") << QString();
        QTest::newRow("gtk-mnemonic") << QString("_Print") << QString();
        QTest::newRow("ascii-ellipsis") << QString("Print...") << QString();
        QTest::newRow("plain-title") << QString("Print") << QString();
        QTest::newRow("document-title")
            << QString("Report_2026 & notes.pdf") << QString("Report_2026 & notes.pdf");
    }
    void printDialogHidesGenericTitles() {
        QFETCH(QString, title);
        QFETCH(QString, expected);
        QTest::failOnWarning(QRegularExpression(".*"));
        PrintHarness harness;

        auto *request = harness.showPrintDialog(title);
        if (!request->window())
            QFAIL("Print dialog failed to load");

        QCOMPARE(request->window()->property("documentTitle").toString(), expected);
    }
    void printButtonDoesNotDisplayMnemonicMarkers_data() {
        QTest::addColumn<QString>("label");
        QTest::addColumn<QString>("expected");
        QTest::newRow("qt") << QString("&Print") << QString("Print");
        QTest::newRow("gtk") << QString("_Print") << QString("Print");
        QTest::newRow("literal-ampersand") << QString("Save && close") << QString("Save & close");
        QTest::newRow("literal-underscore") << QString("Save __copy") << QString("Save _copy");
    }
    void printButtonDoesNotDisplayMnemonicMarkers() {
        QFETCH(QString, label);
        QFETCH(QString, expected);
        QTest::failOnWarning(QRegularExpression(".*"));
        PrintHarness harness;

        auto *request = harness.showPrintDialog("Example.pdf", {{"accept_label", label}});
        if (!request->window())
            QFAIL("Print dialog failed to load");
        auto *button = findVisual(request->window()->contentItem(), "printAcceptButton");
        if (!button)
            QFAIL("Print button missing");

        QCOMPARE(button->property("text").toString(), expected);
    }
    void printDropdownsStayCompactAndBalanced_data() {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<qreal>("dropdownWidth");
        QTest::newRow("small") << QSize(560, 420) << qreal(252);
        QTest::newRow("normal") << QSize(760, 700) << qreal(348);
        QTest::newRow("wide") << QSize(1180, 790) << qreal(348);
    }
    void printDropdownsStayCompactAndBalanced() {
        QFETCH(QSize, size);
        QFETCH(qreal, dropdownWidth);
        QTest::failOnWarning(QRegularExpression(".*"));
        PrintHarness harness;
        auto *request = harness.showPrintDialog();
        auto *window = request->window();
        if (!window)
            QFAIL("Print dialog failed to load");

        window->resize(size);
        QCoreApplication::processEvents();
        auto *paper = findVisual(window->contentItem(), "printPaperPicker");
        auto *orientation = findVisual(window->contentItem(), "printOrientationPicker");
        if (!paper || !orientation)
            QFAIL("Paper settings missing");

        QCOMPARE((QList<qreal>{paper->width(), orientation->width()}),
                 (QList<qreal>{dropdownWidth, dropdownWidth}));
    }
    void commonPrintSettingsFitWithoutScrollingAtTheDefaultSize() {
        QTest::failOnWarning(QRegularExpression(".*"));
        PrintHarness harness;
        auto *request = harness.showPrintDialog();
        if (!request->window())
            QFAIL("Print dialog failed to load");
        auto *scroll = findVisual(request->window()->contentItem(), "printScroller");
        if (!scroll)
            QFAIL("Print scroller missing");

        QCOMPARE(scroll->property("contentHeight").toDouble() <=
                     scroll->property("availableHeight").toDouble(),
                 true);
    }
    void expandingMarginsKeepsThePrintButtonInPlace() {
        QTest::failOnWarning(QRegularExpression(".*"));
        PrintHarness harness;
        auto *request = harness.showPrintDialog();
        auto *window = request->window();
        if (!window)
            QFAIL("Print dialog failed to load");
        auto *toggle = findVisual(window->contentItem(), "printMarginsToggle");
        auto *print = findVisual(window->contentItem(), "printAcceptButton");
        if (!toggle || !print)
            QFAIL("Print controls missing");
        const auto before = print->mapToScene(QPointF());

        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          toggle->mapToScene(QPointF(toggle->width() / 2, toggle->height() / 2)).toPoint());
        QCoreApplication::processEvents();

        QCOMPARE(print->mapToScene(QPointF()), before);
    }
    void expandedMarginsAreReachableInASmallWindow() {
        QTest::failOnWarning(QRegularExpression(".*"));
        PrintHarness harness;
        auto *request = harness.showPrintDialog();
        auto *window = request->window();
        if (!window)
            QFAIL("Print dialog failed to load");
        auto *toggle = findVisual(window->contentItem(), "printMarginsToggle");
        if (!toggle)
            QFAIL("Margins toggle missing");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          toggle->mapToScene(QPointF(toggle->width() / 2, toggle->height() / 2)).toPoint());

        window->resize(560, 420);
        QCoreApplication::processEvents();
        auto *scroll = findVisual(window->contentItem(), "printScroller");
        auto *margin = findVisual(window->contentItem(), "printMargin-MarginRight");
        if (!scroll || !margin || !margin->isVisible())
            QFAIL("Expanded margins missing");
        auto *viewport = scroll->property("contentItem").value<QQuickItem *>();
        viewport->setProperty("contentY",
                              qMax(0.0, viewport->property("contentHeight").toDouble() - viewport->height()));
        QCoreApplication::processEvents();

        QCOMPARE(viewport->mapRectToScene(QRectF(0, 0, viewport->width(), viewport->height()))
                     .contains(margin->mapRectToScene(QRectF(0, 0, margin->width(), margin->height()))),
                 true);
    }
    void printNumericFieldsApplyTypedValues_data() {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("setting");
        QTest::addColumn<QString>("value");
        QTest::newRow("copies") << QString("printCopiesField") << QString("n-copies") << QString("42");
        QTest::newRow("scale") << QString("printScaleField") << QString("scale") << QString("75");
    }
    void printNumericFieldsApplyTypedValues() {
        QFETCH(QString, name);
        QFETCH(QString, setting);
        QFETCH(QString, value);
        QTest::failOnWarning(QRegularExpression(".*"));
        PrintHarness harness;
        auto *request = harness.showPrintDialog();
        auto *window = request->window();
        if (!window)
            QFAIL("Print dialog failed to load");
        auto *spin = findVisual(window->contentItem(), name);
        if (!spin)
            QFAIL("Numeric field missing");
        auto *editor = spin->property("contentItem").value<QQuickItem *>();

        editor->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        for (const auto character : value)
            QTest::keyClick(window, Qt::Key(Qt::Key_0 + character.digitValue()));
        QTest::keyClick(window, Qt::Key_Return);

        QCOMPARE(request->settings().value(setting).toString(), value);
    }
    void printMarginsAcceptFractionalMillimetres() {
        QTest::failOnWarning(QRegularExpression(".*"));
        PrintHarness harness;
        auto *request = harness.showPrintDialog();
        auto *window = request->window();
        if (!window)
            QFAIL("Print dialog failed to load");
        auto *toggle = findVisual(window->contentItem(), "printMarginsToggle");
        if (!toggle)
            QFAIL("Margins toggle missing");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          toggle->mapToScene(QPointF(toggle->width() / 2, toggle->height() / 2)).toPoint());
        QCoreApplication::processEvents();
        auto *spin = findVisual(window->contentItem(), "printMargin-MarginTop");
        if (!spin)
            QFAIL("Top margin missing");
        spin->setProperty("locale", QVariant::fromValue(QLocale::c()));
        auto *editor = spin->property("contentItem").value<QQuickItem *>();

        editor->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_6);
        QTest::keyClick(window, Qt::Key_Period);
        QTest::keyClick(window, Qt::Key_5);
        QTest::keyClick(window, Qt::Key_Return);

        QCOMPARE(request->pageSetup().value("MarginTop").toDouble(), 6.5);
    }
    void portalPrepareCanBeCanceled() {
        QTest::failOnWarning(QRegularExpression(".*"));
        PrintHarness harness(true);
        if (!harness.ready)
            QFAIL("Private print portal could not start");
        QDBusPendingCallWatcher call(harness.prepare());
        if (!QTest::qWaitFor([&] { return harness.window() || call.isFinished(); }, 3000))
            QFAIL("Portal did not respond");
        if (!harness.window())
            QFAIL(qPrintable((QDBusPendingReply<uint, QVariantMap>(call).error().message())));
        auto *button = findVisual(harness.window()->contentItem(), "printCancelButton");
        QTest::mouseClick(harness.window(), Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        if (!QTest::qWaitFor([&] { return call.isFinished(); }, 3000))
            QFAIL("Cancel did not finish");
        QDBusPendingReply<uint, QVariantMap> reply = call;
        QCOMPARE(reply.argumentAt<0>(), 1u);
    }
    void desktopPortalRoutesPrintRequestsToSpool() {
        QTest::failOnWarning(QRegularExpression(".*"));
        DesktopPortalHarness harness;
        QSignalSpy response(&harness.response, &PortalResponse::received);

        QDBusPendingCallWatcher call(harness.prepare());
        if (!QTest::qWaitFor([&] { return harness.print.window() || (call.isFinished() && call.isError()); },
                             5000))
            QFAIL(qPrintable(QString::fromUtf8(harness.frontend.readAllStandardError())));
        if (!harness.print.window())
            QFAIL(qPrintable(call.error().message() + "\n" + harness.frontend.readAllStandardOutput() +
                             harness.frontend.readAllStandardError()));
        auto *cancel = findVisual(harness.print.window()->contentItem(), "printCancelButton");
        QTest::mouseClick(harness.print.window(), Qt::LeftButton, Qt::NoModifier,
                          cancel->mapToScene(QPointF(cancel->width() / 2, cancel->height() / 2)).toPoint());
        if (response.isEmpty() && !response.wait(3000))
            QFAIL("Public portal did not return a cancellation response");

        QCOMPARE(response.first().first().toUInt(), 1u);
    }
    void gtkApplicationUsesSpoolThroughTheDesktopPortal() {
        if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("DISPLAY"))
            QSKIP("GTK client integration needs a desktop display");
        QTest::failOnWarning(QRegularExpression(".*"));
        DesktopPortalHarness harness;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("GTK_USE_PORTAL", "1");
        harness.application.setProcessEnvironment(env);

        harness.application.start(
            "/usr/bin/python3",
            {"-I", "-c",
             "import gi\n"
             "gi.require_version('Gtk','3.0')\n"
             "from gi.repository import Gtk\n"
             "operation=Gtk.PrintOperation()\n"
             "operation.set_n_pages(1)\n"
             "operation.connect('begin-print',lambda op,context:op.cancel())\n"
             "print(operation.run(Gtk.PrintOperationAction.PRINT_DIALOG,None).value_nick)\n"});
        if (!QTest::qWaitFor(
                [&] { return harness.print.window() || harness.application.state() == QProcess::NotRunning; },
                5000))
            QFAIL("GTK application did not open Spool's dialog");
        if (!harness.print.window())
            QFAIL(qPrintable(QString::fromUtf8(harness.application.readAllStandardError())));
        auto *cancel = findVisual(harness.print.window()->contentItem(), "printCancelButton");
        QTest::mouseClick(harness.print.window(), Qt::LeftButton, Qt::NoModifier,
                          cancel->mapToScene(QPointF(cancel->width() / 2, cancel->height() / 2)).toPoint());
        if (!QTest::qWaitFor([&] { return harness.application.state() == QProcess::NotRunning; }, 3000))
            QFAIL("GTK client did not finish after cancellation");

        QCOMPARE(harness.application.readAllStandardOutput().trimmed(), QByteArray("cancel"));
    }
    void gtkApplicationRendersAndSubmitsAPdfThroughSpool() {
        if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("DISPLAY"))
            QSKIP("GTK client integration needs a desktop display");
        QTest::failOnWarning(QRegularExpression(".*"));
        DesktopPortalHarness harness(false);
        const auto target = harness.print.directory.filePath("gtk-output.pdf");
        // GtkPrintOperation returns before the final Response; keep the client's event loop alive.
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("GTK_USE_PORTAL", "1");
        harness.application.setProcessEnvironment(env);
        harness.application.start(
            "/usr/bin/python3",
            {"-I", "-c",
             "import gi\n"
             "gi.require_version('Gtk','3.0')\n"
             "from gi.repository import Gtk,Gio,GLib\n"
             "loop=GLib.MainLoop()\n"
             "completed=[]\n"
             "def response(connection,sender,path,interface,signal,parameters,data):\n"
             "    code,results=parameters.unpack()\n"
             "    if 'token' not in results:\n"
             "        completed.append(code)\n"
             "        loop.quit()\n"
             "bus=Gio.bus_get_sync(Gio.BusType.SESSION,None)\n"
             "bus.signal_subscribe('org.freedesktop.portal.Desktop','org.freedesktop.portal.Request','"
             "Response',None,None,Gio.DBusSignalFlags.NONE,response,None)\n"
             "operation=Gtk.PrintOperation()\n"
             "operation.set_n_pages(1)\n"
             "operation.set_job_name('Spool GTK file-only test')\n"
             "def draw(op,context,page):\n"
             "    cr=context.get_cairo_context()\n"
             "    cr.move_to(30,30)\n"
             "    cr.show_text('Spool GTK-generated PDF fixture')\n"
             "operation.connect('draw-page',draw)\n"
             "result=operation.run(Gtk.PrintOperationAction.PRINT_DIALOG,None)\n"
             "if result==Gtk.PrintOperationResult.APPLY and not completed: loop.run()\n"
             "if completed and completed[0]!=0: raise RuntimeError('Portal submission failed')\n"
             "print(result.value_nick)\n"});
        if (!QTest::qWaitFor(
                [&] { return harness.print.window() || harness.application.state() == QProcess::NotRunning; },
                5000))
            QFAIL("GTK print dialog did not open");
        if (!harness.print.window())
            QFAIL(qPrintable(QString::fromUtf8(harness.application.readAllStandardError())));
        auto *request = harness.print.portal.findChild<PrintRequest *>();
        request->setSetting("printer", "Print to File");
        request->setOutputPath(target);
        auto *accept = findVisual(harness.print.window()->contentItem(), "printAcceptButton");
        if (!QTest::qWaitFor([&] { return accept->isEnabled(); }, 5000))
            QFAIL("Printer discovery did not finish");

        QTest::mouseClick(harness.print.window(), Qt::LeftButton, Qt::NoModifier,
                          accept->mapToScene(QPointF(accept->width() / 2, accept->height() / 2)).toPoint());
        if (!QTest::qWaitFor([&] { return harness.application.state() == QProcess::NotRunning; }, 20000)) {
            for (auto *pending : harness.print.portal.findChildren<PrintRequest *>())
                if (pending->failed())
                    QFAIL(qPrintable(pending->error()));
            QFAIL(qPrintable(QString::fromUtf8(harness.application.readAllStandardError())));
        }
        if (harness.application.readAllStandardOutput().trimmed() != "apply")
            QFAIL(qPrintable(QString::fromUtf8(harness.application.readAllStandardError())));
        QFile output(target);
        if (!output.open(QIODevice::ReadOnly))
            QFAIL("GTK output PDF missing");

        QCOMPARE(output.read(5), QByteArray("%PDF-"));
    }
    void desktopPortalCloseDismissesSpoolsDialog() {
        QTest::failOnWarning(QRegularExpression(".*"));
        DesktopPortalHarness harness;
        QDBusPendingCallWatcher prepare(harness.prepare());
        if (!QTest::qWaitFor(
                [&] { return harness.print.window() || (prepare.isFinished() && prepare.isError()); }, 5000))
            QFAIL("Public print request did not open");
        if (!harness.print.window())
            QFAIL(qPrintable(prepare.error().message()));
        auto *window = harness.print.window();
        QSignalSpy hidden(window, &QWindow::visibleChanged);

        auto close = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", harness.requestPath,
                                                    "org.freedesktop.portal.Request", "Close");
        harness.print.client.asyncCall(close);
        if (hidden.isEmpty() && !hidden.wait(3000))
            QFAIL("Public cancellation did not dismiss the print dialog");

        QCOMPARE(hidden.first().first().toBool(), false);
    }
    void desktopPortalCanSubmitAGeneratedPdfToANewFile() {
        if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("DISPLAY"))
            QSKIP("GTK file-only integration needs a desktop display");
        QTest::failOnWarning(QRegularExpression(".*"));
        DesktopPortalHarness harness(false);
        const auto source = harness.print.directory.filePath("input.pdf");
        const auto target = harness.print.directory.filePath("output.pdf");
        {
            QPdfWriter writer(source);
            QPainter painter(&writer);
            painter.drawText(100, 100, "Public desktop portal file-only fixture");
        }
        QSignalSpy response(&harness.response, &PortalResponse::received);
        QDBusPendingCallWatcher prepare(harness.prepare(
            {{"printer", "Print to File"}, {"output-uri", QUrl::fromLocalFile(target).toString()}}));
        if (!QTest::qWaitFor(
                [&] { return harness.print.window() || (prepare.isFinished() && prepare.isError()); }, 5000))
            QFAIL("Public print dialog did not open");
        if (!harness.print.window())
            QFAIL(qPrintable(prepare.error().message()));
        auto *accept = findVisual(harness.print.window()->contentItem(), "printAcceptButton");
        if (!QTest::qWaitFor([&] { return accept->isEnabled(); }, 5000))
            QFAIL("Printer discovery did not finish");
        QTest::mouseClick(harness.print.window(), Qt::LeftButton, Qt::NoModifier,
                          accept->mapToScene(QPointF(accept->width() / 2, accept->height() / 2)).toPoint());
        if (response.isEmpty() && !response.wait(3000))
            QFAIL("Public prepare response missing");
        const auto prepared = response.takeFirst();
        if (prepared.first().toUInt() != 0)
            QFAIL("Public print preparation failed");
        const auto token = prepared.at(1).toMap().value("token").toUInt();
        if (!token)
            QFAIL("Public print token missing");
        const auto path = harness.requestPath + "submit";
        if (!harness.print.client.connect("org.freedesktop.portal.Desktop", path,
                                          "org.freedesktop.portal.Request", "Response", &harness.response,
                                          SLOT(response(uint, QVariantMap))))
            QFAIL("Cannot watch print submission");
        QFile input(source);
        if (!input.open(QIODevice::ReadOnly))
            QFAIL("Cannot open generated PDF");
        auto call = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop",
                                                   "/org/freedesktop/portal/desktop",
                                                   "org.freedesktop.portal.Print", "Print");
        call << QString() << QString("Generated file-only fixture")
             << QVariant::fromValue(QDBusUnixFileDescriptor(input.handle()))
             << QVariantMap{{"token", token}, {"handle_token", "spooltestsubmit"}};

        QDBusPendingCallWatcher submission(harness.print.client.asyncCall(call));
        if (!QTest::qWaitFor(
                [&] {
                    return !response.isEmpty() || (submission.isFinished() && submission.isError()) ||
                           harness.print.window();
                },
                20000))
            QFAIL("Public file-only submission timed out");
        if (response.isEmpty()) {
            for (auto *request : harness.print.portal.findChildren<PrintRequest *>())
                if (request->failed())
                    QFAIL(qPrintable(request->error()));
            QFAIL(qPrintable(submission.error().message()));
        }
        if (response.first().first().toUInt() != 0)
            QFAIL("Public print submission failed");
        QFile output(target);
        if (!output.open(QIODevice::ReadOnly))
            QFAIL("Public print output missing");

        QCOMPARE(output.readAll(), input.readAll());
    }
    void portalCloseCanCancelAQueuedRequest() {
        QTest::failOnWarning(QRegularExpression(".*"));
        PrintHarness harness(true);
        if (!harness.ready)
            QFAIL("Private print portal could not start");
        QDBusPendingCallWatcher active(harness.prepare("org.example.First", "active"));
        if (!QTest::qWaitFor([&] { return harness.window(); }, 3000))
            QFAIL("Active dialog missing");
        QDBusPendingCallWatcher queued(harness.prepare("org.example.Second", "queued"));
        if (!QTest::qWaitFor([&] { return harness.portal.findChildren<PrintRequest *>().size() == 2; }, 3000))
            QFAIL("Second request was not queued");

        auto close = QDBusMessage::createMethodCall("org.freedesktop.impl.portal.desktop.esyuprint",
                                                    "/org/freedesktop/portal/desktop/request/test/queued",
                                                    "org.freedesktop.impl.portal.Request", "Close");
        harness.client.asyncCall(close);
        if (!QTest::qWaitFor([&] { return queued.isFinished(); }, 3000))
            QFAIL("Queued request did not cancel");

        QCOMPARE((QDBusPendingReply<uint, QVariantMap>(queued).argumentAt<0>()), 1u);
    }
    void desktopPortalDisappearanceCancelsItsRequests() {
        QTest::failOnWarning(QRegularExpression(".*"));
        PrintHarness harness(true);
        if (!harness.ready)
            QFAIL("Private print portal could not start");
        QDBusPendingCallWatcher request(harness.prepare());
        if (!QTest::qWaitFor([&] { return harness.window(); }, 3000))
            QFAIL("Print dialog missing");

        harness.client.unregisterService("org.freedesktop.portal.Desktop");
        if (!QTest::qWaitFor([&] { return request.isFinished(); }, 3000))
            QFAIL("Orphaned print request did not cancel");

        QCOMPARE((QDBusPendingReply<uint, QVariantMap>(request).argumentAt<0>()), 1u);
    }
    void portalTokensAreBoundToTheRequestingApplication() {
        QTest::failOnWarning(QRegularExpression(".*"));
        PrintHarness harness(true);
        if (!harness.ready)
            QFAIL("Private print portal could not start");
        QDBusPendingCallWatcher prepare(harness.prepare());
        if (!QTest::qWaitFor([&] { return harness.window() || prepare.isFinished(); }, 3000))
            QFAIL("Portal did not respond");
        if (!harness.window())
            QFAIL(qPrintable((QDBusPendingReply<uint, QVariantMap>(prepare).error().message())));
        auto *button = findVisual(harness.window()->contentItem(), "printAcceptButton");
        QTest::mouseClick(harness.window(), Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        if (!QTest::qWaitFor([&] { return prepare.isFinished(); }, 3000))
            QFAIL("Prepare did not finish");
        QDBusPendingReply<uint, QVariantMap> prepared = prepare;
        if (prepared.isError() || prepared.argumentAt<0>() != 0)
            QFAIL("Prepare failed");
        const uint token = prepared.argumentAt<1>().value("token").toUInt();
        QFile file(harness.directory.filePath("input.pdf"));
        if (!file.open(QIODevice::ReadWrite))
            QFAIL("Cannot create fixture");
        file.write("%PDF-test");
        file.flush();
        const QDBusUnixFileDescriptor fd(file.handle());
        QDBusPendingCallWatcher foreign(harness.print(fd, token, "org.example.Other", "foreign"));
        if (!QTest::qWaitFor([&] { return harness.window() || foreign.isFinished(); }, 3000))
            QFAIL("Foreign token did not respond");
        if (!harness.window())
            QFAIL("A foreign application bypassed the dialog");
        auto *cancel = findVisual(harness.window()->contentItem(), "printCancelButton");
        QTest::mouseClick(harness.window(), Qt::LeftButton, Qt::NoModifier,
                          cancel->mapToScene(QPointF(cancel->width() / 2, cancel->height() / 2)).toPoint());
        if (!QTest::qWaitFor([&] { return foreign.isFinished(); }, 3000))
            QFAIL("Foreign request did not cancel");
        QDBusPendingCallWatcher original(harness.print(fd, token, "org.example.Test", "original"));
        if (!QTest::qWaitFor([&] { return original.isFinished(); }, 3000))
            QFAIL("Original token did not finish");
        QDBusPendingReply<uint, QVariantMap> result = original;
        QCOMPARE(result.argumentAt<0>(), 0u);
    }
    void portalRejectsCallersOtherThanTheDesktopPortal() {
        PrintHarness harness(true);
        if (!harness.ready)
            QFAIL("Private print portal could not start");
        auto call = QDBusMessage::createMethodCall("org.freedesktop.impl.portal.desktop.esyuprint",
                                                   "/org/freedesktop/portal/desktop",
                                                   "org.freedesktop.impl.portal.Print", "PreparePrint");
        call << QDBusObjectPath("/org/freedesktop/portal/desktop/request/test/untrusted")
             << QString("org.example.Other") << QString() << QString("Do not show") << QVariantMap{}
             << QVariantMap{} << QVariantMap{};
        const QString name = "untrusted-" + QUuid::createUuid().toString(QUuid::Id128);
        auto connection = QDBusConnection::connectToBus(QDBusConnection::SessionBus, name);
        QDBusPendingCallWatcher watcher(connection.asyncCall(call));
        if (!QTest::qWaitFor([&] { return watcher.isFinished(); }, 3000))
            QFAIL("Unauthorized call hung");
        QDBusPendingReply<uint, QVariantMap> reply = watcher;
        QDBusConnection::disconnectFromBus(name);
        QCOMPARE(reply.error().type(), QDBusError::AccessDenied);
    }
    void gtkSdkCanPrintToANewPrivateFileWithoutSubmittingAPrinterJob() {
        if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("DISPLAY"))
            QSKIP("GTK print-to-file integration needs a desktop display");
        QTemporaryDir directory;
        const QString source = directory.filePath("source.pdf"), target = directory.filePath("output.pdf");
        {
            QPdfWriter writer(source);
            QPainter painter(&writer);
            painter.drawText(100, 100, "Spool generated print-to-file fixture");
        }
        PrintHarness harness;
        PrintRequest request(
            &harness.portal, {}, {}, {}, {},
            {{"printer", "Print to File"}, {"output-uri", QUrl::fromLocalFile(target).toString()}}, {}, {});
        auto *job = new PrintJob(this);
        QSignalSpy finished(job, &PrintJob::finished);
        job->start(source, "Spool file-only test", request.settings(), request.pageSetup());
        if (finished.isEmpty() && !finished.wait(20000))
            QFAIL("GTK print-to-file timed out");
        if (!finished.first().first().toString().isEmpty())
            QFAIL(qPrintable(finished.first().first().toString()));
        QFile original(source), output(target);
        if (!original.open(QIODevice::ReadOnly) || !output.open(QIODevice::ReadOnly))
            QFAIL("Print output missing");
        QCOMPARE(output.readAll(), original.readAll());
    }
    void printToFileNeverOverwritesAnExistingFile() {
        if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("DISPLAY"))
            QSKIP("GTK file-only integration needs a desktop display");
        PrintHarness harness;
        const auto source = harness.directory.filePath("source.pdf");
        const auto target = harness.directory.filePath("existing.pdf");
        writeFile(source, "%PDF-test input");
        writeFile(target, "Keep this existing document");
        PrintRequest request(
            &harness.portal, {}, {}, {}, {},
            {{"printer", "Print to File"}, {"output-uri", QUrl::fromLocalFile(target).toString()}}, {}, {});
        auto *job = new PrintJob(this);
        QSignalSpy finished(job, &PrintJob::finished);

        job->start(source, "Never overwrite", request.settings(), request.pageSetup());
        if (finished.isEmpty() && !finished.wait(3000))
            QFAIL("Overwrite refusal did not finish");
        if (finished.first().first().toString().isEmpty())
            QFAIL("Existing output was not rejected");
        QFile output(target);
        if (!output.open(QIODevice::ReadOnly))
            QFAIL("Existing output disappeared");

        QCOMPARE(output.readAll(), QByteArray("Keep this existing document"));
    }
    void portalSubmitsTheWholeDescriptorToAPrivateFile() {
        if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("DISPLAY"))
            QSKIP("GTK file-only integration needs a desktop display");
        QTest::failOnWarning(QRegularExpression(".*"));
        PrintHarness harness(true, false);
        if (!harness.ready)
            QFAIL("Private print portal could not start");
        const QString source = harness.directory.filePath("source.pdf"),
                      target = harness.directory.filePath("output.pdf");
        {
            QPdfWriter writer(source);
            QPainter painter(&writer);
            painter.drawText(100, 100, "Generated portal file-descriptor fixture");
        }
        QDBusPendingCallWatcher prepare(
            harness.prepare("org.example.Test", "fileprepare",
                            {{"printer", "Print to File"},
                             {"n-copies", "1"},
                             {"output-uri", QUrl::fromLocalFile(target).toString()}}));
        if (!QTest::qWaitFor([&] { return harness.window() || prepare.isFinished(); }, 3000))
            QFAIL("Prepare did not respond");
        if (!harness.window())
            QFAIL("Print-to-file dialog did not open");
        auto *button = findVisual(harness.window()->contentItem(), "printAcceptButton");
        if (!QTest::qWaitFor([&] { return button->isEnabled(); }, 5000))
            QFAIL("Printer discovery did not finish");
        QTest::mouseClick(harness.window(), Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        if (!QTest::qWaitFor([&] { return prepare.isFinished(); }, 3000))
            QFAIL("File settings did not finish");
        QDBusPendingReply<uint, QVariantMap> prepared = prepare;
        if (prepared.isError() || prepared.argumentAt<0>() != 0)
            QFAIL("File settings were rejected");
        QFile input(source);
        if (!input.open(QIODevice::ReadOnly))
            QFAIL("Cannot open source fixture");
        input.seek(input.size());
        QDBusPendingCallWatcher print(harness.print(QDBusUnixFileDescriptor(input.handle()),
                                                    prepared.argumentAt<1>().value("token").toUInt(),
                                                    "org.example.Test", "filesubmit"));
        if (!QTest::qWaitFor([&] { return print.isFinished() || harness.window(); }, 20000))
            QFAIL("File submission timed out");
        if (!print.isFinished()) {
            for (auto *request : harness.portal.findChildren<PrintRequest *>())
                if (request->failed())
                    QFAIL(qPrintable(request->error()));
            QFAIL("File submission unexpectedly opened another dialog");
        }
        QDBusPendingReply<uint, QVariantMap> printed = print;
        if (printed.isError() || printed.argumentAt<0>() != 0)
            QFAIL("File submission failed");
        QFile original(source), output(target);
        if (!original.open(QIODevice::ReadOnly) || !output.open(QIODevice::ReadOnly))
            QFAIL("Print output missing");
        QCOMPARE(output.readAll(), original.readAll());
    }
    void dialogsOpenWithoutWarnings_data() {
        QTest::addColumn<QString>("name");
        for (const QString name : {"addPrinterDialog", "driverDialog", "bannerDialog", "moveDialog",
                                   "jobAuthDialog", "connectionDialog", "authDialog", "helpDialog"})
            QTest::newRow(qPrintable(name)) << name;
    }
    void dialogsOpenWithoutWarnings() {
        QFETCH(QString, name);
        QTest::failOnWarning(QRegularExpression(".*"));
        QTemporaryDir dir;
        Theme theme(dir.path());
        Translator translator(dir.filePath("language"));
        Backend backend(true);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.load(QUrl("qrc:/Main.qml"));
        if (engine.rootObjects().isEmpty())
            QFAIL("QML failed to load");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *dialog = window->findChild<QObject *>(name);
        if (!dialog)
            QFAIL("Dialog not found");
        QSignalSpy opened(dialog, SIGNAL(opened()));
        QMetaObject::invokeMethod(dialog, "open");
        if (opened.isEmpty() && !opened.wait(3000))
            QFAIL("Dialog did not finish opening");
        const auto screenshots = qEnvironmentVariable("ESYUPRINT_TEST_SCREENSHOTS");
        if (!screenshots.isEmpty() && !window->grabWindow().save(screenshots + "/spool-" + name + ".png"))
            QFAIL("Cannot save dialog screenshot");
        const auto image = window->grabWindow();
        const QPoint sample(dialog->property("x").toInt() + 5,
                            dialog->property("y").toInt() + dialog->property("height").toInt() / 2);
        QCOMPARE(image.pixelColor(sample).rgba(), theme.surface().rgba());
    }
};
QTEST_MAIN(SpoolTests)
#include "spool_tests.moc"
