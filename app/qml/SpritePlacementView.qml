pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Item {
    id: root
    required property real zoomScale
    readonly property var slots: editorProject.spriteEditorSlots
    readonly property int slotCount: slots.length
    readonly property int renderedSpriteCount: spriteRepeater.count
    readonly property bool pixelGridVisible: zoomScale >= 5.0

    function paletteColor(index) {
        const colors = editorProject.spritePaletteColors
        if (index < 0 || index >= colors.length)
            return "#000000"
        return index === 0 ? "transparent" : colors[index]
    }

    Flickable {
        id: placementFlick
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: Math.max(width, placementBox.width + 8)
        contentHeight: Math.max(height, placementBox.height + 8)
        ScrollBar.horizontal: ScrollBar {}
        ScrollBar.vertical: ScrollBar {}

        Rectangle {
            id: placementBox
            objectName: "spritePlacementBox"
            x: 4
            y: 4
            width: editorProject.placementWidth * root.zoomScale
            height: editorProject.placementHeight * root.zoomScale
            color: "#111820"
            border.width: 1
            border.color: "#73808c"
            clip: true

            Image {
                anchors.fill: parent
                source: imageInput.convertedPreview
                fillMode: Image.Stretch
                opacity: 0.38
                visible: imageInput.hasConversion
            }

            Repeater {
                model: Math.floor(editorProject.placementWidth / 8) + 1
                Rectangle {
                    required property int index
                    x: Math.min(placementBox.width - 1,
                                index * 8 * root.zoomScale)
                    width: 1
                    height: placementBox.height
                    color: index % 4 === 0 ? "#394854" : "#28343e"
                }
            }
            Repeater {
                model: Math.floor(editorProject.placementHeight / 8) + 1
                Rectangle {
                    required property int index
                    y: Math.min(placementBox.height - 1,
                                index * 8 * root.zoomScale)
                    width: placementBox.width
                    height: 1
                    color: index % 4 === 0 ? "#394854" : "#28343e"
                }
            }

            Repeater {
                objectName: "spritePixelGridVerticalLines"
                model: editorProject.placementWidth + 1
                Rectangle {
                    required property int index
                    visible: root.pixelGridVisible && index > 0
                             && index < editorProject.placementWidth
                             && index % 8 !== 0
                    x: index * root.zoomScale
                    width: 1
                    height: placementBox.height
                    color: "#26333d"
                }
            }
            Repeater {
                objectName: "spritePixelGridHorizontalLines"
                model: editorProject.placementHeight + 1
                Rectangle {
                    required property int index
                    visible: root.pixelGridVisible && index > 0
                             && index < editorProject.placementHeight
                             && index % 8 !== 0
                    y: index * root.zoomScale
                    width: placementBox.width
                    height: 1
                    color: "#26333d"
                }
            }

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
                id: spriteRepeater
                objectName: "selectedSpritePlacementRepeater"
                model: root.slotCount

                delegate: Item {
                    id: spriteMarker
                    required property int index
                    readonly property var slotData: root.slots[index]
                    readonly property int spriteSize: slotData.size
                    readonly property int spriteWidth:
                        editorProject.spritePatternWidth(spriteSize)
                    readonly property int spriteHeight:
                        editorProject.spritePatternHeight(spriteSize)
                    readonly property var pixels: {
                        const revision = editorProject.spriteRevision
                        if (!spriteMarker.slotData.loaded)
                            return []
                        return editorProject.spritePatternPixels(
                            spriteMarker.slotData.setIndex,
                            spriteMarker.slotData.spriteIndex, spriteSize)
                    }
                    readonly property bool active:
                        spriteMarker.slotData.index
                        === editorProject.activeSpriteEditor
                    x: slotData.x * root.zoomScale
                    y: slotData.y * root.zoomScale
                    width: spriteWidth * root.zoomScale
                    height: spriteHeight * root.zoomScale
                    z: active ? 1000 : slotData.index + 10
                    visible: slotData.loaded && slotData.visible
                             && slotData.activeForPlacement

                    Repeater {
                        model: spriteMarker.spriteWidth * spriteMarker.spriteHeight
                        Rectangle {
                            required property int index
                            readonly property int value:
                                spriteMarker.pixels.length > index
                                ? spriteMarker.pixels[index] : 0
                            x: (index % spriteMarker.spriteWidth) * root.zoomScale
                            y: Math.floor(index / spriteMarker.spriteWidth)
                               * root.zoomScale
                            width: root.zoomScale
                            height: root.zoomScale
                            color: value === 0 ? "transparent"
                                  : editorProject.editScope === 0
                                    && editorProject.activeTargetInfo.spriteMaximumColorDepth <= 1
                                    ? root.paletteColor(
                                          spriteMarker.slotData.color)
                                    : root.paletteColor(value)
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        border.width: spriteMarker.active ? 2 : 1
                        border.color: spriteMarker.active ? "white" : "#62c5ff"
                    }

                    Rectangle {
                        visible: spriteHover.hovered
                        z: 2000
                        y: spriteMarker.height + 2
                        width: spriteLabel.implicitWidth + 8
                        height: 18
                        color: "#202a33"
                        border.width: 1
                        border.color: spriteMarker.active ? "white" : "#73808c"
                        radius: 2
                        Label {
                            id: spriteLabel
                            anchors.centerIn: parent
                            text: qsTr("Sprite %1 · %2×%3 · %4,%5")
                                  .arg(spriteMarker.slotData.spriteIndex)
                                  .arg(spriteMarker.spriteWidth)
                                  .arg(spriteMarker.spriteHeight)
                                  .arg(spriteMarker.slotData.x)
                                  .arg(spriteMarker.slotData.y)
                            font.pixelSize: 10
                        }
                    }

                    HoverHandler {
                        id: spriteHover
                        cursorShape: spriteDrag.active
                                     ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    }
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped:
                            editorProject.activeSpriteEditor =
                                spriteMarker.slotData.index
                    }
                    DragHandler {
                        id: spriteDrag
                        target: null
                        acceptedButtons: Qt.LeftButton
                        property int startingX
                        property int startingY

                        onActiveChanged: {
                            if (active) {
                                startingX = spriteMarker.slotData.x
                                startingY = spriteMarker.slotData.y
                                editorProject.activeSpriteEditor =
                                    spriteMarker.slotData.index
                            }
                        }
                        onTranslationChanged: {
                            if (!active)
                                return
                            editorProject.moveSpriteEditorTile(
                                spriteMarker.slotData.index,
                                Math.round(startingX + translation.x / root.zoomScale),
                                Math.round(startingY + translation.y / root.zoomScale))
                        }
                    }
                }
            }
        }
    }
}
