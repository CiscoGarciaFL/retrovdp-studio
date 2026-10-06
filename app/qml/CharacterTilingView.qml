pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// A target-sized tile-map grid. Genesis projects can author and composite
// Plane A, Plane B, Window, and the active sprite set using VDP priority order.
Item {
    id: root
    required property real zoomScale

    readonly property var allSlots: editorProject.characterEditorSlots
    readonly property bool genesis:
        editorProject.activeTargetInfo.id === "sega-genesis-vdp"
    readonly property var slots: {
        if (!genesis || editorProject.genesisCompositePreview)
            return allSlots
        const selected = []
        for (let index = 0; index < allSlots.length; ++index) {
            if (allSlots[index].plane === editorProject.activeCharacterPlane)
                selected.push(allSlots[index])
        }
        return selected
    }
    readonly property int slotCount: slots.length
    readonly property int renderedTileCount: tileRepeater.count
    readonly property var activeSlot: {
        for (let index = 0; index < allSlots.length; ++index) {
            if (allSlots[index].index === editorProject.activeCharacterEditor)
                return allSlots[index]
        }
        return null
    }
    readonly property int relatedTileCount: {
        if (!activeSlot || !activeSlot.loaded)
            return 0
        let count = 0
        for (let index = 0; index < slots.length; ++index) {
            const slot = slots[index]
            if (slot.loaded && slot.setIndex === activeSlot.setIndex
                    && slot.patternIndex === activeSlot.patternIndex) {
                ++count
            }
        }
        return count
    }

    function paletteColor(index, bank) {
        const colors = genesis ? imageInput.paletteColors
                               : editorProject.characterPaletteColors
        const resolved = genesis ? bank * 16 + index : index
        if (resolved < 0 || resolved >= colors.length)
            return "#000000"
        if (index === 0
                && editorProject.activeTargetInfo.id !== "sega-sms-vdp")
            return "#303842"
        return colors[resolved]
    }

    function tilePriorityZ(slot) {
        if (!genesis || !editorProject.genesisCompositePreview)
            return slot.index + 10
        if (slot.plane === 1)
            return slot.priority ? 50 : 10
        if (slot.plane === 2)
            return slot.priority ? 80 : 40
        return slot.priority ? 60 : 20
    }

    Flickable {
        id: tilingFlick
        objectName: "characterTilingFlickable"
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: Math.max(width, screenCanvas.width + 8)
        contentHeight: Math.max(height, screenCanvas.height + 8)
        ScrollBar.horizontal: ScrollBar {}
        ScrollBar.vertical: ScrollBar {}

        Rectangle {
            id: screenCanvas
            objectName: "characterTilingScreenGrid"
            x: 4
            y: 4
            width: editorProject.characterMapColumns * 8 * root.zoomScale
            height: editorProject.characterMapRows * 8 * root.zoomScale
            color: "#111820"
            border.width: 1
            border.color: "#73808c"
            clip: true

            Repeater {
                model: editorProject.characterMapColumns + 1
                Rectangle {
                    required property int index
                    x: Math.min(screenCanvas.width - 1,
                                index * 8 * root.zoomScale)
                    width: 1
                    height: screenCanvas.height
                    color: index % 4 === 0 ? "#394854" : "#28343e"
                }
            }
            Repeater {
                model: editorProject.characterMapRows + 1
                Rectangle {
                    required property int index
                    y: Math.min(screenCanvas.height - 1,
                                index * 8 * root.zoomScale)
                    width: screenCanvas.width
                    height: 1
                    color: index % 4 === 0 ? "#394854" : "#28343e"
                }
            }

            // The emphasized corner is screen coordinate 0,0 (Home).
            Rectangle {
                width: 12
                height: 2
                color: palette.highlight
                z: 2000
            }
            Rectangle {
                width: 2
                height: 12
                color: palette.highlight
                z: 2000
            }

            Repeater {
                id: tileRepeater
                objectName: "characterTileRepeater"
                model: root.slotCount

                delegate: Item {
                    id: tile
                    objectName: "characterTile"
                    required property int index
                    readonly property var slotData: root.slots[index]
                    readonly property var patternRows: {
                        const revision = editorProject.characterRevision
                        if (!slotData.loaded)
                            return []
                        return editorProject.characterPatternRows(
                            slotData.setIndex, slotData.patternIndex)
                    }
                    readonly property bool active:
                        slotData.index === editorProject.activeCharacterEditor
                    readonly property bool relatedPattern:
                        !active && slotData.loaded && root.activeSlot
                        && root.activeSlot.loaded
                        && slotData.setIndex === root.activeSlot.setIndex
                        && slotData.patternIndex === root.activeSlot.patternIndex
                    x: slotData.tileX * root.zoomScale
                    y: slotData.tileY * root.zoomScale
                    width: 8 * root.zoomScale
                    height: 8 * root.zoomScale
                    z: root.genesis && editorProject.genesisCompositePreview
                       ? root.tilePriorityZ(slotData)
                       : active ? 1000 : relatedPattern ? 500
                                                  : root.tilePriorityZ(slotData)

                    Repeater {
                        model: 64
                        Rectangle {
                            required property int index
                            readonly property int displayRow: Math.floor(index / 8)
                            readonly property int displayColumn: index % 8
                            readonly property int patternRow:
                                root.genesis && tile.slotData.flipY
                                ? 7 - displayRow : displayRow
                            readonly property int patternColumn:
                                root.genesis && tile.slotData.flipX
                                ? 7 - displayColumn : displayColumn
                            readonly property int colorIndex:
                                !tile.slotData.loaded || tile.patternRows.length !== 8
                                ? 0
                                : tile.patternRows[patternRow].indexed
                                  ? tile.patternRows[patternRow].pixels[patternColumn]
                                  : ((tile.patternRows[patternRow].pattern
                                      & (0x80 >> patternColumn)) !== 0
                                     ? tile.patternRows[patternRow].foreground
                                     : tile.patternRows[patternRow].background)
                            x: displayColumn * root.zoomScale
                            y: displayRow * root.zoomScale
                            width: root.zoomScale
                            height: root.zoomScale
                            visible: !(root.genesis
                                       && editorProject.genesisCompositePreview
                                       && colorIndex === 0)
                            color: !tile.slotData.loaded
                                   || tile.patternRows.length !== 8 ? "#20272e"
                                   : root.paletteColor(colorIndex,
                                                       tile.slotData.palette || 0)
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        border.width: 1
                        border.color: tile.active ? "#ffffff"
                                      : tile.relatedPattern ? "#62c5ff"
                                      : tile.slotData.loaded ? "#68737f"
                                      : "#d6a64b"
                    }

                    Item {
                        anchors.fill: parent
                        visible: !tile.slotData.loaded
                        Rectangle {
                            anchors.centerIn: parent
                            width: tile.width
                            height: 1
                            rotation: 45
                            color: "#d6a64b"
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            width: tile.width
                            height: 1
                            rotation: -45
                            color: "#d6a64b"
                        }
                    }

                    Rectangle {
                        id: hoverLabel
                        visible: tileHover.hovered
                        z: 3000
                        width: hoverText.implicitWidth + 8
                        height: 18
                        x: Math.max(-tile.x,
                                    Math.min((tile.width - width) / 2,
                                             screenCanvas.width - tile.x - width))
                        y: tile.y >= height + 2 ? -height - 2 : tile.height + 2
                        color: "#202a33"
                        border.width: 1
                        border.color: tile.active ? "#ffffff" : "#73808c"
                        radius: 2

                        Label {
                            id: hoverText
                            anchors.centerIn: parent
                            text: tile.slotData.loaded
                                  ? qsTr("Pattern %1")
                                        .arg(Number(tile.slotData.patternIndex)
                                             .toString(16).toUpperCase()
                                             .padStart(2, "0"))
                                  : qsTr("Empty")
                            font.pixelSize: 10
                        }
                    }

                    HoverHandler {
                        id: tileHover
                        cursorShape: tileDrag.active ? Qt.ClosedHandCursor
                                                     : Qt.OpenHandCursor
                    }
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: editorProject.activeCharacterEditor = tile.slotData.index
                    }
                    DragHandler {
                        id: tileDrag
                        target: null
                        acceptedButtons: Qt.LeftButton
                        property int startingX
                        property int startingY

                        onActiveChanged: {
                            if (active) {
                                startingX = tile.slotData.tileX
                                startingY = tile.slotData.tileY
                                editorProject.activeCharacterEditor = tile.slotData.index
                            }
                        }
                        onTranslationChanged: {
                            if (!active)
                                return
                            editorProject.moveCharacterTile(
                                tile.slotData.index,
                                startingX + translation.x / root.zoomScale,
                                startingY + translation.y / root.zoomScale)
                        }
                    }
                }
            }

            Repeater {
                id: compositeSpriteRepeater
                model: root.genesis && editorProject.genesisCompositePreview
                       ? editorProject.activeSpritePlacements : []

                delegate: Item {
                    id: compositeSprite
                    required property int index
                    required property var modelData
                    readonly property var placement: modelData
                    readonly property int spriteWidth:
                        editorProject.spritePatternWidth(placement.size)
                    readonly property int spriteHeight:
                        editorProject.spritePatternHeight(placement.size)
                    readonly property var pixels: {
                        const revision = editorProject.spriteRevision
                        return editorProject.spritePatternPixels(
                            editorProject.activeSpriteSet,
                            placement.index, placement.size)
                    }
                    visible: placement.visible
                    x: placement.x * root.zoomScale
                    y: placement.y * root.zoomScale
                    width: spriteWidth * root.zoomScale
                    height: spriteHeight * root.zoomScale
                    z: placement.priority ? 70 : 30

                    Repeater {
                        model: compositeSprite.spriteWidth
                               * compositeSprite.spriteHeight
                        Rectangle {
                            required property int index
                            readonly property int displayRow:
                                Math.floor(index / compositeSprite.spriteWidth)
                            readonly property int displayColumn:
                                index % compositeSprite.spriteWidth
                            readonly property int sourceRow:
                                compositeSprite.placement.flipY
                                ? compositeSprite.spriteHeight - 1 - displayRow
                                : displayRow
                            readonly property int sourceColumn:
                                compositeSprite.placement.flipX
                                ? compositeSprite.spriteWidth - 1 - displayColumn
                                : displayColumn
                            readonly property int colorIndex:
                                compositeSprite.pixels[
                                    sourceRow * compositeSprite.spriteWidth
                                    + sourceColumn] || 0
                            visible: colorIndex !== 0
                            x: displayColumn * root.zoomScale
                            y: displayRow * root.zoomScale
                            width: root.zoomScale
                            height: root.zoomScale
                            color: root.paletteColor(
                                colorIndex, compositeSprite.placement.palette || 0)
                        }
                    }
                }
            }
        }
    }
}
