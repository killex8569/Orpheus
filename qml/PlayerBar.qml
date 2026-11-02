import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: OTheme.panel
    property bool lyricsOpen: false
    signal toggleLyrics()

    function fmt(s) {
        if (!(s > 0)) return "0:00"
        s = Math.floor(s)
        const m = Math.floor(s / 60), sec = s % 60
        return m + ":" + (sec < 10 ? "0" : "") + sec
    }

    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: OTheme.border }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 20
        spacing: 16

        // ---------- Morceau en cours ----------
        RowLayout {
            Layout.preferredWidth: root.width * 0.28
            Layout.maximumWidth: root.width * 0.28
            spacing: 12
            Rectangle {
                Layout.preferredWidth: 58
                Layout.preferredHeight: 58
                radius: 8
                color: OTheme.hover
                visible: backend.hasTrack
                Image {
                    anchors.fill: parent
                    source: backend.cover
                    sourceSize: Qt.size(116, 116)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
            }
            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true
                Text {
                    text: backend.hasTrack ? backend.title : "Rien en lecture"
                    color: backend.hasTrack ? OTheme.text : OTheme.muted
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: backend.artist
                    color: OTheme.muted
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    visible: text !== ""
                    text: backend.status !== "" ? backend.status + (backend.cacheProgress > 0 ? " " + Math.round(backend.cacheProgress) + " %" : "")
                          : backend.source === "local" ? "local"
                          : backend.source === "stream" ? "streaming" : ""
                    color: backend.status !== "" ? OTheme.muted : backend.source === "local" ? OTheme.ok : OTheme.warn
                    font.pixelSize: 11
                }
            }
        }

        // ---------- Contrôles ----------
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 14
                IconButton {
                    iconName: "shuffle"; iconSize: 17; active: backend.shuffle
                    tip: "Aléatoire (S)"; onClicked: backend.toggleShuffle()
                }
                IconButton { iconName: "prev"; iconSize: 18; color: OTheme.text; tip: "Précédent"; onClicked: backend.prev() }
                AbstractButton {
                    id: playBtn
                    implicitWidth: 42
                    implicitHeight: 42
                    hoverEnabled: true
                    focusPolicy: Qt.NoFocus
                    onClicked: backend.togglePlay()
                    background: Rectangle {
                        radius: 21
                        color: OTheme.text
                        scale: playBtn.hovered ? 1.06 : 1
                        Behavior on scale { NumberAnimation { duration: 100 } }
                    }
                    contentItem: Item {
                        Icon {
                            anchors.centerIn: parent
                            visible: !backend.loading
                            iconName: backend.paused ? "play" : "pause"
                            size: 18
                            color: "#111111"
                        }
                        Icon {
                            id: spinner
                            anchors.centerIn: parent
                            visible: backend.loading
                            iconName: "loader"
                            size: 20
                            color: "#111111"
                            RotationAnimator on rotation {
                                running: spinner.visible
                                from: 0; to: 360; duration: 900; loops: Animation.Infinite
                            }
                        }
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: "Lecture / pause (Espace)"
                    ToolTip.delay: 600
                }
                IconButton { iconName: "next"; iconSize: 18; color: OTheme.text; tip: "Suivant"; onClicked: backend.next() }
                IconButton {
                    iconName: backend.repeat === 2 ? "repeat-one" : "repeat"; iconSize: 17
                    active: backend.repeat !== 0
                    tip: ["Répétition désactivée", "Répéter la file", "Répéter le morceau"][backend.repeat] + " (R)"
                    onClicked: backend.cycleRepeat()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.maximumWidth: 680
                Layout.alignment: Qt.AlignHCenter
                spacing: 10
                Text {
                    text: root.fmt(seek.pressed ? seek.value : backend.position)
                    color: OTheme.muted
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignRight
                    Layout.preferredWidth: 40
                }
                Slider {
                    id: seek
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, backend.duration)
                    enabled: backend.source !== ""
                    // Ne suit pas la lecture pendant que l'utilisateur fait glisser.
                    Binding on value { value: backend.position; when: !seek.pressed }
                    onPressedChanged: if (!pressed) backend.seek(value)
                    background: Rectangle {
                        x: seek.leftPadding
                        y: seek.topPadding + seek.availableHeight / 2 - height / 2
                        width: seek.availableWidth
                        height: 4
                        radius: 2
                        color: OTheme.hover
                        Rectangle {
                            width: seek.visualPosition * parent.width
                            height: parent.height
                            radius: 2
                            color: seek.hovered || seek.pressed ? OTheme.accent : OTheme.text
                        }
                    }
                    handle: Rectangle {
                        x: seek.leftPadding + seek.visualPosition * (seek.availableWidth - width)
                        y: seek.topPadding + seek.availableHeight / 2 - height / 2
                        width: 12; height: 12; radius: 6
                        color: OTheme.text
                        visible: seek.hovered || seek.pressed
                    }
                }
                Text {
                    text: root.fmt(backend.duration)
                    color: OTheme.muted
                    font.pixelSize: 12
                    Layout.preferredWidth: 40
                }
            }
        }

        // ---------- Volume ----------
        RowLayout {
            Layout.preferredWidth: root.width * 0.24
            Layout.maximumWidth: root.width * 0.24
            spacing: 8
            Item { Layout.fillWidth: true }
            IconButton {
                iconName: "lyrics"
                iconSize: 17
                active: root.lyricsOpen
                tip: "Paroles (L)"
                onClicked: root.toggleLyrics()
            }
            Text {
                visible: backend.quality !== ""
                text: backend.quality
                color: OTheme.muted
                font.pixelSize: 11
            }
            IconButton {
                property real lastVolume: 80
                iconName: backend.volume < 1 ? "volume-mute" : "volume"
                iconSize: 17
                tip: "Couper le son"
                onClicked: {
                    if (backend.volume >= 1) { lastVolume = backend.volume; backend.setVolume(0) }
                    else backend.setVolume(lastVolume)
                }
            }
            Slider {
                id: vol
                Layout.preferredWidth: 120
                from: 0; to: 130
                Binding on value { value: backend.volume; when: !vol.pressed }
                onMoved: backend.setVolume(value)
                background: Rectangle {
                    x: vol.leftPadding
                    y: vol.topPadding + vol.availableHeight / 2 - height / 2
                    width: vol.availableWidth; height: 4; radius: 2
                    color: OTheme.hover
                    Rectangle {
                        width: vol.visualPosition * parent.width; height: parent.height; radius: 2
                        color: vol.hovered || vol.pressed ? OTheme.accent : OTheme.text
                    }
                }
                handle: Rectangle {
                    x: vol.leftPadding + vol.visualPosition * (vol.availableWidth - width)
                    y: vol.topPadding + vol.availableHeight / 2 - height / 2
                    width: 12; height: 12; radius: 6
                    color: OTheme.text
                    visible: vol.hovered || vol.pressed
                }
            }
        }
    }
}
