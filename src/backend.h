#pragma once
#include "portal.h"
#include "rowmodel.h"
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantMap details READ details NOTIFY detailsChanged)
    Q_PROPERTY(QVariantList options READ options NOTIFY optionsChanged)
    Q_PROPERTY(RowModel *printerRows READ printerRows CONSTANT)
    Q_PROPERTY(RowModel *optionRows READ optionRows CONSTANT)
    Q_PROPERTY(RowModel *supplyRows READ supplyRows CONSTANT)
    Q_PROPERTY(RowModel *jobRows READ jobRows CONSTANT)
    Q_PROPERTY(QString printerFilter READ printerFilter WRITE setPrinterFilter NOTIFY printerFilterChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList drivers READ drivers NOTIFY driversChanged)
    Q_PROPERTY(QVariantMap serverSettings READ serverSettings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap diagnostics READ diagnostics NOTIFY diagnosticsChanged)
    Q_PROPERTY(QVariantMap pending READ pending NOTIFY changed)
    Q_PROPERTY(QString selected READ selected WRITE select NOTIFY selectedChanged)
    Q_PROPERTY(QString serverLabel READ serverLabel NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool mutating READ mutating NOTIFY changed)
    Q_PROPERTY(bool demo READ demo CONSTANT)
    Q_PROPERTY(QString ppdFile READ ppdFile NOTIFY changed)
  public:
    explicit Backend(bool demo = false, QObject *parent = nullptr, bool autoStart = true,
                     const QByteArray &bridge = bridgeSource());
    ~Backend() override;
    QVariantMap snapshot() const { return m_snapshot; }
    QVariantMap details() const { return m_details; }
    QVariantList options() const { return m_options; }
    RowModel *printerRows() { return &m_printerRows; }
    RowModel *optionRows() { return &m_optionRows; }
    RowModel *supplyRows() { return &m_supplyRows; }
    RowModel *jobRows() { return &m_jobRows; }
    QString printerFilter() const { return m_printerFilter; }
    void setPrinterFilter(const QString &filter);
    QVariantList devices() const { return m_devices; }
    QVariantList drivers() const { return m_drivers; }
    QVariantMap serverSettings() const { return m_settings; }
    QVariantMap diagnostics() const { return m_diagnostics; }
    QVariantMap pending() const;
    QString selected() const { return m_selected; }
    QString serverLabel() const;
    QString error() const { return m_error; }
    QString notice() const { return m_notice; }
    bool busy() const {
        return running() && (!m_background || !m_queuedRequest.isEmpty() || !m_deferredQuery.isEmpty());
    }
    bool mutating() const {
        return m_queuedRequest.value("apply").toBool() || (running() && m_request.value("apply").toBool());
    }
    bool demo() const { return m_demo; }
    QString ppdFile() const { return m_ppdFile; }
    Q_INVOKABLE void choosePpd();
    Q_INVOKABLE void select(const QString &name);
    Q_INVOKABLE void refresh(bool background = false);
    Q_INVOKABLE void query(const QString &action);
    Q_INVOKABLE void stage(const QString &action, const QVariantMap &arguments = {});
    Q_INVOKABLE void confirm();
    Q_INVOKABLE void cancelPending();
    Q_INVOKABLE void connectServer(const QString &host, int port, const QString &user,
                                   const QString &password);
    Q_INVOKABLE void authenticate(const QString &user, const QString &password);
    static QByteArray bridgeSource();
  signals:
    void changed();
    void snapshotChanged();
    void detailsChanged();
    void optionsChanged();
    void devicesChanged();
    void driversChanged();
    void settingsChanged();
    void diagnosticsChanged();
    void selectedChanged();
    void authenticationRequired();
    void printerFilterChanged();

  private:
    bool running() const { return m_process.state() != QProcess::NotRunning; }
    void request(QJsonObject request, bool background = false);
    void updateRows();
    void completed(int code, QProcess::ExitStatus status);
    void loadDemo();
    bool m_demo;
    bool m_pollEnabled;
    QByteArray m_bridge;
    QProcess m_process;
    QTimer m_timeout, m_poll;
    QJsonObject m_server, m_request, m_pending, m_retry, m_queuedRequest;
    QVariantMap m_snapshot, m_details, m_settings, m_diagnostics;
    QVariantList m_devices, m_drivers;
    QVariantList m_options;
    QString m_selected, m_error, m_notice;
    bool m_timedOut = false;
    Portal m_portal;
    QString m_ppdFile;
    QString m_deferredQuery;
    QString m_printerFilter;
    bool m_background = false;
    RowModel m_printerRows{"name"}, m_optionRows{"key"}, m_supplyRows{"name"}, m_jobRows{"id"};
};
