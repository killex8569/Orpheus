import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1280
    height: 820
    minimumWidth: 860
    minimumHeight: 540
    visible: true
    title: backend.hasTrack ? backend.title + " — Orpheus" : "Orpheus"
    color: OTheme.bg
    font.family: OTheme.font
    font.pixelSize: 14

    // Palette sombre pour les contrôles standard (menus, info-bulles, listes déroulantes).
    palette {
        window: OTheme.panel2
        windowText: OTheme.text
        base: OTheme.panel2
        alternateBase: OTheme.panel
        text: OTheme.text
        button: OTheme.panel2
        buttonText: OTheme.text
        highlight: OTheme.accentStrong
        highlightedText: "white"
        toolTipBase: "#2a2a38"
        toolTipText: OTheme.text
        mid: OTheme.border
        dark: OTheme.border
        light: OTheme.hover
        midlight: OTheme.hover
        placeholderText: OTheme.muted
    }

    property string view: "search"
    property var playlistId: 0
    property string previousView: "search"
    property var previousPlaylistId: 0

    // Bouton « Paroles » : ouvre la vue, ou revient à la vue précédente.
    function toggleLyrics() {
        if (view === "lyrics")
            navigate(previousView, previousPlaylistId)
        else
            navigate("lyrics", 0)
    }

    function navigate(v, id) {
        if (v === "lyrics" && view !== "lyrics") {
            previousView = view
            previousPlaylistId = playlistId
        }
        if (v === "playlist")
            backend.openPlaylist(id)
        playlistId = id
        view = v
        if (v === "search")
            Qt.callLater(searchView.focusSearch)
    }

    // ---------- Dialogues ----------
    property string pendingList: ""
    property int pendingRow: -1
    property var renameId: 0

    function askNewPlaylist(list, row) {
        pendingList = list || ""
        pendingRow = row === undefined ? -1 : row
        renameId = 0
        nameDialog.title = "Nouvelle playlist"
        nameField.text = ""
        nameDialog.open()
    }
    function askRename(id, name) {
        renameId = id
        nameDialog.title = "Renommer la playlist"
        nameField.text = name
        nameDialog.open()
    }
    function askDelete(id, name) {
        deleteDialog.playlistId = id
        deleteDialog.name = name
        deleteDialog.open()
    }

    Dialog {
        id: nameDialog
        anchors.centerIn: parent
        modal: true
        width: 380
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: nameField.forceActiveFocus()
        background: Rectangle { radius: 12; color: OTheme.panel2; border.color: OTheme.border }
        header: Text { text: nameDialog.title; color: OTheme.text; font.pixelSize: 17; font.weight: Font.DemiBold; padding: 18; bottomPadding: 0 }
        TextField {
            id: nameField
            width: parent.width
            placeholderText: "Nom de la playlist"
            color: OTheme.text
            selectByMouse: true
            onAccepted: nameDialog.accept()
            background: Rectangle { radius: 8; color: OTheme.panel; border.color: nameField.activeFocus ? OTheme.accent : OTheme.border }
        }
        onAccepted: {
            if (nameField.text.trim() === "") return
            if (win.renameId) {
                backend.renamePlaylist(win.renameId, nameField.text)
                backend.openPlaylist(win.renameId)
            } else {
                const id = backend.createPlaylist(nameField.text, win.pendingList, win.pendingRow)
                if (id && win.pendingRow < 0) win.navigate("playlist", id)
            }
        }
    }

    Dialog {
        id: deleteDialog
        property var playlistId: 0
        property string name
        anchors.centerIn: parent
        modal: true
        width: 400
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle { radius: 12; color: OTheme.panel2; border.color: OTheme.border }
        header: Text { text: "Supprimer la playlist ?"; color: OTheme.text; font.pixelSize: 17; font.weight: Font.DemiBold; padding: 18; bottomPadding: 0 }
        Text {
            width: parent.width
            text: "« " + deleteDialog.name + " » sera supprimée. Les fichiers restent dans le cache local."
            color: OTheme.muted
            wrapMode: Text.WordWrap
        }
        onAccepted: { backend.deletePlaylist(playlistId); win.navigate("search", 0) }
    }

    // ---------- Raccourcis ----------
    Shortcut { sequence: "Ctrl+K"; onActivated: win.navigate("search", 0) }
    // Dans une playlist, Ctrl+F cherche dans la playlist ; ailleurs, recherche en ligne.
    Shortcut {
        sequences: ["Ctrl+F"]
        onActivated: win.view === "playlist" ? playlistView.focusFilter() : win.navigate("search", 0)
    }
    // Les raccourcis à une touche ne s'appliquent pas pendant la saisie de texte.
    readonly property bool typing: activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit
    Shortcut { sequence: "Space"; enabled: !win.typing; onActivated: backend.togglePlay() }
    Shortcut { sequence: "Media Play"; enabled: !mprisActive; onActivated: backend.togglePlay() }
    Shortcut { sequence: "Media Next"; enabled: !mprisActive; onActivated: backend.next() }
    Shortcut { sequence: "Media Previous"; enabled: !mprisActive; onActivated: backend.prev() }
    Shortcut { sequence: "Shift+Right"; enabled: !win.typing; onActivated: backend.next() }
    Shortcut { sequence: "Shift+Left"; enabled: !win.typing; onActivated: backend.prev() }
    Shortcut { sequence: "Right"; enabled: !win.typing; onActivated: backend.seekRelative(10) }
    Shortcut { sequence: "Left"; enabled: !win.typing; onActivated: backend.seekRelative(-10) }
    Shortcut { sequence: "Up"; enabled: !win.typing; onActivated: backend.setVolume(backend.volume + 5) }
    Shortcut { sequence: "Down"; enabled: !win.typing; onActivated: backend.setVolume(backend.volume - 5) }
    Shortcut { sequence: "S"; enabled: !win.typing; onActivated: backend.toggleShuffle() }
    Shortcut { sequence: "R"; enabled: !win.typing; onActivated: backend.cycleRepeat() }
    Shortcut { sequence: "L"; enabled: !win.typing; onActivated: win.toggleLyrics() }

    // ---------- Disposition ----------
    RowLayout {
        anchors.fill: parent
        anchors.bottomMargin: player.height
        spacing: 0

        Sidebar {
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            view: win.view
            playlistId: win.playlistId
            onNavigate: (v, id) => win.navigate(v, id)
            onNewPlaylist: win.askNewPlaylist("", -1)
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 28
            Layout.bottomMargin: 8
            currentIndex: ["search", "playlist", "queue", "import", "settings", "lyrics", "stats", "jam", "updates"].indexOf(win.view)

            SearchView { id: searchView }
            PlaylistView { id: playlistView }
            QueueView {}
            ImportView {
                onOpenSettings: win.navigate("settings", 0)
                onOpenPlaylist: (id) => win.navigate("playlist", id)
            }
            SettingsView {}
            LyricsView {}
            StatsView {}
            JamView {}
            UpdatesView {}
        }
    }

    PlayerBar {
        id: player
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 92
        lyricsOpen: win.view === "lyrics"
        onToggleLyrics: win.toggleLyrics()
    }

    Toasts {
        id: toasts
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: player.top
        anchors.bottomMargin: 16
        width: parent.width
        z: 10
    }

    Connections {
        target: backend
        function onToast(message, error) { toasts.show(message, error) }
    }
}
