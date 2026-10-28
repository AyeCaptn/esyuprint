import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.CheckBox {
    id: control
    implicitHeight: 24
    font.pixelSize: 11
    padding: 0
    spacing: 8
    palette.windowText: theme.foreground
    palette.text: theme.foreground
    palette.base: theme.background
    palette.mid: theme.border
    palette.highlight: theme.accent
    palette.highlightedText: theme.accentText
    indicator: Rectangle {
        width: 16
        height: 16
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: Math.min(3, theme.radius)
        color: control.checked ? theme.accent : theme.background
        border.color: control.visualFocus || control.hovered ? theme.accent : theme.border
        Text {
            anchors.centerIn: parent
            text: control.checked ? "✓" : ""
            font.pixelSize: 12
            color: theme.accentText
        }
    }
}
