#include "translator.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>

Translator::Translator(const QString &path, const QString &catalogs, QObject *parent)
    : QTranslator(parent), m_path(path), m_catalogs(catalogs) {
    if (m_path.isEmpty())
        m_path = qEnvironmentVariable("XDG_CONFIG_HOME", QDir::homePath() + "/.config") + "/omarchy/language";
    connect(&m_poll, &QTimer::timeout, this, &Translator::reload);
    m_poll.start(1000);
    reload();
}
QString Translator::translate(const char *, const char *source, const char *, int) const {
    return m_strings.value(QString::fromUtf8(source));
}
void Translator::reload() {
    QFile file(m_path);
    QString language = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readLine()).trimmed() : "en";
    if (!QRegularExpression("^[a-z]{2,3}(-[A-Za-z]{2})?$").match(language).hasMatch())
        language = "en";
    if (language == m_language)
        return;
    m_language = language;
    m_strings.clear();
    QStringList codes{"en", language.section('-', 0, 0), language};
    codes.removeDuplicates();
    for (const auto &code : codes) {
        QFile catalog(m_catalogs + "/" + code + ".json");
        if (!catalog.open(QIODevice::ReadOnly))
            continue;
        const auto object = QJsonDocument::fromJson(catalog.readAll()).object();
        for (auto it = object.begin(); it != object.end(); ++it)
            if (it.value().isString() && !it.value().toString().isEmpty())
                m_strings.insert(it.key(), it.value().toString());
    }
    QString localeName = language;
    QFile registry(qEnvironmentVariable("OMARCHY_PATH", "/usr/share/omarchy") + "/default/i18n/locales.json");
    if (registry.open(QIODevice::ReadOnly)) {
        const auto entry = QJsonDocument::fromJson(registry.readAll()).object().value(language).toObject();
        if (!entry.value("formatLocale").toString().isEmpty())
            localeName = entry.value("formatLocale").toString();
    }
    const QLocale locale(localeName);
    QLocale::setDefault(locale);
    QGuiApplication::setLayoutDirection(locale.textDirection());
    emit changed();
}
