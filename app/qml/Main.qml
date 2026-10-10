import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: window

    width: 1360
    height: 860
    minimumWidth: 720
    minimumHeight: 560
    visible: true
    title: qsTr("RetroVDP Studio")
    // Normally tracks the window; kept as a separate property so embedded
    // hosts and automated layout checks can supply their available width.
    property real layoutWidth: width
    readonly property bool compactLayout: layoutWidth < 1100
    property int previewLayout: appPreferences.previewLayout
    // 0 = Screen Image, 1 = Character Editor, 2 = Sprite Editor.
    property int workspaceMode: 0
    property bool screenImageEditingActive: false
    property bool conversionPanelVisible: appPreferences.sidePanelVisible
    property int conversionPanelMode: appPreferences.sidePanelMode
    property bool mediaWorkspaceVisible: mediaClip.clipLoaded
    property url pendingMediaRecipe

    function synchronizeConversionPanel() {
        if (conversionPanelMode === 1 && conversionPanelVisible)
            overlayConversionPanel.open()
        else
            overlayConversionPanel.close()
    }

    onPreviewLayoutChanged: {
        if (appPreferences.previewLayout !== previewLayout)
            appPreferences.previewLayout = previewLayout
    }
    onConversionPanelVisibleChanged: {
        if (appPreferences.sidePanelVisible !== conversionPanelVisible)
            appPreferences.sidePanelVisible = conversionPanelVisible
        Qt.callLater(synchronizeConversionPanel)
    }
    onConversionPanelModeChanged: {
        if (appPreferences.sidePanelMode !== conversionPanelMode)
            appPreferences.sidePanelMode = conversionPanelMode
        Qt.callLater(synchronizeConversionPanel)
    }
    onWorkspaceModeChanged: {
        if (editorProject.workspaceMode !== workspaceMode)
            editorProject.workspaceMode = workspaceMode
        if (appPreferences.rememberWorkspaceMode
                && appPreferences.lastWorkspaceMode !== workspaceMode)
            appPreferences.lastWorkspaceMode = workspaceMode
    }
    onClosing: close => appPreferences.saveWindowGeometry(x, y, width, height)
    Component.onCompleted: {
        if (appPreferences.restoreWindowGeometry
                && appPreferences.hasWindowGeometry) {
            window.x = appPreferences.windowX
            window.y = appPreferences.windowY
            window.width = appPreferences.windowWidth
            window.height = appPreferences.windowHeight
        }
        if (appPreferences.rememberWorkspaceMode)
            window.workspaceMode = appPreferences.lastWorkspaceMode
        synchronizeConversionPanel()
    }

    Connections {
        target: editorProject
        function onProjectChanged() {
            if (window.workspaceMode !== editorProject.workspaceMode)
                window.workspaceMode = editorProject.workspaceMode
        }
    }

    Connections {
        target: appPreferences
        function onPreferencesChanged() {
            if (window.previewLayout !== appPreferences.previewLayout)
                window.previewLayout = appPreferences.previewLayout
            if (window.conversionPanelVisible !== appPreferences.sidePanelVisible)
                window.conversionPanelVisible = appPreferences.sidePanelVisible
            if (window.conversionPanelMode !== appPreferences.sidePanelMode)
                window.conversionPanelMode = appPreferences.sidePanelMode
        }
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open as Source")
        nameFilters: [
            qsTr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.webp *.pcx)"),
            qsTr("Retro images (*.tiap *.tiac *.tiam *_P *_C *_M *.sc2 *.pc *.pp *.hgr *.hgrh)"),
            qsTr("All files (*)")
        ]
        onAccepted: imageInput.openUrl(selectedFile)
    }

    FileDialog {
        id: openMediaClipDialog
        objectName: "openMediaClipDialog"
        title: qsTr("Open media clip package")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("RetroVDP clip metadata (clip.json)"),
                      qsTr("JSON files (*.json)"), qsTr("All files (*)")]
        onAccepted: {
            if (mediaClip.openClipUrl(selectedFile))
                window.mediaWorkspaceVisible = true
        }
    }

    FileDialog {
        id: mediaRecipeDialog
        objectName: "mediaRecipeDialog"
        title: qsTr("Choose conversion recipe")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("RetroVDP Studio recipes (*.rvdp.json *.json)"),
                      qsTr("All files (*)")]
        onAccepted: {
            window.pendingMediaRecipe = selectedFile
            mediaOutputParentDialog.open()
        }
    }

    FolderDialog {
        id: mediaOutputParentDialog
        objectName: "mediaOutputParentDialog"
        title: qsTr("Choose parent folder for the target run")
        onAccepted: mediaBatch.startConversion(
            window.pendingMediaRecipe, selectedFolder,
            mediaClip.clipName + "-target-run")
    }

    Connections {
        target: mediaBatch
        function onConversionFinished() {
            if (mediaClip.openClipUrl(mediaBatch.outputManifestUrl))
                window.mediaWorkspaceVisible = true
        }
    }

    FolderDialog {
        id: exportDialog
        title: qsTr("Choose export folder")
        onAccepted: imageInput.exportToDirectory(selectedFolder)
    }

    Dialog {
        id: overwriteDialog
        title: qsTr("Replace existing export files?")
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        width: Math.min(window.width - 48, 620)
        onAccepted: imageInput.confirmOverwrite()
        onRejected: imageInput.cancelOverwrite()

        Label {
            width: parent.width
            text: imageInput.overwriteMessage
            wrapMode: Text.WrapAnywhere
        }
    }

    Connections {
        target: imageInput
        function onExportChanged() {
            if (imageInput.overwriteMessage.length > 0 && !overwriteDialog.opened)
                overwriteDialog.open()
        }
    }

    Dialog {
        id: aboutDialog
        objectName: "aboutDialog"
        title: qsTr("About RetroVDP Studio")
        modal: true
        standardButtons: Dialog.Close
        width: Math.min(window.width - 48, 620)

        ColumnLayout {
            width: parent.width
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                spacing: 14
                Image {
                    objectName: "aboutApplicationIcon"
                    source: "qrc:/qt/qml/RetroVDPStudio/assets/icons/RetroVDPStudio-64.png"
                    sourceSize.width: 64
                    sourceSize.height: 64
                    Layout.preferredWidth: 64
                    Layout.preferredHeight: 64
                    fillMode: Image.PreserveAspectFit
                    Accessible.name: qsTr("RetroVDP Studio icon")
                }
                Label {
                    text: qsTr("RetroVDP Studio")
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
            }
            Label {
                objectName: "aboutVersionLabel"
                Layout.fillWidth: true
                text: qsTr("Version %1").arg(Qt.application.version)
                font.weight: Font.DemiBold
                color: palette.placeholderText
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("RetroVDP Studio cross-platform architecture, Qt interface, user experience, and new features are created by Cisco Garcia / CiscoGarciaFL.")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Copyright © 2026 Cisco Garcia / CiscoGarciaFL")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.RichText
                text: qsTr("<b>Third-party work</b><br>Separately licensed third-party components must retain their own copyright and license notices. Qt image I/O and independently licensed or independently implemented codecs provide those boundaries.<br><br><b>Inspiration</b><br>RetroVDP Studio is inspired by <a href='https://github.com/tursilion/convert9918'>Convert9918</a>'s conversion behavior, supported target systems, and file-format knowledge designed and created by Mike Brent, also known as Tursi, of <a href='http://harmlesslion.com/'>HarmlessLion.com</a>.")
                onLinkActivated: link => Qt.openUrlExternally(link)
                Accessible.name: qsTr("Project and attribution links")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("See NOTICE.md for the complete attribution notice.")
                color: palette.placeholderText
            }
        }
    }

    Action {
        id: newAction
        objectName: "newAction"
        text: qsTr("&New Project…")
        shortcut: StandardKey.New
        onTriggered: projectDialog.openForNewProject()
    }
    Action {
        id: openAction
        objectName: "openAction"
        text: qsTr("Open &Source…")
        shortcut: "Ctrl+O"
        onTriggered: openDialog.open()
    }
    Action {
        id: openMediaClipAction
        objectName: "openMediaClipAction"
        text: qsTr("Open Media &Clip…")
        shortcut: "Ctrl+Alt+O"
        onTriggered: openMediaClipDialog.open()
    }
    Action {
        id: pasteAction
        text: qsTr("&Paste")
        shortcut: "Ctrl+V"
        onTriggered: {
            if (window.workspaceMode === 0 && !window.screenImageEditingActive
                    && imageInput.hasImage)
                imageInput.pasteSourceImage()
            else if (window.workspaceMode === 0 && imageInput.hasConversion)
                imageInput.pasteScreenImage()
            else
                imageInput.pasteClipboard()
        }
    }

    PreferencesDialog {
        id: preferencesDialog
        objectName: "preferencesDialog"
        applicationWindow: window
    }

    TargetSupportDialog {
        id: targetSupportDialog
    }

    ProjectDialog {
        id: projectDialog
    }

    FileDialog {
        id: loadRecipeDialog
        title: qsTr("Open project")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("RetroVDP Studio projects (*.rvdp.json *.json)"),
                      qsTr("All files (*)")]
        onAccepted: editorProject.loadRecipe(selectedFile)
    }

    FileDialog {
        id: saveRecipeDialog
        title: qsTr("Save project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "rvdp.json"
        nameFilters: [qsTr("RetroVDP Studio projects (*.rvdp.json)"),
                      qsTr("JSON files (*.json)")]
        onAccepted: editorProject.saveRecipe(selectedFile)
    }
    FolderDialog {
        id: genesisCharacterExportDialog
        objectName: "genesisCharacterExportDialog"
        title: editorProject.activeTargetInfo.id === "sega-genesis-vdp"
               ? qsTr("Export native Genesis character assets")
               : qsTr("Export native Nintendo PPU assets")
        onAccepted: {
            if (editorProject.activeTargetInfo.id === "sega-genesis-vdp")
                editorProject.exportGenesisCharacterAssets(selectedFolder)
            else
                editorProject.exportNintendoEditorAssets(selectedFolder)
        }
    }
    Action {
        id: reloadAction
        objectName: "reloadAction"
        text: qsTr("&Reload")
        shortcut: "Ctrl+R"
        enabled: imageInput.canReload
        onTriggered: imageInput.reloadSource()
    }
    Action {
        id: exportAction
        objectName: "exportAction"
        text: qsTr("&Export…")
        shortcut: "Ctrl+E"
        enabled: (window.workspaceMode === 0 && imageInput.hasConversion)
                 || (window.workspaceMode === 1
                     && editorProject.activeTargetInfo.id === "sega-genesis-vdp")
                 || ((window.workspaceMode === 1 || window.workspaceMode === 2)
                     && (editorProject.activeTargetInfo.id === "game-boy-ppu"
                         || editorProject.activeTargetInfo.id === "game-boy-color-ppu"
                         || editorProject.activeTargetInfo.id === "super-nes-ppu"))
        onTriggered: {
            if (window.workspaceMode === 1 || window.workspaceMode === 2)
                genesisCharacterExportDialog.open()
            else
                exportDialog.open()
        }
    }
    Action {
        id: exitAction
        objectName: "exitAction"
        text: qsTr("E&xit")
        shortcut: StandardKey.Quit
        onTriggered: window.close()
    }
    Action {
        id: supportedTargetsAction
        objectName: "supportedTargetsAction"
        text: qsTr("About &Supported Targets")
        onTriggered: targetSupportDialog.open()
    }
    Action {
        id: aboutAction
        objectName: "aboutAction"
        text: qsTr("&About RetroVDP Studio")
        shortcut: "F1"
        onTriggered: aboutDialog.open()
    }

    Shortcut {
        sequence: "Ctrl+Z"
        enabled: imageInput.canUndoScreenImage
                 || imageInput.canUndoDrawing || imageInput.canUndo
        onActivated: {
            if (window.workspaceMode === 0 && !window.screenImageEditingActive
                    && imageInput.canUndoDrawing)
                imageInput.undoDrawing()
            else if (window.workspaceMode === 0 && imageInput.canUndoScreenImage)
                imageInput.undoScreenImage()
            else if (imageInput.canUndoDrawing)
                imageInput.undoDrawing()
            else
                imageInput.undoSettings()
        }
    }
    Action {
        id: preferencesAction
        objectName: "preferencesAction"
        text: qsTr("&Preferences…")
        shortcut: "Ctrl+,"
        onTriggered: preferencesDialog.open()
    }
    Action {
        id: loadRecipeAction
        objectName: "loadRecipeAction"
        text: qsTr("&Open Project…")
        shortcut: "Ctrl+Shift+O"
        onTriggered: loadRecipeDialog.open()
    }
    Action {
        id: saveRecipeAction
        objectName: "saveRecipeAction"
        text: qsTr("&Save Project…")
        shortcut: "Ctrl+Shift+S"
        onTriggered: saveRecipeDialog.open()
    }
    Action {
        id: projectSettingsAction
        objectName: "projectSettingsAction"
        text: qsTr("Project &Settings…")
        onTriggered: projectDialog.openForProjectSettings()
    }
    Shortcut {
        sequence: "Ctrl+Y"
        enabled: imageInput.canRedoScreenImage || imageInput.canRedoDrawing
        onActivated: {
            if (window.workspaceMode === 0 && !window.screenImageEditingActive
                    && imageInput.canRedoDrawing)
                imageInput.redoDrawing()
            else if (window.workspaceMode === 0 && imageInput.canRedoScreenImage)
                imageInput.redoScreenImage()
            else
                imageInput.redoDrawing()
        }
    }
    Shortcut {
        sequence: "Ctrl+C"
        enabled: window.workspaceMode === 0
                 && (imageInput.hasConversion || imageInput.hasImage)
        onActivated: {
            if (!window.screenImageEditingActive && imageInput.hasImage)
                imageInput.copySourceImage()
            else
                imageInput.copyScreenImage()
        }
    }
    Shortcut { sequence: "Ctrl+0"; onActivated: imageInput.resetSettings() }

    menuBar: MenuBar {
        objectName: "mainMenuBar"

        Menu {
            objectName: "fileMenu"
            title: qsTr("&File")
            MenuItem {
                objectName: "newMenuItem"
                action: newAction
            }
            MenuItem { action: loadRecipeAction }
            MenuItem { action: saveRecipeAction }
            MenuItem { action: projectSettingsAction }
            MenuSeparator {}
            MenuItem { action: openAction }
            MenuItem { action: openMediaClipAction }
            MenuItem {
                objectName: "reloadMenuItem"
                action: reloadAction
            }
            MenuItem { action: pasteAction }
            MenuSeparator {}
            MenuItem { action: exportAction }
            MenuItem { action: preferencesAction }
            MenuSeparator { objectName: "exitMenuSeparator" }
            MenuItem {
                objectName: "exitMenuItem"
                action: exitAction
            }
        }

        Menu {
            objectName: "viewMenu"
            title: qsTr("&View")

            MenuItem {
                text: qsTr("&Tabbed")
                checkable: true
                checked: window.previewLayout === 0
                onTriggered: window.previewLayout = 0
            }
            MenuItem {
                text: qsTr("&Horizontal")
                checkable: true
                checked: window.previewLayout === 1
                onTriggered: window.previewLayout = 1
            }
            MenuItem {
                text: qsTr("&Vertical")
                checkable: true
                checked: window.previewLayout === 2
                onTriggered: window.previewLayout = 2
            }

            MenuSeparator {}

            MenuItem {
                objectName: "showSidePanelMenuItem"
                text: qsTr("Show &Side Panel")
                checkable: true
                checked: window.conversionPanelVisible
                onTriggered: window.conversionPanelVisible = !window.conversionPanelVisible
            }

            Menu {
                objectName: "sidePanelPlacementMenu"
                title: qsTr("Side Panel &Placement")
                MenuItem {
                    text: qsTr("&Adjacent")
                    checkable: true
                    checked: window.conversionPanelMode === 0
                    onTriggered: {
                        window.conversionPanelMode = 0
                        window.conversionPanelVisible = true
                    }
                }
                MenuItem {
                    text: qsTr("&Overlay")
                    checkable: true
                    checked: window.conversionPanelMode === 1
                    onTriggered: {
                        window.conversionPanelMode = 1
                        window.conversionPanelVisible = true
                    }
                }
            }
        }

        Menu {
            objectName: "modeMenu"
            title: qsTr("&Mode")
            enabled: window.previewLayout !== 0

            MenuItem {
                objectName: "screenImageModeMenuItem"
                text: qsTr("&Screen Image")
                enabled: editorProject.screenImageModeAvailable
                checkable: true
                checked: window.workspaceMode === 0
                onTriggered: window.workspaceMode = 0
            }
            MenuItem {
                objectName: "characterEditorModeMenuItem"
                text: qsTr("&Character Editor")
                enabled: editorProject.characterModeAvailable
                checkable: true
                checked: window.workspaceMode === 1
                onTriggered: window.workspaceMode = 1
            }
            MenuItem {
                objectName: "spriteEditorModeMenuItem"
                text: qsTr("S&prite Editor")
                enabled: editorProject.spriteModeAvailable
                checkable: true
                checked: window.workspaceMode === 2
                onTriggered: window.workspaceMode = 2
            }
        }

        Menu {
            title: qsTr("&Help")
            objectName: "helpMenu"
            MenuItem {
                objectName: "supportedTargetsMenuItem"
                action: supportedTargetsAction
            }
            MenuSeparator {}
            MenuItem { action: aboutAction }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        ThemedFrame {
            objectName: "projectBar"
            Layout.fillWidth: true
            Layout.preferredHeight: activeTargetCombo.implicitHeight + 8
            padding: 0

            RowLayout {
                objectName: "projectBarContent"
                anchors.fill: parent
                anchors.margins: 4
                spacing: 12

                Label {
                    text: qsTr("Project:")
                    color: palette.placeholderText
                }
                Label {
                    objectName: "projectBarName"
                    text: editorProject.projectName
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("Active target:")
                    color: palette.placeholderText
                }
                ComboBox {
                    id: activeTargetCombo
                    objectName: "activeTargetCombo"
                    Layout.preferredWidth: 190
                    model: editorProject.supportedTargets
                    textRole: "name"
                    valueRole: "value"
                    currentIndex: count > 0
                        ? indexOfValue(editorProject.activeTarget) : -1
                    onActivated: editorProject.activeTarget = currentValue
                    Accessible.name: qsTr("Active project target")
                }
            }
        }

        RowLayout {
            visible: !window.mediaWorkspaceVisible
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            PreviewWorkspace {
                id: previews
                objectName: "previewWorkspace"
                layoutMode: window.previewLayout
                workspaceMode: window.workspaceMode
                screenImageModeAvailable: editorProject.screenImageModeAvailable
                characterModeAvailable: editorProject.characterModeAvailable
                spriteModeAvailable: editorProject.spriteModeAvailable
                onModeRequested: mode => window.workspaceMode = mode
                onEditingSurfaceActivated: screenImage =>
                    window.screenImageEditingActive = screenImage
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            ThemedFrame {
                objectName: "adjacentConversionPanel"
                visible: window.conversionPanelVisible && window.conversionPanelMode === 0
                Layout.preferredWidth: 350
                Layout.maximumWidth: 420
                Layout.fillHeight: true

                SidePanelHost {
                    anchors.fill: parent
                    workspaceMode: window.workspaceMode
                    onExportRequested: exportDialog.open()
                }
            }
        }

        MediaClipWorkspace {
            objectName: "mediaClipWorkspace"
            visible: window.mediaWorkspaceVisible
            Layout.fillWidth: true
            Layout.fillHeight: true
            onCloseRequested: {
                mediaClip.pause()
                window.mediaWorkspaceVisible = false
            }
            onFrameOpenRequested: frameUrl => {
                mediaClip.pause()
                imageInput.openUrl(frameUrl)
                window.mediaWorkspaceVisible = false
            }
            onConvertRequested: mediaRecipeDialog.open()
        }
    }

    Item {
        id: overlayExpandRail
        objectName: "overlayExpandRail"
        anchors.top: parent.top
        anchors.topMargin: 60
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 38
        z: 20
        visible: window.conversionPanelMode === 1 && !window.conversionPanelVisible

        ToolButton {
            id: overlayExpandButton
            objectName: "overlayExpandButton"
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: 6
            text: qsTr("‹")
            font.pixelSize: 24
            background: Rectangle {
                objectName: "overlayExpandButtonBackground"
                radius: 3
                color: overlayExpandButton.down
                       ? overlayExpandButton.palette.mid
                       : overlayExpandButton.hovered
                         ? overlayExpandButton.palette.light
                         : overlayExpandButton.palette.button
                border.width: 1
                border.color: overlayExpandButton.palette.mid
            }
            activeFocusOnTab: true
            Accessible.name: qsTr("Show Side Panel")
            ToolTip.visible: hovered
            ToolTip.text: Accessible.name
            onClicked: window.conversionPanelVisible = true
        }
    }

    Drawer {
        id: overlayConversionPanel
        objectName: "overlayConversionPanel"
        edge: Qt.RightEdge
        width: Math.min(window.width * 0.9, 390)
        height: window.height
        modal: false
        dim: false
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: overlayConversionPanel.palette.window
            border.width: 1
            border.color: overlayConversionPanel.palette.windowText
        }
        property bool pointerWasInside: false
        function handlePointerPresence(inside) {
            if (inside) {
                pointerWasInside = true
            } else if (pointerWasInside && opened
                       && window.conversionPanelMode === 1) {
                window.conversionPanelVisible = false
            }
        }
        onOpened: pointerWasInside = false
        onClosed: {
            if (window.conversionPanelMode === 1 && window.conversionPanelVisible)
                window.conversionPanelVisible = false
        }

        HoverHandler {
            acceptedDevices: PointerDevice.Mouse
            onHoveredChanged: overlayConversionPanel.handlePointerPresence(hovered)
        }

        SidePanelHost {
            anchors.fill: parent
            anchors.margins: 12
            workspaceMode: window.workspaceMode
            closable: true
            onExportRequested: exportDialog.open()
            onCloseRequested: window.conversionPanelVisible = false
        }
    }

    footer: ToolBar {
        height: Math.max(36, statusLabel.implicitHeight + 12)
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            Label {
                id: sourceNameLabel
                objectName: "statusSourceName"
                visible: text.length > 0
                Layout.maximumWidth: Math.max(120, window.width * 0.4)
                text: window.mediaWorkspaceVisible
                      ? mediaClip.clipName : imageInput.sourceName
                elide: Text.ElideMiddle
                Accessible.name: qsTr("Source file: %1").arg(text)
                ToolTip.visible: sourceNameHover.hovered && truncated
                ToolTip.text: text

                HoverHandler {
                    id: sourceNameHover
                }
            }
            ToolSeparator {
                visible: sourceNameLabel.visible
            }
            Label {
                id: statusLabel
                Layout.fillWidth: true
                text: mediaBatch.errorMessage.length > 0
                      ? mediaBatch.errorMessage
                      : mediaBatch.statusMessage.length > 0
                        ? mediaBatch.statusMessage
                      : mediaClip.errorMessage.length > 0
                      ? mediaClip.errorMessage
                      : window.mediaWorkspaceVisible
                        && mediaClip.statusMessage.length > 0
                        ? mediaClip.statusMessage
                      : editorProject.errorMessage.length > 0
                        ? editorProject.errorMessage
                      : editorProject.statusMessage.length > 0
                        ? editorProject.statusMessage
                        : imageInput.errorMessage.length > 0
                          ? imageInput.errorMessage : imageInput.statusMessage
                color: mediaBatch.errorMessage.length > 0
                       || mediaClip.errorMessage.length > 0
                       || editorProject.errorMessage.length > 0
                       || imageInput.errorMessage.length > 0
                       ? "#ff8f8f" : palette.text
                wrapMode: Text.WordWrap
                Accessible.name: text
            }
            BusyIndicator {
                running: imageInput.busy || mediaBatch.busy
                visible: running
                implicitWidth: 24
                implicitHeight: 24
            }
        }
    }
}
