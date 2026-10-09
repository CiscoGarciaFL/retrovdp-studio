import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "targetSupportDialog"
    title: qsTr("About Supported Targets")
    modal: true
    standardButtons: Dialog.Close
    width: Math.min(parent ? parent.width - 36 : 980, 980)
    height: Math.min(parent ? parent.height - 36 : 760, 760)

    readonly property var categories: targetSupportCatalog.categories
    readonly property var targets: targetSupportCatalog.targets
    readonly property bool catalogReady: targetSupportCatalog.ready
    readonly property int categoryCount: categories.length
    readonly property int targetCount: targets.length
    readonly property int plannedTargetCount: countPlannedTargets()

    function bundledImageUrl(relativePath) {
        return relativePath ? Qt.resolvedUrl("../" + relativePath) : "";
    }

    function attributions(images) {
        const entries = [];
        for (let index = 0; index < images.length; ++index)
            entries.push(images[index].credit + " — " + images[index].license);
        return entries.join("  |  ");
    }

    function targetsForCategory(categoryId) {
        const matches = [];
        for (let index = 0; index < targets.length; ++index) {
            if (targets[index].category === categoryId)
                matches.push(targets[index]);
        }
        return matches;
    }

    function countPlannedTargets() {
        let count = 0;
        for (let index = 0; index < targets.length; ++index) {
            if (targets[index].status !== "implemented")
                ++count;
        }
        return count;
    }

    contentItem: Loader {
        active: root.visible
        sourceComponent: catalogContent
    }

    Component {
        id: catalogContent

        ColumnLayout {
            spacing: 10

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: palette.placeholderText
                text: qsTr(
                          "Implemented and planned targets are summarized with built-in real consumer-product photographs. The complete catalog works offline; source pages are optional attribution and read-more links and are never checked automatically.")
            }

            Label {
                visible: !root.catalogReady
                Layout.fillWidth: true
                text: qsTr("The bundled target catalog could not be loaded: %1").arg(
                          targetSupportCatalog.errorMessage)
                color: palette.brightText
                wrapMode: Text.WordWrap
            }

            ScrollView {
                id: catalogScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    id: catalogGrid
                    objectName: "targetSupportCatalogGrid"
                    width: catalogScroll.availableWidth
                    spacing: 14

                    Repeater {
                        model: root.categories

                        delegate: ColumnLayout {
                            id: categorySection
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                objectName: "targetSupportCategoryBar"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 38
                                leftPadding: 12
                                rightPadding: 12
                                text: categorySection.modelData.title
                                color: palette.highlightedText
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                                verticalAlignment: Text.AlignVCenter

                                background: Rectangle {
                                    radius: 4
                                    color: palette.highlight
                                    border.color: palette.mid
                                }
                            }

                            GridLayout {
                                id: categoryGrid
                                Layout.fillWidth: true
                                columns: width >= 680 ? 2 : 1
                                columnSpacing: 12
                                rowSpacing: 12

                                Repeater {
                                    model: root.targetsForCategory(categorySection.modelData.id)

                                    delegate: Frame {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: categoryGrid.columns === 2 ? (
                                                                                                categoryGrid.width
                                                                                                - categoryGrid.columnSpacing)
                                                                                            / 2 : categoryGrid.width
                                        Layout.preferredHeight: 350
                                        Layout.alignment: Qt.AlignTop
                                        padding: 10

                                        ColumnLayout {
                                            anchors.fill: parent
                                            spacing: 7

                                            RowLayout {
                                                Layout.fillWidth: true

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: modelData.name
                                                    font.pixelSize: 17
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                }

                                                Label {
                                                    objectName: modelData.status !== "implemented"
                                                                ? "plannedTargetTag" : ""
                                                    visible: modelData.status !== "implemented"
                                                    text: qsTr("(planned)")
                                                    leftPadding: 7
                                                    rightPadding: 7
                                                    topPadding: 2
                                                    bottomPadding: 2
                                                    color: palette.highlightedText
                                                    font.pixelSize: 10
                                                    font.weight: Font.DemiBold

                                                    background: Rectangle {
                                                        radius: 8
                                                        color: palette.highlight
                                                    }
                                                }
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                text: qsTr("%1 · %2 KiB VRAM").arg(
                                                          modelData.kind).arg(Math.round(
                                                                                  modelData.vramBytes
                                                                                  / 1024))
                                                color: palette.placeholderText
                                                elide: Text.ElideRight
                                            }

                                            RowLayout {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 104
                                                spacing: 6

                                                Repeater {
                                                    model: modelData.images || []

                                                    delegate: Item {
                                                        required property var modelData
                                                        Layout.fillWidth: true
                                                        Layout.preferredHeight: 104

                                                        Rectangle {
                                                            anchors.fill: parent
                                                            radius: 4
                                                            color: palette.alternateBase
                                                            border.color: palette.mid

                                                            Label {
                                                                anchors.centerIn: parent
                                                                text: modelData.credit
                                                                      ? modelData.credit.substring(0,
                                                                                                   16) : qsTr(
                                                                            "Photo")
                                                                width: parent.width - 12
                                                                horizontalAlignment:
                                                                    Text.AlignHCenter
                                                                wrapMode: Text.WordWrap
                                                                color: palette.placeholderText
                                                                font.pixelSize: 11
                                                            }

                                                            Image {
                                                                id: photo
                                                                objectName: "targetSupportPhoto"
                                                                anchors.fill: parent
                                                                anchors.margins: 2
                                                                source: root.bundledImageUrl(
                                                                            modelData.local)
                                                                asynchronous: false
                                                                cache: true
                                                                fillMode: Image.PreserveAspectFit
                                                                sourceSize.width: 320
                                                                sourceSize.height: 180
                                                            }

                                                            MouseArea {
                                                                anchors.fill: parent
                                                                enabled: photo.status
                                                                         === Image.Ready
                                                                hoverEnabled: true
                                                                cursorShape: Qt.PointingHandCursor
                                                                onClicked: Qt.openUrlExternally(
                                                                               modelData.sourcePage)
                                                                ToolTip.visible: containsMouse
                                                                ToolTip.text: qsTr(
                                                                                  "%1\n%2\nOpen attribution and source page").arg(
                                                                                  modelData.credit).arg(
                                                                                  modelData.license)
                                                            }
                                                        }
                                                    }
                                                }
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                text: modelData.hardware
                                                wrapMode: Text.WordWrap
                                                maximumLineCount: 2
                                                elide: Text.ElideRight
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                text: qsTr(
                                                          "Screen: %1\nCharacters: %2\nSprites: %3").arg(
                                                          modelData.workspaces.screenImage).arg(
                                                          modelData.workspaces.character).arg(
                                                          modelData.workspaces.sprite)
                                                wrapMode: Text.WordWrap
                                                maximumLineCount: 3
                                                elide: Text.ElideRight
                                                color: palette.placeholderText
                                                font.pixelSize: 11
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                text: qsTr("Modes: %1").arg((modelData.modes
                                                                             || []).join(", "))
                                                wrapMode: Text.WordWrap
                                                maximumLineCount: 2
                                                elide: Text.ElideRight
                                                color: palette.placeholderText
                                                font.pixelSize: 10
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                text: qsTr("Photos: %1").arg(root.attributions(
                                                                                 modelData.images
                                                                                 || []))
                                                wrapMode: Text.WordWrap
                                                maximumLineCount: 2
                                                elide: Text.ElideRight
                                                color: palette.placeholderText
                                                font.pixelSize: 9
                                                ToolTip.visible: attributionMouse.containsMouse
                                                ToolTip.text: text + qsTr(
                                                                  "\nClick a photograph to open its source page.")

                                                MouseArea {
                                                    id: attributionMouse
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    acceptedButtons: Qt.NoButton
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
