import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.TextField {
    id: control
    implicitWidth: 160
    implicitHeight: 28
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
        opacity: control.enabled ? 1 : 0.5
    }
}
