import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1180
    height: 790
    minimumWidth: 640
    minimumHeight: 420
    visible: true
    title: "esyuprint"
    color: theme.background
    font.family: theme.fontFamily
    font.pixelSize: 11
    Material.theme: theme.light ? Material.Light : Material.Dark
    Material.accent: theme.accent
    Material.primary: theme.accent
    Material.foreground: theme.foreground
    Material.background: theme.surface
    Material.roundedScale: theme.radius

    property int page: 0
    property var printer: {
        let printers = backend.snapshot.printers || [];
        return printers.find(p => p.name === backend.selected) || {};
    }
    property var attrs: backend.details.attributes || {}
    property bool hasPrinter: backend.selected.length > 0
    property var pages: [qsTr("Overview"), qsTr("Print options"), qsTr("Queue"), qsTr("Access & policies"), qsTr("Classes"), qsTr("Server"), qsTr("Diagnostics")]

    function stateLabel(state) {
        return state === 3 ? qsTr("Ready") : state === 4 ? qsTr("Printing") : state === 5 ? qsTr("Paused") : qsTr("Unknown");
    }
    function jobState(state) {
        return ({
                3: qsTr("Pending"),
                4: qsTr("Held"),
                5: qsTr("Printing"),
                6: qsTr("Stopped"),
                7: qsTr("Canceled"),
                8: qsTr("Aborted"),
                9: qsTr("Completed")
            })[state] || qsTr("Unknown");
    }
    function edit(action, heading, initial, choices) {
        editDialog.action = action;
        editDialog.title = heading;
        editDialog.choices = choices || [];
        editValue.text = String(initial === undefined ? "" : initial);
        editDialog.open();
    }
    function navigate(index) {
        page = index;
        contentScroll.contentItem.contentY = 0;
        if (index === 5)
            backend.query("server");
        if (index === 6 && hasPrinter)
            backend.query("diagnose");
    }
    onClosing: function (close) {
        if (backend.mutating || Object.keys(backend.pending).length) {
            close.accepted = false;
            exitDialog.open();
        }
    }
    Shortcut {
        sequence: "?"
        context: Qt.ApplicationShortcut
        onActivated: helpDialog.open()
    }
    Shortcut {
        sequence: "Q"
        context: Qt.ApplicationShortcut
        enabled: !win.activeFocusItem || !win.activeFocusItem.hasOwnProperty("text")
        onActivated: win.close()
    }
    Shortcut {
        sequence: "Ctrl+R"
        context: Qt.ApplicationShortcut
        onActivated: backend.refresh()
    }
    Shortcut {
        sequence: "Space"
        context: Qt.ApplicationShortcut
        enabled: !win.activeFocusItem || !win.activeFocusItem.hasOwnProperty("text")
        onActivated: backend.refresh()
    }
    Shortcut {
        sequence: "Ctrl+N"
        context: Qt.ApplicationShortcut
        onActivated: addDialog.open()
    }
    Connections {
        target: backend
        function onChanged() {
            if (Object.keys(backend.pending).length && !confirmDialog.visible)
                confirmDialog.open();
        }
        function onAuthenticationRequired() {
            authDialog.open();
        }
    }

    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                color: theme.background
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 8
                    Label {
                        text: "▤"
                        color: theme.accent
                        font.pixelSize: 26
                    }
                    Label {
                        text: "esyuprint"
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        color: theme.foreground
                    }
                    Label {
                        text: backend.demo ? qsTr("PREVIEW · SIMULATED PRINTERS") : backend.serverLabel
                        color: backend.demo ? theme.accent : theme.muted
                        font.pixelSize: 11
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideMiddle
                    }
                    Item {
                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 26
                        BusyIndicator {
                            anchors.fill: parent
                            running: backend.busy
                            visible: running
                        }
                    }
                    ActionButton {
                        text: qsTr("Connect…")
                        enabled: !backend.busy
                        onClicked: connectionDialog.open()
                    }
                    ActionButton {
                        objectName: "refreshButton"
                        text: qsTr("Refresh")
                        enabled: !backend.busy
                        onClicked: backend.refresh()
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: theme.border
            }
            Rectangle {
                visible: backend.error.length > 0 || backend.notice.length > 0
                Layout.fillWidth: true
                implicitHeight: banner.implicitHeight + 20
                color: Qt.alpha(backend.error.length ? theme.error : theme.accent, 0.10)
                Label {
                    id: banner
                    anchors.fill: parent
                    anchors.margins: 10
                    text: backend.error || backend.notice
                    color: backend.error.length ? theme.error : theme.foreground
                    wrapMode: Text.Wrap
                }
            }
            RowLayout {
                Layout.fillHeight: true
                Layout.fillWidth: true
                spacing: 0
                Rectangle {
                    Layout.preferredWidth: win.width < 900 ? 200 : 240
                    Layout.fillHeight: true
                    color: theme.surface
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8
                        RowLayout {
                            Label {
                                text: qsTr("YOUR PRINTERS")
                                font.pixelSize: 10
                                font.letterSpacing: 1.3
                                color: theme.muted
                                Layout.fillWidth: true
                            }
                            ActionButton {
                                text: "+"
                                implicitWidth: 32
                                enabled: !backend.busy
                                onClicked: addDialog.open()
                                Accessible.name: qsTr("Add printer")
                            }
                        }
                        SpoolTextField {
                            id: filter
                            objectName: "printerFilter"
                            Layout.fillWidth: true
                            placeholderText: qsTr("Filter printers…")
                            onTextChanged: backend.printerFilter = text
                        }
                        ListView {
                            id: printersList
                            objectName: "printersList"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 8
                            clip: true
                            model: backend.printerRows
                            delegate: ItemDelegate {
                                required property var modelData
                                objectName: "printer-" + modelData.name
                                width: ListView.view.width
                                height: 68
                                onClicked: backend.selected = modelData.name
                                background: Rectangle {
                                    radius: theme.radius
                                    color: modelData.name === backend.selected ? Qt.alpha(theme.accent, 0.10) : "transparent"
                                    border.color: modelData.name === backend.selected ? theme.accent : theme.border
                                    border.width: 1
                                }
                                contentItem: ColumnLayout {
                                    spacing: 3
                                    Label {
                                        text: (modelData.default ? "★  " : "") + modelData.label
                                        font.bold: true
                                        color: theme.foreground
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: modelData.class ? qsTr("Printer class") : win.stateLabel(modelData.state)
                                        color: modelData.state === 5 ? theme.error : theme.accent
                                        font.pixelSize: 11
                                    }
                                    Label {
                                        text: modelData.location || modelData.name
                                        color: theme.muted
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                            Label {
                                anchors.centerIn: parent
                                visible: printersList.count === 0 && !backend.busy
                                text: qsTr("No printers found.\nAdd one to get started.")
                                color: theme.muted
                                wrapMode: Text.Wrap
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                        ActionButton {
                            text: qsTr("Keyboard shortcuts  ?")
                            Layout.fillWidth: true
                            onClicked: helpDialog.open()
                        }
                    }
                }
                Rectangle {
                    Layout.fillHeight: true
                    width: 1
                    color: theme.border
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 14
                    Layout.minimumWidth: 0
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 5
                            Label {
                                text: win.printer.label || qsTr("Printer management")
                                color: theme.foreground
                                font.pixelSize: 18
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: win.printer.model || qsTr("Add a printer, manage classes, or configure your CUPS server.")
                                color: theme.muted
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                        }
                        ActionButton {
                            text: qsTr("Test page")
                            enabled: win.hasPrinter && !backend.busy
                            onClicked: backend.stage("test-page")
                        }
                    }
                    Flow {
                        id: tabs
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 4
                        Repeater {
                            model: win.pages
                            ActionButton {
                                required property int index
                                required property string modelData
                                objectName: "tab" + index
                                text: modelData
                                primary: win.page === index
                                onClicked: win.navigate(index)
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: theme.border
                    }
                    ScrollView {
                        id: contentScroll
                        objectName: "pageScroller"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 0
                        Layout.minimumWidth: 0
                        clip: true
                        rightPadding: 12
                        contentWidth: availableWidth
                        contentHeight: pageStack.children[win.page] ? pageStack.children[win.page].implicitHeight : 0
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        ScrollBar.vertical.policy: contentHeight > availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                        StackLayout {
                            id: pageStack
                            width: contentScroll.availableWidth
                            height: contentScroll.contentHeight
                            currentIndex: win.page

                            ColumnLayout {
                                spacing: 12
                                SectionHeading {
                                    text: qsTr("STATUS & SUPPLIES")
                                }
                                Label {
                                    text: win.hasPrinter ? win.stateLabel(win.printer.state) : qsTr("Select a printer")
                                    color: theme.accent
                                    font.pixelSize: 16
                                }
                                Label {
                                    text: win.printer.message || (win.printer.reasons || []).filter(r => r !== "none").join(" · ") || qsTr("No printer warnings.")
                                    color: theme.muted
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                Repeater {
                                    model: backend.supplyRows
                                    RowLayout {
                                        required property var modelData
                                        objectName: "supply-" + modelData.name
                                        Layout.fillWidth: true
                                        spacing: 10
                                        Label {
                                            text: modelData.name
                                            color: theme.foreground
                                            Layout.preferredWidth: Math.min(160, contentScroll.availableWidth * 0.4)
                                            Layout.minimumWidth: 0
                                            elide: Text.ElideRight
                                        }
                                        ProgressBar {
                                            from: 0
                                            to: 100
                                            value: Math.max(0, modelData.level)
                                            Layout.fillWidth: true
                                        }
                                        Label {
                                            text: modelData.level >= 0 ? modelData.level.toLocaleString(Qt.locale(), 'f', 0) + "%" : qsTr("Unknown")
                                            color: theme.muted
                                            Layout.preferredWidth: 44
                                        }
                                    }
                                }
                                Label {
                                    visible: win.hasPrinter && !(backend.details.supplies || []).length
                                    text: qsTr("This queue has not reported supply levels.")
                                    color: theme.muted
                                }
                                SectionHeading {
                                    text: qsTr("PRINTER DETAILS")
                                }
                                Label {
                                    text: qsTr("Queue: %1").arg(backend.selected || "—")
                                    color: theme.foreground
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: qsTr("Connection: %1").arg(win.printer.uri || "—")
                                    color: theme.muted
                                    wrapMode: Text.WrapAnywhere
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: qsTr("Location: %1").arg(win.printer.location || "—")
                                    color: theme.muted
                                }
                                Label {
                                    text: win.printer.shared ? qsTr("Shared on the CUPS server") : qsTr("Not shared")
                                    color: theme.muted
                                    visible: win.hasPrinter
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    enabled: win.hasPrinter && !backend.busy
                                    ActionButton {
                                        text: qsTr("Set server default")
                                        onClicked: backend.stage("default")
                                    }
                                    ActionButton {
                                        text: win.printer.state === 5 ? qsTr("Resume printer") : qsTr("Pause printer")
                                        onClicked: backend.stage(win.printer.state === 5 ? "enable" : "disable")
                                    }
                                    ActionButton {
                                        text: win.attrs["printer-is-accepting-jobs"] ? qsTr("Reject new jobs") : qsTr("Accept new jobs")
                                        onClicked: backend.stage(win.attrs["printer-is-accepting-jobs"] ? "reject" : "accept")
                                    }
                                }
                                SectionHeading {
                                    text: qsTr("CONFIGURATION")
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    enabled: win.hasPrinter && !backend.busy
                                    ActionButton {
                                        text: qsTr("Description…")
                                        onClicked: win.edit("info", qsTr("Printer description"), win.printer.label)
                                    }
                                    ActionButton {
                                        text: qsTr("Location…")
                                        onClicked: win.edit("location", qsTr("Printer location"), win.printer.location)
                                    }
                                    ActionButton {
                                        text: qsTr("Connection…")
                                        onClicked: win.edit("device", qsTr("Device URI"), win.printer.uri)
                                    }
                                    ActionButton {
                                        text: qsTr("Change driver…")
                                        onClicked: driverDialog.open()
                                    }
                                    ActionButton {
                                        objectName: "removePrinterButton"
                                        text: qsTr("Remove printer…")
                                        dangerous: true
                                        onClicked: backend.stage(win.printer.class ? "class-delete" : "delete")
                                    }
                                }
                            }

                            ColumnLayout {
                                spacing: 10
                                SectionHeading {
                                    text: qsTr("DEFAULT PRINT OPTIONS")
                                }
                                Label {
                                    text: qsTr("Options come from this printer’s IPP capabilities and its installed PPD. Each Save changes the server default for that option.")
                                    color: theme.muted
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                Repeater {
                                    model: backend.optionRows
                                    GridLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        columns: width < 560 ? 1 : 2
                                        columnSpacing: 12
                                        rowSpacing: 4
                                        Label {
                                            text: modelData.label
                                            color: theme.foreground
                                            Layout.preferredWidth: parent.columns === 1 ? -1 : 170
                                            Layout.fillWidth: parent.columns === 1
                                            Layout.minimumWidth: 0
                                            wrapMode: Text.Wrap
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            spacing: 6
                                            SpoolComboBox {
                                                id: optionPicker
                                                objectName: "optionPicker-" + modelData.key
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                                model: modelData.choices
                                                textRole: "label"
                                                valueRole: "value"
                                                currentIndex: modelData.choices.findIndex(c => c.value === modelData.value)
                                            }
                                            ActionButton {
                                                text: qsTr("Save")
                                                enabled: !backend.busy && optionPicker.currentIndex >= 0 && optionPicker.currentValue !== modelData.value
                                                onClicked: backend.stage("option", {
                                                    key: modelData.key,
                                                    value: optionPicker.currentValue
                                                })
                                            }
                                            ActionButton {
                                                text: qsTr("Reset")
                                                enabled: !backend.busy
                                                onClicked: backend.stage("reset-option", {
                                                    key: modelData.key
                                                })
                                            }
                                        }
                                    }
                                }
                                Repeater {
                                    model: backend.details.warnings || []
                                    Label {
                                        required property string modelData
                                        text: modelData
                                        color: theme.error
                                        wrapMode: Text.Wrap
                                        Layout.fillWidth: true
                                    }
                                }
                                Label {
                                    visible: backend.options.length === 0
                                    text: qsTr("No configurable options reported. Select a printer and refresh.")
                                    color: theme.muted
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                            }

                            ColumnLayout {
                                spacing: 10
                                SectionHeading {
                                    text: qsTr("PRINT QUEUE & RECENT HISTORY")
                                }
                                Label {
                                    text: qsTr("Up to 200 recent jobs. Completed jobs cannot be recalled; restarting can print another copy.")
                                    color: theme.muted
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                Repeater {
                                    model: backend.jobRows
                                    Rectangle {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        implicitHeight: jobContent.implicitHeight + 24
                                        color: theme.surface
                                        radius: theme.radius
                                        border.color: theme.border
                                        ColumnLayout {
                                            id: jobContent
                                            anchors.fill: parent
                                            anchors.margins: 12
                                            RowLayout {
                                                Label {
                                                    text: "#" + modelData.id + "  " + (modelData["job-name"] || qsTr("Untitled job"))
                                                    color: theme.foreground
                                                    Layout.fillWidth: true
                                                    elide: Text.ElideRight
                                                }
                                                Label {
                                                    text: win.jobState(modelData["job-state"])
                                                    color: theme.accent
                                                }
                                            }
                                            Label {
                                                text: (modelData["job-originating-user-name"] || "") + " · " + (modelData["job-state-message"] || "")
                                                color: theme.muted
                                                wrapMode: Text.Wrap
                                                Layout.fillWidth: true
                                            }
                                            Flow {
                                                Layout.fillWidth: true
                                                spacing: 6
                                                enabled: !backend.busy
                                                ActionButton {
                                                    text: qsTr("Hold")
                                                    enabled: modelData["job-state"] < 7
                                                    onClicked: backend.stage("job-hold", {
                                                        job: modelData.id
                                                    })
                                                }
                                                ActionButton {
                                                    text: qsTr("Release")
                                                    enabled: modelData["job-state"] === 4 || modelData["job-state"] === 6
                                                    onClicked: backend.stage("job-release", {
                                                        job: modelData.id
                                                    })
                                                }
                                                ActionButton {
                                                    text: qsTr("Cancel")
                                                    dangerous: true
                                                    enabled: modelData["job-state"] < 7
                                                    onClicked: backend.stage("job-cancel", {
                                                        job: modelData.id
                                                    })
                                                }
                                                ActionButton {
                                                    text: qsTr("Restart")
                                                    onClicked: backend.stage("job-restart", {
                                                        job: modelData.id
                                                    })
                                                }
                                                ActionButton {
                                                    text: qsTr("Move…")
                                                    enabled: modelData["job-state"] < 7
                                                    onClicked: {
                                                        moveDialog.jobId = modelData.id;
                                                        moveDialog.open();
                                                    }
                                                }
                                                ActionButton {
                                                    text: qsTr("Authenticate…")
                                                    enabled: modelData["job-state"] === 4
                                                    onClicked: {
                                                        jobAuthDialog.jobId = modelData.id;
                                                        jobAuthDialog.open();
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                Label {
                                    visible: backend.jobRows.count === 0
                                    text: qsTr("The queue is clear.")
                                    color: theme.muted
                                    font.pixelSize: 18
                                }
                            }

                            ColumnLayout {
                                spacing: 12
                                SectionHeading {
                                    text: qsTr("SHARING, ACCESS & POLICIES")
                                }
                                Label {
                                    text: qsTr("These are server-wide printer settings, not just preferences for this app. Sharing also depends on the server’s sharing policy and firewall.")
                                    color: theme.muted
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    enabled: win.hasPrinter && !backend.busy
                                    ActionButton {
                                        text: win.printer.shared ? qsTr("Stop sharing") : qsTr("Share printer")
                                        onClicked: backend.stage("shared", {
                                            value: !win.printer.shared
                                        })
                                    }
                                    ActionButton {
                                        text: qsTr("Error policy…")
                                        onClicked: win.edit("error-policy", qsTr("Error policy"), win.attrs["printer-error-policy"], win.attrs["printer-error-policy-supported"])
                                    }
                                    ActionButton {
                                        text: qsTr("Operation policy…")
                                        onClicked: win.edit("operation-policy", qsTr("Operation policy"), win.attrs["printer-op-policy"], win.attrs["printer-op-policy-supported"])
                                    }
                                    ActionButton {
                                        text: qsTr("Allowed users…")
                                        onClicked: win.edit("allow-users", qsTr("Allowed users · comma separated; all = everyone"), (win.attrs["requesting-user-name-allowed"] || ["all"]).join(", "))
                                    }
                                    ActionButton {
                                        text: qsTr("Denied users…")
                                        onClicked: win.edit("deny-users", qsTr("Denied users · comma separated; none = nobody"), (win.attrs["requesting-user-name-denied"] || ["none"]).join(", "))
                                    }
                                    ActionButton {
                                        text: qsTr("Banner pages…")
                                        onClicked: bannerDialog.open()
                                    }
                                }
                                Label {
                                    text: qsTr("Error policy: %1").arg(win.attrs["printer-error-policy"] || "—")
                                    color: theme.foreground
                                }
                                Label {
                                    text: qsTr("Operation policy: %1").arg(win.attrs["printer-op-policy"] || "—")
                                    color: theme.foreground
                                }
                                Label {
                                    text: qsTr("Allowed: %1").arg((win.attrs["requesting-user-name-allowed"] || ["all"]).join(", "))
                                    color: theme.muted
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: qsTr("Denied: %1").arg((win.attrs["requesting-user-name-denied"] || ["none"]).join(", "))
                                    color: theme.muted
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                            }

                            ColumnLayout {
                                spacing: 12
                                SectionHeading {
                                    text: qsTr("PRINTER CLASSES")
                                }
                                Label {
                                    text: qsTr("A class sends work to an available member printer. Add the first member to create a class; removing its last member deletes it.")
                                    color: theme.muted
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                SpoolTextField {
                                    id: className
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("Class name")
                                    text: win.printer.class ? backend.selected : ""
                                }
                                SpoolComboBox {
                                    id: classMember
                                    Layout.fillWidth: true
                                    model: (backend.snapshot.printers || []).filter(p => !p.class).map(p => p.name)
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    enabled: !backend.busy && className.text.length > 0 && classMember.currentIndex >= 0
                                    ActionButton {
                                        text: qsTr("Add member / create class")
                                        onClicked: backend.stage("class-add", {
                                            name: className.text,
                                            member: classMember.currentText
                                        })
                                    }
                                    ActionButton {
                                        text: qsTr("Remove member")
                                        dangerous: true
                                        onClicked: backend.stage("class-remove", {
                                            name: className.text,
                                            member: classMember.currentText
                                        })
                                    }
                                    ActionButton {
                                        text: qsTr("Delete class")
                                        dangerous: true
                                        onClicked: backend.stage("class-delete", {
                                            name: className.text
                                        })
                                    }
                                }
                                Label {
                                    text: qsTr("Current members: %1").arg((win.attrs["member-names"] || win.printer.members || []).join(", ") || "—")
                                    color: theme.foreground
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                            }

                            ColumnLayout {
                                spacing: 12
                                SectionHeading {
                                    text: qsTr("CUPS SERVER SETTINGS")
                                }
                                Label {
                                    text: qsTr("Load the selected server’s configuration, then edit one setting at a time. Saving can reload CUPS and affect every printer and user. Network changes may disconnect this app.")
                                    color: theme.muted
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                ActionButton {
                                    text: qsTr("Load server settings")
                                    enabled: !backend.busy
                                    onClicked: backend.query("server")
                                }
                                Repeater {
                                    model: Object.keys(backend.serverSettings).sort()
                                    GridLayout {
                                        required property string modelData
                                        Layout.fillWidth: true
                                        columns: width < 560 ? 1 : 2
                                        columnSpacing: 12
                                        rowSpacing: 4
                                        Label {
                                            text: modelData
                                            color: theme.foreground
                                            Layout.preferredWidth: parent.columns === 1 ? -1 : 170
                                            Layout.fillWidth: parent.columns === 1
                                            Layout.minimumWidth: 0
                                            wrapMode: Text.Wrap
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            spacing: 6
                                            SpoolTextField {
                                                id: settingValue
                                                text: String(backend.serverSettings[modelData])
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                            }
                                            ActionButton {
                                                text: qsTr("Save")
                                                enabled: !backend.busy && settingValue.text !== String(backend.serverSettings[modelData])
                                                onClicked: {
                                                    let settings = {};
                                                    settings[modelData] = settingValue.text;
                                                    backend.stage("server-save", {
                                                        value: settings
                                                    });
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            ColumnLayout {
                                spacing: 12
                                SectionHeading {
                                    text: qsTr("READ-ONLY TROUBLESHOOTING")
                                }
                                Label {
                                    text: qsTr("Checks the queue, CUPS-reported conditions, and missing driver executables using upstream cupshelpers. No automatic fixes, package installs, service restarts, or test prints.")
                                    color: theme.muted
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                ActionButton {
                                    text: qsTr("Run diagnostics")
                                    enabled: win.hasPrinter && !backend.busy
                                    onClicked: backend.query("diagnose")
                                }
                                Repeater {
                                    model: backend.diagnostics.checks || []
                                    ColumnLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        Label {
                                            text: (modelData.ok ? "✓  " : "!  ") + modelData.label
                                            color: modelData.ok ? theme.accent : theme.error
                                            font.bold: true
                                        }
                                        Label {
                                            text: modelData.detail
                                            color: theme.muted
                                            wrapMode: Text.Wrap
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                                Label {
                                    text: backend.diagnostics.note || ""
                                    color: theme.muted
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                SectionHeading {
                                    text: qsTr("RAW PRINTER ATTRIBUTES")
                                }
                                SpoolTextArea {
                                    text: JSON.stringify(backend.details.attributes || {}, null, 2)
                                    readOnly: true
                                    selectByMouse: true
                                    Layout.fillWidth: true
                                    wrapMode: TextEdit.WrapAnywhere
                                    font.pixelSize: 11
                                    color: theme.muted
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    SpoolDialog {
        id: confirmDialog
        objectName: "confirmationDialog"
        anchors.centerIn: Overlay.overlay
        width: Math.min(win.width - 60, 610)
        modal: true
        title: qsTr("Confirm printer/server change")
        standardButtons: Dialog.Ok | Dialog.Cancel
        closePolicy: Popup.NoAutoClose
        onAccepted: backend.confirm()
        onRejected: backend.cancelPending()
        ColumnLayout {
            width: parent.width
            Label {
                text: qsTr("Target: %1").arg(backend.serverLabel)
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("This performs the action below on the real CUPS server. Test pages and restarted jobs can print immediately. Removal cannot be undone here.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            SpoolTextArea {
                text: JSON.stringify(backend.pending, null, 2)
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                Layout.fillWidth: true
                font.pixelSize: 12
            }
        }
    }
    SpoolDialog {
        id: editDialog
        property string action: ""
        property var choices: []
        anchors.centerIn: Overlay.overlay
        width: Math.min(win.width - 60, 600)
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            let value = choices.length ? editChoice.currentText : editValue.text;
            if (action === "allow-users" || action === "deny-users")
                value = value.split(",").map(v => v.trim()).filter(v => v.length);
            backend.stage(action, {
                value: value
            });
        }
        ColumnLayout {
            width: parent.width
            SpoolTextField {
                id: editValue
                visible: editDialog.choices.length === 0
                Layout.fillWidth: true
                selectByMouse: true
            }
            SpoolComboBox {
                id: editChoice
                visible: editDialog.choices.length > 0
                model: editDialog.choices
                currentIndex: Math.max(0, editDialog.choices.indexOf(editValue.text))
                Layout.fillWidth: true
            }
        }
    }
    SpoolDialog {
        id: addDialog
        objectName: "addPrinterDialog"
        anchors.centerIn: Overlay.overlay
        width: Math.min(win.width - 60, 680)
        modal: true
        height: Math.min(implicitHeight, win.height - 60)
        title: qsTr("Add printer")
        standardButtons: Dialog.Cancel
        contentItem: ScrollView {
            ScrollBar.vertical.policy: ScrollBar.AlwaysOn
            implicitHeight: addForm.implicitHeight
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                id: addForm
                width: addDialog.availableWidth - 12
                spacing: 10
                Label {
                    text: qsTr("Discover USB/network devices or enter a device URI. Existing CUPS queues are not replaced: choose a new, unique queue name.")
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                ActionButton {
                    text: qsTr("Discover network printers")
                    enabled: !backend.busy
                    onClicked: backend.query("devices")
                }
                ActionButton {
                    text: qsTr("USB & server devices · may ask to authorize…")
                    enabled: !backend.busy
                    onClicked: backend.query("devices-authorized")
                }
                SpoolComboBox {
                    Layout.fillWidth: true
                    model: backend.devices
                    textRole: "device-info"
                    currentIndex: -1
                    displayText: currentIndex < 0 ? qsTr("Select a discovered device…") : currentText
                    onActivated: index => {
                        addUri.text = backend.devices[index].uri;
                        addInfo.text = backend.devices[index]["device-info"] || "";
                    }
                }
                SpoolTextField {
                    id: addName
                    Layout.fillWidth: true
                    placeholderText: qsTr("Queue name · no spaces")
                }
                SpoolTextField {
                    id: addInfo
                    Layout.fillWidth: true
                    placeholderText: qsTr("Description")
                }
                SpoolTextField {
                    id: addUri
                    Layout.fillWidth: true
                    placeholderText: qsTr("Device URI · ipp://host/ipp/print, usb://…, smb://…")
                }
                SpoolTextField {
                    id: addLocation
                    Layout.fillWidth: true
                    placeholderText: qsTr("Location (optional)")
                }
                RowLayout {
                    Layout.fillWidth: true
                    SpoolComboBox {
                        id: addDriver
                        enabled: !addCustomPpd.checked
                        Layout.fillWidth: true
                        textRole: "label"
                        valueRole: "id"
                        model: [
                            {
                                id: "everywhere",
                                label: qsTr("IPP Everywhere · driverless")
                            },
                            ...backend.drivers].filter(d => d.label.toLowerCase().includes(addDriverFilter.text.toLowerCase()))
                    }
                    ActionButton {
                        text: qsTr("Load drivers")
                        enabled: !backend.busy
                        onClicked: backend.query("drivers")
                    }
                }
                SpoolTextField {
                    id: addDriverFilter
                    placeholderText: qsTr("Filter installed drivers…")
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                }
                SpoolCheckBox {
                    id: addCustomPpd
                    text: qsTr("Use a local PPD file instead")
                }
                RowLayout {
                    visible: addCustomPpd.checked
                    Layout.fillWidth: true
                    Label {
                        text: backend.ppdFile || qsTr("No PPD file selected")
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    ActionButton {
                        text: qsTr("Choose PPD…")
                        onClicked: backend.choosePpd()
                    }
                }
                Label {
                    text: qsTr("Driverless requires IPP/IPPS. For USB, SMB, or older printers, select an installed driver. New queues may start paused; resume and accept jobs separately after reviewing them.")
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    color: theme.muted
                }
                ActionButton {
                    text: qsTr("Review & add printer")
                    primary: true
                    enabled: !backend.busy && addName.text.trim().length > 0 && addUri.text.trim().length > 0 && !(backend.snapshot.printers || []).some(p => p.name === addName.text.trim()) && (addCustomPpd.checked ? backend.ppdFile.length > 0 : addDriver.currentIndex >= 0)
                    onClicked: {
                        backend.stage("add", {
                            name: addName.text.trim(),
                            info: addInfo.text,
                            uri: addUri.text.trim(),
                            location: addLocation.text,
                            driver: addCustomPpd.checked ? "" : addDriver.currentValue,
                            filename: addCustomPpd.checked ? backend.ppdFile : ""
                        });
                        addDialog.close();
                    }
                }
            }
        }
    }
    SpoolDialog {
        id: driverDialog
        objectName: "driverDialog"
        anchors.centerIn: Overlay.overlay
        width: Math.min(win.width - 60, 640)
        modal: true
        title: qsTr("Change printer driver")
        standardButtons: Dialog.Cancel
        ColumnLayout {
            width: parent.width
            Label {
                text: qsTr("Changing drivers can reset printer options. Only drivers already supplied by CUPS are offered; esyuprint never installs packages automatically.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            ActionButton {
                text: qsTr("Load installed drivers")
                enabled: !backend.busy
                onClicked: backend.query("drivers")
            }
            SpoolComboBox {
                id: changeDriver
                enabled: !changeCustomPpd.checked
                model: [
                    {
                        id: "everywhere",
                        label: qsTr("IPP Everywhere · driverless")
                    },
                    ...backend.drivers].filter(d => d.label.toLowerCase().includes(changeDriverFilter.text.toLowerCase()))
                textRole: "label"
                valueRole: "id"
                Layout.fillWidth: true
            }
            SpoolTextField {
                id: changeDriverFilter
                placeholderText: qsTr("Filter installed drivers…")
                Layout.fillWidth: true
            }
            SpoolCheckBox {
                id: changeCustomPpd
                text: qsTr("Use a local PPD file instead")
            }
            RowLayout {
                visible: changeCustomPpd.checked
                Layout.fillWidth: true
                Label {
                    text: backend.ppdFile || qsTr("No PPD file selected")
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
                ActionButton {
                    text: qsTr("Choose PPD…")
                    onClicked: backend.choosePpd()
                }
            }
            ActionButton {
                text: qsTr("Review driver change")
                enabled: !backend.busy && (changeCustomPpd.checked ? backend.ppdFile.length > 0 : changeDriver.currentIndex >= 0)
                onClicked: {
                    backend.stage(changeCustomPpd.checked ? "driver-file" : "driver", {
                        driver: changeDriver.currentValue,
                        filename: changeCustomPpd.checked ? backend.ppdFile : ""
                    });
                    driverDialog.close();
                }
            }
        }
    }
    SpoolDialog {
        id: bannerDialog
        objectName: "bannerDialog"
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Banner pages")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: backend.stage("job-sheets", {
            value: [bannerStart.currentText, bannerEnd.currentText]
        })
        ColumnLayout {
            Label {
                text: qsTr("Start / end banner")
            }
            SpoolComboBox {
                id: bannerStart
                model: win.attrs["job-sheets-supported"] || ["none"]
                currentIndex: Math.max(0, model.indexOf((win.attrs["job-sheets-default"] || ["none"])[0]))
            }
            SpoolComboBox {
                id: bannerEnd
                model: win.attrs["job-sheets-supported"] || ["none"]
                currentIndex: Math.max(0, model.indexOf((win.attrs["job-sheets-default"] || ["none", "none"])[1]))
            }
        }
    }
    SpoolDialog {
        id: moveDialog
        objectName: "moveDialog"
        property int jobId: 0
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Move job to printer")
        standardButtons: Dialog.Ok | Dialog.Cancel
        SpoolComboBox {
            id: moveTarget
            model: (backend.snapshot.printers || []).map(p => p.name)
        }
        onAccepted: backend.stage("job-move", {
            job: jobId,
            target: moveTarget.currentText
        })
    }
    SpoolDialog {
        id: jobAuthDialog
        objectName: "jobAuthDialog"
        property int jobId: 0
        anchors.centerIn: Overlay.overlay
        width: Math.min(win.width - 60, 530)
        modal: true
        title: qsTr("Authenticate held job")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            let values = [];
            for (let i = 0; i < jobCredentialFields.count; ++i)
                values.push(jobCredentialFields.itemAt(i).text);
            backend.stage("job-authenticate", {
                job: jobId,
                value: values
            });
        }
        onClosed: {
            for (let i = 0; i < jobCredentialFields.count; ++i)
                jobCredentialFields.itemAt(i).clear();
        }
        ColumnLayout {
            width: parent.width
            Label {
                text: qsTr("Credentials are sent only for this job and are not saved.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Repeater {
                id: jobCredentialFields
                model: win.attrs["auth-info-required"] || ["username", "password"]
                SpoolTextField {
                    required property string modelData
                    Layout.fillWidth: true
                    placeholderText: modelData
                    echoMode: modelData.toLowerCase().includes("password") ? TextInput.Password : TextInput.Normal
                }
            }
        }
    }
    SpoolDialog {
        id: connectionDialog
        objectName: "connectionDialog"
        anchors.centerIn: Overlay.overlay
        width: Math.min(win.width - 60, 540)
        modal: true
        title: qsTr("Connect to CUPS server")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            backend.connectServer(serverHost.text, serverPort.value, serverUser.text, serverPassword.text);
            serverPassword.clear();
        }
        onRejected: serverPassword.clear()
        ColumnLayout {
            width: parent.width
            Label {
                text: qsTr("Leave host blank for your configured CUPS server. Remote connections require TLS. Credentials stay in memory and are never saved.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            SpoolTextField {
                id: serverHost
                placeholderText: qsTr("Hostname or local socket path")
                Layout.fillWidth: true
            }
            SpoolSpinBox {
                id: serverPort
                from: 1
                to: 65535
                value: 631
                editable: true
            }
            SpoolTextField {
                id: serverUser
                placeholderText: qsTr("Username (optional)")
                Layout.fillWidth: true
            }
            SpoolTextField {
                id: serverPassword
                placeholderText: qsTr("Password (optional)")
                echoMode: TextInput.Password
                Layout.fillWidth: true
            }
        }
    }
    SpoolDialog {
        id: authDialog
        objectName: "authDialog"
        anchors.centerIn: Overlay.overlay
        width: 480
        modal: true
        title: qsTr("Server authentication required")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            backend.authenticate(authUser.text, authPassword.text);
            authPassword.clear();
        }
        onRejected: {
            authPassword.clear();
            backend.cancelPending();
        }
        ColumnLayout {
            width: parent.width
            SpoolTextField {
                id: authUser
                placeholderText: qsTr("Username")
                Layout.fillWidth: true
            }
            SpoolTextField {
                id: authPassword
                placeholderText: qsTr("Password")
                echoMode: TextInput.Password
                Layout.fillWidth: true
            }
        }
    }
    SpoolDialog {
        id: helpDialog
        objectName: "helpDialog"
        anchors.centerIn: Overlay.overlay
        width: 500
        modal: true
        title: qsTr("Keyboard shortcuts")
        standardButtons: Dialog.Close
        Label {
            text: qsTr("Ctrl+R / Space    Refresh printers and jobs\nCtrl+N                 Add printer\n?                          Keyboard shortcuts\nQ                          Close window\nEsc                       Cancel a dialog\n\nEvery change requires confirmation. There is no undo for CUPS administration.")
            lineHeight: 1.6
            wrapMode: Text.Wrap
        }
    }
    SpoolDialog {
        id: exitDialog
        anchors.centerIn: Overlay.overlay
        width: 500
        modal: true
        title: qsTr("Request or confirmation still open")
        standardButtons: Dialog.Close
        Label {
            text: qsTr("Wait for the request to finish, or cancel the pending confirmation, before closing. A server-side operation cannot be safely undone by closing this app.")
            width: parent.width
            wrapMode: Text.Wrap
        }
    }
}
