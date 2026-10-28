#include "printjob.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <cerrno>
#include <cstring>
#include <unistd.h>

#undef signals
#undef slots
#include <gtk/gtkunixprint.h>

static GVariant *variantMap(const QVariantMap &map) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    for (auto it = map.begin(); it != map.end(); ++it) {
        GVariant *value = it.value().metaType().id() == QMetaType::Double
                              ? g_variant_new_double(it.value().toDouble())
                              : g_variant_new_string(it.value().toString().toUtf8().constData());
        g_variant_builder_add(&builder, "{sv}", it.key().toUtf8().constData(), value);
    }
    return g_variant_ref_sink(g_variant_builder_end(&builder));
}

struct PrintJob::State {
    PrintJob *owner;
    GtkPrintSettings *settings = nullptr;
    GtkPageSetup *page = nullptr;
    GtkPrintJob *job = nullptr;
    GtkPrinter *printer = nullptr;
    gulong detailsHandler = 0;
    QString file, title, destination, temporary;
    QTimer pump, timeout;
    bool reported = false;
    bool started = false;

    explicit State(PrintJob *owner) : owner(owner) {}
    ~State() {
        if (printer && detailsHandler)
            g_signal_handler_disconnect(printer, detailsHandler);
        if (printer)
            g_object_unref(printer);
        if (job)
            g_object_unref(job);
        if (settings)
            g_object_unref(settings);
        if (page)
            g_object_unref(page);
        if (!temporary.isEmpty())
            QFile::remove(temporary);
    }
    void report(const QString &error) {
        if (reported)
            return;
        reported = true;
        timeout.stop();
        Q_EMIT owner->finished(error);
    }
    void send(GtkPrinter *printer) {
        started = true;
        job = gtk_print_job_new(title.toUtf8().constData(), printer, settings, page);
        GError *error = nullptr;
        if (!gtk_print_job_set_source_file(job, file.toUtf8().constData(), &error)) {
            const auto message = QString::fromUtf8(error ? error->message : "Cannot read print document");
            g_clear_error(&error);
            report(message);
            owner->deleteLater();
            return;
        }
        auto *guard = new QPointer<PrintJob>(owner);
        gtk_print_job_send(
            job,
            [](GtkPrintJob *, gpointer data, const GError *error) {
                const auto guard = static_cast<QPointer<PrintJob> *>(data);
                if (!*guard)
                    return;
                auto *q = guard->data();
                auto &s = *q->m_state;
                QString message = error ? QString::fromUtf8(error->message) : QString();
                if (message.isEmpty() && !s.reported && !s.destination.isEmpty()) {
                    // Publishing by link is atomic and cannot overwrite a file created since the dialog.
                    if (::link(QFile::encodeName(s.temporary).constData(),
                               QFile::encodeName(s.destination).constData()) != 0)
                        message = q->tr("Cannot save print output without overwriting: %1")
                                      .arg(QString::fromLocal8Bit(std::strerror(errno)));
                }
                s.report(message);
                q->deleteLater();
            },
            guard, [](gpointer data) { delete static_cast<QPointer<PrintJob> *>(data); });
    }
};

PrintJob::PrintJob(QObject *parent) : QObject(parent), m_state(std::make_unique<State>(this)) {}
PrintJob::~PrintJob() = default;

void PrintJob::start(const QString &file, const QString &title, const QVariantMap &settings,
                     const QVariantMap &pageSetup, bool preview) {
    auto &s = *m_state;
    if (s.settings || s.reported)
        return;
    s.file = file;
    s.title = title;
    if (!gtk_init_check(nullptr, nullptr)) {
        s.report(tr("The GTK printing SDK cannot connect to the desktop display."));
        deleteLater();
        return;
    }
    GVariant *settingVariant = variantMap(settings), *pageVariant = variantMap(pageSetup);
    s.settings = gtk_print_settings_new_from_gvariant(settingVariant);
    s.page = gtk_page_setup_new_from_gvariant(pageVariant);
    g_variant_unref(settingVariant);
    g_variant_unref(pageVariant);
    if (!s.settings || !s.page) {
        s.report(tr("The printing SDK rejected the page setup or print settings."));
        deleteLater();
        return;
    }
    if (preview) {
        QTemporaryFile config(QDir::tempPath() + "/esyuprint-preview-XXXXXX");
        config.setAutoRemove(false);
        if (!config.open()) {
            s.report(config.errorString());
            deleteLater();
            return;
        }
        s.temporary = config.fileName();
        GKeyFile *key = g_key_file_new();
        gtk_print_settings_to_key_file(s.settings, key, nullptr);
        gtk_page_setup_to_key_file(s.page, key, nullptr);
        g_key_file_set_string(key, "Print Job", "title", title.toUtf8().constData());
        gsize length = 0;
        gchar *bytes = g_key_file_to_data(key, &length, nullptr);
        const bool written = config.write(bytes, length) == qint64(length) && config.flush();
        g_free(bytes);
        g_key_file_unref(key);
        config.close();
        if (!written) {
            s.report(tr("Cannot write private preview settings."));
            deleteLater();
            return;
        }
        auto *process = new QProcess(this);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("GTK_USE_PORTAL", "1");
        process->setProcessEnvironment(environment);
        connect(process, &QProcess::errorOccurred, this, [this, process] {
            m_state->report(tr("Cannot open print preview: %1").arg(process->errorString()));
            deleteLater();
        });
        connect(process, &QProcess::started, this, [this] { m_state->report({}); });
        connect(process, &QProcess::finished, this, &QObject::deleteLater);
        process->start("/usr/bin/evince-previewer", {"--print-settings", s.temporary, file});
        return;
    }
    if (settings.value("printer").toString() == "Print to File") {
        gtk_print_settings_set_printer(s.settings, g_dgettext("gtk30", "Print to File"));
        s.destination = QUrl(settings.value("output-uri").toString()).toLocalFile();
        QTemporaryFile output(QFileInfo(s.destination).absolutePath() + "/.esyuprint-print-XXXXXX");
        output.setAutoRemove(false);
        if (s.destination.isEmpty() || QFileInfo::exists(s.destination) || !output.open()) {
            s.report(tr("Choose a new, writable output file; existing files are never overwritten."));
            deleteLater();
            return;
        }
        s.temporary = output.fileName();
        output.close();
        gtk_print_settings_set(s.settings, "output-uri",
                               QUrl::fromLocalFile(s.temporary).toEncoded().constData());
    }
    connect(&s.pump, &QTimer::timeout, this, [] {
        while (g_main_context_pending(nullptr))
            g_main_context_iteration(nullptr, FALSE);
    });
    s.pump.start(15);
    s.timeout.setSingleShot(true);
    connect(&s.timeout, &QTimer::timeout, this, [this] {
        m_state->report(tr(
            "Printing timed out. A job may already have been submitted; check the queue before retrying."));
        // An in-flight SDK job must retain its callbacks and transport until completion.
        if (!m_state->job)
            deleteLater();
    });
    s.timeout.start(90000);
    auto *guard = new QPointer<PrintJob>(this);
    gtk_enumerate_printers(
        [](GtkPrinter *printer, gpointer data) -> gboolean {
            auto *guard = static_cast<QPointer<PrintJob> *>(data);
            if (!*guard)
                return TRUE;
            auto *q = guard->data();
            if (QString::fromUtf8(gtk_printer_get_name(printer)) !=
                QString::fromUtf8(gtk_print_settings_get_printer(q->m_state->settings)))
                return FALSE;
            auto &s = *q->m_state;
            s.started = true;
            if (gtk_printer_has_details(printer))
                s.send(printer);
            else {
                s.printer = GTK_PRINTER(g_object_ref(printer));
                s.detailsHandler = g_signal_connect(
                    printer, "details-acquired",
                    G_CALLBACK(+[](GtkPrinter *printer, gboolean success, gpointer data) {
                        auto *state = static_cast<State *>(data);
                        if (success && !state->reported)
                            state->send(printer);
                        else {
                            state->report(state->owner->tr("Cannot load printer capabilities."));
                            state->owner->deleteLater();
                        }
                    }),
                    &s);
                gtk_printer_request_details(printer);
            }
            return TRUE;
        },
        guard,
        [](gpointer data) {
            auto *guard = static_cast<QPointer<PrintJob> *>(data);
            if (*guard && !(*guard)->m_state->started) {
                (*guard)->m_state->report((*guard)->tr("The selected printer is no longer available."));
                (*guard)->deleteLater();
            }
            delete guard;
        },
        FALSE);
}
