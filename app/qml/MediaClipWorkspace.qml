import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    signal closeRequested()
    signal frameOpenRequested(url frameUrl)
    signal convertRequested()

    function formatPosition(microseconds) {
        const relative = Math.max(0, microseconds - mediaClip.startMicroseconds)
        const milliseconds = Math.floor(relative / 1000)
        const hours = Math.floor(milliseconds / 3600000)
        const minutes = Math.floor(milliseconds / 60000) % 60
        const seconds = Math.floor(milliseconds / 1000) % 60
        const millis = milliseconds % 1000
        return String(hours).padStart(2, "0") + ":"
                + String(minutes).padStart(2, "0") + ":"
                + String(seconds).padStart(2, "0") + "."
                + String(millis).padStart(3, "0")
    }

    Keys.onSpacePressed: mediaClip.togglePlayback()
    Keys.onLeftPressed: mediaClip.stepBackward()
    Keys.onRightPressed: mediaClip.stepForward()

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        ToolBar {
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 10

                ToolButton {
                    objectName: "closeMediaClipButton"
                    text: qsTr("‹ Back")
                    Accessible.name: qsTr("Close media clip workspace")
                    onClicked: root.closeRequested()
                }
                Label {
                    objectName: "mediaClipTitle"
                    text: mediaClip.clipLoaded
                          ? (mediaClip.outputMonitor
                             ? qsTr("Output Monitor · %1 · %2")
                                   .arg(mediaClip.outputTarget)
                                   .arg(mediaClip.outputMode)
                             : mediaClip.clipName)
                          : qsTr("Media Clip")
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
                Label {
                    text: mediaClip.clipLoaded
                          ? qsTr("%1 frames · %2 fps · %3")
                                .arg(mediaClip.frameCount)
                                .arg(mediaClip.framesPerSecond)
                                .arg(mediaClip.sizingMode)
                          : ""
                    color: palette.placeholderText
                }
                ToolButton {
                    objectName: "openMediaFrameAsSourceButton"
                    text: qsTr("Open Frame as Source")
                    enabled: mediaClip.clipLoaded
                    Accessible.name: qsTr("Open the selected media frame as the image source")
                    onClicked: root.frameOpenRequested(mediaClip.currentFrameUrl)
                }
                ToolButton {
                    objectName: "convertMediaClipButton"
                    text: qsTr("Convert Clip…")
                    enabled: mediaClip.clipLoaded && !mediaClip.outputMonitor
                             && !mediaBatch.busy
                    Accessible.name: qsTr("Convert this clip with a Screen Image recipe")
                    onClicked: root.convertRequested()
                }
            }
        }

        ThemedFrame {
            objectName: "mediaSourceMonitor"
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 8

            Rectangle {
                anchors.fill: parent
                color: "#101318"
                border.width: 1
                border.color: palette.mid

                Image {
                    id: monitorImage
                    objectName: "mediaMonitorImage"
                    anchors.fill: parent
                    anchors.margins: 8
                    source: mediaClip.currentFrameUrl
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    smooth: false
                    mipmap: false
                }

                Label {
                    anchors.centerIn: parent
                    visible: !mediaClip.clipLoaded
                    text: qsTr("Open a clip package to view its source frames")
                    color: "#c7ced8"
                }

                Label {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 12
                    visible: mediaClip.clipLoaded
                    text: root.formatPosition(mediaClip.positionMicroseconds)
                    color: "white"
                    font.family: "monospace"
                    padding: 5
                    background: Rectangle {
                        color: "#b0000000"
                        radius: 3
                    }
                }
            }
        }

        RowLayout {
            objectName: "mediaPlaybackControls"
            Layout.fillWidth: true
            enabled: mediaClip.clipLoaded
            spacing: 6

            ToolButton {
                text: "|‹"
                Accessible.name: qsTr("First frame")
                onClicked: mediaClip.seekToFrame(0)
            }
            ToolButton {
                objectName: "previousMediaFrameButton"
                text: "‹"
                Accessible.name: qsTr("Previous frame")
                onClicked: mediaClip.stepBackward()
            }
            ToolButton {
                objectName: "mediaPlayPauseButton"
                text: mediaClip.playing ? "❚❚" : "▶"
                Accessible.name: mediaClip.playing ? qsTr("Pause") : qsTr("Play")
                onClicked: mediaClip.togglePlayback()
            }
            ToolButton {
                objectName: "nextMediaFrameButton"
                text: "›"
                Accessible.name: qsTr("Next frame")
                onClicked: mediaClip.stepForward()
            }
            ToolButton {
                text: "›|"
                Accessible.name: qsTr("Last frame")
                onClicked: mediaClip.seekToFrame(mediaClip.frameCount - 1)
            }
            Slider {
                id: frameSlider
                objectName: "mediaFrameSlider"
                Layout.fillWidth: true
                from: 0
                to: Math.max(0, mediaClip.frameCount - 1)
                stepSize: 1
                value: Math.max(0, mediaClip.currentFrame)
                snapMode: Slider.SnapAlways
                onMoved: mediaClip.seekToFrame(Math.round(value))
                Accessible.name: qsTr("Media frame position")
            }
            Label {
                Layout.minimumWidth: 96
                horizontalAlignment: Text.AlignRight
                text: mediaClip.clipLoaded
                      ? qsTr("%1 / %2")
                            .arg(mediaClip.currentFrame + 1)
                            .arg(mediaClip.frameCount)
                      : qsTr("0 / 0")
            }
        }

        Label {
            Layout.fillWidth: true
            visible: mediaClip.hasAudio
            text: qsTr("Audio: %1 · synchronized sound playback is not enabled in this monitor yet")
                      .arg(mediaClip.audioFormat)
            color: palette.placeholderText
            elide: Text.ElideMiddle
            ToolTip.visible: audioHover.hovered
            ToolTip.text: mediaClip.audioPath

            HoverHandler { id: audioHover }
        }

        ListView {
            id: filmstrip
            objectName: "mediaFilmstrip"
            Layout.fillWidth: true
            Layout.preferredHeight: 116
            orientation: ListView.Horizontal
            spacing: 6
            clip: true
            model: mediaClip
            currentIndex: mediaClip.currentFrame
            cacheBuffer: Math.min(width * 2, 2048)
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 100
            onCurrentIndexChanged: {
                if (currentIndex >= 0 && currentIndex !== mediaClip.currentFrame)
                    mediaClip.seekToFrame(currentIndex)
            }

            Connections {
                target: mediaClip
                function onCurrentFrameChanged() {
                    filmstrip.currentIndex = mediaClip.currentFrame
                    filmstrip.positionViewAtIndex(mediaClip.currentFrame,
                                                  ListView.Contain)
                }
            }

            delegate: ItemDelegate {
                required property int index
                required property int number
                required property url frameUrl
                required property string timeLabel
                width: 144
                height: filmstrip.height - 4
                padding: 4
                highlighted: index === mediaClip.currentFrame
                onClicked: mediaClip.seekToFrame(index)
                Accessible.name: qsTr("Frame %1 at %2").arg(number).arg(timeLabel)

                contentItem: ColumnLayout {
                    spacing: 3
                    Image {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        source: frameUrl
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                        smooth: false
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("Frame %1").arg(number)
                            font.pixelSize: 11
                            Layout.fillWidth: true
                        }
                        Label {
                            text: timeLabel
                            font.pixelSize: 10
                            color: palette.placeholderText
                        }
                    }
                }
            }

            ScrollBar.horizontal: ScrollBar {}
        }
    }
}
