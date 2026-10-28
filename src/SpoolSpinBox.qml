import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.SpinBox {
    id: control
    implicitWidth: 110
    implicitHeight: 28
    font.pixelSize: 11
    palette.text: theme.foreground
    palette.buttonText: theme.foreground
    palette.base: theme.background
    palette.button: theme.raised
    palette.mid: theme.border
    palette.highlight: theme.accent
    palette.highlightedText: theme.accentText
    contentItem: TextInput {
        text: control.displayText
        font: control.font
        color: theme.foreground
        selectionColor: theme.selection
        selectedTextColor: theme.foreground
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
        selectByMouse: true
        clip: true
    }
    component Step: Rectangle {
        required property var spin
        required property bool increase
        readonly property var button: increase ? spin.up : spin.down
        x: increase !== spin.mirrored ? spin.width - width : 0
        implicitWidth: 28
        implicitHeight: 28
        height: spin.height
        radius: theme.radius
        color: button.pressed || button.hovered ? theme.selection : "transparent"
        Text {
            anchors.centerIn: parent
            text: parent.increase ? "+" : "−"
            font.pixelSize: 16
            color: parent.enabled ? theme.foreground : theme.muted
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            x: parent.increase !== parent.spin.mirrored ? 0 : parent.width - 1
            width: 1
            height: parent.height - 12
            color: theme.border
        }
    }
    up.indicator: Step {
        spin: control
        increase: true
    }
    down.indicator: Step {
        spin: control
        increase: false
    }
    background: Rectangle {
        color: theme.background
        border.color: control.activeFocus ? theme.accent : theme.border
        radius: theme.radius
    }
}
