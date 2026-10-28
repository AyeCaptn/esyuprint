#include "backend.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUrl>

QByteArray Backend::bridgeSource() {
    QFile file(":/bridge.py");
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
Backend::Backend(bool demo, QObject *parent, bool autoStart, const QByteArray &bridge)
    : QObject(parent), m_demo(demo), m_pollEnabled(autoStart), m_bridge(bridge) {
    connect(this, &Backend::snapshotChanged, this, &Backend::updateRows);
    connect(this, &Backend::detailsChanged, this, &Backend::updateRows);
    connect(this, &Backend::optionsChanged, this, &Backend::updateRows);
    connect(this, &Backend::selectedChanged, this, &Backend::updateRows);
    connect(&m_portal, &Portal::chosen, this, [this](const QString &path) {
        m_ppdFile = path;
        emit changed();
    });
    connect(&m_portal, &Portal::failed, this, [this](const QString &message) {
        m_error = message;
        emit changed();
    });
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        m_timedOut = true;
        m_process.kill();
    });
    connect(&m_process, &QProcess::finished, this, &Backend::completed);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_timeout.stop();
            m_error = tr("Cannot start system Python: %1").arg(m_process.errorString());
            emit changed();
        }
    });
    m_poll.setInterval(10000);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        if (!running() && m_pending.isEmpty() && m_retry.isEmpty())
            refresh(true);
    });
    if (demo)
        loadDemo();
    else if (autoStart) {
        m_poll.start();
        QTimer::singleShot(0, this, [this] { refresh(); });
    }
}
Backend::~Backend() {
    if (running()) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}
QString Backend::serverLabel() const {
    const auto host = m_server.value("host").toString();
    return host.isEmpty() ? tr("Default CUPS server")
                          : host + ":" + QString::number(m_server.value("port").toInt(631));
}
QVariantMap Backend::pending() const {
    auto display = m_pending.toVariantMap();
    display.remove("server");
    // Job authentication credentials must never be displayed in the confirmation text.
    if (display.value("action") == "job-authenticate")
        display["args"] = tr("Credentials (not displayed)");
    return display;
}
void Backend::select(const QString &name) {
    if (name == m_selected)
        return;
    m_selected = name;
    m_details.clear();
    m_options.clear();
    m_diagnostics.clear();
    emit selectedChanged();
    emit detailsChanged();
    emit optionsChanged();
    emit diagnosticsChanged();
    emit changed();
    if (m_demo)
        loadDemo();
    else if (!name.isEmpty())
        query("details");
}
void Backend::refresh(bool background) {
    if (m_demo)
        return;
    request({{"action", "overview"}}, background);
}
void Backend::query(const QString &action) {
    if (m_demo)
        return;
    if (!QStringList{"details", "devices", "devices-authorized", "drivers", "server", "diagnose"}.contains(
            action))
        return;
    if (running()) {
        m_deferredQuery = action;
        emit changed();
        return;
    }
    request({{"action", action}, {"name", m_selected}});
}
void Backend::choosePpd() {
    if (!busy() && !m_demo)
        m_portal.choosePpd();
}
void Backend::request(QJsonObject req, bool background) {
    if (running()) {
        if (m_background && !background) {
            m_queuedRequest = req;
            emit changed();
        }
        return;
    }
    m_background = background && !m_snapshot.isEmpty();
    if (!m_background)
        m_error.clear();
    m_timedOut = false;
    req["schema"] = 1;
    if (!req.contains("server"))
        req["server"] = m_server;
    m_request = req;
    m_process.start("/usr/bin/python3", {"-I", "-c", QString::fromUtf8(m_bridge)});
    m_process.write(QJsonDocument(req).toJson(QJsonDocument::Compact));
    m_process.closeWriteChannel();
    // Allow time for a human to answer PolicyKit, but never hang the interface forever.
    m_timeout.start(req.value("apply").toBool() || req.value("action") == "devices-authorized" ? 180000
                                                                                               : 30000);
    emit changed();
}
void Backend::completed(int code, QProcess::ExitStatus status) {
    m_timeout.stop();
    QJsonParseError parse;
    const auto result = QJsonDocument::fromJson(m_process.readAllStandardOutput(), &parse).object();
    const bool mutation = m_request.value("apply").toBool();
    const QString action = m_request.value("action").toString();
    const bool background = m_background;
    if (m_request.value("server").toObject() != m_server) {
        // A connection chosen during a background poll must not receive the old server's data.
    } else if (status != QProcess::NormalExit || parse.error != QJsonParseError::NoError ||
               result.value("schema").toInt() != 1) {
        m_error =
            m_timedOut
                ? tr("Request timed out. A submitted change may have completed; refresh before retrying.")
                : tr("Printing backend failed: %1")
                      .arg(QString::fromUtf8(m_process.readAllStandardError()).left(2000));
    } else if (code != 0 || !result.value("ok").toBool()) {
        m_error = result.value("error").toString(tr("Unknown printing error"));
        if (result.value("code") == "authentication" && !m_error.contains("pkcancel")) {
            m_poll.stop();
            m_retry = m_request;
            emit authenticationRequired();
        }
    } else {
        m_error.clear();
        if (!m_demo && m_pollEnabled)
            m_poll.start();
        const auto data = result.value("data");
        if (action == "overview") {
            const auto snapshot = data.toObject().toVariantMap();
            if (snapshot != m_snapshot) {
                m_snapshot = snapshot;
                emit snapshotChanged();
            }
            const auto printers = m_snapshot.value("printers").toList();
            bool found = false;
            for (const auto &printer : printers)
                if (printer.toMap().value("name") == m_selected)
                    found = true;
            if (!found) {
                const auto selected =
                    printers.isEmpty() ? QString() : printers.first().toMap().value("name").toString();
                if (selected != m_selected) {
                    m_selected = selected;
                    m_details.clear();
                    m_options.clear();
                    m_diagnostics.clear();
                    emit selectedChanged();
                    emit detailsChanged();
                    emit optionsChanged();
                    emit diagnosticsChanged();
                }
            }
        } else if (action == "details" && m_request.value("name").toString() == m_selected) {
            const auto details = data.toObject().toVariantMap();
            const auto options = details.value("options").toList();
            if (options != m_options) {
                m_options = options;
                emit optionsChanged();
            }
            if (details != m_details) {
                m_details = details;
                emit detailsChanged();
            }
        } else if (action == "devices" || action == "devices-authorized") {
            m_devices = data.toObject().value("devices").toArray().toVariantList();
            const auto warnings = data.toObject().value("warnings").toArray();
            if (!warnings.isEmpty()) {
                QStringList messages;
                for (const auto &warning : warnings)
                    messages << warning.toString();
                m_notice = messages.join('\n');
            }
            emit devicesChanged();
        } else if (action == "drivers") {
            m_drivers = data.toArray().toVariantList();
            emit driversChanged();
        } else if (action == "server") {
            m_settings = data.toObject().toVariantMap();
            emit settingsChanged();
        } else if (action == "diagnose" && m_request.value("name").toString() == m_selected) {
            m_diagnostics = data.toObject().toVariantMap();
            emit diagnosticsChanged();
        } else if (mutation) {
            m_notice = tr("Completed: %1").arg(action);
            // Retain the last good view until the follow-up query replaces it.
            if (action == "server-save")
                m_deferredQuery = "server";
            if ((action == "delete" || action == "class-delete") &&
                m_request.value("name").toString() == m_selected) {
                m_selected.clear();
                m_details.clear();
                m_options.clear();
                m_diagnostics.clear();
                emit selectedChanged();
                emit detailsChanged();
                emit optionsChanged();
                emit diagnosticsChanged();
            }
        }
    }
    m_request = {};
    m_background = false;
    emit changed();
    if (!m_queuedRequest.isEmpty()) {
        const auto queued = m_queuedRequest;
        m_queuedRequest = {};
        QTimer::singleShot(0, this, [this, queued] { request(queued); });
        return;
    }
    if (!m_deferredQuery.isEmpty() && m_error.isEmpty()) {
        const auto deferred = m_deferredQuery;
        m_deferredQuery.clear();
        QTimer::singleShot(0, this, [this, deferred] { query(deferred); });
        return;
    }
    if (m_error.isEmpty() && m_retry.isEmpty()) {
        if (mutation)
            QTimer::singleShot(0, this, [this] { refresh(); });
        else if ((action == "overview" || action == "details") && !m_selected.isEmpty() &&
                 (action == "overview" || m_details.value("name").toString() != m_selected))
            QTimer::singleShot(0, this, [this, background] {
                request({{"action", "details"}, {"name", m_selected}}, background);
            });
    }
}
void Backend::stage(const QString &action, const QVariantMap &arguments) {
    if (busy())
        return;
    if (m_demo) {
        m_notice = tr("Preview only · no printer changes or print jobs are permitted");
        emit changed();
        return;
    }
    auto args = QJsonObject::fromVariantMap(arguments);
    m_pending = {{"action", action},
                 {"name", args.take("name").toString(m_selected)},
                 {"args", args},
                 {"server", m_server}};
    if (args.contains("job"))
        m_pending["job"] = args.take("job");
    m_pending["args"] = args;
    emit changed();
}
void Backend::confirm() {
    if (busy() || m_pending.isEmpty())
        return;
    auto req = m_pending;
    m_pending = {};
    req["apply"] = true;
    request(req);
}
void Backend::cancelPending() {
    m_pending = {};
    m_retry = {};
    emit changed();
}
void Backend::connectServer(const QString &host, int port, const QString &user, const QString &password) {
    if (busy() || !m_pending.isEmpty())
        return;
    m_retry = {};
    m_server =
        host.trimmed().isEmpty()
            ? QJsonObject{}
            : QJsonObject{{"host", host.trimmed()}, {"port", port}, {"user", user}, {"password", password}};
    m_snapshot.clear();
    m_details.clear();
    m_options.clear();
    m_settings.clear();
    m_devices.clear();
    m_drivers.clear();
    m_diagnostics.clear();
    m_selected.clear();
    m_deferredQuery.clear();
    emit snapshotChanged();
    emit detailsChanged();
    emit optionsChanged();
    emit settingsChanged();
    emit devicesChanged();
    emit driversChanged();
    emit diagnosticsChanged();
    emit selectedChanged();
    emit changed();
    refresh();
}
void Backend::authenticate(const QString &user, const QString &password) {
    if (m_retry.isEmpty() || busy())
        return;
    m_server["user"] = user;
    m_server["password"] = password;
    auto req = m_retry;
    req["server"] = m_server;
    m_retry = {};
    request(req);
}
void Backend::loadDemo() {
    QFile file(":/demo.json");
    if (!file.open(QIODevice::ReadOnly))
        return;
    const auto data = QJsonDocument::fromJson(file.readAll()).object();
    m_snapshot = data.value("snapshot").toObject().toVariantMap();
    if (m_selected.isEmpty())
        m_selected = "Studio_Printer";
    m_details = m_selected == "Studio_Printer" ? data.value("details").toObject().toVariantMap()
                                               : QVariantMap{{"name", m_selected}};
    m_options = m_details.value("options").toList();
    m_settings = data.value("settings").toObject().toVariantMap();
    m_diagnostics = data.value("diagnostics").toObject().toVariantMap();
    m_drivers = {{QVariantMap{{"id", "everywhere"}, {"label", "IPP Everywhere (driverless)"}}}};
    emit snapshotChanged();
    emit detailsChanged();
    emit optionsChanged();
    emit settingsChanged();
    emit driversChanged();
    emit diagnosticsChanged();
    emit selectedChanged();
    emit changed();
}

void Backend::setPrinterFilter(const QString &filter) {
    if (filter == m_printerFilter)
        return;
    m_printerFilter = filter;
    updateRows();
    emit printerFilterChanged();
}

void Backend::updateRows() {
    QVariantList printers, jobs;
    for (const auto &value : m_snapshot.value("printers").toList()) {
        const auto row = value.toMap();
        if ((row.value("label").toString() + " " + row.value("name").toString())
                .contains(m_printerFilter, Qt::CaseInsensitive))
            printers << value;
    }
    for (const auto &value : m_snapshot.value("jobs").toList()) {
        const auto row = value.toMap();
        if (m_selected.isEmpty() || QUrl(row.value("job-printer-uri").toString()).fileName() == m_selected)
            jobs << value;
    }
    m_printerRows.update(printers);
    m_jobRows.update(jobs);
    m_optionRows.update(m_options);
    m_supplyRows.update(m_details.value("supplies").toList());
}
