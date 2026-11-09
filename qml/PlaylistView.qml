import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root
    readonly property var p: backend.currentPlaylist
    readonly property bool isCache: p.kind === "cache"
    readonly property bool filtering: backend.playlistFilter !== ""
    function focusFilter() { filterField.forceActiveFocus(); filterField.selectAll() }

    // Nouvelle playlist ouverte : la recherche repart de zéro.
    // (currentPlaylist change aussi à chaque téléchargement : on compare l'id.)
    property var shownId: 0
    onPChanged: if (p.id !== shownId) { shownId = p.id; filterTimer.stop(); filterField.text = "" }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true  // sinon une ligne longue élargit toute la vue (écran étroit)
            Text {
                text: ({
                    "cache": "Morceaux écoutés, conservés en local — consultée avant toute recherche en ligne",
                    "youtube": "Importée depuis YouTube",
                    "spotify": "Importée depuis Spotify"
                })[root.p.kind] || "Playlist locale"
                color: OTheme.muted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Text { text: root.p.name || ""; color: OTheme.text; font.pixelSize: 28; font.weight: Font.Bold
                   elide: Text.ElideRight; Layout.fillWidth: true }
            Text {
                text: (root.p.trackCount || 0) + " morceaux · " + (root.p.durationText || "0:00") + " · "
                      + (root.p.readyCount || 0) + " en local · " + (root.p.diskSizeText || "0 octet")
                color: OTheme.muted
                font.pixelSize: 14
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            // Dossier des fichiers de la playlist (« Cache local » : toujours la bibliothèque).
            RowLayout {
                visible: !root.isCache && !OTheme.mobile
                Layout.topMargin: 6
                Layout.fillWidth: true
                spacing: 8
                Icon { iconName: "disk"; size: 14; color: OTheme.muted }
                Text {
                    readonly property string path: Qt.platform.os === "windows"
                                                    ? (root.p.folder || "").replace(/\//g, "\\") : (root.p.folder || "")
                    text: root.p.folder ? "Dossier : " + path : "Dossier : bibliothèque d'Orpheus (par défaut)"
                    color: OTheme.muted
                    font.pixelSize: 13
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: 460
                    ToolTip.visible: folderHover.hovered && root.p.folder
                    ToolTip.text: path
                    ToolTip.delay: 400
                    HoverHandler { id: folderHover }
                }
                PillButton { text: "Choisir un dossier…"; implicitHeight: 28; onClicked: folderDialog.open() }
                PillButton {
                    text: "Ouvrir"; implicitHeight: 28
                    onClicked: Qt.openUrlExternally(backend.folderUrl(root.p.folder || ""))
                }
                PillButton {
                    text: "Par défaut"; implicitHeight: 28
                    visible: !!root.p.folder
                    onClicked: backend.setPlaylistFolder(root.p.id, "")
                }
            }
        }

        FolderDialog {
            id: folderDialog
            title: "Dossier des morceaux de « " + (root.p.name || "") + " »"
            currentFolder: backend.folderUrl(root.p.folder || "")
            onAccepted: backend.setPlaylistFolder(root.p.id, selectedFolder.toString())
        }

        Flow {
            Layout.fillWidth: true
            spacing: 8
            // « Lire » respecte le mode aléatoire ; « Aléatoire » est un interrupteur :
            // activé, toute la playlist est mélangée ; désactivé, l'ordre d'origine revient.
            PillButton { text: "Lire"; iconName: "play"; primary: true; onClicked: backend.playPlaylist(root.p.id, backend.shuffle) }
            PillButton {
                text: "Aléatoire"; iconName: "shuffle"
                checked: backend.shuffle
                onClicked: backend.shufflePlaylist(root.p.id)
            }
            PillButton {
                text: "Tout télécharger"; iconName: "download"
                visible: (root.p.readyCount || 0) < (root.p.trackCount || 0)
                onClicked: backend.downloadPlaylist(root.p.id)
            }
            PillButton {
                text: "Synchroniser"; iconName: "sync"
                visible: (root.p.kind === "youtube" || root.p.kind === "spotify") && !(root.p.sourceRef || "").startsWith("pasted:")
                onClicked: backend.syncPlaylist(root.p.id)
            }
            PillButton {
                text: "Renommer"; iconName: "edit"; visible: !root.isCache
                onClicked: ApplicationWindow.window.askRename(root.p.id, root.p.name)
            }
            PillButton {
                text: "Supprimer"; iconName: "trash"; danger: true; visible: !root.isCache
                onClicked: ApplicationWindow.window.askDelete(root.p.id, root.p.name)
            }
        }

        // Recherche dans la playlist (titre, artiste, album).
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            visible: (root.p.trackCount || 0) > 0
            TextField {
                id: filterField
                Layout.fillWidth: true
                Layout.maximumWidth: 420
                implicitHeight: 38
                placeholderText: "Rechercher dans la playlist"
                placeholderTextColor: OTheme.muted
                color: OTheme.text
                font.pixelSize: 14
                leftPadding: 40
                rightPadding: clearBtn.visible ? 36 : 12
                selectByMouse: true
                onTextChanged: filterTimer.restart()
                onAccepted: backend.filterPlaylist(text)
                Keys.onEscapePressed: { text = ""; backend.filterPlaylist(""); focus = false }
                background: Rectangle {
                    radius: 19
                    color: OTheme.panel2
                    border.color: filterField.activeFocus ? OTheme.accent : OTheme.border
                    Icon { iconName: "search"; size: 16; color: OTheme.muted; anchors.verticalCenter: parent.verticalCenter; x: 14 }
                }
                IconButton {
                    id: clearBtn
                    visible: filterField.text !== ""
                    iconName: "close"; iconSize: 14; tip: "Effacer"
                    anchors.right: parent.right; anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: { filterField.text = ""; backend.filterPlaylist("") }
                }
                // Petite attente : la liste n'est pas recalculée à chaque lettre.
                Timer { id: filterTimer; interval: 150; onTriggered: backend.filterPlaylist(filterField.text) }
            }
            Text {
                visible: root.filtering
                text: backend.playlistTracks.count + " résultat" + (backend.playlistTracks.count > 1 ? "s" : "")
                color: OTheme.muted
                font.pixelSize: 13
            }
        }

        TrackList {
            Layout.fillWidth: true
            Layout.fillHeight: true
            listName: "playlist"
            // Pas de réordonnancement sur une liste filtrée (positions ambiguës).
            editable: !root.isCache && !root.filtering
            model: backend.playlistTracks
            emptyText: root.filtering ? "Aucun morceau ne correspond à « " + backend.playlistFilter + " »."
                     : root.isCache ? "Les morceaux que vous écoutez apparaîtront ici."
                                    : "Playlist vide — ajoutez des morceaux depuis la recherche (clic droit → Ajouter à une playlist)."
        }
    }
}
