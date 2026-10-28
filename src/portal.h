#pragma once
#include <QObject>
#include <QVariantMap>

class Portal : public QObject {
    Q_OBJECT
  public:
    explicit Portal(QObject *parent = nullptr) : QObject(parent) {}
    void choosePpd();
    void chooseOutput(const QString &suggestedName);
  signals:
    void chosen(const QString &path);
    void failed(const QString &message);
  private slots:
    void response(uint code, const QVariantMap &results);

  private:
    QString m_handle;
    void choose(const QString &title, bool save, const QString &suggestedName = {});
};
