import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "preferencesDialog"

    required property ApplicationWindow applicationWindow

    title: qsTr("Preferences")
    modal: true
    standardButtons: Dialog.Close
    width: Math.min(applicationWindow.width - 48, 680)
    height: Math.min(applicationWindow.height - 48, 720)
    x: Math.round((applicationWindow.width - width) / 2)
    y: Math.round((applicationWindow.height - height) / 2)

    contentItem: ScrollView {
        id: settingsScroll
        clip: true

        ColumnLayout {
            width: settingsScroll.availableWidth
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: qsTr("Application-wide preferences are stored between launches. Project content and editor data remain in recipes.")
                wrapMode: Text.WordWrap
                color: palette.placeholderText
            }

            GroupBox {
                objectName: "interfacePreferencesGroup"
                title: qsTr("Interface")
                Layout.fillWidth: true

                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8

                    Label { text: qsTr("Preview layout") }
                    ComboBox {
                        objectName: "defaultPreviewLayoutComboBox"
                        Layout.fillWidth: true
                        model: [qsTr("Tabbed"), qsTr("Horizontal"), qsTr("Vertical")]
                        currentIndex: appPreferences.previewLayout
                        onActivated: index => appPreferences.previewLayout = index
                    }

                    Label { text: qsTr("Side Panel placement") }
                    ComboBox {
                        objectName: "defaultSidePanelModeComboBox"
                        Layout.fillWidth: true
                        model: [qsTr("Adjacent"), qsTr("Overlay")]
                        currentIndex: appPreferences.sidePanelMode
                        onActivated: index => appPreferences.sidePanelMode = index
                    }

                    CheckBox {
                        objectName: "showSidePanelPreferenceCheckBox"
                        Layout.columnSpan: 2
                        text: qsTr("Show the Side Panel")
                        checked: appPreferences.sidePanelVisible
                        onToggled: appPreferences.sidePanelVisible = checked
                    }

                    CheckBox {
                        objectName: "restoreWindowGeometryCheckBox"
                        Layout.columnSpan: 2
                        text: qsTr("Restore window size and position")
                        checked: appPreferences.restoreWindowGeometry
                        onToggled: appPreferences.restoreWindowGeometry = checked
                    }

                    CheckBox {
                        objectName: "rememberWorkspaceModeCheckBox"
                        Layout.columnSpan: 2
                        text: qsTr("Remember the last Screen, Character, or Sprite workspace")
                        checked: appPreferences.rememberWorkspaceMode
                        onToggled: {
                            appPreferences.rememberWorkspaceMode = checked
                            if (checked)
                                appPreferences.lastWorkspaceMode =
                                    root.applicationWindow.workspaceMode
                        }
                    }

                    Button {
                        objectName: "resetInterfacePreferencesButton"
                        Layout.columnSpan: 2
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Reset Interface")
                        onClicked: {
                            appPreferences.resetInterfaceSettings()
                            root.applicationWindow.width = 1360
                            root.applicationWindow.height = 860
                        }
                    }
                }
            }

            GroupBox {
                objectName: "behaviorPreferencesGroup"
                title: qsTr("Behavior")
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    CheckBox {
                        objectName: "preferencesAutoUpdateCheckBox"
                        text: qsTr("Automatically update conversions")
                        checked: imageInput.autoUpdate
                        onToggled: imageInput.autoUpdate = checked
                    }
                    CheckBox {
                        objectName: "preferencesLivePreviewCheckBox"
                        text: qsTr("Show live conversion previews while controls are changing")
                        checked: imageInput.livePreview
                        onToggled: imageInput.livePreview = checked
                    }
                    CheckBox {
                        objectName: "rememberConversionSettingsCheckBox"
                        text: qsTr("Remember the last-used conversion settings")
                        checked: appPreferences.rememberConversionSettings
                        onToggled: appPreferences.rememberConversionSettings = checked
                    }
                    RowLayout {
                        Layout.fillWidth: true

                        Label { text: qsTr("Batch conversion workers") }
                        ComboBox {
                            objectName: "conversionWorkersComboBox"
                            Layout.fillWidth: true
                            model: {
                                let choices = [qsTr("Automatic (%1 workers)")
                                    .arg(appPreferences.automaticConversionWorkers)]
                                for (let worker = 1;
                                     worker <= appPreferences.availableConversionWorkers;
                                     ++worker) {
                                    choices.push(worker === 1
                                        ? qsTr("1 worker")
                                        : qsTr("%1 workers").arg(worker))
                                }
                                return choices
                            }
                            currentIndex: appPreferences.conversionWorkers
                            onActivated: index => appPreferences.conversionWorkers = index
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Parallel workers convert extracted frames; they do not start additional FFmpeg processes.")
                        wrapMode: Text.WordWrap
                        color: palette.placeholderText
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("When remembering is disabled, the selected default preset is applied at the next launch.")
                        wrapMode: Text.WordWrap
                        color: palette.placeholderText
                    }
                    Button {
                        objectName: "resetBehaviorPreferencesButton"
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Reset Behavior")
                        onClicked: appPreferences.resetBehaviorSettings()
                    }
                }
            }

            GroupBox {
                objectName: "defaultPreferencesGroup"
                title: qsTr("Defaults")
                Layout.fillWidth: true

                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8

                    Label { text: qsTr("Conversion preset") }
                    ComboBox {
                        objectName: "defaultConversionPresetComboBox"
                        Layout.fillWidth: true
                        model: [qsTr("Balanced"), qsTr("Crisp pixel art"),
                                qsTr("Smooth photograph"), qsTr("Ordered retro")]
                        currentIndex: appPreferences.defaultPreset
                        onActivated: index => appPreferences.defaultPreset = index
                    }

                    Label { text: qsTr("Export format") }
                    ComboBox {
                        objectName: "defaultExportFormatComboBox"
                        Layout.fillWidth: true
                        model: [qsTr("TIFILES"), qsTr("V9T9"), qsTr("RAW tables"),
                                qsTr("RLE tables"), qsTr("MSX Screen 2"),
                                qsTr("Coleco CVPaint"), qsTr("Adam PowerPaint"),
                                qsTr("Adam HGR"), qsTr("PNG preview")]
                        currentIndex: appPreferences.defaultExportFormat
                        onActivated: index => appPreferences.defaultExportFormat = index
                    }

                    Button {
                        objectName: "resetDefaultPreferencesButton"
                        Layout.columnSpan: 2
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Reset Defaults")
                        onClicked: appPreferences.resetDefaultSettings()
                    }
                }
            }

            GroupBox {
                objectName: "mediaToolsPreferencesGroup"
                title: qsTr("Media Tools")
                Layout.fillWidth: true

                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8

                    Label { text: qsTr("FFmpeg executable") }
                    TextField {
                        objectName: "ffmpegPathField"
                        Layout.fillWidth: true
                        text: appPreferences.ffmpegPath
                        placeholderText: qsTr("Automatic detection")
                        onEditingFinished: appPreferences.ffmpegPath = text
                    }

                    Label { text: qsTr("FFprobe executable") }
                    TextField {
                        objectName: "ffprobePathField"
                        Layout.fillWidth: true
                        text: appPreferences.ffprobePath
                        placeholderText: qsTr("Automatic detection")
                        onEditingFinished: appPreferences.ffprobePath = text
                    }

                    Label {
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        text: appPreferences.mediaToolsStatus
                        wrapMode: Text.WordWrap
                        color: appPreferences.mediaToolsReady
                               ? palette.highlight : palette.placeholderText
                    }

                    RowLayout {
                        Layout.columnSpan: 2
                        Layout.alignment: Qt.AlignRight

                        Button {
                            objectName: "resetMediaToolsButton"
                            text: qsTr("Use Automatic Detection")
                            enabled: !appPreferences.mediaToolsTesting
                            onClicked: appPreferences.resetMediaToolSettings()
                        }
                        Button {
                            objectName: "testMediaToolsButton"
                            text: appPreferences.mediaToolsTesting
                                  ? qsTr("Testing…") : qsTr("Test Media Tools")
                            enabled: !appPreferences.mediaToolsTesting
                            onClicked: {
                                appPreferences.ffmpegPath = ffmpegPathField.text
                                appPreferences.ffprobePath = ffprobePathField.text
                                appPreferences.testMediaTools()
                            }
                        }
                    }
                }
            }

            Button {
                objectName: "restoreAllPreferencesButton"
                Layout.alignment: Qt.AlignRight
                text: qsTr("Restore All Application Defaults")
                onClicked: {
                    appPreferences.restoreAllDefaults()
                    root.applicationWindow.width = 1360
                    root.applicationWindow.height = 860
                }
            }
        }
    }
}
