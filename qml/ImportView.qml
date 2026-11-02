import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: scroll
    contentWidth: availableWidth
    clip: true
    signal openSettings()
    // Relu quand les réglages changent (accountsChanged met à jour ytdlpInfo).
    readonly property string ytBrowser: { backend.ytdlpInfo; return backend.setting("yt_cookies_browser") }
    signal openPlaylist(var id)

    component RemoteList: ColumnLayout {
        property var items: []
        spacing: 2
        Layout.fillWidth: true
        Repeater {
            model: parent.items
            delegate: ItemDelegate {
                id: rd
                required property var modelData
                Layout.fillWidth: true
                height: 44
                hoverEnabled: true
                background: Rectangle { radius: 6; color: rd.hovered ? OTheme.panel2 : "transparent" }
                contentItem: RowLayout {
                    Text { text: rd.modelData.title; color: OTheme.text; font.pixelSize: 14; elide: Text.ElideRight; Layout.fillWidth: true }
                    Text { visible: rd.modelData.count >= 0; text: rd.modelData.count + " titres"; color: OTheme.muted; font.pixelSize: 12 }
                    PillButton { text: "Importer"; implicitHeight: 30; onClicked: backend.importUrl(rd.modelData.url) }
                }
            }
        }
    }

    ColumnLayout {
        width: scroll.availableWidth
        spacing: 14
        WheelScroll { flickable: scroll.contentItem }

        ColumnLayout {
            spacing: 2
            Text { text: "Importer"; color: OTheme.text; font.pixelSize: 28; font.weight: Font.Bold }
            Text {
                text: "Les morceaux sont retrouvés sur YouTube, ajoutés au cache local et téléchargés."
                color: OTheme.muted; font.pixelSize: 14
            }
        }

        Card {
            title: "Depuis un lien"
            subtitle: "Playlist YouTube / YouTube Music, ou playlist / album Spotify (les playlists publiques fonctionnent même sans compte)."
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                TextField {
                    id: url
                    Layout.fillWidth: true
                    implicitHeight: 38
                    placeholderText: "https://open.spotify.com/playlist/…  ou  https://www.youtube.com/playlist?list=…"
                    placeholderTextColor: OTheme.muted
                    color: OTheme.text
                    selectByMouse: true
                    leftPadding: 14
                    background: Rectangle { radius: 8; color: OTheme.panel2; border.color: url.activeFocus ? OTheme.accent : OTheme.border }
                    onAccepted: go()
                    function go() { if (text.trim() !== "") { backend.importUrl(text); text = "" } }
                }
                PillButton { text: "Importer"; primary: true; onClicked: url.go() }
            }
        }

        Card {
            title: "Playlist Spotify privée : coller les morceaux"
            subtitle: "Sans compte ni jeton : dans l'application Spotify pour ordinateur, ouvrez la playlist, cliquez sur un morceau, "
                      + "faites <b>Ctrl+A</b> puis <b>Ctrl+C</b>, et collez ici (<b>Ctrl+V</b>). Les liens de morceaux restent publics "
                      + "même quand la playlist est privée. Recoller avec le même nom met la playlist à jour."
            TextField {
                id: pastedName
                Layout.fillWidth: true
                implicitHeight: 38
                placeholderText: "Nom de la playlist (ex. Ma playlist Spotify)"
                placeholderTextColor: OTheme.muted
                color: OTheme.text
                selectByMouse: true
                leftPadding: 14
                background: Rectangle { radius: 8; color: OTheme.panel2; border.color: pastedName.activeFocus ? OTheme.accent : OTheme.border }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                TextArea {
                    id: pasted
                    placeholderText: "https://open.spotify.com/track/…\nhttps://open.spotify.com/track/…"
                    placeholderTextColor: OTheme.muted
                    color: OTheme.text
                    selectByMouse: true
                    wrapMode: TextArea.WrapAnywhere
                    font.family: "monospace"
                    font.pixelSize: 12
                    background: Rectangle { radius: 8; color: OTheme.panel2; border.color: pasted.activeFocus ? OTheme.accent : OTheme.border }
                }
            }
            RowLayout {
                spacing: 10
                // Même motif que spotifyTrackIds() côté C++ (compte indicatif, doublons inclus).
                readonly property int found: (pasted.text.match(/track[:\/][A-Za-z0-9]{22}/g) || []).length
                Text {
                    text: parent.found > 0 ? parent.found + " lien(s) de morceau détecté(s)" : "Aucun lien détecté pour le moment"
                    color: OTheme.muted
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }
                PillButton {
                    text: "Importer ces morceaux"; primary: true
                    enabled: parent.found > 0
                    onClicked: {
                        backend.importTrackLinks(pasted.text, pastedName.text)
                        pasted.text = ""
                    }
                }
            }
        }

        Card {
            title: "Mon compte YouTube"
            subtitle: scroll.ytBrowser === ""
                      ? "Choisissez dans les paramètres le navigateur dans lequel vous êtes connecté à YouTube."
                      : "Vos playlists et titres « J'aime », lus via les cookies de " + scroll.ytBrowser + "."
            RowLayout {
                spacing: 10
                PillButton {
                    text: scroll.ytBrowser === "" ? "Ouvrir les paramètres" : "Afficher mes playlists"
                    onClicked: scroll.ytBrowser === "" ? scroll.openSettings() : backend.loadRemote("youtube")
                }
                Spinner { visible: backend.remoteLoading }
            }
            RemoteList { items: backend.remoteYoutube }
        }

        Card {
            title: "Mon compte Spotify"
            subtitle: backend.spotifyConnected
                      ? "Connecté" + (backend.spotifyUser ? " en tant que " + backend.spotifyUser : "") + "."
                      : "Connectez votre compte dans les paramètres pour vos playlists privées et titres likés."
            PillButton {
                text: backend.spotifyConnected ? "Afficher mes playlists" : "Ouvrir les paramètres"
                onClicked: backend.spotifyConnected ? backend.loadRemote("spotify") : scroll.openSettings()
            }
            RemoteList { items: backend.remoteSpotify }
        }
        Text {
            visible: backend.remoteError !== ""
            text: backend.remoteError
            color: OTheme.err
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.topMargin: 8
            Text { text: "Imports"; color: OTheme.text; font.pixelSize: 17; font.weight: Font.DemiBold; Layout.fillWidth: true }
            PillButton { text: "Effacer l'historique"; implicitHeight: 30; visible: backend.jobs.length > 0; onClicked: backend.clearJobs() }
        }
        Text { visible: backend.jobs.length === 0; text: "Aucun import pour le moment."; color: OTheme.muted }

        Repeater {
            model: backend.jobs
            delegate: Rectangle {
                id: job
                required property var modelData
                readonly property real frac: modelData.status === "done" ? 1
                                             : modelData.total > 0 ? (modelData.done + modelData.failed) / modelData.total : 0
                Layout.fillWidth: true
                implicitHeight: jobCol.implicitHeight + 24
                radius: 8
                color: OTheme.panel
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 16
                    ColumnLayout {
                        id: jobCol
                        Layout.fillWidth: true
                        spacing: 3
                        RowLayout {
                            spacing: 8
                            Icon { iconName: job.modelData.source === "spotify" ? "spotify" : "youtube"; size: 15; color: OTheme.muted }
                            Text { text: job.modelData.name; color: OTheme.text; font.pixelSize: 14; elide: Text.ElideRight; Layout.fillWidth: true }
                        }
                        Text {
                            text: ({ "pending": "En attente", "running": "En cours", "done": "Terminé", "error": "Erreur",
                                     "interrupted": "Interrompu" })[job.modelData.status] + " · " + job.modelData.done + "/"
                                  + job.modelData.total + " importés"
                                  + (job.modelData.failed > 0 ? " · " + job.modelData.failed + " introuvables" : "")
                            color: OTheme.muted
                            font.pixelSize: 12
                        }
                        Text {
                            visible: job.modelData.message !== ""
                            text: job.modelData.message
                            color: job.modelData.status === "error" ? OTheme.err : OTheme.muted
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            maximumLineCount: 4
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    Rectangle {
                        Layout.preferredWidth: 180
                        implicitHeight: 6
                        radius: 3
                        color: OTheme.panel2
                        Rectangle {
                            width: parent.width * job.frac
                            height: parent.height
                            radius: 3
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: OTheme.accent }
                                GradientStop { position: 1; color: OTheme.accent2 }
                            }
                            Behavior on width { NumberAnimation { duration: 300 } }
                        }
                    }
                    PillButton {
                        visible: job.modelData.playlistId > 0
                        text: "Ouvrir"
                        implicitHeight: 30
                        onClicked: scroll.openPlaylist(job.modelData.playlistId)
                    }
                }
            }
        }
        Item { implicitHeight: 20 }
    }
}
