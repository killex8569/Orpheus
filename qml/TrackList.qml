import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Liste de morceaux virtualisée (seules les lignes visibles sont créées).
ListView {
    id: list
    property string listName              // search | local | playlist | queue
    property bool editable: false          // monter / descendre / retirer
    property string emptyText: "Aucun morceau"
    property bool fitContent: false        // hauteur = contenu (listes imbriquées)

    clip: true
    reuseItems: true
    cacheBuffer: 600
    boundsBehavior: Flickable.StopAtBounds
    interactive: !fitContent
    implicitHeight: fitContent ? contentHeight : 400
    ScrollBar.vertical: ScrollBar { policy: list.fitContent ? ScrollBar.AlwaysOff : ScrollBar.AsNeeded }
    // Liste imbriquée (fitContent) : la molette fait défiler la vue parente.
    WheelScroll { enabled: !list.fitContent }
    // Morceaux déjà dans la playlist cible grisés (résultats de recherche uniquement).
    readonly property bool showMarks: listName === "search" || listName === "local"

    readonly property var userPlaylists: backend.playlists.filter(p => p.kind !== "cache")

    function isCurrent(index, trackId) {
        if (listName === "queue")
            return index === backend.queueIndex
        return trackId > 0 && trackId === backend.currentTrackId
    }

    Text {
        anchors.centerIn: parent
        visible: list.count === 0
        text: list.emptyText
        color: OTheme.muted
        font.pixelSize: 14
    }

    delegate: ItemDelegate {
        id: row
        required property int index
        required property var trackId
        required property string title
        required property string artist
        required property string album
        required property string durationText
        required property string cover
        required property string trackState
        required property string error
        required property string quality
        required property string inPlaylist
        required property string extra
        readonly property bool current: list.isCurrent(index, trackId)
        readonly property bool marked: list.showMarks && inPlaylist !== "" && !current

        width: ListView.view.width
        height: 58
        hoverEnabled: true
        topPadding: 0
        bottomPadding: 0
        leftPadding: 4
        rightPadding: 6
        onDoubleClicked: backend.play(list.listName, index)

        background: Rectangle {
            radius: 8
            color: row.current ? OTheme.current : (row.hovered ? OTheme.panel2 : "transparent")
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: list.openMenu(row.index)
        }

        contentItem: RowLayout {
            spacing: 12
            opacity: row.marked && !row.hovered ? 0.4 : 1

            Item {
                Layout.leftMargin: 8
                Layout.preferredWidth: 24
                Layout.fillHeight: true
                Text {
                    anchors.centerIn: parent
                    visible: !row.current && !row.hovered
                    text: row.index + 1
                    color: OTheme.muted
                    font.pixelSize: 12
                }
                Icon {
                    anchors.centerIn: parent
                    visible: row.current || row.hovered
                    iconName: row.current && !backend.paused ? "pause" : "play"
                    size: 14
                    color: row.current ? OTheme.accent : OTheme.text
                    TapHandler {
                        onTapped: row.current ? backend.togglePlay() : backend.play(list.listName, row.index)
                    }
                }
            }
            Rectangle {
                Layout.preferredWidth: 42
                Layout.preferredHeight: 42
                radius: 6
                color: OTheme.hover
                Image {
                    anchors.fill: parent
                    source: row.cover
                    sourceSize: Qt.size(84, 84)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 3
                spacing: 2
                // Titre + badge positionnés à la main (pas de largeur implicite :
                // évite les boucles de mise en page).
                Item {
                    id: titleRow
                    Layout.fillWidth: true
                    implicitHeight: titleText.implicitHeight
                    Text {
                        id: titleText
                        width: Math.min(implicitWidth, titleRow.width - (badgeBox.visible ? badgeBox.width + 8 : 0))
                        text: row.title
                        color: row.current ? OTheme.accent : OTheme.text
                        font.pixelSize: 14
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        id: badgeBox
                        readonly property var info: ({
                            "ready": ["local", OTheme.ok],
                            "queued": ["en file", OTheme.accent2],
                            "downloading": ["téléchargement", OTheme.accent2],
                            "failed": ["échec", OTheme.err]
                        })[row.trackState]
                        visible: info !== undefined
                        x: titleText.width + 8
                        anchors.verticalCenter: titleText.verticalCenter
                        width: badge.implicitWidth + 12
                        height: 17
                        radius: 9
                        color: Qt.rgba(info ? info[1].r : 0, info ? info[1].g : 0, info ? info[1].b : 0, 0.14)
                        Text {
                            id: badge
                            anchors.centerIn: parent
                            text: parent.info ? parent.info[0] : ""
                            color: parent.info ? parent.info[1] : OTheme.muted
                            font.pixelSize: 10
                        }
                        HoverHandler { id: badgeHover }
                        ToolTip.visible: badgeHover.hovered && (row.error !== "" || row.quality !== "")
                        ToolTip.text: row.trackState === "failed" ? row.error : row.quality
                    }
                }
                Text {
                    text: row.artist + (!row.marked ? ""
                          : row.inPlaylist === "same" ? "  ·  ✓ déjà dans « " + backend.targetPlaylistName + " »"
                          : "  ·  ✓ une version est déjà dans « " + backend.targetPlaylistName + " »")
                    color: OTheme.muted
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            Text {
                visible: row.extra !== ""
                text: row.extra
                color: OTheme.muted
                font.pixelSize: 12
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 150
            }
            Text {
                visible: list.width > 820 && row.extra === ""
                text: row.album
                color: OTheme.muted
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.preferredWidth: 2
            }
            Text {
                text: row.durationText
                color: OTheme.muted
                font.pixelSize: 13
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 48
            }
            IconButton {
                iconName: "more"
                tip: "Plus d'actions"
                opacity: row.hovered || row.current ? 1 : 0
                Layout.rightMargin: 6
                onClicked: list.openMenu(row.index)
            }
        }
    }

    // ---------- Menu contextuel ----------
    property int menuRow: -1
    property var menuState: ""
    property var menuMarks: ({})    // {id de playlist: "same" | "similar"}

    function openMenu(row) {
        menuRow = row
        const item = itemAtIndex(row)
        menuState = item ? item.trackState : ""
        menuMarks = backend.playlistMarks(listName, row)
        menu.popup()
    }

    Menu {
        id: menu
        MenuItem { text: "Lire"; onTriggered: backend.play(list.listName, list.menuRow) }
        MenuItem { text: "Lire ensuite"; visible: list.listName !== "queue"; height: visible ? implicitHeight : 0
            onTriggered: backend.trackAction(list.listName, list.menuRow, "next") }
        MenuItem { text: "Ajouter à la file"; visible: list.listName !== "queue"; height: visible ? implicitHeight : 0
            onTriggered: backend.trackAction(list.listName, list.menuRow, "enqueue") }
        Menu {
            id: playlistMenu
            title: "Ajouter à une playlist"
            Instantiator {
                model: list.userPlaylists
                delegate: MenuItem {
                    required property var modelData
                    readonly property string mark: list.menuMarks[String(modelData.id)] || ""
                    // Grisé si le morceau (ou une autre version du même titre) y est déjà.
                    enabled: mark === ""
                    text: modelData.name + (mark === "same" ? "   ✓ déjà dedans"
                                            : mark === "similar" ? "   ✓ une version y est déjà" : "")
                    onTriggered: backend.trackAction(list.listName, list.menuRow, "addTo", modelData.id)
                }
                onObjectAdded: (i, o) => playlistMenu.insertItem(i, o)
                onObjectRemoved: (i, o) => playlistMenu.removeItem(o)
            }
            MenuSeparator {}
            MenuItem {
                text: "Nouvelle playlist…"
                onTriggered: ApplicationWindow.window.askNewPlaylist(list.listName, list.menuRow)
            }
        }
        MenuSeparator {}
        MenuItem {
            text: list.menuState === "ready" ? "Supprimer le fichier local" : "Télécharger en local"
            enabled: list.menuState !== "downloading" && list.menuState !== "queued"
            onTriggered: backend.trackAction(list.listName, list.menuRow, list.menuState === "ready" ? "deleteFile" : "download")
        }
        MenuItem { text: "Copier le lien YouTube"; onTriggered: backend.trackAction(list.listName, list.menuRow, "copyLink") }
        MenuSeparator { visible: list.editable }
        MenuItem { text: "Monter"; visible: list.editable; height: visible ? implicitHeight : 0
            onTriggered: backend.trackAction(list.listName, list.menuRow, "moveUp") }
        MenuItem { text: "Descendre"; visible: list.editable; height: visible ? implicitHeight : 0
            onTriggered: backend.trackAction(list.listName, list.menuRow, "moveDown") }
        MenuItem {
            text: list.listName === "queue" ? "Retirer de la file" : "Retirer de la playlist"
            visible: list.listName === "queue" || list.listName === "playlist"
            height: visible ? implicitHeight : 0
            onTriggered: backend.trackAction(list.listName, list.menuRow,
                                             list.listName === "queue" ? "removeFromQueue" : "removeFromPlaylist")
        }
    }
}
