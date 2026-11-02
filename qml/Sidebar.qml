import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property string view
    property var playlistId
    signal navigate(string view, var id)
    signal newPlaylist()

    color: OTheme.panel

    component NavItem: ItemDelegate {
        id: item
        property string iconName
        property string label
        property bool active
        property int count: -1
        property bool dot: false   // pastille (ex. mise à jour disponible)
        Layout.fillWidth: true
        width: ListView.view ? ListView.view.width : implicitWidth
        height: 38
        hoverEnabled: true
        leftPadding: 12
        rightPadding: 12
        background: Rectangle {
            radius: 8
            color: item.active ? OTheme.panel2 : (item.hovered ? OTheme.hover : "transparent")
        }
        contentItem: RowLayout {
            spacing: 12
            Icon {
                iconName: item.iconName
                size: 17
                color: item.active || item.hovered ? OTheme.text : OTheme.muted
            }
            Text {
                text: item.label
                color: item.active || item.hovered ? OTheme.text : OTheme.muted
                font.pixelSize: 14
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Rectangle {
                visible: item.dot
                width: 8; height: 8; radius: 4
                color: OTheme.accent
            }
            Text {
                visible: item.count >= 0
                text: item.count
                color: OTheme.muted
                font.pixelSize: 11
            }
        }
    }

    // En-tête de section repliable (clic sur le titre), état mémorisé dans les réglages.
    component SectionHeader: Item {
        id: sh
        property string label
        property string settingKey
        property bool collapsed: backend.settingBool(settingKey, false)
        default property alias extra: extraRow.data
        Layout.fillWidth: true
        Layout.leftMargin: 6
        Layout.bottomMargin: 2
        implicitHeight: 28

        function toggle() {
            collapsed = !collapsed
            backend.setSetting(settingKey, collapsed ? "true" : "false")
        }

        RowLayout {
            anchors.fill: parent
            spacing: 4
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                HoverHandler { id: shHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: sh.toggle() }
                RowLayout {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Icon {
                        iconName: "chevron-down"
                        size: 12
                        rotation: sh.collapsed ? -90 : 0
                        color: shHover.hovered ? OTheme.text : OTheme.muted
                        Behavior on rotation { NumberAnimation { duration: 120 } }
                    }
                    Text {
                        text: sh.label
                        color: shHover.hovered ? OTheme.text : OTheme.muted
                        font.pixelSize: 11
                        font.letterSpacing: 1
                        font.weight: Font.DemiBold
                    }
                }
            }
            Row { id: extraRow }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 2

        RowLayout {
            Layout.leftMargin: 8
            Layout.topMargin: 6
            Layout.bottomMargin: 18
            spacing: 10
            Image { source: OTheme.icon("orpheus"); sourceSize: Qt.size(56, 56)
                    Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
            Text { text: "Orpheus"; color: OTheme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
        }

        // ---------- Section « Musique » (repliable) ----------
        SectionHeader {
            id: musicHeader
            label: "MUSIQUE"
            settingKey: "sidebar_music_collapsed"
        }
        // Repliée, une section garde visibles l'élément ouvert et ceux qui signalent
        // quelque chose (Jam en cours, mise à jour disponible), comme sur Discord.
        NavItem { iconName: "search"; label: "Rechercher"; active: root.view === "search"
                  visible: !musicHeader.collapsed || active; onClicked: root.navigate("search", 0) }
        NavItem { iconName: "queue"; label: "File d'attente"; active: root.view === "queue"
                  visible: !musicHeader.collapsed || active; onClicked: root.navigate("queue", 0) }
        NavItem { iconName: "lyrics"; label: "Paroles"; active: root.view === "lyrics"
                  visible: !musicHeader.collapsed || active; onClicked: root.navigate("lyrics", 0) }
        NavItem {
            iconName: "jam"; label: "Jam"; active: root.view === "jam"
            count: backend.jam.active ? backend.jam.members.length : -1
            visible: !musicHeader.collapsed || active || backend.jam.active
            onClicked: root.navigate("jam", 0)
        }
        NavItem { iconName: "stats"; label: "Statistiques"; active: root.view === "stats"
                  visible: !musicHeader.collapsed || active; onClicked: root.navigate("stats", 0) }
        NavItem { iconName: "download"; label: "Importer"; active: root.view === "import"
                  visible: !musicHeader.collapsed || active; onClicked: root.navigate("import", 0) }
        NavItem { iconName: "settings"; label: "Paramètres"; active: root.view === "settings"
                  visible: !musicHeader.collapsed || active; onClicked: root.navigate("settings", 0) }
        NavItem {
            iconName: "update"; label: "Mises à jour"; active: root.view === "updates"
            dot: backend.updates.updateAvailable
            visible: !musicHeader.collapsed || active || dot
            onClicked: root.navigate("updates", 0)
        }

        // ---------- Section « Playlists » (repliable, réordonnable) ----------
        SectionHeader {
            id: playlistsHeader
            Layout.topMargin: 14
            label: "PLAYLISTS"
            settingKey: "sidebar_playlists_collapsed"
            IconButton { iconName: "plus"; iconSize: 15; tip: "Nouvelle playlist"; onClicked: root.newPlaylist() }
        }

        ListView {
            id: plList
            // Glisser-déposer : position de départ et point d'insertion (avant l'élément « dropAt »).
            property int dragFrom: -1
            property int dropAt: -1
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: backend.playlists
            boundsBehavior: Flickable.StopAtBounds
            // Pas de défilement par glisser : il volerait le geste du glisser-déposer.
            // La molette (WheelScroll) et la barre de défilement restent disponibles.
            interactive: false
            ScrollBar.vertical: ScrollBar {}
            WheelScroll {}

            // Point d'insertion sous le pointeur.
            function updateDrop(scenePos) {
                const p = mapFromItem(null, scenePos.x, scenePos.y)
                const y = p.y + contentY
                let idx = indexAt(10, y)
                if (idx < 0) {
                    idx = p.y < 0 ? 0 : count
                } else {
                    const it = itemAtIndex(idx)
                    if (it && y > it.y + it.height / 2)
                        idx += 1
                }
                dropAt = Math.max(0, idx)
            }

            delegate: Item {
                id: slot
                required property var modelData
                required property int index
                readonly property bool active: root.view === "playlist" && root.playlistId === modelData.id
                readonly property bool shown: !playlistsHeader.collapsed || active
                width: ListView.view.width
                height: shown ? 38 : 0
                visible: shown
                opacity: plList.dragFrom === index ? 0.45 : 1

                NavItem {
                    anchors.fill: parent
                    iconName: ({ "cache": "disk", "youtube": "youtube", "spotify": "spotify" })[slot.modelData.kind] || "music"
                    label: slot.modelData.name
                    count: slot.modelData.trackCount
                    active: slot.active
                    onClicked: root.navigate("playlist", slot.modelData.id)

                    // Les gestionnaires sont sur la ligne elle-même : posés sur son conteneur,
                    // ils ne verraient jamais l'appui, déjà pris par le bouton.
                    DragHandler {
                        enabled: !playlistsHeader.collapsed
                        target: null
                        xAxis.enabled: false
                        onActiveChanged: {
                            if (active) {
                                plList.dragFrom = slot.index
                                plList.updateDrop(centroid.scenePosition)
                                return
                            }
                            const id = slot.modelData.id, from = plList.dragFrom, at = plList.dropAt
                            plList.dragFrom = -1
                            plList.dropAt = -1
                            if (from >= 0 && at >= 0 && at !== from && at !== from + 1)
                                backend.movePlaylist(id, at > from ? at - 1 : at)
                        }
                        onCentroidChanged: if (active) plList.updateDrop(centroid.scenePosition)
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: plMenu.popup()
                    }
                }

                // Repère d'insertion pendant un glisser-déposer.
                Rectangle {
                    readonly property bool before: plList.dropAt === slot.index
                    readonly property bool after: plList.dropAt === plList.count && slot.index === plList.count - 1
                    visible: plList.dragFrom >= 0 && (before || after)
                             && plList.dropAt !== plList.dragFrom && plList.dropAt !== plList.dragFrom + 1
                    x: 8; width: parent.width - 16; height: 2; radius: 1
                    y: before ? -2 : parent.height
                    color: OTheme.accent
                }

                Menu {
                    id: plMenu
                    MenuItem { text: "Monter"; enabled: slot.index > 0
                               onTriggered: backend.movePlaylist(slot.modelData.id, slot.index - 1) }
                    MenuItem { text: "Descendre"; enabled: slot.index < plList.count - 1
                               onTriggered: backend.movePlaylist(slot.modelData.id, slot.index + 1) }
                }
            }
        }

        // Espace disque de la playlist actuelle : celle affichée, sinon celle en
        // lecture, sinon la dernière ouverte.
        Rectangle {
            id: storage
            readonly property var pl: {
                const id = root.view === "playlist" ? root.playlistId
                         : backend.playingPlaylistId > 0 ? backend.playingPlaylistId
                         : (backend.currentPlaylist.id || 0)
                return backend.playlists.find(p => p.id === id) || null
            }
            readonly property real ratio: pl && pl.trackCount > 0 ? pl.readyCount / pl.trackCount : 0
            visible: pl !== null
            Layout.fillWidth: true
            Layout.topMargin: 6
            implicitHeight: storageCol.implicitHeight + 22
            radius: 8
            color: OTheme.panel2
            ToolTip.visible: storageHover.hovered
            ToolTip.delay: 400
            ToolTip.text: "Espace occupé sur le disque par les morceaux de cette playlist téléchargés en local."
            HoverHandler { id: storageHover }
            ColumnLayout {
                id: storageCol
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 11 }
                spacing: 5
                RowLayout {
                    spacing: 8
                    Icon { iconName: "disk"; size: 14; color: OTheme.muted }
                    Text {
                        text: storage.pl ? storage.pl.name : ""
                        color: OTheme.muted; font.pixelSize: 11
                        elide: Text.ElideRight; Layout.fillWidth: true
                    }
                }
                Text {
                    text: storage.pl ? storage.pl.diskSizeText : ""
                    color: OTheme.text; font.pixelSize: 17; font.weight: Font.DemiBold
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 4
                    radius: 2
                    color: OTheme.hover
                    Rectangle {
                        width: parent.width * storage.ratio
                        height: parent.height
                        radius: 2
                        color: storage.ratio >= 1 ? OTheme.ok : OTheme.accent
                    }
                }
                Text {
                    text: storage.pl ? storage.pl.readyCount + " / " + storage.pl.trackCount + " morceaux en local" : ""
                    color: OTheme.muted; font.pixelSize: 11
                }
            }
        }

        // Téléchargements en cours.
        Rectangle {
            visible: backend.downloadsCount > 0
            Layout.fillWidth: true
            implicitHeight: dlText.implicitHeight + 20
            radius: 8
            color: OTheme.panel2
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10
                Icon {
                    id: dlSpin
                    iconName: "loader"; size: 16; color: OTheme.accent2
                    RotationAnimator on rotation { running: dlSpin.visible; from: 0; to: 360; duration: 900; loops: Animation.Infinite }
                }
                ColumnLayout {
                    id: dlText
                    Layout.fillWidth: true
                    spacing: 1
                    Repeater {
                        model: backend.downloadsText.split("\n")
                        delegate: Text {
                            required property string modelData
                            required property int index
                            text: modelData
                            color: index === 0 ? OTheme.text : OTheme.muted
                            font.pixelSize: index === 0 ? 12 : 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }
    }
}
