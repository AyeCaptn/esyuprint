import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.Button {
    id: control
    property bool primary: false
    property bool dangerous: false
    implicitHeight: 28
    implicitWidth: Math.max(28, contentItem.implicitWidth + leftPadding + rightPadding)
    font.pixelSize: 11
    topPadding: 5
    bottomPadding: 5
    contentItem: Text {
        text: control.text
        textFormat: Text.PlainText
        font: control.font
        color: control.enabled ? (control.primary ? theme.accentText : (control.dangerous ? theme.error : theme.foreground)) : theme.muted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: theme.radius
        color: control.primary ? theme.accent : (control.down || control.hovered ? theme.selection : theme.raised)
        opacity: control.enabled ? 1 : 0.45
        border.width: control.primary ? 0 : 1
        border.color: control.dangerous ? theme.error : (control.hovered || control.visualFocus ? theme.accent : theme.border)
    }
    leftPadding: 12
    rightPadding: 12
    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }
}
