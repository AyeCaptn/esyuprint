#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QTimer>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QColor muted READ muted NOTIFY changed)
    Q_PROPERTY(QColor border READ border NOTIFY changed)
    Q_PROPERTY(QColor raised READ raised NOTIFY changed)
    Q_PROPERTY(QColor selection READ selection NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentText READ accentText NOTIFY changed)
    Q_PROPERTY(QColor error READ error NOTIFY changed)
    Q_PROPERTY(bool light READ light NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY changed)
  public:
    explicit Theme(const QString &directory = {}, QObject *parent = nullptr);
    QColor background() const { return m_colors.value("background", QColor("#18181b")); }
    QColor surface() const { return m_colors.value("dark_background", background().darker(115)); }
    QColor foreground() const { return m_colors.value("foreground", QColor("#f4f4f5")); }
    QColor muted() const {
        return m_colors.value("light_foreground", light() ? QColor("#626262") : QColor("#a1a1aa"));
    }
    QColor border() const { return m_colors.value("muted", light() ? QColor("#b5b5b0") : QColor("#3f3f46")); }
    QColor raised() const { return m_colors.value("lighter_background", background()); }
    QColor selection() const { return m_colors.value("selection", raised()); }
    QColor accent() const { return m_colors.value("accent", QColor("#FFD60A")); }
    QColor error() const { return m_colors.value("red", QColor("#ef7777")); }
    QColor accentText() const;
    bool light() const;
    int radius() const { return m_radius; }
    QString status() const { return m_status; }
    QString fontFamily() const { return m_font; }
  signals:
    void changed();

  private:
    void reload();
    QString m_directory, m_status, m_font;
    QHash<QString, QColor> m_colors;
    QFileSystemWatcher m_watcher;
    QTimer m_poll;
    int m_radius = 0, m_ticks = 0;
    bool m_desktop;
};
