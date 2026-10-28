#include "portal.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QUrl>
#include <QUuid>

void Portal::choosePpd() { choose(tr("Choose a printer PPD file"), false); }
void Portal::chooseOutput(const QString &suggestedName) {
    choose(tr("Save print output"), true, suggestedName);
}
void Portal::choose(const QString &title, bool save, const QString &suggestedName) {
    if (!m_handle.isEmpty())
        return;
    auto bus = QDBusConnection::sessionBus();
    QString sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    const auto token = "esyuprint" + QUuid::createUuid().toString(QUuid::Id128);
    m_handle = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
    if (!bus.connect("org.freedesktop.portal.Desktop", m_handle, "org.freedesktop.portal.Request", "Response",
                     this, SLOT(response(uint, QVariantMap)))) {
        m_handle.clear();
        emit failed(tr("Cannot connect to the desktop file chooser portal."));
        return;
    }
    auto message =
        QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                       "org.freedesktop.portal.FileChooser", save ? "SaveFile" : "OpenFile");
    QVariantMap options{{"handle_token", token}, {"modal", true}};
    if (save)
        options["current_name"] = suggestedName;
    else
        options["multiple"] = false;
    message << QString() << title << options;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        if (reply.isError()) {
            response(2, {});
            emit failed(tr("File chooser failed: %1").arg(reply.error().message()));
        }
        watcher->deleteLater();
    });
}
void Portal::response(uint code, const QVariantMap &results) {
    QDBusConnection::sessionBus().disconnect("org.freedesktop.portal.Desktop", m_handle,
                                             "org.freedesktop.portal.Request", "Response", this,
                                             SLOT(response(uint, QVariantMap)));
    m_handle.clear();
    if (code != 0)
        return;
    const auto urls = results.value("uris").toStringList();
    if (urls.size() != 1 || !QUrl(urls.first()).isLocalFile()) {
        emit failed(tr("Choose a local PPD file."));
        return;
    }
    emit chosen(QUrl(urls.first()).toLocalFile());
}
