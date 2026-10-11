pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Character and sprite editors use the same workspace chrome as Screen Image.
// Their eventual canvases and tools can be added without allowing the three
// destination layouts to drift apart.
PreviewPane {
    id: root

    required property string description
    required property string importDescription
    required property int editorKind // 0 = character, 1 = sprite
    property int characterDrawingTool: 1 // 1 pencil, 2 eraser, 3 line, 4 K-Line, 5 Rays
    property int characterActiveColor: 0 // 0 = foreground, 1 = background
    property int spriteDrawingTool: 1 // 1 pencil, 2 eraser, 3 line, 4 K-Line, 5 Rays
    property int spriteActiveColor: 0 // 0 = foreground, 1 = transparent
    readonly property var activeCharacterSlot:
        editorProject.characterEditorSlots.length > 0
        ? editorProject.characterEditorSlots[editorProject.activeCharacterEditor]
        : null
    readonly property bool activeCharacterPatternLoaded:
        activeCharacterSlot !== null && activeCharacterSlot.loaded
    readonly property var activeSpriteSlot:
        editorProject.spriteEditorSlots.length > 0
        ? editorProject.spriteEditorSlots[editorProject.activeSpriteEditor]
        : null
    readonly property bool activeSpriteLoaded:
        activeSpriteSlot !== null && activeSpriteSlot.loaded
    readonly property bool activeEditorItemLoaded:
        editorKind === 0 ? activeCharacterPatternLoaded : activeSpriteLoaded
    readonly property bool activeEditorPan:
        editorKind === 0 ? editorProject.characterPanActive
                         : editorProject.spritePanActive
    readonly property int activeDrawingTool:
        editorKind === 0 ? characterDrawingTool : spriteDrawingTool
    readonly property int activeColorSide:
        editorKind === 0 ? characterActiveColor : spriteActiveColor

    function patternPaletteColor(index) {
        const colors = editorKind === 1 ? editorProject.spritePaletteColors
                                        : editorProject.characterPaletteColors
        if (index < 0 || index >= colors.length)
            return "#000000"
        if (index === 0 && (editorKind === 1
                || editorProject.activeTargetInfo.id !== "sega-sms-vdp"))
            return "#303842"
        return colors[index]
    }

    function hexNibble(value) {
        return Number(value).toString(16).toUpperCase()
    }

    function contrastColor(colorValue) {
        return colorValue.r * 0.299 + colorValue.g * 0.587
                + colorValue.b * 0.114 > 0.58 ? "#11161b" : "white"
    }

    function setActiveDrawingTool(tool) {
        if (editorKind === 0)
            characterDrawingTool = tool
        else
            spriteDrawingTool = tool
    }

    function setActiveColorSide(side) {
        if (editorKind === 0)
            characterActiveColor = side
        else
            spriteActiveColor = side
    }

    function chooseActivePaletteColor(index) {
        if (editorKind === 0) {
            if (characterActiveColor === 0)
                editorProject.characterForegroundColorIndex = index
            else
                editorProject.characterBackgroundColorIndex = index
        } else if (index > 0) {
            editorProject.spriteDrawingColorIndex = index
        }
    }

    CharacterExtractionDialog {
        id: characterExtractionDialog
        objectName: root.editorKind === 0 ? "characterExtractionDialog" : ""
    }

    CharacterPatternPreviewDialog {
        id: characterPatternPreviewDialog
        objectName: root.editorKind === 0 ? "characterPatternPreviewDialog" : ""
    }

    imageSource: ""
    details: editorKind === 0
             ? qsTr("Set %1 of %2 · Pattern %3 of %4")
                   .arg(editorProject.activeCharacterSet + 1)
                   .arg(editorProject.characterSetCount)
                   .arg(editorProject.activeCharacterPattern + 1)
                   .arg(editorProject.characterPatternsPerSet)
             : (editorProject.activeTargetInfo.objectProcessor
                ? qsTr("Object set %1 of %2 · Object %3 of %4")
                : qsTr("Sprite set %1 of %2 · Sprite %3 of %4"))
                   .arg(editorProject.activeSpriteSet + 1)
                   .arg(editorProject.spriteSetNames.length)
                   .arg(editorProject.activeSprite + 1)
                   .arg(editorProject.activeTargetInfo.spritePatternsPerSet)
    detailsTrailingText: qsTr("Editing for %1")
                             .arg(editorProject.activeTargetInfo.name)
    emptyText: root.description + "\n\n" + root.importDescription
    zoomInteractive: (root.editorKind === 0 && editorProject.characterTilingMode)
                     || (root.editorKind === 1 && editorProject.spritePlacementMode)
    zoomContentAvailable: zoomInteractive

    upperToolbarContent: Component {
        RowLayout {
            spacing: 2

            Popup {
                id: characterPalettePopup
                objectName: root.editorKind === 0
                            ? "characterPatternPalettePopup"
                            : "spritePatternPalettePopup"
                property string pickerTitle:
                    root.editorKind === 0
                    ? qsTr("%1 palette · choose %2")
                          .arg(editorProject.activeTargetInfo.name)
                          .arg(root.activeColorSide === 0
                               ? qsTr("foreground") : qsTr("background"))
                    : editorProject.activeTargetInfo.spriteMaximumColorDepth > 1
                      ? qsTr("%1 sprite pixel index · %2 bpp")
                            .arg(editorProject.activeTargetInfo.name)
                            .arg(editorProject.activeSpriteColorDepth)
                      : qsTr("%1 sprite color")
                            .arg(editorProject.activeTargetInfo.name)
                readonly property int maximumSelectableColorIndex:
                    root.editorKind === 1
                    && editorProject.activeTargetInfo.spriteMaximumColorDepth > 1
                    ? (1 << editorProject.activeSpriteColorDepth) - 1 : 15
                parent: root
                x: Math.max(0, Math.min(root.width - width,
                                       colorControl.mapToItem(root, 0, 0).x))
                y: colorControl.mapToItem(root, 0, 0).y + colorControl.height + 4
                width: 184
                height: root.editorKind === 1 ? 210 : 184
                padding: 8
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                background: Rectangle {
                    color: characterPalettePopup.palette.window
                    border.width: 1
                    border.color: characterPalettePopup.palette.windowText
                    radius: 3
                }

                contentItem: ColumnLayout {
                    spacing: 5

                    Label {
                        Layout.fillWidth: true
                        text: characterPalettePopup.pickerTitle
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }

                    Label {
                        visible: root.editorKind === 1
                        Layout.fillWidth: true
                        text: editorProject.activeTargetInfo.spriteMaximumColorDepth > 1
                              ? editorProject.activeTargetInfo.id === "sega-sms-vdp"
                                ? qsTr("Choose index 1–15 from the dedicated Mode 4 sprite palette. Index 0 is transparent.")
                                : qsTr("Choose index 1–%1. Increase color depth in Sprite Options for more indexes.")
                                      .arg((1 << editorProject.activeSpriteColorDepth) - 1)
                              : qsTr("Choose one opaque color for the active sprite. Index 0 is transparent.")
                        wrapMode: Text.WordWrap
                        color: palette.placeholderText
                        font.pixelSize: 10
                    }

                    GridLayout {
                        Layout.alignment: Qt.AlignHCenter
                        columns: 4
                        columnSpacing: 3
                        rowSpacing: 3

                        Repeater {
                            model: root.editorKind === 1
                                   ? editorProject.spritePaletteColors
                                   : editorProject.characterPaletteColors

                            Rectangle {
                                required property int index
                                required property color modelData
                                readonly property bool selectable:
                                    root.editorKind === 0
                                    || (index > 0
                                        && index <= characterPalettePopup.maximumSelectableColorIndex)
                                width: 34
                                height: 30
                                opacity: selectable ? 1.0 : 0.3
                                radius: 2
                                color: root.patternPaletteColor(index)
                                border.width: patternPaletteHover.hovered ? 2 : 1
                                border.color: patternPaletteHover.hovered
                                              ? palette.highlight : "#707780"

                                Text {
                                    anchors.centerIn: parent
                                    text: root.hexNibble(parent.index)
                                    color: root.contrastColor(parent.color)
                                    font.bold: true
                                    font.pixelSize: 12
                                }
                                Rectangle {
                                    visible: parent.index === 0
                                    anchors.centerIn: parent
                                    width: parent.width - 8
                                    height: 1
                                    rotation: -35
                                    color: "#d8dde3"
                                }
                                HoverHandler { id: patternPaletteHover }
                                TapHandler {
                                    enabled: parent.selectable
                                    onTapped: {
                                        root.chooseActivePaletteColor(parent.index)
                                        characterPalettePopup.close()
                                    }
                                }
                                ToolTip.visible: patternPaletteHover.hovered
                                ToolTip.text: selectable
                                              ? (root.editorKind === 0
                                                 ? qsTr("Color %1")
                                                       .arg(root.hexNibble(index))
                                                 : editorProject.activeTargetInfo.spriteMaximumColorDepth > 1
                                                   ? qsTr("Pixel index %1")
                                                         .arg(root.hexNibble(index))
                                                   : qsTr("Sprite color %1")
                                                         .arg(root.hexNibble(index)))
                                               : (index === 0
                                                  ? qsTr("Transparent pixels are drawn with Eraser")
                                                  : qsTr("Increase the active target color depth in Sprite Options to enable this pixel index"))
                            }
                        }
                    }
                }
            }

            ToolButton {
                objectName: root.objectName + "ImportSourceButton"
                implicitHeight: 26
                text: qsTr("↳")
                enabled: root.editorKind === 0 && imageInput.hasConversion
                Accessible.name: root.editorKind === 0
                                 ? qsTr("Extract patterns from Screen Image")
                                 : (editorProject.activeTargetInfo.objectProcessor
                                    ? qsTr("Import objects from Object Scene")
                                    : qsTr("Import sprites from Screen Image"))
                ToolTip.visible: hovered
                ToolTip.text: root.editorKind === 0
                              ? Accessible.name
                               : (editorProject.activeTargetInfo.objectProcessor
                                  ? qsTr("Edit objects directly, then export the native object list")
                                  : qsTr("Sprite extraction is not implemented yet"))
                onClicked: {
                    if (root.editorKind === 0)
                        characterExtractionDialog.openForScreenImage()
                }
            }
            ToolButton {
                objectName: root.editorKind === 0
                            ? "characterPatternPreviewerButton" : ""
                visible: root.editorKind === 0
                implicitWidth: visible ? 26 : 0
                implicitHeight: 26
                text: qsTr("▦")
                enabled: visible
                Accessible.name: qsTr("Open Pattern Previewer")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: characterPatternPreviewDialog.openPreview()
            }
            ToolSeparator { height: 26 }
            ToolButton {
                id: characterPencilButton
                objectName: root.editorKind === 0
                            ? "characterPatternPencilButton"
                            : "spritePatternPencilButton"
                implicitWidth: 26
                implicitHeight: 26
                enabled: root.activeEditorItemLoaded && !root.activeEditorPan
                         && !(root.editorKind === 1
                              && (editorProject.spritePlacementMode
                                  || !editorProject.canRotateSpritePattern))
                checkable: true
                checked: root.activeDrawingTool === 1
                Accessible.name: root.editorKind === 0
                                 ? qsTr("Draw pattern pixels")
                                 : qsTr("Draw sprite pixels")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: root.setActiveDrawingTool(1)

                contentItem: Canvas {
                    implicitWidth: 16
                    implicitHeight: 16
                    property color iconColor: characterPencilButton.palette.buttonText
                    onIconColorChanged: requestPaint()
                    onPaint: {
                        const context = getContext("2d")
                        context.clearRect(0, 0, width, height)
                        context.strokeStyle = iconColor
                        context.fillStyle = iconColor
                        context.lineCap = "square"
                        context.lineWidth = 3
                        context.beginPath()
                        context.moveTo(4, 12)
                        context.lineTo(12, 4)
                        context.stroke()
                        context.beginPath()
                        context.moveTo(2, 14)
                        context.lineTo(5, 13)
                        context.lineTo(3, 11)
                        context.closePath()
                        context.fill()
                        context.lineWidth = 1
                        context.strokeRect(11, 2, 3, 3)
                    }
                }
            }
            ToolButton {
                id: characterEraserButton
                objectName: root.editorKind === 0
                            ? "characterPatternEraserButton"
                            : "spritePatternEraserButton"
                implicitWidth: 26
                implicitHeight: 26
                enabled: root.activeEditorItemLoaded && !root.activeEditorPan
                         && !(root.editorKind === 1
                              && editorProject.spritePlacementMode)
                checkable: true
                checked: root.activeDrawingTool === 2
                Accessible.name: root.editorKind === 0
                                 ? qsTr("Erase pattern pixels")
                                 : qsTr("Erase sprite pixels")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: root.setActiveDrawingTool(2)

                contentItem: Canvas {
                    implicitWidth: 16
                    implicitHeight: 16
                    property color iconColor: characterEraserButton.palette.buttonText
                    onIconColorChanged: requestPaint()
                    onPaint: {
                        const context = getContext("2d")
                        context.clearRect(0, 0, width, height)
                        context.save()
                        context.translate(width / 2, height / 2)
                        context.rotate(-Math.PI / 4)
                        context.fillStyle = iconColor
                        context.fillRect(-5, -3, 10, 6)
                        context.strokeStyle = characterEraserButton.palette.button
                        context.lineWidth = 1
                        context.beginPath()
                        context.moveTo(1, -3)
                        context.lineTo(1, 3)
                        context.stroke()
                        context.restore()
                    }
                }
            }
            ToolButton {
                id: patternLineButton
                objectName: root.editorKind === 0
                            ? "characterPatternLineButton"
                            : "spritePatternLineButton"
                implicitWidth: 26
                implicitHeight: 26
                enabled: root.activeEditorItemLoaded && !root.activeEditorPan
                         && !(root.editorKind === 1
                              && editorProject.spritePlacementMode)
                checkable: true
                checked: root.activeDrawingTool === 3
                Accessible.name: root.editorKind === 0
                                 ? qsTr("Draw a pattern line")
                                 : qsTr("Draw a sprite line")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("%1; hold Shift for horizontal or vertical")
                                  .arg(Accessible.name)
                onClicked: root.setActiveDrawingTool(3)

                contentItem: Canvas {
                    implicitWidth: 16
                    implicitHeight: 16
                    property color iconColor: patternLineButton.palette.buttonText
                    onIconColorChanged: requestPaint()
                    onPaint: {
                        const context = getContext("2d")
                        context.clearRect(0, 0, width, height)
                        context.strokeStyle = iconColor
                        context.lineCap = "round"
                        context.lineWidth = 2
                        context.beginPath()
                        context.moveTo(3, 13)
                        context.lineTo(13, 3)
                        context.stroke()
                    }
                }
            }
            ToolButton {
                id: patternKLineButton
                objectName: root.editorKind === 0
                            ? "characterPatternKLineButton"
                            : "spritePatternKLineButton"
                implicitWidth: 26
                implicitHeight: 26
                enabled: root.activeEditorItemLoaded && !root.activeEditorPan
                         && !(root.editorKind === 1
                              && editorProject.spritePlacementMode)
                checkable: true
                checked: root.activeDrawingTool === 4
                Accessible.name: root.editorKind === 0
                                 ? qsTr("Draw connected pattern lines")
                                 : qsTr("Draw connected sprite lines")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("%1; hold Shift to constrain, Escape to finish")
                                  .arg(Accessible.name)
                onClicked: root.setActiveDrawingTool(4)

                contentItem: Canvas {
                    implicitWidth: 16
                    implicitHeight: 16
                    property color iconColor: patternKLineButton.palette.buttonText
                    onIconColorChanged: requestPaint()
                    onPaint: {
                        const context = getContext("2d")
                        context.clearRect(0, 0, width, height)
                        context.strokeStyle = iconColor
                        context.fillStyle = iconColor
                        context.lineJoin = "round"
                        context.lineCap = "round"
                        context.lineWidth = 2
                        context.beginPath()
                        context.moveTo(2, 12)
                        context.lineTo(7, 4)
                        context.lineTo(14, 10)
                        context.stroke()
                        context.beginPath()
                        context.arc(2, 12, 1.5, 0, Math.PI * 2)
                        context.arc(7, 4, 1.5, 0, Math.PI * 2)
                        context.arc(14, 10, 1.5, 0, Math.PI * 2)
                        context.fill()
                    }
                }
            }
            ToolButton {
                id: patternRaysButton
                objectName: root.editorKind === 0
                            ? "characterPatternRaysButton"
                            : "spritePatternRaysButton"
                implicitWidth: 26
                implicitHeight: 26
                enabled: root.activeEditorItemLoaded && !root.activeEditorPan
                         && !(root.editorKind === 1
                              && editorProject.spritePlacementMode)
                checkable: true
                checked: root.activeDrawingTool === 5
                Accessible.name: root.editorKind === 0
                                 ? qsTr("Draw fixed-origin pattern rays")
                                 : qsTr("Draw fixed-origin sprite rays")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("%1; hold Shift to constrain, Escape to finish")
                                  .arg(Accessible.name)
                onClicked: root.setActiveDrawingTool(5)

                contentItem: Canvas {
                    implicitWidth: 16
                    implicitHeight: 16
                    property color iconColor: patternRaysButton.palette.buttonText
                    onIconColorChanged: requestPaint()
                    onPaint: {
                        const context = getContext("2d")
                        context.clearRect(0, 0, width, height)
                        context.strokeStyle = iconColor
                        context.fillStyle = iconColor
                        context.lineCap = "round"
                        context.lineWidth = 1.5
                        context.beginPath()
                        context.moveTo(3, 8)
                        context.lineTo(13, 3)
                        context.moveTo(3, 8)
                        context.lineTo(14, 8)
                        context.moveTo(3, 8)
                        context.lineTo(13, 13)
                        context.stroke()
                        context.beginPath()
                        context.arc(3, 8, 2, 0, Math.PI * 2)
                        context.fill()
                    }
                }
            }
            Item {
                id: colorControl
                objectName: root.editorKind === 0
                            ? "characterPatternColorControl"
                            : "spritePatternColorControl"
                implicitWidth: 26
                implicitHeight: 26
                property string colorHint: root.editorKind === 0
                    ? qsTr("Pattern foreground and background colors")
                    : qsTr("Choose Sprite Color — click the upper-left color swatch; lower-right transparency selects Eraser")
                Accessible.role: Accessible.Button
                Accessible.name: colorHint
                Accessible.onPressAction: openColorPicker()

                function openColorPicker() {
                    root.setActiveColorSide(0)
                    if (root.editorKind === 1)
                        root.setActiveDrawingTool(1)
                    characterPalettePopup.open()
                }

                Rectangle {
                    anchors.fill: parent
                    radius: 3
                    color: characterColorHover.hovered
                           ? palette.midlight : "transparent"
                    border.width: root.editorKind === 1 ? 1 : 0
                    border.color: palette.mid
                }

                Rectangle {
                    objectName: root.editorKind === 0
                                ? "characterBackgroundColorSwatch"
                                : "spriteTransparencySwatch"
                    x: 8
                    y: 8
                    width: 16
                    height: 16
                    z: root.activeColorSide === 1 ? 2 : 1
                    color: root.editorKind === 0
                           ? root.patternPaletteColor(
                                 editorProject.characterBackgroundColorIndex)
                           : "transparent"
                    radius: 3
                    border.width: root.activeColorSide === 1 ? 2 : 1
                    border.color: root.activeColorSide === 1
                                  ? palette.highlight : "#707780"
                    Canvas {
                        anchors.fill: parent
                        anchors.margins: 2
                        visible: root.editorKind === 1
                        onPaint: {
                            const context = getContext("2d")
                            context.clearRect(0, 0, width, height)
                            const cell = width / 2
                            context.fillStyle = "#d8dde3"
                            context.fillRect(0, 0, cell, cell)
                            context.fillRect(cell, cell, cell, cell)
                            context.fillStyle = "#68737f"
                            context.fillRect(cell, 0, cell, cell)
                            context.fillRect(0, cell, cell, cell)
                            context.strokeStyle = "#ef6565"
                            context.lineWidth = 1.5
                            context.beginPath()
                            context.moveTo(1, height - 1)
                            context.lineTo(width - 1, 1)
                            context.stroke()
                        }
                    }
                    TapHandler {
                        onTapped: {
                            root.setActiveColorSide(1)
                            if (root.editorKind === 0)
                                characterPalettePopup.open()
                            else
                                root.setActiveDrawingTool(2)
                        }
                    }
                }
                Rectangle {
                    objectName: root.editorKind === 0
                                ? "characterForegroundColorSwatch"
                                : "spriteColorSwatch"
                    x: 0
                    y: 0
                    width: 16
                    height: 16
                    z: root.activeColorSide === 0 ? 2 : 1
                    color: root.patternPaletteColor(
                               root.editorKind === 0
                               ? editorProject.characterForegroundColorIndex
                               : editorProject.spriteDrawingColorIndex)
                    radius: 3
                    border.width: root.activeColorSide === 0 ? 2 : 1
                    border.color: root.activeColorSide === 0
                                  ? palette.highlight : "#707780"
                    Text {
                        visible: root.editorKind === 1
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.rightMargin: 1
                        anchors.bottomMargin: -1
                        text: "▾"
                        color: root.contrastColor(parent.color)
                        font.bold: true
                        font.pixelSize: 8
                    }
                    TapHandler {
                        onTapped: colorControl.openColorPicker()
                    }
                }
                HoverHandler { id: characterColorHover }
                ToolTip.delay: 250
                ToolTip.visible: characterColorHover.hovered
                ToolTip.text: colorControl.colorHint
            }
            ToolButton {
                id: rotatePatternButton
                objectName: root.editorKind === 0
                            ? "rotateCharacterPatternButton"
                            : "rotateSpritePatternButton"
                implicitWidth: 26
                implicitHeight: 26
                enabled: root.activeEditorItemLoaded && !root.activeEditorPan
                         && !(root.editorKind === 1
                              && editorProject.spritePlacementMode)
                Accessible.name: root.editorKind === 0
                                 ? qsTr("Rotate pattern clockwise")
                                 : qsTr("Rotate sprite clockwise")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: {
                    if (root.editorKind === 0)
                        editorProject.rotateActiveCharacterPattern()
                    else
                        editorProject.rotateActiveSpritePattern()
                }

                contentItem: Canvas {
                    implicitWidth: 16
                    implicitHeight: 16
                    property color iconColor: rotatePatternButton.palette.buttonText
                    onIconColorChanged: requestPaint()
                    onPaint: {
                        const context = getContext("2d")
                        context.clearRect(0, 0, width, height)
                        context.strokeStyle = iconColor
                        context.fillStyle = iconColor
                        context.lineWidth = 2
                        context.beginPath()
                        context.arc(8, 8, 5, Math.PI * 0.2, Math.PI * 1.65)
                        context.stroke()
                        context.beginPath()
                        context.moveTo(12.5, 2)
                        context.lineTo(14.5, 6)
                        context.lineTo(10.2, 5.2)
                        context.closePath()
                        context.fill()
                    }
                }
            }
            ToolButton {
                id: mirrorPatternButton
                objectName: root.editorKind === 0
                            ? "mirrorCharacterPatternButton"
                            : "mirrorSpritePatternButton"
                implicitWidth: 26
                implicitHeight: 26
                enabled: root.activeEditorItemLoaded && !root.activeEditorPan
                         && !(root.editorKind === 1
                              && editorProject.spritePlacementMode)
                Accessible.name: root.editorKind === 0
                                 ? qsTr("Mirror pattern horizontally")
                                 : qsTr("Mirror sprite horizontally")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: {
                    if (root.editorKind === 0)
                        editorProject.mirrorActiveCharacterPattern()
                    else
                        editorProject.mirrorActiveSpritePattern()
                }

                contentItem: Canvas {
                    implicitWidth: 16
                    implicitHeight: 16
                    property color iconColor: mirrorPatternButton.palette.buttonText
                    onIconColorChanged: requestPaint()
                    onPaint: {
                        const context = getContext("2d")
                        context.clearRect(0, 0, width, height)
                        context.strokeStyle = iconColor
                        context.fillStyle = iconColor
                        context.lineWidth = 1.5
                        context.setLineDash([2, 2])
                        context.beginPath()
                        context.moveTo(8, 1)
                        context.lineTo(8, 15)
                        context.stroke()
                        context.setLineDash([])
                        context.beginPath()
                        context.moveTo(1.5, 8)
                        context.lineTo(6.5, 3.5)
                        context.lineTo(6.5, 12.5)
                        context.closePath()
                        context.stroke()
                        context.beginPath()
                        context.moveTo(14.5, 8)
                        context.lineTo(9.5, 3.5)
                        context.lineTo(9.5, 12.5)
                        context.closePath()
                        context.fill()
                    }
                }
            }
            ToolButton {
                id: flipPatternButton
                objectName: root.editorKind === 0
                            ? "flipCharacterPatternButton"
                            : "flipSpritePatternButton"
                implicitWidth: 26
                implicitHeight: 26
                enabled: root.activeEditorItemLoaded && !root.activeEditorPan
                         && !(root.editorKind === 1
                              && editorProject.spritePlacementMode)
                Accessible.name: root.editorKind === 0
                                 ? qsTr("Flip pattern vertically")
                                 : qsTr("Flip sprite vertically")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: {
                    if (root.editorKind === 0)
                        editorProject.flipActiveCharacterPattern()
                    else
                        editorProject.flipActiveSpritePattern()
                }

                contentItem: Canvas {
                    implicitWidth: 16
                    implicitHeight: 16
                    property color iconColor: flipPatternButton.palette.buttonText
                    onIconColorChanged: requestPaint()
                    onPaint: {
                        const context = getContext("2d")
                        context.clearRect(0, 0, width, height)
                        context.strokeStyle = iconColor
                        context.fillStyle = iconColor
                        context.lineWidth = 1.5
                        context.setLineDash([2, 2])
                        context.beginPath()
                        context.moveTo(1, 8)
                        context.lineTo(15, 8)
                        context.stroke()
                        context.setLineDash([])
                        context.beginPath()
                        context.moveTo(8, 1.5)
                        context.lineTo(3.5, 6.5)
                        context.lineTo(12.5, 6.5)
                        context.closePath()
                        context.stroke()
                        context.beginPath()
                        context.moveTo(8, 14.5)
                        context.lineTo(3.5, 9.5)
                        context.lineTo(12.5, 9.5)
                        context.closePath()
                        context.fill()
                    }
                }
            }
            ToolButton {
                id: blankPatternButton
                objectName: root.editorKind === 0
                            ? "blankCharacterPatternButton"
                            : "blankSpritePatternButton"
                implicitWidth: 26
                implicitHeight: 26
                enabled: root.activeEditorItemLoaded && !root.activeEditorPan
                         && !(root.editorKind === 1
                              && editorProject.spritePlacementMode)
                Accessible.name: root.editorKind === 0
                                 ? qsTr("Blank pattern") : qsTr("Blank sprite")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: {
                    if (root.editorKind === 0)
                        editorProject.blankActiveCharacterPattern()
                    else
                        editorProject.blankActiveSpritePattern()
                }

                contentItem: Canvas {
                    implicitWidth: 16
                    implicitHeight: 16
                    property color iconColor: blankPatternButton.palette.buttonText
                    onIconColorChanged: requestPaint()
                    onPaint: {
                        const context = getContext("2d")
                        context.clearRect(0, 0, width, height)
                        context.fillStyle = iconColor
                        context.beginPath()
                        for (let point = 0; point < 16; ++point) {
                            const angle = -Math.PI / 2 + point * Math.PI / 8
                            const radius = point % 2 === 0 ? 7 : 3.2
                            const x = 8 + Math.cos(angle) * radius
                            const y = 8 + Math.sin(angle) * radius
                            if (point === 0)
                                context.moveTo(x, y)
                            else
                                context.lineTo(x, y)
                        }
                        context.closePath()
                        context.fill()
                    }
                }
            }
            ToolButton {
                id: copyPatternButton
                objectName: root.editorKind === 0
                            ? "copyCharacterPatternButton"
                            : "copySpritePatternButton"
                implicitWidth: 26
                implicitHeight: 26
                enabled: root.activeEditorItemLoaded && !root.activeEditorPan
                         && !(root.editorKind === 1
                              && editorProject.spritePlacementMode)
                Accessible.name: root.editorKind === 0
                                 ? qsTr("Copy pattern as JSON")
                                 : qsTr("Copy sprite as JSON")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: {
                    if (root.editorKind === 0)
                        editorProject.copyActiveCharacterPattern()
                    else
                        editorProject.copyActiveSpritePattern()
                }

                contentItem: Canvas {
                    implicitWidth: 16
                    implicitHeight: 16
                    property color iconColor: copyPatternButton.palette.buttonText
                    onIconColorChanged: requestPaint()
                    onPaint: {
                        const context = getContext("2d")
                        context.clearRect(0, 0, width, height)
                        context.strokeStyle = iconColor
                        context.lineWidth = 1.5
                        context.strokeRect(2, 1.5, 9, 11)
                        context.strokeRect(5, 4.5, 9, 10)
                    }
                }
            }
            ToolButton {
                id: pastePatternButton
                objectName: root.editorKind === 0
                            ? "pasteCharacterPatternButton"
                            : "pasteSpritePatternButton"
                implicitWidth: 26
                implicitHeight: 26
                enabled: root.activeEditorItemLoaded && !root.activeEditorPan
                         && !(root.editorKind === 1
                              && editorProject.spritePlacementMode)
                         && (root.editorKind === 0
                             ? editorProject.canPasteCharacterPattern
                             : editorProject.canPasteSpritePattern)
                Accessible.name: root.editorKind === 0
                                 ? qsTr("Paste pattern JSON")
                                 : qsTr("Paste sprite JSON")
                ToolTip.visible: hovered
                ToolTip.text: (root.editorKind === 0
                               ? editorProject.canPasteCharacterPattern
                               : editorProject.canPasteSpritePattern)
                              ? Accessible.name
                              : qsTr("Clipboard does not contain compatible JSON")
                onClicked: {
                    if (root.editorKind === 0)
                        editorProject.pasteActiveCharacterPattern()
                    else
                        editorProject.pasteActiveSpritePattern()
                }

                contentItem: Canvas {
                    implicitWidth: 16
                    implicitHeight: 16
                    property color iconColor: pastePatternButton.palette.buttonText
                    onIconColorChanged: requestPaint()
                    onPaint: {
                        const context = getContext("2d")
                        context.clearRect(0, 0, width, height)
                        context.strokeStyle = iconColor
                        context.lineWidth = 1.5
                        context.strokeRect(3, 3.5, 10, 11)
                        context.strokeRect(5.5, 1.5, 5, 3)
                        context.beginPath()
                        context.moveTo(5, 7)
                        context.lineTo(11, 7)
                        context.moveTo(5, 10)
                        context.lineTo(11, 10)
                        context.stroke()
                    }
                }
            }
            ToolSeparator {
                height: 26
            }
            ToolButton {
                objectName: root.objectName + "SetGridButton"
                visible: false
                implicitWidth: 26
                implicitHeight: 26
                text: qsTr("▦")
                checkable: true
                checked: !editorProject.spritePlacementMode
                Accessible.name: qsTr("Sprite set grid")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: editorProject.spritePlacementMode = false
            }
            ToolButton {
                id: characterTilingButton
                objectName: root.editorKind === 0
                            ? "characterTilingButton"
                            : "spritePlacementModeButton"
                implicitWidth: 26
                implicitHeight: 26
                padding: 1
                checkable: true
                checked: root.editorKind === 0
                         ? editorProject.characterTilingMode
                         : editorProject.spritePlacementMode
                property string modeHint:
                    checked
                    ? (root.editorKind === 0
                       ? qsTr("Change to Pattern Editor")
                        : (editorProject.activeTargetInfo.objectProcessor
                           ? qsTr("Change to Object Editor")
                           : qsTr("Change to Sprite Editor")))
                    : (root.editorKind === 0
                       ? qsTr("Change to Tiling Screen")
                        : (editorProject.activeTargetInfo.objectProcessor
                           ? qsTr("Change to Object Scene")
                           : qsTr("Change to Sprite Placement")))
                Accessible.name: modeHint
                ToolTip.visible: hovered
                ToolTip.text: modeHint
                onClicked: {
                    if (root.editorKind === 0)
                        editorProject.characterTilingMode = checked
                    else
                        editorProject.spritePlacementMode = checked
                }

                contentItem: Item {
                    implicitWidth: 24
                    implicitHeight: 24

                    Rectangle {
                        id: patternEditorModeIcon
                        objectName: "patternEditorModeIcon"
                        x: 0
                        y: 0
                        width: 16
                        height: 16
                        z: characterTilingButton.checked ? 1 : 2
                        color: characterTilingButton.palette.button
                        border.width: 1
                        border.color: characterTilingButton.palette.buttonText

                        Canvas {
                            anchors.fill: parent
                            anchors.margins: 2
                            property color iconColor:
                                characterTilingButton.palette.buttonText
                            onIconColorChanged: requestPaint()
                            onPaint: {
                                const context = getContext("2d")
                                context.clearRect(0, 0, width, height)
                                context.strokeStyle = iconColor
                                context.lineWidth = 1.5
                                const squareSize = 6
                                context.strokeRect(
                                    (width - squareSize) / 2,
                                    (height - squareSize) / 2,
                                    squareSize, squareSize)
                            }
                        }
                    }

                    Rectangle {
                        id: tilingScreenModeIcon
                        objectName: "tilingScreenModeIcon"
                        x: 8
                        y: 8
                        width: 16
                        height: 16
                        z: characterTilingButton.checked ? 2 : 1
                        color: characterTilingButton.palette.button
                        border.width: 1
                        border.color: characterTilingButton.palette.buttonText

                        Canvas {
                            anchors.fill: parent
                            anchors.margins: 1
                            property color iconColor:
                                characterTilingButton.palette.buttonText
                            onIconColorChanged: requestPaint()
                            onPaint: {
                                const context = getContext("2d")
                                context.clearRect(0, 0, width, height)
                                context.strokeStyle = iconColor
                                context.fillStyle = iconColor
                                context.lineWidth = 1
                                for (let position = 4.5;
                                     position < width; position += 4) {
                                    context.beginPath()
                                    context.moveTo(position, 0)
                                    context.lineTo(position, height)
                                    context.moveTo(0, position)
                                    context.lineTo(width, position)
                                    context.stroke()
                                }
                                context.fillRect(5, 5, 3, 3)
                                context.fillRect(9, 9, 3, 3)
                            }
                        }
                    }
                }
            }
            ToolButton {
                objectName: root.objectName + "PlacementButton"
                visible: false
                implicitHeight: 26
                text: qsTr("⌗")
                checkable: true
                checked: editorProject.spritePlacementMode
                Accessible.name: qsTr("Sprite placement workspace")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Arrange a %1-sprite set over the Screen Image reference")
                              .arg(editorProject.spritePatternsPerSet)
                onClicked: editorProject.spritePlacementMode = true
            }
            Item { Layout.fillWidth: true }
        }
    }

    workspaceContent: Component {
        Item {
            CharacterSetView {
                anchors.fill: parent
                visible: root.editorKind === 0
                drawingTool: root.characterDrawingTool
                tilingScale: root.effectiveZoom
            }
            SpriteSetView {
                anchors.fill: parent
                visible: root.editorKind === 1
                drawingTool: root.spriteDrawingTool
                placementScale: root.effectiveZoom
            }
        }
    }

    lowerToolbarContent: root.editorKind === 0
                         ? characterLowerToolbar : spriteLowerToolbar

    Component {
        id: characterLowerToolbar

        RowLayout {
            spacing: 1

            ToolButton {
                objectName: "undoCharacterEditButton"
                implicitWidth: 26
                implicitHeight: 26
                text: qsTr("↶")
                enabled: editorProject.canUndoCharacter
                Accessible.name: qsTr("Undo character edit")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: editorProject.undoCharacterEdit()
            }
            ToolButton {
                objectName: "redoCharacterEditButton"
                implicitWidth: 26
                implicitHeight: 26
                text: qsTr("↷")
                enabled: editorProject.canRedoCharacter
                Accessible.name: qsTr("Redo character edit")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: editorProject.redoCharacterEdit()
            }
            ToolSeparator { width: 5; height: 26 }
            ToolButton {
                id: characterPanButton
                objectName: "characterPanButton"
                implicitWidth: 26
                implicitHeight: 26
                checkable: true
                checked: editorProject.characterPanActive
                enabled: root.activeCharacterPatternLoaded
                         && !editorProject.characterTilingMode
                Accessible.name: checked
                                 ? qsTr("Finish pattern panning")
                                 : qsTr("Pan pattern on a 24 by 24 virtual grid")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: editorProject.characterPanActive = checked

                contentItem: Canvas {
                    implicitWidth: 16
                    implicitHeight: 16
                    property color iconColor: characterPanButton.palette.buttonText
                    onIconColorChanged: requestPaint()
                    onPaint: {
                        const context = getContext("2d")
                        context.clearRect(0, 0, width, height)
                        context.strokeStyle = iconColor
                        context.fillStyle = iconColor
                        context.lineWidth = 1.5
                        context.beginPath()
                        context.moveTo(8, 2)
                        context.lineTo(8, 14)
                        context.moveTo(2, 8)
                        context.lineTo(14, 8)
                        context.stroke()
                        const points = [[8, 1, 5.5, 4, 10.5, 4],
                                        [15, 8, 12, 5.5, 12, 10.5],
                                        [8, 15, 5.5, 12, 10.5, 12],
                                        [1, 8, 4, 5.5, 4, 10.5]]
                        for (let index = 0; index < points.length; ++index) {
                            const point = points[index]
                            context.beginPath()
                            context.moveTo(point[0], point[1])
                            context.lineTo(point[2], point[3])
                            context.lineTo(point[4], point[5])
                            context.closePath()
                            context.fill()
                        }
                    }
                }
            }
            ToolSeparator { width: 5; height: 26 }
            ToolButton {
                objectName: "characterPanLeftButton"
                implicitWidth: 24
                implicitHeight: 26
                text: qsTr("←")
                enabled: editorProject.characterPanActive
                         && editorProject.characterPanX > -8
                Accessible.name: qsTr("Move pattern left")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: editorProject.nudgeCharacterPan(-1, 0)
            }
            ToolButton {
                objectName: "characterPanUpButton"
                implicitWidth: 24
                implicitHeight: 26
                text: qsTr("↑")
                enabled: editorProject.characterPanActive
                         && editorProject.characterPanY > -8
                Accessible.name: qsTr("Move pattern up")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: editorProject.nudgeCharacterPan(0, -1)
            }
            ToolButton {
                objectName: "characterPanCenterButton"
                implicitWidth: 24
                implicitHeight: 26
                text: qsTr("◎")
                enabled: editorProject.characterPanActive
                         && (editorProject.characterPanX !== 0
                             || editorProject.characterPanY !== 0)
                Accessible.name: qsTr("Center pattern")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: editorProject.centerCharacterPan()
            }
            ToolButton {
                objectName: "characterPanDownButton"
                implicitWidth: 24
                implicitHeight: 26
                text: qsTr("↓")
                enabled: editorProject.characterPanActive
                         && editorProject.characterPanY < 8
                Accessible.name: qsTr("Move pattern down")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: editorProject.nudgeCharacterPan(0, 1)
            }
            ToolButton {
                objectName: "characterPanRightButton"
                implicitWidth: 24
                implicitHeight: 26
                text: qsTr("→")
                enabled: editorProject.characterPanActive
                         && editorProject.characterPanX < 8
                Accessible.name: qsTr("Move pattern right")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: editorProject.nudgeCharacterPan(1, 0)
            }
            Item { Layout.fillWidth: true }
        }
    }

    Component {
        id: spriteLowerToolbar

        RowLayout {
            spacing: 1

            ToolButton {
                objectName: "undoSpriteEditButton"
                implicitWidth: 26
                implicitHeight: 26
                text: qsTr("↶")
                enabled: editorProject.canUndoSprite
                Accessible.name: qsTr("Undo sprite edit")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: editorProject.undoSpriteEdit()
            }
            ToolButton {
                objectName: "redoSpriteEditButton"
                implicitWidth: 26
                implicitHeight: 26
                text: qsTr("↷")
                enabled: editorProject.canRedoSprite
                Accessible.name: qsTr("Redo sprite edit")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: editorProject.redoSpriteEdit()
            }
            ToolSeparator { width: 5; height: 26 }
            ToolButton {
                id: spritePanButton
                objectName: "spritePanButton"
                implicitWidth: 26
                implicitHeight: 26
                text: qsTr("✥")
                checkable: true
                checked: editorProject.spritePanActive
                enabled: root.activeSpriteLoaded
                         && !editorProject.spritePlacementMode
                Accessible.name: checked
                                 ? qsTr("Finish sprite panning")
                                 : qsTr("Pan sprite on its virtual grid")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: editorProject.spritePanActive = checked
            }
            ToolSeparator { width: 5; height: 26 }
            ToolButton {
                objectName: "spritePanLeftButton"
                implicitWidth: 24
                implicitHeight: 26
                text: qsTr("←")
                enabled: editorProject.spritePanActive
                         && editorProject.spritePanX > -editorProject.activeSpriteSize
                Accessible.name: qsTr("Move sprite left")
                onClicked: editorProject.nudgeSpritePan(-1, 0)
            }
            ToolButton {
                objectName: "spritePanUpButton"
                implicitWidth: 24
                implicitHeight: 26
                text: qsTr("↑")
                enabled: editorProject.spritePanActive
                         && editorProject.spritePanY > -editorProject.activeSpriteSize
                Accessible.name: qsTr("Move sprite up")
                onClicked: editorProject.nudgeSpritePan(0, -1)
            }
            ToolButton {
                objectName: "spritePanCenterButton"
                implicitWidth: 24
                implicitHeight: 26
                text: qsTr("◎")
                enabled: editorProject.spritePanActive
                         && (editorProject.spritePanX !== 0
                             || editorProject.spritePanY !== 0)
                Accessible.name: qsTr("Center sprite")
                onClicked: editorProject.centerSpritePan()
            }
            ToolButton {
                objectName: "spritePanDownButton"
                implicitWidth: 24
                implicitHeight: 26
                text: qsTr("↓")
                enabled: editorProject.spritePanActive
                         && editorProject.spritePanY < editorProject.activeSpriteSize
                Accessible.name: qsTr("Move sprite down")
                onClicked: editorProject.nudgeSpritePan(0, 1)
            }
            ToolButton {
                objectName: "spritePanRightButton"
                implicitWidth: 24
                implicitHeight: 26
                text: qsTr("→")
                enabled: editorProject.spritePanActive
                         && editorProject.spritePanX < editorProject.activeSpriteSize
                Accessible.name: qsTr("Move sprite right")
                onClicked: editorProject.nudgeSpritePan(1, 0)
            }
            ToolSeparator { width: 5; height: 26 }
            Label { text: qsTr("Set") }
            ComboBox {
                objectName: root.objectName + "SetComboBox"
                Layout.preferredWidth: 78
                model: editorProject.spriteSetNames
                currentIndex: editorProject.activeSpriteSet
                onActivated: index => editorProject.activeSpriteSet = index
            }
            ToolButton {
                objectName: root.objectName + "AddSetButton"
                implicitWidth: 26
                implicitHeight: 26
                text: qsTr("+")
                Accessible.name: qsTr("Add sprite set")
                onClicked: editorProject.addSpriteSet()
            }
            ToolButton {
                objectName: root.objectName + "RemoveSetButton"
                implicitWidth: 26
                implicitHeight: 26
                text: qsTr("−")
                enabled: editorProject.spriteSetNames.length > 1
                Accessible.name: qsTr("Remove active sprite set")
                onClicked: editorProject.removeActiveSpriteSet()
            }
            Item { Layout.fillWidth: true }
        }
    }
}
