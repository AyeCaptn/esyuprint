#pragma once
#include <QHash>
#include <QTimer>
#include <QTranslator>

class Translator : public QTranslator {
    Q_OBJECT
    Q_PROPERTY(QString language READ language NOTIFY changed)
  public:
    explicit Translator(const QString &path = {}, const QString &catalogs = ":/i18n",
                        QObject *parent = nullptr);
    QString language() const { return m_language; }
    // Register even in English so a later live language switch can supply strings.
    bool isEmpty() const override { return false; }
    QString translate(const char *, const char *source, const char * = nullptr, int = -1) const override;
  signals:
    void changed();

  private:
    void reload();
    QString m_path, m_catalogs, m_language;
    QHash<QString, QString> m_strings;
    QTimer m_poll;
};
