pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    objectName: "spritePatternEditor"

    required property int editorIndex
    required property int editorCount
    required property bool loaded
    required property int setIndex
    required property int spriteIndex
    required property int spriteSize
    required property int spriteColorIndex
    required property int drawingTool
    required property bool active
    readonly property real cellSize: 16
    readonly property int pixelWidth: editorProject.spritePatternWidth(spriteSize)
    readonly property int pixelHeight: editorProject.spritePatternHeight(spriteSize)
    readonly property real gridWidth: cellSize * pixelWidth
    readonly property real gridHeight: cellSize * pixelHeight
    readonly property var pixels: {
        const revision = editorProject.spriteRevision
        if (!loaded)
            return []
        return editorProject.spritePatternPixels(setIndex, spriteIndex, spriteSize)
    }
    readonly property var paletteColors: editorProject.spritePaletteColors
    readonly property bool pixelEditingEnabled:
        loaded && !editorProject.spritePanActive
    property bool kLineActive: false
    property int kLineRow: 0
    property int kLineColumn: 0
    property bool raysActive: false
    property int raysOriginRow: 0
    property int raysOriginColumn: 0
    property var linePreviewCells: []

    signal selected()
    signal moveRequested(int targetIndex)
    signal removeRequested()

    implicitWidth: gridWidth + 96
    implicitHeight: gridHeight + 60
    color: "#13191f"
    border.width: active ? 2 : 1
    border.color: active ? palette.highlight : "#46515d"
    radius: 4

    function hexRow(row) {
        let value = 0
        for (let column = 0; column < pixelWidth; ++column) {
            if (loaded && pixels.length > row * pixelWidth + column
                    && pixels[row * pixelWidth + column] !== 0)
                value += Math.pow(2, pixelWidth - 1 - column)
        }
        return value.toString(16).toUpperCase().padStart(pixelWidth / 4, "0")
    }

    function pixelColor(value) {
        if (value === 0)
            return "#20272e"
        if (editorProject.editScope === 0
                && editorProject.activeTargetInfo.spriteMaximumColorDepth <= 1)
            return paletteColors[spriteColorIndex]
        return paletteColors[Math.min(paletteColors.length - 1, value)]
    }

    function paintAt(localX, localY) {
        root.selected()
        if (!pixelEditingEnabled)
            return
        const column = Math.max(0, Math.min(pixelWidth - 1,
                                            Math.floor(localX / cellSize)))
        const row = Math.max(0, Math.min(pixelHeight - 1,
                                         Math.floor(localY / cellSize)))
        editorProject.paintSpritePixel(setIndex, spriteIndex, spriteSize,
                                       row, column, drawingTool === 1)
    }

    function cellAt(localX, localY) {
        return Qt.point(
            Math.max(0, Math.min(pixelWidth - 1,
                                 Math.floor(localX / cellSize))),
            Math.max(0, Math.min(pixelHeight - 1,
                                 Math.floor(localY / cellSize))))
    }

    function constrainedCell(fromColumn, fromRow, column, row, locked) {
        if (!locked)
            return Qt.point(column, row)
        if (Math.abs(column - fromColumn) >= Math.abs(row - fromRow))
            return Qt.point(column, fromRow)
        return Qt.point(fromColumn, row)
    }

    function lineCells(fromColumn, fromRow, toColumn, toRow) {
        const result = []
        let x = fromColumn
        let y = fromRow
        const deltaX = Math.abs(toColumn - fromColumn)
        const stepX = fromColumn < toColumn ? 1 : -1
        const deltaY = -Math.abs(toRow - fromRow)
        const stepY = fromRow < toRow ? 1 : -1
        let error = deltaX + deltaY
        while (true) {
            result.push(y * pixelWidth + x)
            if (x === toColumn && y === toRow)
                break
            const twiceError = error * 2
            if (twiceError >= deltaY) {
                error += deltaY
                x += stepX
            }
            if (twiceError <= deltaX) {
                error += deltaX
                y += stepY
            }
        }
        return result
    }

    function updateLinePreview(fromColumn, fromRow, localX, localY, locked) {
        const cell = cellAt(localX, localY)
        const end = constrainedCell(fromColumn, fromRow, cell.x, cell.y, locked)
        linePreviewCells = lineCells(fromColumn, fromRow, end.x, end.y)
        return end
    }

    function finishMultiLine() {
        if (!kLineActive && !raysActive)
            return
        editorProject.endSpriteEdit()
        kLineActive = false
        raysActive = false
        linePreviewCells = []
    }

    function finishKLine() {
        finishMultiLine()
    }

    onDrawingToolChanged: {
        if ((kLineActive && drawingTool !== 4)
                || (raysActive && drawingTool !== 5))
            finishMultiLine()
    }
    onActiveChanged: {
        if (!active)
            finishMultiLine()
    }
    Component.onDestruction: finishMultiLine()

    Menu {
        id: orderMenu
        parent: patternLabel

        MenuItem {
            text: qsTr("Move left")
            enabled: root.editorIndex > 0
            onTriggered: root.moveRequested(root.editorIndex - 1)
        }
        MenuItem {
            text: qsTr("Move right")
            enabled: root.editorIndex + 1 < root.editorCount
            onTriggered: root.moveRequested(root.editorIndex + 1)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Move to first")
            enabled: root.editorIndex > 0
            onTriggered: root.moveRequested(0)
        }
        MenuItem {
            text: qsTr("Move to last")
            enabled: root.editorIndex + 1 < root.editorCount
            onTriggered: root.moveRequested(root.editorCount - 1)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Remove editor")
            enabled: root.editorCount > 1
            onTriggered: root.removeRequested()
        }
    }

    Row {
        id: dataRow
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.top: parent.top
        anchors.topMargin: 8
        height: root.gridHeight
        spacing: 2

        Item {
            width: 10
            height: root.gridHeight
            Label {
                anchors.centerIn: parent
                text: qsTr("Mask")
                color: palette.placeholderText
                font.pixelSize: 10
                rotation: -90
            }
        }

        Column {
            width: 38
            Repeater {
                model: root.pixelHeight
                Label {
                    required property int index
                    width: 38
                    height: root.cellSize
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    text: root.hexRow(index)
                    color: "#c2c8cf"
                    font.family: "monospace"
                    font.pixelSize: root.spriteSize === 16 ? 8 : 11
                }
            }
        }

        Item {
            id: pixelGrid
            objectName: "spritePatternPixelGrid"
            width: root.gridWidth
            height: root.gridHeight

            Repeater {
                model: root.pixelWidth * root.pixelHeight
                Rectangle {
                    required property int index
                    readonly property int pixelRow: Math.floor(index / root.pixelWidth)
                    readonly property int pixelColumn: index % root.pixelWidth
                    x: pixelColumn * root.cellSize
                    y: pixelRow * root.cellSize
                    width: root.cellSize
                    height: root.cellSize
                    color: !root.loaded || root.pixels.length <= index
                           ? "#20272e" : root.pixelColor(root.pixels[index])
                }
            }

            Repeater {
                model: root.pixelWidth + 1
                Rectangle {
                    required property int index
                    x: Math.max(0, Math.min(pixelGrid.width - width,
                                           index * root.cellSize - width / 2))
                    width: index > 0 && index < root.pixelWidth
                           && index % 8 === 0 ? 3 : 1
                    height: pixelGrid.height
                    color: width > 1 ? "#d8dde3" : "#68737f"
                }
            }
            Repeater {
                model: root.pixelHeight + 1
                Rectangle {
                    required property int index
                    y: Math.max(0, Math.min(pixelGrid.height - height,
                                           index * root.cellSize - height / 2))
                    width: pixelGrid.width
                    height: index > 0 && index < root.pixelHeight
                            && index % 8 === 0 ? 3 : 1
                    color: height > 1 ? "#d8dde3" : "#68737f"
                }
            }

            Repeater {
                model: root.linePreviewCells
                Rectangle {
                    required property int modelData
                    x: (modelData % root.pixelWidth) * root.cellSize + 2
                    y: Math.floor(modelData / root.pixelWidth) * root.cellSize + 2
                    width: root.cellSize - 4
                    height: root.cellSize - 4
                    color: "#55ffffff"
                    border.width: 2
                    border.color: root.palette.highlight
                }
            }

            MouseArea {
                id: drawingArea
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: root.pixelEditingEnabled
                             ? Qt.CrossCursor : Qt.PointingHandCursor
                preventStealing: true
                hoverEnabled: true
                focus: root.kLineActive || root.raysActive
                property int dragTool: 0
                property int startRow: 0
                property int startColumn: 0
                Keys.onEscapePressed: event => {
                    if (root.kLineActive || root.raysActive) {
                        root.finishMultiLine()
                        event.accepted = true
                    }
                }
                onPressed: mouse => {
                    root.selected()
                    dragTool = root.drawingTool
                    if (!root.pixelEditingEnabled)
                        return
                    const cell = root.cellAt(mouse.x, mouse.y)
                    if (dragTool <= 2) {
                        editorProject.beginSpriteEdit(root.setIndex,
                                                      root.spriteIndex,
                                                      root.spriteSize)
                        root.paintAt(mouse.x, mouse.y)
                    } else if (dragTool === 3) {
                        startColumn = cell.x
                        startRow = cell.y
                        root.updateLinePreview(startColumn, startRow,
                                               mouse.x, mouse.y, false)
                    } else if (dragTool === 4 || dragTool === 5) {
                        forceActiveFocus()
                        const active = dragTool === 4
                            ? root.kLineActive : root.raysActive
                        if (!active) {
                            editorProject.beginSpriteEdit(
                                root.setIndex, root.spriteIndex, root.spriteSize)
                            editorProject.paintSpritePixel(
                                root.setIndex, root.spriteIndex, root.spriteSize,
                                cell.y, cell.x, true)
                            if (dragTool === 4) {
                                root.kLineColumn = cell.x
                                root.kLineRow = cell.y
                                root.kLineActive = true
                            } else {
                                root.raysOriginColumn = cell.x
                                root.raysOriginRow = cell.y
                                root.raysActive = true
                            }
                        } else {
                            const fromColumn = dragTool === 4
                                ? root.kLineColumn : root.raysOriginColumn
                            const fromRow = dragTool === 4
                                ? root.kLineRow : root.raysOriginRow
                            const end = root.constrainedCell(
                                fromColumn, fromRow,
                                cell.x, cell.y,
                                (mouse.modifiers & Qt.ShiftModifier) !== 0)
                            editorProject.drawSpriteLine(
                                root.setIndex, root.spriteIndex, root.spriteSize,
                                fromRow, fromColumn,
                                end.y, end.x, true)
                            if (dragTool === 4) {
                                root.kLineColumn = end.x
                                root.kLineRow = end.y
                            }
                        }
                        root.linePreviewCells = []
                    }
                }
                onPositionChanged: mouse => {
                    if ((root.kLineActive && root.drawingTool === 4)
                            || (root.raysActive && root.drawingTool === 5)) {
                        const fromColumn = root.drawingTool === 4
                            ? root.kLineColumn : root.raysOriginColumn
                        const fromRow = root.drawingTool === 4
                            ? root.kLineRow : root.raysOriginRow
                        root.updateLinePreview(
                            fromColumn, fromRow,
                            mouse.x, mouse.y,
                            (mouse.modifiers & Qt.ShiftModifier) !== 0)
                    } else if (pressed && root.pixelEditingEnabled) {
                        if (dragTool <= 2) {
                            root.paintAt(mouse.x, mouse.y)
                        } else if (dragTool === 3) {
                            root.updateLinePreview(
                                startColumn, startRow, mouse.x, mouse.y,
                                (mouse.modifiers & Qt.ShiftModifier) !== 0)
                        }
                    }
                }
                onReleased: mouse => {
                    if (dragTool <= 2) {
                        editorProject.endSpriteEdit()
                    } else if (dragTool === 3 && root.pixelEditingEnabled) {
                        const end = root.updateLinePreview(
                            startColumn, startRow, mouse.x, mouse.y,
                            (mouse.modifiers & Qt.ShiftModifier) !== 0)
                        editorProject.drawSpriteLine(
                            root.setIndex, root.spriteIndex, root.spriteSize,
                            startRow, startColumn, end.y, end.x, true)
                        root.linePreviewCells = []
                    }
                    dragTool = 0
                }
                onCanceled: {
                    if (dragTool <= 2)
                        editorProject.endSpriteEdit()
                    else if (dragTool === 4 || dragTool === 5)
                        root.finishMultiLine()
                    root.linePreviewCells = []
                    dragTool = 0
                }
            }
        }
    }

    ToolButton {
        id: patternLabel
        objectName: "spritePatternEditorLabel"
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        x: dataRow.x + pixelGrid.x + (pixelGrid.width - width) / 2
        implicitWidth: 104
        implicitHeight: 24
        checkable: true
        checked: root.active
        text: root.loaded
              ? qsTr("%1x%2 Sprite %3")
                    .arg(root.pixelWidth)
                    .arg(root.pixelHeight)
                    .arg(root.spriteIndex.toString().padStart(2, "0"))
              : qsTr("Empty")
        Accessible.name: root.loaded
                         ? qsTr("Select %1 by %2 sprite %3")
                               .arg(root.pixelWidth).arg(root.pixelHeight)
                               .arg(root.spriteIndex)
                         : qsTr("Select empty sprite editor")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Select this editor; right-click or hold for ordering options")
        onClicked: root.selected()
        onPressAndHold: {
            root.selected()
            orderMenu.open()
        }

        background: Rectangle {
            radius: 2
            color: root.active ? patternLabel.palette.highlight
                               : patternLabel.palette.button
            border.width: 1
            border.color: root.active ? patternLabel.palette.highlight
                                      : patternLabel.palette.mid
        }
        contentItem: Text {
            text: patternLabel.text
            font: patternLabel.font
            color: root.active ? patternLabel.palette.highlightedText
                               : patternLabel.palette.buttonText
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: {
                root.selected()
                orderMenu.open()
            }
        }
    }

    Rectangle {
        id: preview
        objectName: "spritePatternPreview"
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        width: 34
        height: 34
        color: "#20272e"
        border.width: 1
        border.color: root.active ? palette.highlight : "#68737f"

        Item {
            anchors.centerIn: parent
            width: root.pixelWidth
            height: root.pixelHeight
            Repeater {
                model: root.pixelWidth * root.pixelHeight
                Rectangle {
                    required property int index
                    x: index % root.pixelWidth
                    y: Math.floor(index / root.pixelWidth)
                    width: 1
                    height: 1
                    color: !root.loaded || root.pixels.length <= index
                           ? "#20272e" : root.pixelColor(root.pixels[index])
                }
            }
        }
        TapHandler { onTapped: root.selected() }
    }
}
