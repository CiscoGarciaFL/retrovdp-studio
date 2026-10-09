pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "spriteSetView"
    required property int drawingTool
    required property real placementScale
    readonly property var placements: editorProject.activeSpritePlacements
    readonly property var editorSlots: editorProject.spriteEditorSlots
    readonly property var visibleEditorSlots: {
        if (editorProject.activeTargetInfo.spritePerItemSize)
            return editorSlots
        const visible = []
        for (let index = 0; index < editorSlots.length; ++index) {
            if (editorSlots[index].size === editorProject.spriteGlobalSize)
                visible.push(editorSlots[index])
        }
        return visible
    }
    readonly property int editorSlotCount: visibleEditorSlots.length
    readonly property bool entryListMode:
        editorProject.activeTargetInfo.id === "sega-genesis-vdp"
        || editorProject.activeTargetInfo.id === "huc6270"
        || editorProject.activeTargetInfo.id === "vic-ii"
    property bool sprite8Expanded:
        editorProject.activeTargetInfo.spritePerItemSize
        || editorProject.spriteGlobalSize === 8
    property bool sprite16Expanded:
        editorProject.activeTargetInfo.spritePerItemSize
        || editorProject.spriteGlobalSize === 16
    property int observedEditScope: editorProject.editScope
    property int observedGlobalSize: editorProject.spriteGlobalSize
    readonly property int square8Size: 8
    readonly property int square16Size: 16

    function selectGenesisSprite(index) {
        editorProject.activeSprite = index
    }

    function bankExpanded(size) {
        return editorProject.spritePatternWidth(size) === 8
                && editorProject.spritePatternHeight(size) === 8
               ? sprite8Expanded : sprite16Expanded
    }

    function setBankExpanded(size, expanded) {
        if (editorProject.spritePatternWidth(size) === 8
                && editorProject.spritePatternHeight(size) === 8)
            sprite8Expanded = expanded
        else
            sprite16Expanded = expanded
    }

    function activateSpriteBank(size) {
        const wasExpanded = bankExpanded(size)
        if (!editorProject.activeTargetInfo.spritePerItemSize) {
            if (editorProject.spriteGlobalSize !== size) {
                editorProject.spriteGlobalSize = size
                sprite8Expanded = size === root.square8Size
                sprite16Expanded = size === root.square16Size
            } else {
                setBankExpanded(size, !wasExpanded)
            }
            return
        }
        editorProject.activeSpriteSize = size
        setBankExpanded(size, !wasExpanded)
    }

    Connections {
        target: editorProject
        function onProjectChanged() {
            const scope = editorProject.activeTargetInfo.spritePerItemSize ? 1 : 0
            const globalSize = editorProject.spriteGlobalSize
            if (scope !== root.observedEditScope) {
                if (scope === 1) {
                    root.sprite8Expanded = true
                    root.sprite16Expanded = true
                } else {
                    root.sprite8Expanded = globalSize === 8
                    root.sprite16Expanded = globalSize === 16
                }
            } else if (scope === 0 && globalSize !== root.observedGlobalSize) {
                root.sprite8Expanded = globalSize === 8
                root.sprite16Expanded = globalSize === 16
            }
            root.observedEditScope = scope
            root.observedGlobalSize = globalSize
        }
    }

    function paletteColor(index) {
        const colors = editorProject.spritePaletteColors
        if (index < 0 || index >= colors.length)
            return "#000000"
        return index === 0 ? "#20272e" : colors[index]
    }

    function genesisPaletteColor(spriteIndex, colorIndex) {
        if (colorIndex === 0)
            return "transparent"
        const placement = root.placements[spriteIndex]
        const paletteBank = placement ? placement.palette : 0
        const paletteIndex = paletteBank * 16 + colorIndex
        const colors = imageInput.paletteColors
        if (paletteIndex >= 0 && paletteIndex < colors.length)
            return colors[paletteIndex]
        const fallback = editorProject.characterPaletteColors
        return colorIndex < fallback.length ? fallback[colorIndex] : "#000000"
    }

    component SpriteBank: Item {
        id: bank
        required property int spriteSize
        required property string title
        readonly property bool expanded: root.bankExpanded(spriteSize)
        readonly property bool activeForChipset:
            editorProject.activeTargetInfo.spritePerItemSize
            || editorProject.spriteGlobalSize === spriteSize
        readonly property int pixelWidth:
            editorProject.spritePatternWidth(spriteSize)
        readonly property int pixelHeight:
            editorProject.spritePatternHeight(spriteSize)

        Rectangle {
            id: bankHeader
            objectName: bank.pixelWidth === 8 && bank.pixelHeight === 8
                        ? "sprite8PatternHeader" : "sprite16PatternHeader"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 28
            color: bank.activeForChipset ? "#315f82" : "#1b2229"
            border.width: 1
            border.color: bank.activeForChipset ? "#8fd0ff" : "#53606d"
            radius: 3
            Accessible.role: Accessible.Button
            Accessible.name: bank.title
            Accessible.description: bank.expanded
                                    ? qsTr("Collapse sprite pattern bank")
                                    : qsTr("Expand and activate sprite pattern bank")

            function activate() {
                root.activateSpriteBank(bank.spriteSize)
            }

            Label {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: bank.title
                font.weight: Font.DemiBold
                color: bank.activeForChipset ? "white" : palette.placeholderText
            }

            Label {
                anchors.right: parent.right
                anchors.rightMargin: 9
                anchors.verticalCenter: parent.verticalCenter
                text: bank.expanded ? "▾" : "▸"
                color: bank.activeForChipset ? "white" : palette.placeholderText
                font.pixelSize: 16
            }

            TapHandler {
                onTapped: bankHeader.activate()
            }
        }

        GridView {
            id: spriteGrid
            objectName: bank.pixelWidth === 8 && bank.pixelHeight === 8
                        ? "sprite8PatternGrid" : "sprite16PatternGrid"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: bankHeader.bottom
            anchors.topMargin: 3
            anchors.bottom: parent.bottom
            visible: bank.expanded
            enabled: visible
            model: editorProject.spritePatternsPerSet
            clip: true
            cellWidth: bank.pixelWidth === 8 && bank.pixelHeight === 8
                       ? Math.max(24, Math.floor(width / 16))
                       : Math.max(48, Math.floor(width / 8))
            cellHeight: cellWidth
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: spriteCell
                required property int index
                readonly property bool active:
                    index === editorProject.activeSprite
                    && bank.spriteSize === editorProject.activeSpriteSize
                readonly property var pixels: {
                    const revision = editorProject.spriteRevision
                    return editorProject.spritePatternPixels(
                        editorProject.activeSpriteSet, index, bank.spriteSize)
                }
                width: spriteGrid.cellWidth - 3
                height: spriteGrid.cellHeight - 3
                color: active ? "#315f82" : "#1b2229"
                border.width: active ? 2 : 1
                border.color: active ? "#8fd0ff" : "#53606d"
                radius: 2

                Item {
                    id: thumbnail
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 8,
                                    (parent.height - 8) * bank.pixelWidth
                                    / bank.pixelHeight)
                    height: width * bank.pixelHeight / bank.pixelWidth

                    Repeater {
                        model: bank.pixelWidth * bank.pixelHeight
                        Rectangle {
                            required property int index
                            readonly property int value:
                                spriteCell.pixels.length > index
                                ? spriteCell.pixels[index] : 0
                            readonly property int colorIndex: {
                                if (value === 0)
                                    return 0
                                if (editorProject.editScope === 0
                                        && editorProject.activeTargetInfo.spriteMaximumColorDepth <= 1) {
                                    const placement = root.placements[spriteCell.index]
                                    return placement ? placement.color : 15
                                }
                                return value
                            }
                            x: (index % bank.pixelWidth)
                               * thumbnail.width / bank.pixelWidth
                            y: Math.floor(index / bank.pixelWidth)
                               * thumbnail.height / bank.pixelHeight
                            width: Math.ceil(thumbnail.width / bank.pixelWidth)
                            height: Math.ceil(thumbnail.height / bank.pixelHeight)
                            color: root.paletteColor(colorIndex)
                        }
                    }
                }

                Label {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 2
                    padding: 1
                    text: spriteCell.index.toString(16).toUpperCase()
                                     .padStart(2, "0")
                    color: "white"
                    font.pixelSize: 10
                    background: Rectangle { color: "#99000000"; radius: 1 }
                }

                TapHandler {
                    onTapped: editorProject.selectSpritePattern(
                                  spriteCell.index, bank.spriteSize)
                }
            }
        }
    }

    component GenesisSpriteList: Item {
        id: genesisList

        Rectangle {
            id: genesisHeader
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 30
            color: "#315f82"
            border.width: 1
            border.color: "#8fd0ff"
            radius: 3

            Label {
                id: genesisListTitle
                objectName: "genesisSpriteListTitle"
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("%1 sprite entries · %2")
                      .arg(editorProject.activeTargetInfo.name)
                      .arg(editorProject.spritePatternsPerSet)
                color: "white"
                font.weight: Font.DemiBold
            }

            Label {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Each entry keeps its own size")
                color: "#d9efff"
                font.pixelSize: 10
            }
        }

        GridView {
            id: genesisGrid
            objectName: "genesisSpriteEntryGrid"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: genesisHeader.bottom
            anchors.topMargin: 3
            anchors.bottom: parent.bottom
            model: editorProject.spritePatternsPerSet
            clip: true
            cellWidth: Math.max(76, Math.floor(width / 10))
            cellHeight: 86
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: genesisCell
                required property int index
                readonly property var placement:
                    root.placements.length > index ? root.placements[index] : null
                readonly property int spriteSize: placement ? placement.size : 808
                readonly property int pixelWidth:
                    editorProject.spritePatternWidth(spriteSize)
                readonly property int pixelHeight:
                    editorProject.spritePatternHeight(spriteSize)
                readonly property bool active:
                    index === editorProject.activeSprite
                readonly property var pixels: {
                    const revision = editorProject.spriteRevision
                    return editorProject.spritePatternPixels(
                        editorProject.activeSpriteSet, index, spriteSize)
                }
                width: genesisGrid.cellWidth - 3
                height: genesisGrid.cellHeight - 3
                color: active ? "#315f82" : "#1b2229"
                border.width: active ? 2 : 1
                border.color: active ? "#8fd0ff" : "#53606d"
                radius: 3
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Sprite %1, %2 by %3 pixels")
                                 .arg(index).arg(pixelWidth).arg(pixelHeight)

                Canvas {
                    id: genesisThumbnail
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: sizeLabel.top
                    anchors.margins: 5
                    antialiasing: false
                    property int observedRevision: editorProject.spriteRevision
                    property int observedPalette:
                        genesisCell.placement ? genesisCell.placement.palette : 0
                    property int observedSize: genesisCell.spriteSize
                    onObservedRevisionChanged: requestPaint()
                    onObservedPaletteChanged: requestPaint()
                    onObservedSizeChanged: requestPaint()
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()

                    onPaint: {
                        const context = getContext("2d")
                        context.clearRect(0, 0, width, height)
                        const scale = Math.min(width / genesisCell.pixelWidth,
                                               height / genesisCell.pixelHeight)
                        const drawWidth = genesisCell.pixelWidth * scale
                        const drawHeight = genesisCell.pixelHeight * scale
                        const offsetX = (width - drawWidth) / 2
                        const offsetY = (height - drawHeight) / 2
                        for (let pixel = 0;
                             pixel < genesisCell.pixelWidth
                                 * genesisCell.pixelHeight; ++pixel) {
                            const value = genesisCell.pixels.length > pixel
                                          ? genesisCell.pixels[pixel] : 0
                            if (value === 0)
                                continue
                            context.fillStyle = root.genesisPaletteColor(
                                genesisCell.index, value)
                            const column = pixel % genesisCell.pixelWidth
                            const row = Math.floor(pixel / genesisCell.pixelWidth)
                            context.fillRect(offsetX + column * scale,
                                             offsetY + row * scale,
                                             Math.ceil(scale), Math.ceil(scale))
                        }
                    }
                }

                Label {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 3
                    padding: 1
                    text: genesisCell.index.toString(16).toUpperCase()
                                             .padStart(2, "0")
                    color: "white"
                    font.pixelSize: 10
                    background: Rectangle { color: "#99000000"; radius: 1 }
                }

                Label {
                    id: sizeLabel
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 19
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("%1×%2")
                          .arg(genesisCell.pixelWidth).arg(genesisCell.pixelHeight)
                    color: genesisCell.active ? "white" : palette.placeholderText
                    font.pixelSize: 10
                }

                TapHandler {
                    onTapped: root.selectGenesisSprite(genesisCell.index)
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        Rectangle {
            id: editorTray
            objectName: "spritePatternEditorTray"
            Layout.fillWidth: true
            readonly property int editorCount: root.visibleEditorSlots.length
            readonly property int totalEditorCount: root.editorSlots.length
            property int renderedEditorCount: 0
            readonly property bool activeEditorEmpty:
                editorCount > 0
                && !root.editorSlots[editorProject.activeSpriteEditor].loaded
            readonly property real naturalHeight:
                Math.max(188, editorFlow.implicitHeight) + 40
            Layout.preferredHeight: editorProject.spritePlacementMode
                                    ? 192 * root.placementScale + 42
                                    : Math.min(naturalHeight,
                                               Math.max(224, root.height * 0.68))
            Layout.minimumHeight: editorProject.spritePlacementMode ? 234 : 228
            color: "#0f151a"
            border.width: 1
            border.color: "#46515d"
            radius: 4

            Flickable {
                id: editorFlick
                objectName: "spritePatternEditorGrid"
                visible: !editorProject.spritePlacementMode
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
                contentHeight: editorFlow.implicitHeight
                ScrollBar.vertical: ScrollBar {
                    policy: editorFlick.contentHeight > editorFlick.height
                            ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                }

                Flow {
                    id: editorFlow
                    width: editorFlick.width
                    spacing: 4

                    Repeater {
                        id: editorRepeater
                        objectName: "spritePatternEditorRepeater"
                        model: root.editorSlotCount
                        onItemAdded: (index, item) => {
                            editorTray.renderedEditorCount += 1
                        }
                        onItemRemoved: (index, item) => {
                            editorTray.renderedEditorCount -= 1
                        }

                        delegate: Item {
                            required property int index
                            readonly property var slotData:
                                root.visibleEditorSlots[index]
                            width: spriteEditor.implicitWidth
                            height: spriteEditor.implicitHeight

                            SpritePatternEditor {
                                id: spriteEditor
                                objectName: "spritePatternEditor"
                                editorIndex: parent.slotData.index
                                editorCount: editorTray.editorCount
                                loaded: parent.slotData.loaded
                                setIndex: parent.slotData.setIndex
                                spriteIndex: parent.slotData.spriteIndex
                                spriteSize: parent.slotData.size
                                spriteColorIndex: parent.slotData.color
                                drawingTool: root.drawingTool
                                active: parent.slotData.index
                                        === editorProject.activeSpriteEditor
                                onSelected:
                                    editorProject.activeSpriteEditor =
                                        parent.slotData.index
                                onMoveRequested: targetIndex => {
                                    const target = root.visibleEditorSlots[targetIndex]
                                    if (target) {
                                        editorProject.moveSpriteEditor(
                                            parent.slotData.index, target.index)
                                    }
                                }
                                onRemoveRequested: {
                                    editorProject.activeSpriteEditor =
                                        parent.slotData.index
                                    editorProject.removeActiveSpriteEditor()
                                }
                            }
                        }
                    }
                }
            }

            SpritePlacementView {
                objectName: "spritePlacementWorkspace"
                anchors.fill: parent
                anchors.margins: 4
                visible: editorProject.spritePlacementMode
                enabled: visible
                zoomScale: root.placementScale
            }

            Row {
                id: editorTrayControls
                objectName: "spritePatternEditorControls"
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 5
                anchors.bottomMargin: 4
                spacing: 2

                ToolButton {
                    objectName: "removeSpriteEditorButton"
                    implicitWidth: 26
                    implicitHeight: 26
                    text: "−"
                    enabled: editorProject.activeTargetInfo.spritePerItemSize
                             ? editorTray.totalEditorCount > 1
                             : editorTray.editorCount > 1
                    Accessible.name: editorProject.spritePlacementMode
                                     ? qsTr("Remove active placed sprite")
                                     : qsTr("Remove active sprite editor")
                    ToolTip.visible: hovered
                    ToolTip.text: Accessible.name
                    onClicked: editorProject.removeActiveSpriteEditor()
                }
                ToolButton {
                    objectName: "addSpriteEditorButton"
                    implicitWidth: 26
                    implicitHeight: 26
                    text: "+"
                    enabled: editorTray.totalEditorCount
                             < editorProject.spritePatternsPerSet
                    Accessible.name: editorProject.spritePlacementMode
                                     ? qsTr("Add empty placed sprite")
                                     : qsTr("Add empty sprite editor")
                    ToolTip.visible: hovered
                    ToolTip.text: Accessible.name
                    onClicked: editorProject.addSpriteEditor()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Label {
                Layout.fillWidth: true
                text: editorProject.activeTargetInfo.spritePerItemSize
                      ? qsTr("%1: each sprite selects its own pattern size")
                            .arg(editorProject.activeTargetInfo.name)
                      : qsTr("%1: the global size selects the active pattern bank")
                            .arg(editorProject.activeTargetInfo.name)
                color: palette.placeholderText
                font.pixelSize: 11
            }
            Label { text: qsTr("Set") }
            ComboBox {
                objectName: "spriteSetComboBox"
                Layout.preferredWidth: 76
                model: editorProject.spriteSetNames
                currentIndex: editorProject.activeSpriteSet
                onActivated: index => editorProject.activeSpriteSet = index
            }
            Label { text: qsTr("Sprite") }
            SpinBox {
                objectName: "spriteItemSpinBox"
                implicitWidth: 60
                from: 0
                to: editorProject.spritePatternsPerSet - 1
                value: editorProject.activeSprite
                editable: true
                onValueModified: editorProject.activeSprite = value
            }
        }

        SplitView {
            visible: !root.entryListMode
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical

            SpriteBank {
                objectName: "sprite8PatternBank"
                SplitView.preferredHeight: expanded ? parent.height / 2 : 28
                SplitView.minimumHeight: expanded
                                         ? 31 + (2 * Math.max(
                                             24, Math.floor(width / 16)))
                                         : 28
                SplitView.maximumHeight: expanded ? 16777215 : 28
                spriteSize: root.square8Size
                title: qsTr("8×8 sprite patterns · %1")
                       .arg(editorProject.spritePatternsPerSet)
            }
            SpriteBank {
                objectName: "sprite16PatternBank"
                SplitView.preferredHeight: expanded ? parent.height / 2 : 28
                SplitView.minimumHeight: expanded ? 80 : 28
                SplitView.maximumHeight: expanded ? 16777215 : 28
                spriteSize: root.square16Size
                title: qsTr("%1×%2 sprite patterns · %3")
                       .arg(editorProject.spritePatternWidth(16))
                       .arg(editorProject.spritePatternHeight(16))
                       .arg(editorProject.spritePatternsPerSet)
            }
        }

        Loader {
            active: root.entryListMode
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: GenesisSpriteList {
                objectName: "genesisSpriteList"
            }
        }
    }
}
