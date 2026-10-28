import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: form
    required property var printRequest
    required property var printerBackend
    property bool toFile: printRequest.settings.printer === "Print to File"
    property bool marginsExpanded: false
    readonly property var selectedPrinter: (printerBackend.snapshot.printers || []).find(p => p.name === printRequest.settings.printer) || {}
    readonly property var paperChoices: {
        let option = printerBackend.options.find(o => o.key === "media");
        return option ? option.choices : [
            {
                value: "iso_a4_210x297mm",
                label: "A4"
            },
            {
                value: "na_letter_8.5x11in",
                label: "Letter"
            }
        ];
    }
    readonly property var rangeChoices: {
        let choices = [
            {
                value: "all",
                label: qsTr("All pages")
            },
            {
                value: "ranges",
                label: qsTr("Page ranges")
            }
        ];
        if (printRequest.options.has_current_page)
            choices.push({
                value: "current",
                label: qsTr("Current page")
            });
        if (printRequest.options.has_selected_pages)
            choices.push({
                value: "selection",
                label: qsTr("Selected pages")
            });
        return choices;
    }
    spacing: 12
    enabled: !printRequest.busy && !printRequest.failed

    component Field: ColumnLayout {
        property string label
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 1
        Layout.alignment: Qt.AlignTop
        spacing: 5
        Label {
            text: parent.label
            textFormat: Text.PlainText
            color: theme.foreground
            font.pixelSize: 11
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        uniformCellWidths: true
        columnSpacing: 24
        rowSpacing: 12
        Field {
            label: qsTr("Printer")
            SpoolComboBox {
                objectName: "printPrinterPicker"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                model: form.printRequest.printers
                textRole: "label"
                valueRole: "name"
                currentIndex: form.printRequest.printers.findIndex(p => p.name === form.printRequest.settings.printer)
                displayText: currentIndex < 0 ? qsTr("Select a printer…") : currentText
                onActivated: form.printRequest.setSetting("printer", currentValue)
            }
        }
        Field {
            label: form.toFile ? qsTr("File format") : qsTr("Status")
            SpoolComboBox {
                objectName: "printFormatPicker"
                visible: form.toFile
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                model: form.printRequest.formats.map(value => ({
                            value: value,
                            label: value.toUpperCase()
                        }))
                textRole: "label"
                valueRole: "value"
                currentIndex: form.printRequest.formats.indexOf(form.printRequest.settings["output-file-format"])
                onActivated: form.printRequest.setSetting("output-file-format", currentValue)
            }
            Label {
                visible: !form.toFile
                text: {
                    if (form.selectedPrinter.message)
                        return form.selectedPrinter.message;
                    let reasons = (form.selectedPrinter.reasons || []).filter(r => r !== "none");
                    if (reasons.includes("marker-supply-low-warning"))
                        return qsTr("Ink or toner is running low");
                    if (reasons.length)
                        return reasons.map(r => r.replace(/-/g, " ")).join(" · ");
                    if (!form.selectedPrinter.name)
                        return qsTr("Choose a printer");
                    return form.selectedPrinter.state === 5 ? qsTr("Paused") : form.selectedPrinter.state === 4 ? qsTr("Printing") : qsTr("Ready");
                }
                textFormat: Text.PlainText
                color: theme.muted
                font.pixelSize: 11
                wrapMode: Text.Wrap
                verticalAlignment: Text.AlignVCenter
                Layout.fillWidth: true
                Layout.minimumHeight: 28
            }
        }
        Field {
            visible: form.toFile
            Layout.columnSpan: 2
            label: qsTr("Output file")
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                SpoolTextField {
                    objectName: "printOutputField"
                    text: form.printRequest.outputPath
                    placeholderText: qsTr("Choose a new file…")
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    onTextEdited: form.printRequest.setOutputPath(text)
                }
                ActionButton {
                    text: qsTr("Choose…")
                    onClicked: form.printRequest.chooseOutput()
                }
            }
        }
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: theme.border
    }
    GridLayout {
        id: settingsGrid
        objectName: "printSettingsGrid"
        Layout.fillWidth: true
        columns: 2
        uniformCellWidths: true
        columnSpacing: 24
        rowSpacing: 12

        SectionHeading {
            text: qsTr("PAGES & COPIES")
            Layout.columnSpan: 2
        }
        Field {
            label: qsTr("Pages to print")
            SpoolComboBox {
                objectName: "printRangePicker"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                model: form.rangeChoices
                textRole: "label"
                valueRole: "value"
                currentIndex: form.rangeChoices.findIndex(c => c.value === form.printRequest.settings["print-pages"])
                onActivated: form.printRequest.setSetting("print-pages", currentValue)
            }
            SpoolTextField {
                objectName: "printRangeField"
                visible: form.printRequest.settings["print-pages"] === "ranges"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: form.printRequest.ranges
                placeholderText: qsTr("For example: 1-3, 5, 8-10")
                onTextEdited: form.printRequest.ranges = text
            }
        }
        Field {
            label: qsTr("Copies")
            SpoolSpinBox {
                objectName: "printCopiesField"
                from: 1
                to: 999
                value: Number(form.printRequest.settings["n-copies"] || 1)
                editable: true
                onValueModified: form.printRequest.setSetting("n-copies", String(value))
            }
        }
        Field {
            label: qsTr("Page set")
            SpoolComboBox {
                objectName: "printPageSetPicker"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                property var choices: [
                    {
                        value: "all",
                        label: qsTr("All pages")
                    },
                    {
                        value: "even",
                        label: qsTr("Even pages only")
                    },
                    {
                        value: "odd",
                        label: qsTr("Odd pages only")
                    }
                ]
                model: choices
                textRole: "label"
                valueRole: "value"
                currentIndex: choices.findIndex(c => c.value === form.printRequest.settings["page-set"])
                onActivated: form.printRequest.setSetting("page-set", currentValue)
            }
        }
        Field {
            label: qsTr("Page order")
            RowLayout {
                Layout.preferredHeight: 28
                spacing: 16
                SpoolCheckBox {
                    text: qsTr("Collate")
                    checked: form.printRequest.settings.collate === "true"
                    onToggled: form.printRequest.setSetting("collate", checked ? "true" : "false")
                }
                SpoolCheckBox {
                    text: qsTr("Reverse")
                    checked: form.printRequest.settings.reverse === "true"
                    onToggled: form.printRequest.setSetting("reverse", checked ? "true" : "false")
                }
            }
        }

        SectionHeading {
            text: qsTr("PAPER & LAYOUT")
            Layout.columnSpan: 2
        }
        Field {
            label: qsTr("Paper size")
            SpoolComboBox {
                objectName: "printPaperPicker"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                model: form.paperChoices
                textRole: "label"
                valueRole: "value"
                currentIndex: form.paperChoices.findIndex(p => p.value.startsWith(form.printRequest.settings["paper-format"] + "_"))
                displayText: currentIndex < 0 ? form.printRequest.pageSetup.DisplayName : currentText
                onActivated: form.printRequest.setPaper(currentValue)
            }
        }
        Field {
            label: qsTr("Orientation")
            SpoolComboBox {
                objectName: "printOrientationPicker"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                property var choices: [
                    {
                        value: "portrait",
                        label: qsTr("Portrait")
                    },
                    {
                        value: "landscape",
                        label: qsTr("Landscape")
                    },
                    {
                        value: "reverse_portrait",
                        label: qsTr("Reverse portrait")
                    },
                    {
                        value: "reverse_landscape",
                        label: qsTr("Reverse landscape")
                    }
                ]
                model: choices
                textRole: "label"
                valueRole: "value"
                currentIndex: choices.findIndex(c => c.value === form.printRequest.settings.orientation)
                onActivated: form.printRequest.setSetting("orientation", currentValue)
            }
        }
        Field {
            label: qsTr("Two-sided printing")
            SpoolComboBox {
                objectName: "printDuplexPicker"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                property var choices: [
                    {
                        value: "simplex",
                        label: qsTr("One-sided")
                    },
                    {
                        value: "vertical",
                        label: qsTr("Long-edge binding")
                    },
                    {
                        value: "horizontal",
                        label: qsTr("Short-edge binding")
                    }
                ]
                model: choices
                textRole: "label"
                valueRole: "value"
                currentIndex: choices.findIndex(c => c.value === form.printRequest.settings.duplex)
                onActivated: form.printRequest.setSetting("duplex", currentValue)
            }
        }
        Field {
            label: qsTr("Pages per sheet")
            SpoolComboBox {
                objectName: "printNumberUpPicker"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                model: ["1", "2", "4", "6", "9", "16"]
                currentIndex: model.indexOf(form.printRequest.settings["number-up"])
                onActivated: form.printRequest.setSetting("number-up", currentText)
            }
        }

        SectionHeading {
            text: qsTr("OUTPUT")
            Layout.columnSpan: 2
        }
        Field {
            label: qsTr("Color mode")
            SpoolComboBox {
                objectName: "printColorPicker"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                model: [qsTr("Color"), qsTr("Grayscale")]
                currentIndex: form.printRequest.settings["use-color"] === "true" ? 0 : 1
                onActivated: form.printRequest.setSetting("use-color", currentIndex === 0 ? "true" : "false")
            }
        }
        Field {
            label: qsTr("Quality")
            SpoolComboBox {
                objectName: "printQualityPicker"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                property var choices: [
                    {
                        value: "draft",
                        label: qsTr("Draft")
                    },
                    {
                        value: "normal",
                        label: qsTr("Normal")
                    },
                    {
                        value: "high",
                        label: qsTr("High")
                    }
                ]
                model: choices
                textRole: "label"
                valueRole: "value"
                currentIndex: choices.findIndex(c => c.value === form.printRequest.settings.quality)
                onActivated: form.printRequest.setSetting("quality", currentValue)
            }
        }
        Field {
            label: qsTr("Scale (%)")
            SpoolSpinBox {
                objectName: "printScaleField"
                from: 1
                to: 1000
                value: Number(form.printRequest.settings.scale || 100)
                editable: true
                onValueModified: form.printRequest.setSetting("scale", String(value))
            }
        }
        Field {
            label: qsTr("Page margins")
            ActionButton {
                objectName: "printMarginsToggle"
                text: form.marginsExpanded ? qsTr("Hide margins") : qsTr("Edit margins…")
                onClicked: form.marginsExpanded = !form.marginsExpanded
            }
        }
        GridLayout {
            objectName: "printMarginsGrid"
            visible: form.marginsExpanded
            Layout.columnSpan: 2
            Layout.fillWidth: true
            columns: 2
            uniformCellWidths: true
            columnSpacing: 24
            rowSpacing: 12
            Repeater {
                model: [
                    {
                        key: "MarginTop",
                        label: qsTr("Top (mm)")
                    },
                    {
                        key: "MarginBottom",
                        label: qsTr("Bottom (mm)")
                    },
                    {
                        key: "MarginLeft",
                        label: qsTr("Left (mm)")
                    },
                    {
                        key: "MarginRight",
                        label: qsTr("Right (mm)")
                    }
                ]
                Field {
                    required property var modelData
                    label: modelData.label
                    SpoolSpinBox {
                        id: marginControl
                        objectName: "printMargin-" + modelData.key
                        from: 0
                        to: 1000
                        value: Math.round(Number(form.printRequest.pageSetup[modelData.key]) * 10)
                        editable: true
                        validator: DoubleValidator {
                            locale: marginControl.locale.name
                            bottom: 0
                            top: 100
                            decimals: 1
                            notation: DoubleValidator.StandardNotation
                        }
                        textFromValue: (value, locale) => Number(value / 10).toLocaleString(locale, 'f', 1)
                        valueFromText: (text, locale) => Math.round(Number.fromLocaleString(locale, text) * 10)
                        onValueModified: form.printRequest.setPageValue(modelData.key, value / 10)
                    }
                }
            }
        }
    }
    Label {
        text: form.printRequest.error || form.printerBackend.error
        visible: text.length > 0
        textFormat: Text.PlainText
        color: theme.error
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
}
