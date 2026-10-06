import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: root

    required property string editorTitle
    required property string importDescription
    required property int editorKind // 0 = character, 1 = sprite
    property bool closable: false
    signal closeRequested()
    clip: true
    contentWidth: availableWidth

    CharacterExtractionDialog {
        id: characterExtractionDialog
        objectName: root.editorKind === 0
                    ? "characterSidePanelExtractionDialog" : ""
    }

    CharacterPatternPreviewDialog {
        id: characterPatternPreviewDialog
        objectName: root.editorKind === 0
                    ? "characterSidePanelPatternPreviewDialog" : ""
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 12

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: root.editorTitle
                font.pixelSize: 20
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            ToolButton {
                objectName: root.closable ? "overlayHideButton" : "adjacentHideButton"
                visible: root.closable
                text: qsTr("›")
                font.pixelSize: 24
                activeFocusOnTab: true
                Accessible.name: qsTr("Hide Side Panel")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: root.closeRequested()
            }
        }

        ThemedGroupBox {
            objectName: root.objectName + "ActiveTargetGroup"
            title: qsTr("Active Target")
            Layout.fillWidth: true

            ActiveTargetSummary {
                anchors.fill: parent
                detailKind: root.editorKind + 1
            }
        }

        ThemedGroupBox {
            title: root.editorKind === 0
                   ? qsTr("Extract from Screen Image")
                   : qsTr("Import from Screen Image")
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                Label {
                    Layout.fillWidth: true
                    text: root.importDescription
                    wrapMode: Text.WordWrap
                    color: palette.placeholderText
                }
                Button {
                    objectName: root.objectName + "ImportFromSourceButton"
                    Layout.fillWidth: true
                    text: root.editorKind === 0
                          ? editorProject.activeTargetInfo.id === "sega-genesis-vdp"
                            ? qsTr("Extract into active layer…")
                            : qsTr("Extract into active pattern set…")
                          : qsTr("Populate sprite placement from Screen Image")
                    enabled: root.editorKind === 0 && imageInput.hasConversion
                    ToolTip.visible: hovered
                    ToolTip.text: root.editorKind === 0
                                  ? qsTr("Choose a character region, destination pattern, and wrapping order")
                                  : qsTr("Sprite extraction is not implemented yet")
                    onClicked: {
                        if (root.editorKind === 0)
                            characterExtractionDialog.openForScreenImage()
                    }
                }
                Button {
                    objectName: root.objectName + "PatternPreviewerButton"
                    visible: root.editorKind === 0
                    Layout.fillWidth: true
                    text: qsTr("Pattern Previewer…")
                    onClicked: characterPatternPreviewDialog.openPreview()
                }
            }
        }

        ThemedGroupBox {
            objectName: root.objectName + "SetOptionsGroup"
            title: root.editorKind === 0 ? qsTr("Pattern Sets") : qsTr("Sprite Sets")
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                Label {
                    Layout.fillWidth: true
                    text: root.editorKind === 0
                          ? qsTr("%1 sets with %2 pattern slots each. Active: Set %3, pattern %4.")
                                .arg(editorProject.characterSetCount)
                                .arg(editorProject.characterPatternsPerSet)
                                .arg(editorProject.activeCharacterSet + 1)
                                .arg(editorProject.activeCharacterPattern)
                          : qsTr("Each set contains %1 pattern slots and supports up to %2 placed sprites. Active: Set %3, sprite %4.")
                                .arg(editorProject.activeTargetInfo.spritePatternsPerSet)
                                .arg(editorProject.activeTargetInfo.spriteMaximumVisible)
                                .arg(editorProject.activeSpriteSet + 1)
                                .arg(editorProject.activeSprite)
                    wrapMode: Text.WordWrap
                }
                GridLayout {
                    visible: root.editorKind === 1
                             || editorProject.activeTargetInfo.characterPaletteBankCount > 1
                    columns: 2
                    Layout.fillWidth: true
                    Label {
                        visible: root.editorKind === 0
                                 && editorProject.activeTargetInfo.characterPaletteBankCount > 1
                        text: qsTr("Tile palette")
                    }
                    ComboBox {
                        objectName: root.objectName + "CharacterPaletteBankComboBox"
                        visible: root.editorKind === 0
                                 && editorProject.activeTargetInfo.characterPaletteBankCount > 1
                        model: editorProject.activeTargetInfo.characterPaletteBankCount
                        currentIndex: editorProject.characterPaletteBank
                        delegate: ItemDelegate {
                            required property int index
                            width: parent ? parent.width : implicitWidth
                            text: qsTr("Palette %1").arg(index)
                        }
                        contentItem: Label {
                            text: qsTr("Palette %1").arg(
                                      editorProject.characterPaletteBank)
                            verticalAlignment: Text.AlignVCenter
                        }
                        onActivated: index => editorProject.characterPaletteBank = index
                    }
                    Label {
                        visible: editorProject.activeTargetInfo.spriteUsesGlobalSize
                        text: qsTr("Global pattern size")
                    }
                    ComboBox {
                        objectName: root.objectName + "GlobalSpriteSizeComboBox"
                        visible: editorProject.activeTargetInfo.spriteUsesGlobalSize
                        model: editorProject.activeTargetInfo.spriteSizes
                        currentIndex: indexOfValue(editorProject.spriteGlobalSize)
                        delegate: ItemDelegate {
                            required property var modelData
                            width: parent ? parent.width : implicitWidth
                            text: qsTr("%1×%2")
                                  .arg(editorProject.spritePatternWidth(modelData))
                                  .arg(editorProject.spritePatternHeight(modelData))
                        }
                        contentItem: Label {
                            text: qsTr("%1×%2")
                                  .arg(editorProject.spritePatternWidth(
                                           editorProject.spriteGlobalSize))
                                  .arg(editorProject.spritePatternHeight(
                                           editorProject.spriteGlobalSize))
                            verticalAlignment: Text.AlignVCenter
                        }
                        onActivated: editorProject.spriteGlobalSize = currentValue
                    }
                    Label {
                        visible: editorProject.activeTargetInfo.spritePerItemSize
                        text: qsTr("Active sprite size")
                    }
                    ComboBox {
                        objectName: root.objectName + "ActiveSpriteSizeComboBox"
                        visible: editorProject.activeTargetInfo.spritePerItemSize
                        model: editorProject.activeTargetInfo.spriteSizes
                        currentIndex: indexOfValue(editorProject.activeSpriteSize)
                        delegate: ItemDelegate {
                            required property var modelData
                            width: parent ? parent.width : implicitWidth
                            text: qsTr("%1×%2")
                                  .arg(editorProject.spritePatternWidth(modelData))
                                  .arg(editorProject.spritePatternHeight(modelData))
                        }
                        contentItem: Label {
                            text: qsTr("%1×%2")
                                  .arg(editorProject.spritePatternWidth(
                                           editorProject.activeSpriteSize))
                                  .arg(editorProject.spritePatternHeight(
                                           editorProject.activeSpriteSize))
                            verticalAlignment: Text.AlignVCenter
                        }
                        onActivated: editorProject.activeSpriteSize = currentValue
                    }
                    Label {
                        visible: editorProject.activeTargetInfo.spriteMaximumColorDepth > 1
                        enabled: editorProject.activeTargetInfo.id !== "sega-sms-vdp"
                                 && editorProject.activeTargetInfo.id !== "sega-genesis-vdp"
                        text: qsTr("Active sprite color depth")
                    }
                    ComboBox {
                        objectName: root.objectName + "SpriteColorDepthComboBox"
                        visible: editorProject.activeTargetInfo.spriteMaximumColorDepth > 1
                        enabled: editorProject.activeTargetInfo.id !== "sega-sms-vdp"
                                 && editorProject.activeTargetInfo.id !== "sega-genesis-vdp"
                        model: editorProject.activeTargetInfo.spriteMaximumColorDepth
                        delegate: ItemDelegate {
                            required property int index
                            width: parent ? parent.width : implicitWidth
                            text: qsTr("%1 bpp · %2 indexes")
                                    .arg(index + 1).arg(1 << (index + 1))
                        }
                        contentItem: Label {
                            text: qsTr("%1 bpp · %2 indexes")
                                    .arg(editorProject.activeSpriteColorDepth)
                                    .arg(1 << editorProject.activeSpriteColorDepth)
                            verticalAlignment: Text.AlignVCenter
                        }
                        currentIndex: editorProject.activeSpriteColorDepth - 1
                        onActivated: index =>
                            editorProject.activeSpriteColorDepth = index + 1
                    }
                    Label {
                        visible: editorProject.activeTargetInfo.spritePaletteBankCount > 1
                        text: qsTr("Active sprite palette")
                    }
                    ComboBox {
                        objectName: root.objectName + "SpritePaletteBankComboBox"
                        visible: editorProject.activeTargetInfo.spritePaletteBankCount > 1
                        model: editorProject.activeTargetInfo.spritePaletteBankCount
                        currentIndex: editorProject.activeSpritePaletteBank
                        delegate: ItemDelegate {
                            required property int index
                            width: parent ? parent.width : implicitWidth
                            text: qsTr("Palette %1").arg(index)
                        }
                        contentItem: Label {
                            text: qsTr("Palette %1").arg(
                                      editorProject.activeSpritePaletteBank)
                            verticalAlignment: Text.AlignVCenter
                        }
                        onActivated: index => editorProject.activeSpritePaletteBank = index
                    }
                    Label {
                        visible: root.editorKind === 1
                                 && editorProject.activeTargetInfo.id === "sega-genesis-vdp"
                        text: qsTr("Display priority")
                    }
                    CheckBox {
                        objectName: root.objectName + "SpritePriorityCheckBox"
                        visible: root.editorKind === 1
                                 && editorProject.activeTargetInfo.id === "sega-genesis-vdp"
                        text: qsTr("High")
                        checked: editorProject.activeSpritePriority
                        onToggled: editorProject.activeSpritePriority = checked
                    }
                    Label {
                        visible: root.editorKind === 1
                        text: qsTr("Placement width")
                    }
                    SpinBox {
                        objectName: root.objectName + "PlacementWidthSpinBox"
                        visible: root.editorKind === 1
                        from: 8
                        to: 1024
                        value: editorProject.placementWidth
                        editable: true
                        onValueModified: editorProject.placementWidth = value
                    }
                    Label {
                        visible: root.editorKind === 1
                        text: qsTr("Placement height")
                    }
                    SpinBox {
                        objectName: root.objectName + "PlacementHeightSpinBox"
                        visible: root.editorKind === 1
                        from: 8
                        to: 1024
                        value: editorProject.placementHeight
                        editable: true
                        onValueModified: editorProject.placementHeight = value
                    }
                }
                Label {
                    visible: root.editorKind === 1
                    Layout.fillWidth: true
                    text: editorProject.activeTargetInfo.id === "sega-genesis-vdp"
                          ? qsTr("Mode V gives every sprite its own 8, 16, 24, or 32-pixel width and height, a 4bpp palette, and transparent color index 0. Choose the entry first, then set its size before drawing.")
                          : editorProject.activeTargetInfo.spritePerItemSize
                          ? qsTr("This target stores size and color depth per sprite. Target-specific edits are kept separate from compatible base data.")
                          : editorProject.activeTargetInfo.id === "sega-sms-vdp"
                            ? qsTr("Mode 4 uses one global 8×8 or 8×16 size, 4bpp pixels, the dedicated 16-color sprite palette, 64 sprite entries, and an 8-sprites-per-scanline limit.")
                          : qsTr("This target applies one global sprite size. Each sprite uses one opaque hardware color plus transparency.")
                    wrapMode: Text.WordWrap
                    color: palette.placeholderText
                }
            }
        }

        ThemedGroupBox {
            objectName: root.objectName + "GenesisMapOptionsGroup"
            visible: root.editorKind === 0
                     && editorProject.activeTargetInfo.id === "sega-genesis-vdp"
            title: qsTr("Genesis Map Composition")
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent

                GridLayout {
                    columns: 2
                    Layout.fillWidth: true

                    Label { text: qsTr("Authoring layer") }
                    ComboBox {
                        objectName: root.objectName + "GenesisPlaneComboBox"
                        model: [qsTr("Plane A"), qsTr("Plane B"), qsTr("Window")]
                        currentIndex: editorProject.activeCharacterPlane
                        onActivated: index => editorProject.activeCharacterPlane = index
                    }
                    Label { text: qsTr("Tile palette") }
                    ComboBox {
                        objectName: root.objectName + "GenesisTilePaletteComboBox"
                        model: 4
                        currentIndex: editorProject.activeCharacterTilePalette
                        delegate: ItemDelegate {
                            required property int index
                            width: parent ? parent.width : implicitWidth
                            text: qsTr("Palette %1").arg(index)
                        }
                        contentItem: Label {
                            text: qsTr("Palette %1").arg(
                                      editorProject.activeCharacterTilePalette)
                            verticalAlignment: Text.AlignVCenter
                        }
                        onActivated: index =>
                            editorProject.activeCharacterTilePalette = index
                    }
                    Label { text: qsTr("Tile attributes") }
                    RowLayout {
                        CheckBox {
                            objectName: root.objectName + "GenesisTileFlipXCheckBox"
                            text: qsTr("H flip")
                            checked: editorProject.activeCharacterTileFlipX
                            onToggled: editorProject.activeCharacterTileFlipX = checked
                        }
                        CheckBox {
                            objectName: root.objectName + "GenesisTileFlipYCheckBox"
                            text: qsTr("V flip")
                            checked: editorProject.activeCharacterTileFlipY
                            onToggled: editorProject.activeCharacterTileFlipY = checked
                        }
                    }
                    Label { text: qsTr("Display priority") }
                    CheckBox {
                        objectName: root.objectName + "GenesisTilePriorityCheckBox"
                        text: qsTr("High")
                        checked: editorProject.activeCharacterTilePriority
                        onToggled: editorProject.activeCharacterTilePriority = checked
                    }
                }

                CheckBox {
                    objectName: root.objectName + "GenesisCompositePreviewCheckBox"
                    text: qsTr("Composite planes and sprites by VDP priority")
                    checked: editorProject.genesisCompositePreview
                    onToggled: editorProject.genesisCompositePreview = checked
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Tiling mode edits the selected layer. Composite preview uses the Genesis low/high order for Plane B, Plane A, sprites, and Window. Color index 0 remains transparent for sprites.")
                    wrapMode: Text.WordWrap
                    color: palette.placeholderText
                }
            }
        }
    }
}
