#pragma once
#include "backend.h"
#include "portal.h"
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QDeadlineTimer>
#include <QFutureWatcher>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSharedPointer>
#include <QTemporaryDir>
#include <QUrl>
#include <atomic>

class PrintPortal;
class PrintRequest : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Request")
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString appId READ appId CONSTANT)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY changed)
    Q_PROPERTY(QVariantMap pageSetup READ pageSetup NOTIFY changed)
    Q_PROPERTY(QVariantMap options READ options CONSTANT)
    Q_PROPERTY(QVariantList printers READ printers NOTIFY changed)
    Q_PROPERTY(QStringList formats READ formats CONSTANT)
    Q_PROPERTY(QString ranges READ ranges WRITE setRanges NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool failed READ failed NOTIFY changed)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY changed)
    Q_PROPERTY(bool previewAvailable READ previewAvailable CONSTANT)
    Q_PROPERTY(Backend *backend READ backend CONSTANT)
  public:
    PrintRequest(PrintPortal *portal, const QDBusMessage &message, const QString &handle,
                 const QString &appId, const QString &title, const QVariantMap &settings,
                 const QVariantMap &pageSetup, const QVariantMap &options,
                 const QDBusUnixFileDescriptor &fd = {});
    ~PrintRequest() override;
    QString title() const { return m_title; }
    QString appId() const { return m_appId; }
    QVariantMap settings() const { return m_settings; }
    QVariantMap pageSetup() const { return m_page; }
    QVariantMap options() const { return m_options; }
    QVariantList printers() const;
    QStringList formats() const;
    QString ranges() const { return m_ranges; }
    void setRanges(const QString &value);
    QString error() const { return m_error; }
    bool busy() const { return m_busy; }
    bool failed() const { return m_failed; }
    QString outputPath() const { return QUrl(m_settings.value("output-uri").toString()).toLocalFile(); }
    bool previewAvailable() const;
    Backend *backend() { return &m_backend; }
    QQuickWindow *window() const { return m_window; }
    void activate(bool skipDialog = false, bool preview = false);
    Q_INVOKABLE void setSetting(const QString &key, const QString &value);
    Q_INVOKABLE void setPageValue(const QString &key, const QVariant &value);
    Q_INVOKABLE void setPaper(const QString &name);
    Q_INVOKABLE void chooseOutput();
    Q_INVOKABLE void setOutputPath(const QString &path);
    Q_INVOKABLE void accept(bool preview = false);
    Q_INVOKABLE void cancel();
    static QString validateSettings(const QVariantMap &settings, const QVariantMap &page,
                                    const QVariantMap &options, const QString &ranges,
                                    QString *portalRanges = nullptr);
  public slots:
    Q_SCRIPTABLE void Close();
  signals:
    void changed();

  private:
    void finish(uint response, const QVariantMap &results = {});
    void submit(bool preview);
    void fail(const QString &error);
    PrintPortal *m_portal;
    QDBusMessage m_message;
    QString m_handle, m_appId, m_title, m_error, m_ranges;
    QVariantMap m_settings, m_page, m_options;
    QDBusUnixFileDescriptor m_fd;
    Backend m_backend;
    Portal m_chooser;
    QQuickWindow *m_window = nullptr;
    QSharedPointer<QTemporaryDir> m_storage;
    QSharedPointer<std::atomic_bool> m_cancelled;
    QFutureWatcher<QString> m_copy;
    bool m_busy = false, m_complete = false, m_preparing = true;
    bool m_failed = false;
};

class PrintPortal : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Print")
    Q_PROPERTY(uint version READ version CONSTANT)
  public:
    explicit PrintPortal(QQmlEngine *engine, bool demo = false, QObject *parent = nullptr);
    ~PrintPortal() override;
    bool start();
    uint version() const { return 4; }
    bool demo() const { return m_demo; }
    QQmlEngine *engine() const { return m_engine; }
    bool trustedCaller(const QString &caller) const;
    void completed(PrintRequest *request);
    uint remember(const QString &appId, const QVariantMap &settings, const QVariantMap &page, bool preview);
  public slots:
    Q_SCRIPTABLE uint PreparePrint(const QDBusObjectPath &handle, const QString &app_id,
                                   const QString &parent_window, const QString &title,
                                   const QVariantMap &settings, const QVariantMap &page_setup,
                                   const QVariantMap &options, QVariantMap &results);
    Q_SCRIPTABLE uint Print(const QDBusObjectPath &handle, const QString &app_id,
                            const QString &parent_window, const QString &title,
                            const QDBusUnixFileDescriptor &fd, const QVariantMap &options,
                            QVariantMap &results);

  private:
    friend class PrintRequest;
    struct Prepared {
        QString appId;
        QVariantMap settings, page;
        bool preview;
        QDeadlineTimer expires;
    };
    uint enqueue(const QDBusObjectPath &handle, const QString &appId, const QString &title,
                 const QVariantMap &settings, const QVariantMap &page, const QVariantMap &options,
                 const QDBusUnixFileDescriptor &fd = {});
    QQmlEngine *m_engine;
    bool m_demo;
    bool m_started = false;
    QList<PrintRequest *> m_requests;
    QHash<uint, Prepared> m_prepared;
    QTimer m_cleanup;
};
