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

    // ---------- Éléments communs (ordinateur et téléphone) ----------
    component Cover: Rectangle {
        property int size: 58
        Layout.preferredWidth: size
        Layout.preferredHeight: size
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

    component TrackInfo: ColumnLayout {
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
            text: backend.jam.active ? "Jam · " + backend.jam.statusText
                  : backend.status !== "" ? backend.status + (backend.cacheProgress > 0 ? " " + Math.round(backend.cacheProgress) + " %" : "")
                  : backend.source === "local" ? "local"
                  : backend.source === "stream" ? "streaming" : ""
            color: backend.jam.active ? OTheme.accent
                 : backend.status !== "" ? OTheme.muted : backend.source === "local" ? OTheme.ok : OTheme.warn
            font.pixelSize: 11
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }

    component PlayButton: AbstractButton {
        id: playBtn
        implicitWidth: 42
        implicitHeight: 42
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        onClicked: backend.togglePlay()
        background: Rectangle {
            radius: playBtn.width / 2
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
        ToolTip.visible: hovered && !OTheme.mobile
        ToolTip.text: "Lecture / pause (Espace)"
        ToolTip.delay: 600
    }

    component BarSlider: Slider {
        id: bar
        background: Rectangle {
            x: bar.leftPadding
            y: bar.topPadding + bar.availableHeight / 2 - height / 2
            width: bar.availableWidth
            height: 4
            radius: 2
            color: OTheme.hover
            Rectangle {
                width: bar.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: bar.hovered || bar.pressed ? OTheme.accent : OTheme.text
            }
        }
        handle: Rectangle {
            x: bar.leftPadding + bar.visualPosition * (bar.availableWidth - width)
            y: bar.topPadding + bar.availableHeight / 2 - height / 2
            width: 12; height: 12; radius: 6
            color: OTheme.text
            visible: bar.hovered || bar.pressed || OTheme.mobile
        }
    }

    // Temps écoulé, barre de progression, durée.
    component SeekRow: RowLayout {
        spacing: 10
        Text {
            text: root.fmt(seek.pressed ? seek.value : backend.position)
            color: OTheme.muted
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: 40
        }
        BarSlider {
            id: seek
            Layout.fillWidth: true
            from: 0
            to: Math.max(1, backend.duration)
            enabled: backend.source !== ""
            // Ne suit pas la lecture pendant que l'utilisateur fait glisser.
            Binding on value { value: backend.position; when: !seek.pressed }
            onPressedChanged: if (!pressed) backend.seek(value)
        }
        Text {
            text: root.fmt(backend.duration)
            color: OTheme.muted
            font.pixelSize: 12
            Layout.preferredWidth: 40
        }
    }

    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: OTheme.border }

    // ---------- Ordinateur : une ligne ----------
    RowLayout {
        visible: !OTheme.mobile
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 20
        spacing: 16

        // Morceau en cours
        RowLayout {
            Layout.preferredWidth: root.width * 0.28
            Layout.maximumWidth: root.width * 0.28
            spacing: 12
            Cover {}
            TrackInfo {}
        }

        // Contrôles
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 14
                IconButton {
                    iconName: "shuffle"; iconSize: 17; active: backend.shuffle
                    enabled: !backend.jam.active; opacity: enabled ? 1 : 0.3
                    tip: "Aléatoire (S)"; onClicked: backend.toggleShuffle()
                }
                IconButton { iconName: "prev"; iconSize: 18; color: OTheme.text; tip: "Précédent"; onClicked: backend.prev() }
                PlayButton {}
                IconButton { iconName: "next"; iconSize: 18; color: OTheme.text; tip: "Suivant"; onClicked: backend.next() }
                IconButton {
                    iconName: backend.repeat === 2 ? "repeat-one" : "repeat"; iconSize: 17
                    active: backend.repeat !== 0
                    enabled: !backend.jam.active; opacity: enabled ? 1 : 0.3
                    tip: ["Répétition désactivée", "Répéter la file", "Répéter le morceau"][backend.repeat] + " (R)"
                    onClicked: backend.cycleRepeat()
                }
            }
            SeekRow {
                Layout.fillWidth: true
                Layout.maximumWidth: 680
                Layout.alignment: Qt.AlignHCenter
            }
        }

        // Volume
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
            BarSlider {
                id: vol
                Layout.preferredWidth: 120
                from: 0; to: 130
                Binding on value { value: backend.volume; when: !vol.pressed }
                onMoved: backend.setVolume(value)
            }
        }
    }

    // ---------- Téléphone : morceau et commandes, puis progression ----------
    // (le volume passe par les boutons du téléphone)
    ColumnLayout {
        visible: OTheme.mobile
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        anchors.topMargin: 10
        anchors.bottomMargin: 4
        spacing: 2
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Cover { size: 46 }
            TrackInfo {}
            IconButton { iconName: "lyrics"; iconSize: 18; active: root.lyricsOpen; onClicked: root.toggleLyrics() }
            IconButton { iconName: "prev"; iconSize: 20; color: OTheme.text; onClicked: backend.prev() }
            PlayButton { implicitWidth: 40; implicitHeight: 40 }
            IconButton { iconName: "next"; iconSize: 20; color: OTheme.text; onClicked: backend.next() }
        }
        SeekRow { Layout.fillWidth: true }
    }
}
