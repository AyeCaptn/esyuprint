#include "theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>

static double luminance(QColor c) {
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    return .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
}

Theme::Theme(const QString &directory, QObject *parent)
    : QObject(parent), m_directory(directory), m_desktop(directory.isEmpty()) {
    if (m_directory.isEmpty()) {
        const QString state = qEnvironmentVariable("XDG_STATE_HOME", QDir::homePath() + "/.local/state");
        m_directory = state + "/omarchy/current";
    }
    m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Theme::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &Theme::reload);
    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, &Theme::reload);
    m_poll.start();
    reload();
}

QColor Theme::accentText() const { return luminance(accent()) > .179 ? QColor("black") : QColor("white"); }
bool Theme::light() const { return luminance(background()) > .4; }

void Theme::reload() {
    auto old = m_colors;
    const auto oldStatus = m_status;
    QFile file(m_directory + "/theme/colors.toml");
    if (!file.exists()) {
        m_status = tr("Default palette · Omarchy theme unavailable");
    } else if (!file.open(QIODevice::ReadOnly)) {
        m_status = tr("Cannot read Omarchy theme: %1").arg(file.errorString());
    } else {
        QHash<QString, QColor> colors;
        const QRegularExpression pattern(R"re(^\s*([a-z_]+)\s*=\s*["'](#[0-9a-fA-F]{6})["'])re",
                                         QRegularExpression::MultilineOption);
        auto matches = pattern.globalMatch(QString::fromUtf8(file.readAll()));
        while (matches.hasNext()) {
            const auto match = matches.next();
            colors.insert(match.captured(1), QColor(match.captured(2)));
        }
        if (colors.contains("background") && colors.contains("foreground") && colors.contains("accent")) {
            m_colors = colors;
            m_status = tr("Following Omarchy");
        } else {
            m_status = tr("Invalid Omarchy palette · retaining last good colors");
        }
    }
    // Poll the stable path as well: an atomic symlink replacement drops inode watches.
    const auto watches = m_watcher.files() + m_watcher.directories();
    if (!watches.isEmpty())
        m_watcher.removePaths(watches);
    for (const QString &path : {m_directory, m_directory + "/theme", m_directory + "/theme/colors.toml"})
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);
    if (old != m_colors || oldStatus != m_status)
        emit changed();
    if (!m_desktop || m_ticks++ % 10 != 0)
        return;
    auto query = [this](const QString &program, const QStringList &args, bool rounding) {
        auto *process = new QProcess(this);
        auto *timeout = new QTimer(process);
        timeout->setSingleShot(true);
        connect(timeout, &QTimer::timeout, process, &QProcess::kill);
        connect(process, &QProcess::errorOccurred, process, &QObject::deleteLater);
        connect(process, &QProcess::finished, this, [this, process, rounding](int code) {
            if (code == 0) {
                const auto output = process->readAllStandardOutput();
                if (rounding) {
                    const auto value = QJsonDocument::fromJson(output).object().value("int");
                    if (value.isDouble() && value.toInt() >= 0 && value.toInt() != m_radius) {
                        m_radius = value.toInt();
                        emit changed();
                    }
                } else {
                    const auto font = QString::fromUtf8(output).section(',', 0, 0).trimmed();
                    if (!font.isEmpty() && font != m_font) {
                        m_font = font;
                        emit changed();
                    }
                }
            }
            process->deleteLater();
        });
        process->start(program, args);
        timeout->start(1000);
    };
    query("/usr/bin/fc-match", {"monospace", "-f", "%{family}"}, false);
    if (!qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
        query("/usr/bin/hyprctl", {"-j", "getoption", "decoration:rounding"}, true);
}
