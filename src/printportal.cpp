#include "printportal.h"
#include "printjob.h"
#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQmlComponent>
#include <QQmlContext>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <QtConcurrent>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

static const QString serviceName = "org.freedesktop.impl.portal.desktop.esyuprint";
static const QString desktopName = "org.freedesktop.portal.Desktop";

static QVariantMap initialSettings(const QVariantMap &input) {
    const auto allowed =
        QString("printer orientation paper-format paper-width paper-height n-copies default-source quality "
                "resolution use-color duplex collate reverse media-type dither scale print-pages page-ranges "
                "page-set finishings number-up number-up-layout output-bin resolution-x resolution-y "
                "printer-lpi output-basename output-file-format output-uri")
            .split(' ');
    QVariantMap result{{"n-copies", "1"},
                       {"collate", "true"},
                       {"reverse", "false"},
                       {"print-pages", "all"},
                       {"page-set", "all"},
                       {"duplex", "simplex"},
                       {"use-color", "true"},
                       {"scale", "100"},
                       {"number-up", "1"},
                       {"quality", "normal"},
                       {"orientation", "portrait"},
                       {"paper-format", "iso_a4"},
                       {"output-file-format", "pdf"}};
    for (const auto &key : allowed)
        if (input.contains(key))
            result[key] = input.value(key).toString();
    return result;
}

static QVariantMap initialPage(const QVariantMap &input) {
    QVariantMap result{{"Name", "iso_a4"},   {"DisplayName", "A4"}, {"Width", 210.0},
                       {"Height", 297.0},    {"MarginTop", 6.35},   {"MarginBottom", 6.35},
                       {"MarginLeft", 6.35}, {"MarginRight", 6.35}, {"Orientation", "portrait"}};
    for (auto it = result.begin(); it != result.end(); ++it)
        if (input.contains(it.key()))
            it.value() = it.value().metaType().id() == QMetaType::Double
                             ? QVariant(input.value(it.key()).toDouble())
                             : QVariant(input.value(it.key()).toString());
    return result;
}

PrintPortal::PrintPortal(QQmlEngine *engine, bool demo, QObject *parent)
    : QObject(parent), m_engine(engine), m_demo(demo) {
    auto *desktop = new QDBusServiceWatcher(desktopName, QDBusConnection::sessionBus(),
                                            QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(desktop, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &oldOwner, const QString &) {
                if (oldOwner.isEmpty())
                    return;
                m_prepared.clear();
                const auto requests = m_requests;
                m_requests.clear();
                for (auto *request : requests)
                    request->cancel();
            });
    connect(&m_cleanup, &QTimer::timeout, this, [this] {
        for (auto it = m_prepared.begin(); it != m_prepared.end();)
            if (it->expires.hasExpired())
                it = m_prepared.erase(it);
            else
                ++it;
    });
    m_cleanup.start(30000);
}
bool PrintPortal::start() {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(serviceName))
        return false;
    if (!bus.registerObject("/org/freedesktop/portal/desktop", this,
                            QDBusConnection::ExportScriptableSlots |
                                QDBusConnection::ExportScriptableProperties)) {
        bus.unregisterService(serviceName);
        return false;
    }
    m_started = true;
    return true;
}
PrintPortal::~PrintPortal() {
    if (!m_started)
        return;
    auto bus = QDBusConnection::sessionBus();
    bus.unregisterObject("/org/freedesktop/portal/desktop");
    bus.unregisterService(serviceName);
}
bool PrintPortal::trustedCaller(const QString &caller) const {
    auto *interface = QDBusConnection::sessionBus().interface();
    return interface && !caller.isEmpty() && interface->serviceOwner(desktopName).value() == caller;
}
uint PrintPortal::PreparePrint(const QDBusObjectPath &handle, const QString &app_id,
                               const QString &parent_window, const QString &title,
                               const QVariantMap &settings, const QVariantMap &page_setup,
                               const QVariantMap &options, QVariantMap &results) {
    Q_UNUSED(parent_window)
    Q_UNUSED(results)
    return enqueue(handle, app_id, title, settings, page_setup, options);
}
uint PrintPortal::Print(const QDBusObjectPath &handle, const QString &app_id, const QString &parent_window,
                        const QString &title, const QDBusUnixFileDescriptor &fd, const QVariantMap &options,
                        QVariantMap &results) {
    Q_UNUSED(parent_window)
    Q_UNUSED(results)
    if (!fd.isValid())
        return 2;
    struct stat info;
    if (::fstat(fd.fileDescriptor(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size > (1LL << 30))
        return 2;
    return enqueue(handle, app_id, title, {}, {}, options, fd);
}
uint PrintPortal::enqueue(const QDBusObjectPath &handle, const QString &appId, const QString &title,
                          const QVariantMap &settings, const QVariantMap &page, const QVariantMap &options,
                          const QDBusUnixFileDescriptor &fd) {
    if (!trustedCaller(message().service())) {
        sendErrorReply(QDBusError::AccessDenied,
                       "Only the desktop portal may call esyuprint's print backend.");
        return 2;
    }
    if (!handle.path().startsWith("/org/freedesktop/portal/desktop/request/") || m_requests.size() >= 8)
        return 2;
    QVariantMap chosenSettings = settings, chosenPage = page;
    bool skipDialog = false, preview = false;
    const auto token = options.value("token").toUInt();
    auto prepared = m_prepared.find(token);
    if (fd.isValid() && prepared != m_prepared.end() && !prepared->expires.hasExpired() &&
        prepared->appId == appId) {
        chosenSettings = prepared->settings;
        chosenPage = prepared->page;
        preview = prepared->preview;
        skipDialog = true;
        m_prepared.erase(prepared);
    }
    auto *request = new PrintRequest(this, message(), handle.path(), appId, title, chosenSettings, chosenPage,
                                     options, fd);
    if (!QDBusConnection::sessionBus().registerObject(handle.path(), request,
                                                      QDBusConnection::ExportScriptableSlots)) {
        request->deleteLater();
        return 2;
    }
    setDelayedReply(true);
    // Keep request-specific activation data with the queued request, not in global state.
    request->setProperty("skipDialog", skipDialog);
    request->setProperty("preview", preview);
    m_requests.append(request);
    if (m_requests.size() == 1)
        QTimer::singleShot(0, request,
                           [request, skipDialog, preview] { request->activate(skipDialog, preview); });
    return 0;
}
uint PrintPortal::remember(const QString &appId, const QVariantMap &settings, const QVariantMap &page,
                           bool preview) {
    // A client may prepare without ever submitting; do not retain unbounded tokens.
    if (m_prepared.size() >= 256) {
        auto oldest = m_prepared.begin();
        for (auto it = m_prepared.begin(); it != m_prepared.end(); ++it)
            if (it->expires.remainingTime() < oldest->expires.remainingTime())
                oldest = it;
        m_prepared.erase(oldest);
    }
    uint token;
    do {
        token = QRandomGenerator::system()->generate();
    } while (!token || m_prepared.contains(token));
    m_prepared.insert(token, {appId, settings, page, preview, QDeadlineTimer(300000)});
    return token;
}
void PrintPortal::completed(PrintRequest *request) {
    const bool active = !m_requests.isEmpty() && m_requests.first() == request;
    m_requests.removeAll(request);
    request->deleteLater();
    if (active && !m_requests.isEmpty()) {
        auto *next = m_requests.first();
        QTimer::singleShot(0, next, [next] {
            next->activate(next->property("skipDialog").toBool(), next->property("preview").toBool());
        });
    }
}

PrintRequest::PrintRequest(PrintPortal *portal, const QDBusMessage &message, const QString &handle,
                           const QString &appId, const QString &title, const QVariantMap &settings,
                           const QVariantMap &page, const QVariantMap &options,
                           const QDBusUnixFileDescriptor &fd)
    : QObject(portal), m_portal(portal), m_message(message), m_handle(handle), m_appId(appId), m_title(title),
      m_settings(initialSettings(settings)), m_page(initialPage(page)), m_options(options), m_fd(fd),
      m_backend(portal->demo(), nullptr, false), m_cancelled(QSharedPointer<std::atomic_bool>::create(false)),
      m_preparing(!fd.isValid()) {
    if (!settings.contains("orientation") && page.contains("Orientation"))
        m_settings["orientation"] = page.value("Orientation").toString().replace('-', '_');
    m_page["Orientation"] = m_settings.value("orientation").toString().replace('_', '-');
    for (const auto &pair : {qMakePair(QString("paper-width"), QString("Width")),
                             qMakePair(QString("paper-height"), QString("Height"))})
        if (settings.contains(pair.first) && !page.contains(pair.second))
            m_page[pair.second] = settings.value(pair.first).toDouble();
    QStringList human;
    for (const auto &range : m_settings.value("page-ranges").toString().split(',', Qt::SkipEmptyParts)) {
        const auto ends = range.split('-');
        QString text = QString::number(ends.first().toInt() + 1);
        if (ends.size() == 2)
            text += "-" + QString::number(ends.last().toInt() + 1);
        human << text;
    }
    m_ranges = human.join(',');
    if (!formats().isEmpty() &&
        !formats().contains(m_settings.value("output-file-format").toString().toLower()))
        m_settings["output-file-format"] = formats().value(0);
    connect(&m_backend, &Backend::snapshotChanged, this, [this] {
        QTimer::singleShot(0, this, [this] {
            if (m_settings.value("printer").toString().isEmpty()) {
                const auto rows = m_backend.snapshot().value("printers").toList();
                QString target;
                for (const auto &row : rows)
                    if (row.toMap().value("default").toBool())
                        target = row.toMap().value("name").toString();
                if (target.isEmpty() && !rows.isEmpty())
                    target = rows.first().toMap().value("name").toString();
                if (!target.isEmpty())
                    setSetting("printer", target);
            } else if (m_settings.value("printer") != "Print to File")
                m_backend.select(m_settings.value("printer").toString());
            emit changed();
        });
    });
    connect(&m_backend, &Backend::changed, this, &PrintRequest::changed);
    connect(&m_chooser, &Portal::chosen, this,
            [this](const QString &path) { setSetting("output-uri", QUrl::fromLocalFile(path).toString()); });
    connect(&m_chooser, &Portal::failed, this, [this](const QString &error) {
        m_error = error;
        emit changed();
    });
}
PrintRequest::~PrintRequest() {
    *m_cancelled = true;
    m_complete = true;
    // Destroy bindings before the QObject members they reference are torn down.
    delete m_window;
    m_window = nullptr;
}
QVariantList PrintRequest::printers() const {
    auto rows = m_backend.snapshot().value("printers").toList();
    if (!formats().isEmpty())
        rows.prepend(QVariantMap{{"name", "Print to File"}, {"label", tr("Print to file")}});
    return rows;
}
QStringList PrintRequest::formats() const {
    auto values = qdbus_cast<QStringList>(m_options.value("supported_output_file_formats"));
    if (!m_options.contains("supported_output_file_formats"))
        values = {"pdf", "ps"};
    QStringList result;
    for (const auto &value : values)
        if (QStringList{"pdf", "ps", "svg"}.contains(value.toLower()))
            result << value.toLower();
    result.removeDuplicates();
    return result;
}
bool PrintRequest::previewAvailable() const { return QFileInfo("/usr/bin/evince-previewer").isExecutable(); }
void PrintRequest::setRanges(const QString &value) {
    m_ranges = value;
    emit changed();
}
void PrintRequest::setSetting(const QString &key, const QString &value) {
    if (m_busy || m_complete || m_failed)
        return;
    m_settings[key] = value;
    m_error.clear();
    if (key == "printer")
        m_backend.select(value == "Print to File" ? QString() : value);
    if (key == "orientation")
        m_page["Orientation"] = QString(value).replace('_', '-');
    emit changed();
}
void PrintRequest::setPageValue(const QString &key, const QVariant &value) {
    if (m_busy || m_complete || m_failed)
        return;
    m_page[key] = value;
    m_error.clear();
    emit changed();
}
void PrintRequest::setPaper(const QString &name) {
    if (m_busy || m_complete || m_failed)
        return;
    static const QRegularExpression size("^(.+)_([0-9.]+)x([0-9.]+)(mm|in)$");
    const auto match = size.match(name);
    if (!match.hasMatch())
        return;
    const double units = match.captured(4) == "in" ? 25.4 : 1;
    m_page["Name"] = match.captured(1);
    m_page["DisplayName"] = name;
    m_page["Width"] = match.captured(2).toDouble() * units;
    m_page["Height"] = match.captured(3).toDouble() * units;
    m_settings["paper-format"] = match.captured(1);
    m_settings["paper-width"] = QString::number(m_page.value("Width").toDouble());
    m_settings["paper-height"] = QString::number(m_page.value("Height").toDouble());
    emit changed();
}
void PrintRequest::chooseOutput() {
    if (!m_busy)
        m_chooser.chooseOutput("Print." + m_settings.value("output-file-format").toString());
}
QString PrintRequest::validateSettings(const QVariantMap &settings, const QVariantMap &page,
                                       const QVariantMap &options, const QString &ranges,
                                       QString *portalRanges) {
    bool ok;
    const int copies = settings.value("n-copies").toString().toInt(&ok);
    if (!ok || copies < 1 || copies > 999)
        return tr("Choose between 1 and 999 copies.");
    const QMap<QString, QStringList> enums{
        {"duplex", {"simplex", "horizontal", "vertical"}},
        {"orientation", {"portrait", "landscape", "reverse_portrait", "reverse_landscape"}},
        {"quality", {"low", "draft", "normal", "high"}},
        {"page-set", {"all", "even", "odd"}},
        {"collate", {"true", "false"}},
        {"reverse", {"true", "false"}},
        {"use-color", {"true", "false"}},
        {"number-up", {"1", "2", "4", "6", "9", "16"}}};
    for (auto it = enums.begin(); it != enums.end(); ++it)
        if (!it.value().contains(settings.value(it.key()).toString()))
            return tr("Choose a valid value for %1.").arg(it.key());
    const double scale = settings.value("scale").toString().toDouble(&ok);
    if (!ok || !std::isfinite(scale) || scale <= 0 || scale > 1000)
        return tr("Choose a scale between 1 and 1000 percent.");
    const auto mode = settings.value("print-pages").toString();
    if (!QStringList{"all", "ranges", "current", "selection"}.contains(mode))
        return tr("Choose which pages to print.");
    if (mode == "current" && !options.value("has_current_page").toBool())
        return tr("This application did not provide a current page.");
    if (mode == "selection" && !options.value("has_selected_pages").toBool())
        return tr("This application did not provide selected pages.");
    QStringList converted;
    if (mode == "ranges") {
        static const QRegularExpression pattern("^([1-9][0-9]*)(?:\\s*-\\s*([1-9][0-9]*))?$");
        for (const auto &entry : ranges.split(',')) {
            const auto match = pattern.match(entry.trimmed());
            const int first = match.captured(1).toInt(&ok);
            if (!match.hasMatch() || !ok)
                return tr("Use page ranges such as 1-3,5,8-10.");
            int last = first;
            if (!match.captured(2).isEmpty())
                last = match.captured(2).toInt(&ok);
            if (!ok || last < first)
                return tr("Page ranges must run from a lower to a higher page.");
            converted << (last == first ? QString::number(first - 1)
                                        : QString("%1-%2").arg(first - 1).arg(last - 1));
        }
    }
    const double width = page.value("Width").toDouble(), height = page.value("Height").toDouble();
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
        return tr("Paper dimensions must be positive numbers.");
    for (const auto &key : {"MarginTop", "MarginBottom", "MarginLeft", "MarginRight"}) {
        const double value = page.value(key).toDouble();
        if (!std::isfinite(value) || value < 0)
            return tr("Margins must be nonnegative numbers.");
    }
    if (page.value("MarginLeft").toDouble() + page.value("MarginRight").toDouble() >= width ||
        page.value("MarginTop").toDouble() + page.value("MarginBottom").toDouble() >= height)
        return tr("Margins leave no printable area.");
    if (portalRanges)
        *portalRanges = converted.join(',');
    return {};
}
void PrintRequest::activate(bool skipDialog, bool preview) {
    if (m_complete)
        return;
    if (skipDialog) {
        submit(preview);
        return;
    }
    if (m_portal->demo() && m_settings.value("printer").toString().isEmpty())
        setSetting("printer", "Studio_Printer");
    m_backend.refresh();
    auto *context = new QQmlContext(m_portal->engine()->rootContext(), this);
    context->setContextProperty("request", this);
    context->setContextProperty("backend", &m_backend);
    QQmlComponent component(m_portal->engine(), QUrl("qrc:/PrintWindow.qml"));
    m_window = qobject_cast<QQuickWindow *>(component.create(context));
    if (!m_window) {
        finish(2, {{"error", component.errorString()}});
        return;
    }
    m_window->QObject::setParent(this);
    m_window->show();
    m_window->requestActivate();
}
void PrintRequest::accept(bool preview) {
    if (m_busy || m_complete || m_failed)
        return;
    if (m_portal->demo() && m_message.type() == QDBusMessage::InvalidMessage) {
        m_error = tr("Preview mode · no print jobs or files are created.");
        emit changed();
        return;
    }
    QString portalRanges;
    m_error = validateSettings(m_settings, m_page, m_options, m_ranges, &portalRanges);
    bool found = false;
    for (const auto &row : printers())
        if (row.toMap().value("name") == m_settings.value("printer"))
            found = true;
    if (!found)
        m_error = tr("Select an available printer or print to file.");
    if (m_settings.value("printer") == "Print to File") {
        const QUrl uri(m_settings.value("output-uri").toString());
        const auto path = uri.toLocalFile();
        if (!uri.isLocalFile() || !QFileInfo(path).isAbsolute() || QFileInfo::exists(path) ||
            !QFileInfo(QFileInfo(path).absolutePath()).isWritable())
            m_error = tr("Choose a new output file in a writable folder.");
        if (!formats().contains(m_settings.value("output-file-format").toString().toLower()))
            m_error = tr("Choose a format supported by the requesting application.");
    }
    if (preview && !previewAvailable())
        m_error = tr("Print preview needs evince-previewer.");
    if (preview && m_settings.value("printer") == "Print to File")
        m_error = tr(
            "Choose a physical printer to preview; print-to-file saves the application’s rendered document.");
    if (!m_error.isEmpty()) {
        emit changed();
        return;
    }
    m_settings["page-ranges"] = portalRanges;
    m_settings["n-copies"] = QString::number(m_settings.value("n-copies").toString().toInt());
    if (m_preparing) {
        const uint token = m_portal->remember(m_appId, m_settings, m_page, preview);
        finish(0, {{"settings", m_settings}, {"page-setup", m_page}, {"token", token}});
    } else
        submit(preview);
}
void PrintRequest::submit(bool preview) {
    if (m_portal->demo()) {
        finish(0, {{"demo", true}});
        return;
    }
    m_busy = true;
    emit changed();
    m_storage = QSharedPointer<QTemporaryDir>::create(QDir::tempPath() + "/esyuprint-print-XXXXXX");
    if (!m_storage->isValid()) {
        fail(tr("Cannot create a private print spool directory."));
        return;
    }
    auto storage = m_storage;
    const auto descriptor = m_fd;
    const auto cancelled = m_cancelled;
    connect(&m_copy, &QFutureWatcher<QString>::finished, this, [this, preview, storage] {
        if (m_complete)
            return;
        const auto error = m_copy.result();
        if (!error.isEmpty()) {
            fail(error);
            return;
        }
        auto *job = new PrintJob(m_portal);
        // Preview and asynchronous SDK jobs retain their private document until the consumer exits.
        connect(job, &QObject::destroyed, m_portal, [storage] {});
        connect(job, &PrintJob::finished, this, [this](const QString &error) {
            if (error.isEmpty())
                finish(0);
            else
                fail(error);
        });
        job->start(storage->filePath("document"), m_title, m_settings, m_page, preview);
    });
    m_copy.setFuture(QtConcurrent::run([descriptor, storage, cancelled]() -> QString {
        QSaveFile output(storage->filePath("document"));
        if (!output.open(QIODevice::WriteOnly))
            return output.errorString();
        struct stat info;
        if (::fstat(descriptor.fileDescriptor(), &info) != 0)
            return QString::fromLocal8Bit(std::strerror(errno));
        if (!S_ISREG(info.st_mode) || info.st_size < 0 || info.st_size > (1LL << 30))
            return QStringLiteral("esyuprint accepts regular print documents up to 1 GiB.");
        QByteArray buffer(1024 * 1024, Qt::Uninitialized);
        off_t offset = 0;
        while (offset < info.st_size) {
            if (*cancelled)
                return QStringLiteral("Print request canceled.");
            const auto amount = ::pread(descriptor.fileDescriptor(), buffer.data(),
                                        qMin<off_t>(buffer.size(), info.st_size - offset), offset);
            if (amount < 0 && errno == EINTR)
                continue;
            if (amount <= 0)
                return QStringLiteral("Cannot read the complete print document.");
            if (output.write(buffer.constData(), amount) != amount)
                return output.errorString();
            offset += amount;
        }
        return output.commit() ? QString() : output.errorString();
    }));
}
void PrintRequest::finish(uint response, const QVariantMap &results) {
    if (m_complete)
        return;
    m_complete = true;
    *m_cancelled = true;
    if (!m_handle.isEmpty())
        QDBusConnection::sessionBus().unregisterObject(m_handle);
    if (m_message.type() != QDBusMessage::InvalidMessage)
        QDBusConnection::sessionBus().send(m_message.createReply({response, results}));
    if (m_window)
        m_window->close();
    m_portal->completed(this);
}
void PrintRequest::fail(const QString &error) {
    if (m_complete)
        return;
    m_failed = true;
    m_busy = false;
    m_error = error;
    if (!m_window)
        activate();
    emit changed();
}
void PrintRequest::setOutputPath(const QString &path) {
    setSetting("output-uri", QUrl::fromLocalFile(path).toString());
}
void PrintRequest::cancel() {
    finish(m_failed ? 2 : 1, m_failed ? QVariantMap{{"error", m_error}} : QVariantMap{});
}
void PrintRequest::Close() {
    if (!m_portal->trustedCaller(message().service())) {
        sendErrorReply(QDBusError::AccessDenied, "Only the desktop portal may cancel this request.");
        return;
    }
    cancel();
}
