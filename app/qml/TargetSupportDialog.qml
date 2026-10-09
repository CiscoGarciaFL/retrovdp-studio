import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "targetSupportDialog"
    title: qsTr("Target support")
    modal: true
    standardButtons: Dialog.Close
    width: Math.min(parent ? parent.width - 36 : 980, 980)
    height: Math.min(parent ? parent.height - 36 : 760, 760)

    property var targets: []
    property bool catalogLoaded: false
    readonly property string catalogUrl: "qrc:/qt/qml/RetroVDPStudio/data/target-support/targets.json"

    function loadCatalog() {
        const request = new XMLHttpRequest()
        request.onreadystatechange = function() {
            if (request.readyState !== XMLHttpRequest.DONE)
                return
            if (request.status === 0 || (request.status >= 200 && request.status < 300)) {
                try {
                    const catalog = JSON.parse(request.responseText)
                    root.targets = catalog.targets || []
                    root.catalogLoaded = true
                } catch (error) {
                    root.targets = []
                }
            }
        }
        request.open("GET", root.catalogUrl)
        request.send()
    }

    onOpened: {
        if (!catalogLoaded)
            loadCatalog()
    }

    contentItem: ColumnLayout {
        spacing: 10

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: palette.placeholderText
            text: qsTr("Implemented targets are summarized against real consumer-product photographs. Images are loaded from their credited source pages; when a source is unavailable, the card keeps its hardware summary and shows an offline placeholder.")
        }

        Label {
            visible: !root.catalogLoaded
            Layout.fillWidth: true
            text: qsTr("Loading target catalog…")
            color: palette.placeholderText
        }

        ScrollView {
            id: catalogScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            GridView {
                id: catalogGrid
                width: catalogScroll.availableWidth
                cellWidth: Math.max(320, Math.floor(width / 2))
                cellHeight: 330
                model: root.targets
                clip: true

                delegate: Frame {
                    required property var modelData
                    width: catalogGrid.cellWidth - 12
                    height: catalogGrid.cellHeight - 12
                    padding: 10

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 7

                        Label {
                            Layout.fillWidth: true
                            text: modelData.name
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("%1 · %2 KiB VRAM").arg(modelData.kind).arg(Math.round(modelData.vramBytes / 1024))
                            color: palette.placeholderText
                            elide: Text.ElideRight
                        }

                        Row {
                            Layout.fillWidth: true
                            height: 104
                            spacing: 6

                            Repeater {
                                model: modelData.images || []

                                delegate: Item {
                                    required property var modelData
                                    width: Math.max(104, (catalogGrid.cellWidth - 54) / Math.min(2, root.targets.length > 0 ? 2 : 1))
                                    height: 104

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 4
                                        color: palette.alternateBase
                                        border.color: palette.mid

                                        Label {
                                            anchors.centerIn: parent
                                            text: modelData.credit ? modelData.credit.substring(0, 16) : qsTr("Photo")
                                            width: parent.width - 12
                                            horizontalAlignment: Text.AlignHCenter
                                            wrapMode: Text.WordWrap
                                            color: palette.placeholderText
                                            font.pixelSize: 11
                                        }

                                        Image {
                                            id: photo
                                            property bool attemptedRemote: false
                                            anchors.fill: parent
                                            anchors.margins: 2
                                            source: modelData.local && !attemptedRemote
                                                    ? "qrc:/qt/qml/RetroVDPStudio/" + modelData.local
                                                    : modelData.url
                                            asynchronous: true
                                            fillMode: Image.PreserveAspectFit
                                            sourceSize.width: 320
                                            sourceSize.height: 180
                                            onStatusChanged: {
                                                if (status === Image.Error && modelData.local
                                                        && !attemptedRemote)
                                                    attemptedRemote = true
                                            }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: Qt.openUrlExternally(modelData.sourcePage)
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
                            text: qsTr("Screen: %1\nCharacters: %2\nSprites: %3").arg(modelData.workspaces.screenImage).arg(modelData.workspaces.character).arg(modelData.workspaces.sprite)
                            wrapMode: Text.WordWrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                            color: palette.placeholderText
                            font.pixelSize: 11
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Modes: %1").arg((modelData.modes || []).join(", "))
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            color: palette.placeholderText
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }
    }
}
