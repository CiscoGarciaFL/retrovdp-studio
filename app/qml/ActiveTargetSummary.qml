import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    // 0 = Screen Image, 1 = Character, 2 = Sprite
    property int detailKind: 0
    readonly property var info: editorProject.activeTargetInfo
    spacing: 5

    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: root.info.name || ""
            font.weight: Font.DemiBold
        }
        Label {
            text: root.info.implemented ? qsTr("Ready") : qsTr("Planned")
            color: root.info.implemented ? "#267447" : palette.placeholderText
            font.pixelSize: 11
        }
    }

    Label {
        Layout.fillWidth: true
        text: qsTr("Controlled by the Active Target in the Project Bar.")
        wrapMode: Text.WordWrap
        color: palette.placeholderText
        font.pixelSize: 11
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 10
        rowSpacing: 3

        Label { text: qsTr("Video memory") }
        Label {
            Layout.fillWidth: true
            text: qsTr("%1 KiB").arg(root.info.vramKiB || 0)
        }

        Label { text: qsTr("Palette") }
        Label {
            Layout.fillWidth: true
            text: root.info.paletteDescription || ""
        }

        Label { visible: root.detailKind === 0; text: qsTr("Conversion modes") }
        Label {
            visible: root.detailKind === 0
            Layout.fillWidth: true
            text: root.info.conversionModeCount || 0
        }

        Label { visible: root.detailKind === 1; text: qsTr("Pattern cell") }
        Label {
            visible: root.detailKind === 1
            Layout.fillWidth: true
            text: qsTr("%1×%2 pixels")
                    .arg(root.info.characterPixelWidth || 0)
                    .arg(root.info.characterPixelHeight || 0)
        }

        Label { visible: root.detailKind === 1; text: qsTr("Pattern capacity") }
        Label {
            visible: root.detailKind === 1
            Layout.fillWidth: true
            text: qsTr("%1 sets × %2 patterns")
                    .arg(root.info.characterSetCount || 0)
                    .arg(root.info.characterPatternsPerSet || 0)
        }

        Label { visible: root.detailKind === 1; text: qsTr("Map") }
        Label {
            visible: root.detailKind === 1
            Layout.fillWidth: true
            text: qsTr("%1×%2 cells")
                    .arg(root.info.characterMapColumns || 0)
                    .arg(root.info.characterMapRows || 0)
        }

        Label {
            visible: root.detailKind === 2
            text: root.info.objectProcessor ? qsTr("Object dimensions") : qsTr("Pattern sizes")
        }
        Label {
            visible: root.detailKind === 2
            Layout.fillWidth: true
            text: root.info.id === "sega-sms-vdp"
                  ? qsTr("8×8 and 8×16")
                  : root.info.id === "atari-lynx"
                  ? qsTr("8/16/32/64 pixels per axis")
                  : root.info.id === "neo-geo"
                  ? qsTr("16×16 through 16×512 strips")
                  : root.info.spriteMinimumSize === root.info.spriteMaximumSize
                  ? qsTr("%1×%1").arg(root.info.spriteMinimumSize || 0)
                  : qsTr("%1×%1 and %2×%2")
                        .arg(root.info.spriteMinimumSize || 0)
                        .arg(root.info.spriteMaximumSize || 0)
        }

        Label { visible: root.detailKind === 2; text: qsTr("Sizing") }
        Label {
            visible: root.detailKind === 2
            Layout.fillWidth: true
            text: root.info.spritePerItemSize
                  ? (root.info.objectProcessor ? qsTr("Per object") : qsTr("Per sprite"))
                  : qsTr("Global for target")
        }

        Label { visible: root.detailKind === 2; text: qsTr("Color depth") }
        Label {
            visible: root.detailKind === 2
            Layout.fillWidth: true
            text: qsTr("Up to %1 bpp")
                    .arg(root.info.spriteMaximumColorDepth || 1)
        }
    }
}
