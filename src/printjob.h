#pragma once
#include <QObject>
#include <QVariantMap>
#include <memory>

// GTK supplies the print transport and serialized settings, never the dialog.
class PrintJob : public QObject {
    Q_OBJECT
  public:
    explicit PrintJob(QObject *parent = nullptr);
    ~PrintJob() override;
    void start(const QString &file, const QString &title, const QVariantMap &settings,
               const QVariantMap &pageSetup, bool preview = false);
  signals:
    void finished(const QString &error);

  private:
    struct State;
    std::unique_ptr<State> m_state;
};
