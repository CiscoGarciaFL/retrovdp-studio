import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property int drawingTool
    required property real tilingScale
    readonly property var allEditorSlots: editorProject.characterEditorSlots
    readonly property var patternEditorSlots: {
        const unique = []
        const seen = ({})
        for (let index = 0; index < allEditorSlots.length; ++index) {
            const slot = allEditorSlots[index]
            if (!slot.loaded) {
                unique.push(slot)
                continue
            }
            const key = slot.setIndex + ":" + slot.patternIndex
            if (!seen[key]) {
                seen[key] = true
                unique.push(slot)
            }
        }
        return unique
    }

    function slotRepresentsActivePattern(slot) {
        if (allEditorSlots.length === 0)
            return false
        const activeSlot = allEditorSlots[editorProject.activeCharacterEditor]
        if (!slot.loaded || !activeSlot.loaded)
            return slot.index === activeSlot.index
        return slot.setIndex === activeSlot.setIndex
            && slot.patternIndex === activeSlot.patternIndex
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        Rectangle {
            id: editorTray
            objectName: "characterPatternEditorTray"
            Layout.fillWidth: true
            readonly property int editorCount: root.patternEditorSlots.length
            readonly property int editorColumns: Math.max(
                1, Math.floor(Math.max(234, width - 8) / 234))
            readonly property int editorRows: Math.max(
                1, Math.ceil(editorCount / editorColumns))
            readonly property real naturalHeight: editorRows * 194 + 36
            readonly property int renderedEditorCount: editorRepeater.count
            readonly property bool activeEditorEmpty:
                editorCount > 0
                && !editorProject.characterEditorSlots[
                    editorProject.activeCharacterEditor].loaded
            Layout.preferredHeight: editorProject.characterTilingMode
                                    ? 192 * root.tilingScale + 42
                                    : Math.min(naturalHeight,
                                               Math.max(224, root.height * 0.56))
            Layout.minimumHeight: editorProject.characterTilingMode ? 234 : 224
            color: "#0f151a"
            border.width: 1
            border.color: "#46515d"
            radius: 4

            function increaseActivePreviewScale() {
                for (let index = 0; index < editorRepeater.count; ++index) {
                    const item = editorRepeater.itemAt(index)
                    if (item && item.activeEntry) {
                        item.increasePreviewScale()
                        return
                    }
                }
            }

            function decreaseActivePreviewScale() {
                for (let index = 0; index < editorRepeater.count; ++index) {
                    const item = editorRepeater.itemAt(index)
                    if (item && item.activeEntry) {
                        item.decreasePreviewScale()
                        return
                    }
                }
            }

            function currentActivePreviewScale() {
                for (let index = 0; index < editorRepeater.count; ++index) {
                    const item = editorRepeater.itemAt(index)
                    if (item && item.activeEntry)
                        return item.previewScale
                }
                return 0
            }

            Flickable {
                id: editorFlick
                objectName: "characterPatternEditorGrid"
                visible: !editorProject.characterTilingMode
                enabled: visible
                anchors {
                    top: parent.top
                    left: parent.left
                    right: parent.right
                    bottom: editorTrayControls.top
                    margins: 4
                }
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: width
                contentHeight: editorFlow.height
                ScrollBar.vertical: ScrollBar {
                    policy: editorFlick.contentHeight > editorFlick.height
                            ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                }

                Flow {
                    id: editorFlow
                    width: editorFlick.width
                    height: editorTray.editorRows * 194
                    spacing: 0

                    Repeater {
                        id: editorRepeater
                        objectName: "characterPatternEditorRepeater"
                        model: root.patternEditorSlots

                        delegate: Item {
                            required property int index
                            required property var modelData
                            readonly property var editorSlot: modelData
                            readonly property bool activeEntry:
                                root.slotRepresentsActivePattern(editorSlot)
                            readonly property int previewScale:
                                patternEditor.previewScale
                            width: editorFlow.width / editorTray.editorColumns
                            height: 194

                            function increasePreviewScale() {
                                patternEditor.increasePreviewScale()
                            }

                            function decreasePreviewScale() {
                                patternEditor.decreasePreviewScale()
                            }

                            PatternEditor {
                                id: patternEditor
                                objectName: "characterPatternEditor"
                                anchors.horizontalCenter: parent.horizontalCenter
                                editorIndex: parent.index
                                editorCount: editorTray.editorCount
                                loaded: parent.editorSlot.loaded
                                patternSetIndex: parent.editorSlot.setIndex
                                patternIndex: parent.editorSlot.patternIndex
                                drawingTool: root.drawingTool
                                active: parent.activeEntry
                                onSelected:
                                    editorProject.activeCharacterEditor =
                                        parent.editorSlot.index
                                onMoveRequested: targetIndex => {
                                    const target = root.patternEditorSlots[targetIndex]
                                    if (target)
                                        editorProject.moveCharacterEditor(
                                            parent.editorSlot.index, target.index)
                                }
                                onRemoveRequested: {
                                    editorProject.activeCharacterEditor =
                                        parent.editorSlot.index
                                    editorProject.removeActiveCharacterEditor()
                                }
                            }
                        }
                    }
                }
            }

            CharacterTilingView {
                id: tilingView
                objectName: "characterTilingView"
                anchors {
                    top: parent.top
                    left: parent.left
                    right: parent.right
                    bottom: editorTrayControls.top
                    margins: 4
                }
                visible: editorProject.characterTilingMode
                enabled: visible
                zoomScale: root.tilingScale
            }

            Row {
                id: editorTrayControls
                objectName: "characterPatternEditorControls"
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 5
                anchors.bottomMargin: 4
                spacing: 2

                ToolButton {
                    objectName: "removeCharacterEditorButton"
                    implicitWidth: 26
                    implicitHeight: 26
                    text: "−"
                    enabled: editorTray.editorCount > 1
                    Accessible.name: editorProject.characterTilingMode
                                     ? qsTr("Remove active tile")
                                     : qsTr("Remove active pattern editor")
                    ToolTip.visible: hovered
                    ToolTip.text: Accessible.name
                    onClicked: editorProject.removeActiveCharacterEditor()
                }
                ToolButton {
                    objectName: "addCharacterEditorButton"
                    implicitWidth: 26
                    implicitHeight: 26
                    text: "+"
                    enabled: root.allEditorSlots.length
                             < (editorProject.activeTargetInfo.id === "sega-genesis-vdp"
                                ? editorProject.characterMapColumns
                                  * editorProject.characterMapRows * 3
                                : 768)
                    Accessible.name: editorProject.characterTilingMode
                                     ? qsTr("Add empty tile")
                                     : qsTr("Add empty pattern editor")
                    ToolTip.visible: hovered
                    ToolTip.text: Accessible.name
                    onClicked: editorProject.addCharacterEditor()
                }
            }
        }

        RowLayout {
            objectName: "characterPatternSelectionRow"
            Layout.fillWidth: true
            spacing: 4

            Item { Layout.fillWidth: true }
            Label { text: qsTr("Set") }
            ComboBox {
                objectName: "characterSetComboBox"
                Layout.preferredWidth: 55
                model: editorProject.characterSetNames
                currentIndex: editorProject.activeCharacterSet
                onActivated: index => editorProject.activeCharacterSet = index
            }
            Label { text: qsTr("Pattern") }
            SpinBox {
                objectName: "characterPatternSpinBox"
                implicitWidth: 48
                from: 0
                to: editorProject.characterPatternsPerSet - 1
                value: editorProject.activeCharacterPattern
                editable: true
                onValueModified: editorProject.activeCharacterPattern = value
            }
        }

        GridView {
            id: patternGrid
            objectName: "characterPatternGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: editorProject.characterPatternsPerSet
            clip: true
            cellWidth: Math.max(28, Math.floor(width / 16))
            cellHeight: cellWidth
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                required property int index
                width: patternGrid.cellWidth - 2
                height: patternGrid.cellHeight - 2
                color: index === editorProject.activeCharacterPattern
                       ? "#315f82" : "#1b2229"
                border.width: index === editorProject.activeCharacterPattern ? 2 : 1
                border.color: index === editorProject.activeCharacterPattern
                              ? "#8fd0ff" : "#53606d"
                radius: 2

                Text {
                    anchors.centerIn: parent
                    text: parent.index.toString(16).toUpperCase().padStart(2, "0")
                    color: "#d8dde3"
                    font.pixelSize: Math.max(13, Math.min(17, parent.width * 0.28 + 5))
                }
                TapHandler {
                    onTapped: editorProject.activeCharacterPattern = parent.index
                }
            }
        }
    }
}
