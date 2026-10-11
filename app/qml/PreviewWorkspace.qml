import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    // 0 = tabbed, 1 = horizontal split, 2 = vertical split.
    property int layoutMode: 1
    // 0 = Screen Image, 1 = Character Editor, 2 = Sprite Editor.
    property int workspaceMode: 0
    property bool screenImageModeAvailable: true
    property bool characterModeAvailable: true
    property bool spriteModeAvailable: true
    signal modeRequested(int mode)
    signal editingSurfaceActivated(bool screenImage)
    readonly property string destinationTitle: workspaceMode === 1
                                               ? qsTr("Character Editor")
                                               : workspaceMode === 2
                                                 ? (editorProject.activeTargetInfo.objectProcessor
                                                    ? qsTr("Object Editor") : qsTr("Sprite Editor"))
                                                 : (editorProject.activeTargetInfo.objectProcessor
                                                    ? qsTr("Object Scene") : qsTr("Screen Image"))

    component SourcePane: PreviewPane {
        objectName: "sourcePreview"
        title: qsTr("Source")
        imageSource: imageInput.sourcePreview
        pixelAspectRatio: imageInput.targetPixelAspectRatio
        details: imageInput.hasImage ? imageInput.sourceDetails : ""
        emptyText: qsTr("Drop an image here or choose Open")
        acceptDrops: true
        sourceTools: true
        onFileDropped: fileUrl => imageInput.openUrl(fileUrl)
        onColorPointPicked: (normalizedX, normalizedY, foreground) =>
            imageInput.pickColor(normalizedX, normalizedY, foreground)
        onEditingSurfaceActivated: screenImage =>
            root.editingSurfaceActivated(screenImage)
    }

    component ScreenImagePane: PreviewPane {
        objectName: "convertedPreview"
        title: editorProject.activeTargetInfo.objectProcessor
               ? qsTr("Object Scene") : qsTr("Screen Image")
        imageSource: imageInput.convertedPreview
        pixelAspectRatio: imageInput.targetPixelAspectRatio
        details: imageInput.conversionDetails
        emptyText: imageInput.hasImage
                   ? (editorProject.activeTargetInfo.objectProcessor
                      ? qsTr("The converted object scene will appear here")
                      : qsTr("The screen image will appear here"))
                   : qsTr("Open a source image to begin")
        busy: imageInput.busy
        screenImageTools: true
        convertedTools: true
        showTitle: root.layoutMode !== 0
        onColorPointPicked: (normalizedX, normalizedY, foreground) =>
            imageInput.pickScreenImageColor(normalizedX, normalizedY, foreground)
        onEditingSurfaceActivated: screenImage =>
            root.editingSurfaceActivated(screenImage)
    }

    component CharacterEditorPane: EditorWorkspacePlaceholder {
        objectName: "characterEditorWorkspace"
        title: qsTr("Character Editor")
        description: qsTr("Create and edit character patterns under the active target's rules.")
        importDescription: qsTr("Extract a character-aligned Screen Image region into the active pattern set, then inspect its arrangement with Pattern Previewer.")
        editorKind: 0
        showTitle: root.layoutMode !== 0
    }

    component SpriteEditorPane: EditorWorkspacePlaceholder {
        objectName: "spriteEditorWorkspace"
        title: editorProject.activeTargetInfo.objectProcessor
               ? qsTr("Object Editor") : qsTr("Sprite Editor")
        description: editorProject.activeTargetInfo.objectProcessor
                     ? qsTr("Create, size, link, and place objects under the active processor's rules.")
                     : qsTr("Create and edit sprite patterns under the active target's rules.")
        importDescription: editorProject.activeTargetInfo.objectProcessor
                           ? qsTr("The object scene keeps pattern data, per-object dimensions, palette selection, attributes, and placement together.")
                           : qsTr("Screen Image selection, scaling, transparency, color reduction, and palette fitting will feed this editor.")
        editorKind: 1
        showTitle: root.layoutMode !== 0
    }

    component DestinationPane: Loader {
        sourceComponent: root.workspaceMode === 1
                         ? characterEditorPane
                         : root.workspaceMode === 2
                           ? spriteEditorPane
                           : screenImagePane
    }

    Component {
        id: screenImagePane
        ScreenImagePane {}
    }

    Component {
        id: characterEditorPane
        CharacterEditorPane {}
    }

    Component {
        id: spriteEditorPane
        SpriteEditorPane {}
    }

    Loader {
        anchors.fill: parent
        sourceComponent: root.layoutMode === 0 ? tabbedLayout : splitLayout
    }

    Component {
        id: tabbedLayout

        ColumnLayout {
            objectName: "tabbedPreviewLayout"
            spacing: 8

            TabBar {
                id: previewTabs
                objectName: "previewTabBar"
                Layout.fillWidth: true

                TabButton {
                    objectName: "sourcePreviewTab"
                    text: qsTr("Source")
                    Accessible.name: qsTr("Show source image")
                    onClicked: root.editingSurfaceActivated(false)
                }
                TabButton {
                    objectName: "convertedPreviewTab"
                    text: editorProject.activeTargetInfo.objectProcessor
                          ? qsTr("Object Scene") : qsTr("Screen Image")
                    enabled: root.screenImageModeAvailable
                    Accessible.name: editorProject.activeTargetInfo.objectProcessor
                                     ? qsTr("Show Object Scene mode")
                                     : qsTr("Show Screen Image mode")
                    onClicked: {
                        root.modeRequested(0)
                        root.editingSurfaceActivated(true)
                    }
                }
                TabButton {
                    objectName: "characterEditorTab"
                    text: qsTr("Character Editor")
                    enabled: root.characterModeAvailable
                    Accessible.name: qsTr("Show Character Editor mode")
                    onClicked: root.modeRequested(1)
                }
                TabButton {
                    objectName: "spriteEditorTab"
                    text: editorProject.activeTargetInfo.objectProcessor
                          ? qsTr("Object Editor") : qsTr("Sprite Editor")
                    enabled: root.spriteModeAvailable
                    Accessible.name: editorProject.activeTargetInfo.objectProcessor
                                     ? qsTr("Show Object Editor mode")
                                     : qsTr("Show Sprite Editor mode")
                    onClicked: root.modeRequested(2)
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: previewTabs.currentIndex

                SourcePane {
                    showTitle: false
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
                ScreenImagePane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
                CharacterEditorPane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
                SpriteEditorPane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }
        }
    }

    Component {
        id: splitLayout

        SplitView {
            id: splitPreviewLayout
            readonly property real handleThickness: 2
            objectName: root.layoutMode === 1
                        ? "horizontalPreviewLayout" : "verticalPreviewLayout"
            orientation: root.layoutMode === 1 ? Qt.Horizontal : Qt.Vertical

            SourcePane {
                SplitView.preferredWidth: (splitPreviewLayout.width
                                           - splitPreviewLayout.handleThickness) / 2
                SplitView.preferredHeight: (splitPreviewLayout.height
                                            - splitPreviewLayout.handleThickness) / 2
                SplitView.minimumWidth: 220
                SplitView.minimumHeight: 180
            }

            DestinationPane {
                SplitView.preferredWidth: (splitPreviewLayout.width
                                           - splitPreviewLayout.handleThickness) / 2
                SplitView.preferredHeight: (splitPreviewLayout.height
                                            - splitPreviewLayout.handleThickness) / 2
                SplitView.minimumWidth: 220
                SplitView.minimumHeight: 180
            }
        }
    }
}
