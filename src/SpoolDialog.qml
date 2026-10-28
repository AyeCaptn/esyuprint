import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Dialog {
    id: control
    Material.theme: theme.light ? Material.Light : Material.Dark
    Material.background: theme.surface
    Material.foreground: theme.foreground
    Material.accent: theme.accent
    Material.roundedScale: theme.radius
    Material.elevation: 0
    padding: 16
    font.pixelSize: 11
    background: Rectangle {
        color: theme.surface
        border.color: theme.border
        radius: theme.radius
    }
    header: Label {
        visible: control.title.length > 0
        text: control.title
        color: theme.foreground
        font.pixelSize: 16
        wrapMode: Text.Wrap
        padding: 16
        bottomPadding: 8
    }
    Overlay.modal: Rectangle {
        color: Qt.alpha(theme.background, 0.78)
    }
    footer: DialogButtonBox {
        visible: count > 0
        standardButtons: control.standardButtons
        delegate: ActionButton {}
        background: Rectangle {
            color: theme.surface
        }
    }
}
