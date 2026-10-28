import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.TextArea {
    id: control
    font.pixelSize: 11
    leftPadding: 8
    rightPadding: 8
    topPadding: 5
    bottomPadding: 5
    color: theme.foreground
    placeholderTextColor: theme.muted
    selectionColor: theme.selection
    selectedTextColor: theme.foreground
    selectByMouse: true
    background: Rectangle {
        color: theme.background
        border.color: control.activeFocus ? theme.accent : theme.border
        radius: theme.radius
    }
}
