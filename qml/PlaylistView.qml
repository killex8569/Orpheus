import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    readonly property var p: backend.currentPlaylist
    readonly property bool isCache: p.kind === "cache"

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        ColumnLayout {
            spacing: 2
            Text {
                text: ({
                    "cache": "Morceaux écoutés, conservés en local — consultée avant toute recherche en ligne",
                    "youtube": "Importée depuis YouTube",
                    "spotify": "Importée depuis Spotify"
                })[root.p.kind] || "Playlist locale"
                color: OTheme.muted
                font.pixelSize: 12
            }
            Text { text: root.p.name || ""; color: OTheme.text; font.pixelSize: 28; font.weight: Font.Bold
                   elide: Text.ElideRight; Layout.fillWidth: true }
            Text {
                text: (root.p.trackCount || 0) + " morceaux · " + (root.p.durationText || "0:00") + " · "
                      + (root.p.readyCount || 0) + " en local"
                color: OTheme.muted
                font.pixelSize: 14
            }
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

        TrackList {
            Layout.fillWidth: true
            Layout.fillHeight: true
            listName: "playlist"
            editable: !root.isCache
            model: backend.playlistTracks
            emptyText: root.isCache ? "Les morceaux que vous écoutez apparaîtront ici."
                                    : "Playlist vide — ajoutez des morceaux depuis la recherche (clic droit → Ajouter à une playlist)."
        }
    }
}
