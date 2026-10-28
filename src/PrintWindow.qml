import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "printWindow"
    width: 760
    height: 700
    minimumWidth: 560
    minimumHeight: 420
    visible: true
    flags: Qt.Dialog
    modality: request.options.modal === false ? Qt.NonModal : Qt.ApplicationModal
    title: qsTr("esyuprint · Print")
    color: theme.background
    font.family: theme.fontFamily
    font.pixelSize: 11
    Material.theme: theme.light ? Material.Light : Material.Dark
    Material.foreground: theme.foreground
    Material.background: theme.background
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    function actionLabel(value) {
        // GTK and Qt use different mnemonic markers; doubled markers are literal.
        return String(value).replace(/&&|__|[&_](.)/g, (match, character) => character || match[0]);
    }
    property string documentTitle: {
        let title = actionLabel(request.title).trim().replace(/(?:\.\.\.|…)$/, "").trim();
        let action = actionLabel(request.options.accept_label || qsTr("Print")).trim().replace(/(?:\.\.\.|…)$/, "").trim();
        return title.toLowerCase() === action.toLowerCase() || title.toLowerCase() === qsTr("Print").toLowerCase() || title.toLowerCase() === "print" ? "" : request.title;
    }
    onClosing: request.cancel()
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: request.cancel()
    }

    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.maximumWidth: 720
                Layout.alignment: Qt.AlignHCenter
                Layout.margins: 16
                spacing: 12
                Label {
                    text: qsTr("Print")
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    color: theme.foreground
                }
                Label {
                    objectName: "printDocumentTitle"
                    text: win.documentTitle
                    visible: text.length > 0
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                    color: theme.muted
                    Layout.maximumWidth: 500
                    Layout.minimumWidth: 0
                }
                Item {
                    Layout.fillWidth: true
                }
                Item {
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    BusyIndicator {
                        anchors.fill: parent
                        running: request.busy || backend.busy
                        visible: running
                    }
                }
            }
            Rectangle {
                height: 1
                Layout.fillWidth: true
                color: theme.border
            }
            ScrollView {
                id: scroller
                objectName: "printScroller"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
                padding: 16
                clip: true
                contentWidth: availableWidth
                contentHeight: form.implicitHeight
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: contentHeight > availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                PrintSettings {
                    id: form
                    width: Math.min(scroller.availableWidth, 720)
                    x: (scroller.availableWidth - width) / 2
                    printRequest: request
                    printerBackend: backend
                }
            }
            Rectangle {
                height: 1
                Layout.fillWidth: true
                color: theme.border
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.maximumWidth: 720
                Layout.alignment: Qt.AlignHCenter
                Layout.margins: 16
                spacing: 8
                ActionButton {
                    objectName: "printCancelButton"
                    text: request.failed ? qsTr("Close") : qsTr("Cancel")
                    onClicked: request.cancel()
                }
                Item {
                    Layout.fillWidth: true
                }
                ActionButton {
                    objectName: "printPreviewButton"
                    text: qsTr("Preview")
                    enabled: !request.busy && !request.failed && !form.toFile && request.previewAvailable
                    onClicked: request.accept(true)
                }
                ActionButton {
                    objectName: "printAcceptButton"
                    text: form.toFile ? qsTr("Save file") : win.actionLabel(request.options.accept_label || qsTr("Print"))
                    primary: true
                    enabled: !request.busy && !request.failed && !backend.busy
                    onClicked: request.accept()
                }
            }
        }
    }
}
