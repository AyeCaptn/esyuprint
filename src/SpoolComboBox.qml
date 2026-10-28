import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.ComboBox {
    id: control
    implicitWidth: 160
    implicitHeight: 28
    font.pixelSize: 11
    padding: 5
    leftPadding: mirrored ? 24 : 8
    rightPadding: mirrored ? 8 : 24
    palette.text: theme.foreground
    palette.buttonText: theme.foreground
    palette.dark: theme.foreground
    palette.base: theme.background
    palette.window: theme.background
    palette.mid: theme.border
    palette.highlight: theme.selection
    palette.highlightedText: theme.foreground
    indicator: Text {
        text: "▾"
        font.pixelSize: 12
        color: theme.foreground
        x: control.mirrored ? 8 : control.width - 18
        y: (control.height - height) / 2
    }
    background: Rectangle {
        color: theme.background
        border.color: control.visualFocus || control.hovered ? theme.accent : theme.border
        radius: theme.radius
        opacity: control.enabled ? 1 : 0.5
    }
    delegate: Basic.ItemDelegate {
        required property var model
        required property int index
        width: ListView.view.width
        implicitHeight: 28
        text: control.textRole ? model[control.textRole] : model.modelData
        font: control.font
        highlighted: control.highlightedIndex === index
        palette.text: theme.foreground
        palette.highlightedText: theme.foreground
        palette.highlight: theme.selection
        background: Rectangle {
            color: parent.highlighted || parent.hovered ? theme.selection : theme.background
        }
    }
}
