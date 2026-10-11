import QtQuick

Item {
    id: root

    required property int workspaceMode
    property bool closable: false
    signal exportRequested()
    signal closeRequested()

    Loader {
        anchors.fill: parent
        sourceComponent: root.workspaceMode === 1
                         ? characterSidePanel
                         : root.workspaceMode === 2
                           ? spriteSidePanel
                           : screenImageSidePanel
    }

    Component {
        id: screenImageSidePanel
        SettingsPanel {
            objectName: "screenImageSidePanel"
            closable: root.closable
            onExportRequested: root.exportRequested()
            onCloseRequested: root.closeRequested()
        }
    }

    Component {
        id: characterSidePanel
        EditorSidePanelPlaceholder {
            objectName: "characterEditorSidePanel"
            editorTitle: qsTr("Character Options")
            editorKind: 0
            importDescription: qsTr("Select a character-aligned Screen Image region, map its pixels to the active target palette, and write the extracted characters into the active pattern set.")
            closable: root.closable
            onCloseRequested: root.closeRequested()
        }
    }

    Component {
        id: spriteSidePanel
        EditorSidePanelPlaceholder {
            objectName: "spriteEditorSidePanel"
            editorTitle: editorProject.activeTargetInfo.objectProcessor
                         ? qsTr("Object Options") : qsTr("Sprite Options")
            editorKind: 1
            importDescription: editorProject.activeTargetInfo.objectProcessor
                               ? qsTr("Object options control dimensions, palette, priority, transform attributes, and scene placement before native export.")
                               : qsTr("Sprite import will select a Screen Image region, choose sprite dimensions and transparency, reduce its colors, and fit it to the active hardware palette before committing editable sprite data.")
            closable: root.closable
            onCloseRequested: root.closeRequested()
        }
    }
}
