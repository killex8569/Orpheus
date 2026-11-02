import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: scroll
    contentWidth: availableWidth
    clip: true

    component Toggle: RowLayout {
        id: tg
        property string key
        property bool def
        property string label
        property string help
        spacing: 14
        Layout.fillWidth: true
        Switch {
            id: sw
            checked: backend.settingBool(tg.key, tg.def)
            onToggled: backend.setSetting(tg.key, checked ? "true" : "false")
            Layout.alignment: Qt.AlignTop
            indicator: Rectangle {
                implicitWidth: 40; implicitHeight: 22; radius: 11
                y: sw.topPadding + (sw.availableHeight - height) / 2
                color: sw.checked ? OTheme.accentStrong : OTheme.hover
                Rectangle {
                    x: sw.checked ? parent.width - width - 3 : 3
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16; height: 16; radius: 8; color: "white"
                    Behavior on x { NumberAnimation { duration: 120 } }
                }
            }
        }
        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true
            Text { text: tg.label; color: OTheme.text; font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { visible: tg.help !== ""; text: tg.help; color: OTheme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }

    component Field: TextField {
        id: tf
        property string key
        implicitHeight: 36
        color: OTheme.text
        placeholderTextColor: OTheme.muted
        selectByMouse: true
        leftPadding: 12
        text: backend.setting(key)
        onEditingFinished: backend.setSetting(key, text.trim())
        background: Rectangle { radius: 8; color: OTheme.panel2; border.color: tf.activeFocus ? OTheme.accent : OTheme.border }
    }

    component Choice: ComboBox {
        id: cb
        property string key
        property var options: []   // [[valeur, libellé], …]
        implicitHeight: 36
        implicitWidth: 260
        model: options.map(o => o[1])
        currentIndex: Math.max(0, options.findIndex(o => o[0] === backend.setting(key)))
        onActivated: (i) => backend.setSetting(key, options[i][0])
        contentItem: Text { leftPadding: 12; text: cb.displayText; color: OTheme.text; verticalAlignment: Text.AlignVCenter; font.pixelSize: 14 }
        background: Rectangle { radius: 8; color: OTheme.panel2; border.color: OTheme.border }
    }

    ColumnLayout {
        width: scroll.availableWidth
        spacing: 14
        WheelScroll { flickable: scroll.contentItem }

        ColumnLayout {
            spacing: 2
            Text { text: "Paramètres"; color: OTheme.text; font.pixelSize: 28; font.weight: Font.Bold }
            Text { text: "Données : " + backend.dataDir; color: OTheme.muted; font.pixelSize: 13 }
        }

        Card {
            title: "Recherche"
            RowLayout {
                spacing: 12
                Text { text: "Source par défaut"; color: OTheme.text; font.pixelSize: 14 }
                Choice { key: "search_source"; options: [["youtube", "YouTube"], ["spotify", "Spotify (compte requis)"]] }
            }
        }

        Card {
            id: storageCard
            // Playlists triées de la plus lourde à la plus légère.
            readonly property var rows: backend.playlists.slice().sort((a, b) => b.diskSize - a.diskSize)
            readonly property real maxSize: rows.length > 0 ? Math.max(1, rows[0].diskSize) : 1
            title: "Stockage des playlists"
            subtitle: rows.length + " playlist" + (rows.length > 1 ? "s" : "") + " · total sur le disque : <b>" + backend.cacheInfo + "</b>"
                      + "<br>Un morceau présent dans plusieurs playlists n'occupe le disque qu'une fois : la somme des lignes peut dépasser le total."

            Repeater {
                model: storageCard.rows
                delegate: ItemDelegate {
                    id: srow
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 46
                    hoverEnabled: true
                    leftPadding: 8
                    rightPadding: 8
                    onClicked: ApplicationWindow.window.navigate("playlist", modelData.id)
                    background: Rectangle { radius: 8; color: srow.hovered ? OTheme.hover : "transparent" }
                    contentItem: RowLayout {
                        spacing: 12
                        Icon {
                            iconName: ({ "cache": "disk", "youtube": "youtube", "spotify": "spotify" })[srow.modelData.kind] || "music"
                            size: 16; color: OTheme.muted
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            RowLayout {
                                spacing: 8
                                Text {
                                    text: srow.modelData.name
                                    color: OTheme.text; font.pixelSize: 14
                                    elide: Text.ElideRight; Layout.fillWidth: true
                                }
                                Text {
                                    text: srow.modelData.readyCount + " / " + srow.modelData.trackCount + " en local"
                                    color: OTheme.muted; font.pixelSize: 12
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: 4
                                radius: 2
                                color: OTheme.hover
                                Rectangle {
                                    width: parent.width * srow.modelData.diskSize / storageCard.maxSize
                                    height: parent.height
                                    radius: 2
                                    color: OTheme.accent
                                }
                            }
                        }
                        Text {
                            text: srow.modelData.diskSizeText
                            color: OTheme.text; font.pixelSize: 14; font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: 90
                        }
                    }
                }
            }
        }

        Card {
            title: "Cache local & qualité"
            subtitle: "Le meilleur flux audio de YouTube (Opus ~130-160 kb/s, 256 kb/s avec YouTube Premium) est conservé tel quel, sans réencodage. Cache actuel : " + backend.cacheInfo + "."
            Toggle { key: "auto_cache"; def: true; label: "Enregistrer les morceaux écoutés en local"
                     help: "Chaque morceau est téléchargé une seule fois puis lu depuis le disque (rapide, sans réseau à la réécoute). Désactivé : streaming uniquement." }
            Toggle { key: "prefetch"; def: true; label: "Préparer le morceau suivant"
                     help: "Télécharge le prochain morceau de la file pendant l'écoute : transition instantanée." }
            Toggle { key: "import_download"; def: true; label: "Télécharger les morceaux importés" }
            RowLayout {
                spacing: 12
                Text { text: "Téléchargements simultanés en arrière-plan"; color: OTheme.text; font.pixelSize: 14 }
                Choice {
                    key: "parallel_downloads"
                    implicitWidth: 200
                    options: [["2", "2 (recommandé)"], ["1", "1 (le plus prudent)"], ["3", "3 (plus rapide)"]]
                }
            }
            Text {
                text: "YouTube impose quelques secondes d'attente avant chaque morceau : en télécharger plusieurs à la fois accélère les playlists. "
                      + "En cas d'erreurs « 403 » ou de vérification anti-robot, revenez à 1."
                color: OTheme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            RowLayout {
                spacing: 12
                Text { text: "Taille maximale du cache (Mo, 0 = illimité)"; color: OTheme.text; font.pixelSize: 14 }
                Field { key: "cache_max_mb"; placeholderText: "20480"; implicitWidth: 110; validator: IntValidator { bottom: 0 } }
            }
            Text {
                text: "Seuls les morceaux absents de vos playlists sont supprimés, du moins récemment écouté au plus récent."
                color: OTheme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
        }

        Card {
            title: "Compte YouTube"
            subtitle: "Orpheus lit les cookies de votre navigateur (ils ne sont envoyés qu'à YouTube) pour accéder à vos playlists, vos titres « J'aime » et à l'audio Premium."
            RowLayout {
                spacing: 12
                Text { text: "Navigateur connecté"; color: OTheme.text; font.pixelSize: 14 }
                Choice {
                    key: "yt_cookies_browser"
                    options: [["", "Aucun"], ["firefox", "Firefox"], ["brave", "Brave"], ["chrome", "Chrome"],
                              ["chromium", "Chromium"], ["edge", "Edge"], ["vivaldi", "Vivaldi"], ["opera", "Opera"]]
                }
            }
            Text {
                visible: Qt.platform.os === "windows"
                text: "Sous Windows, Chrome, Edge, Brave et les autres navigateurs Chromium chiffrent leurs cookies : seul Firefox fonctionne. "
                      + "Si les cookies sont illisibles, Orpheus continue sans eux (recherche, lecture et imports Spotify restent possibles)."
                color: OTheme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
        }

        Card {
            title: "Compte Spotify"
            subtitle: backend.spotifyConnected
                      ? "Connecté" + (backend.spotifyUser ? " en tant que <b>" + backend.spotifyUser + "</b>" : "") + "."
                      : "1. Créez une application gratuite sur <a href=\"https://developer.spotify.com/dashboard\">developer.spotify.com/dashboard</a> (cochez « Web API »).<br>"
                        + "2. Ajoutez la Redirect URI ci-dessous.<br>3. Collez le Client ID puis connectez-vous (votre navigateur s'ouvrira)."
            RowLayout {
                visible: !backend.spotifyConnected
                spacing: 10
                Text { text: "Redirect URI"; color: OTheme.text; font.pixelSize: 14 }
                Rectangle {
                    implicitWidth: uri.implicitWidth + 20; implicitHeight: 30; radius: 6; color: OTheme.panel2
                    Text { id: uri; anchors.centerIn: parent; text: backend.spotifyRedirect; color: OTheme.accent; font.family: "monospace" }
                }
                PillButton { text: "Copier"; implicitHeight: 30; onClicked: backend.copyText(backend.spotifyRedirect) }
            }
            RowLayout {
                visible: !backend.spotifyConnected
                spacing: 10
                Layout.fillWidth: true
                Text { text: "Client ID"; color: OTheme.text; font.pixelSize: 14 }
                Field { id: clientId; key: "spotify_client_id"; Layout.fillWidth: true; placeholderText: "ex. 3f9a…" }
                PillButton {
                    text: "Se connecter à Spotify"; primary: true
                    onClicked: { backend.setSetting("spotify_client_id", clientId.text.trim()); backend.spotifyLogin() }
                }
            }
            PillButton { visible: backend.spotifyConnected; text: "Se déconnecter"; danger: true; onClicked: backend.spotifyLogout() }
        }

        Card {
            title: "Sortie audio"
            subtitle: "Appliqué au prochain démarrage d'Orpheus."
            Toggle { key: "audio_exclusive"; def: false; label: "Mode exclusif"
                     help: "Accès direct à la carte son, sans mixage par le système (bit-perfect si le matériel le permet)." }
            RowLayout {
                spacing: 12
                Layout.fillWidth: true
                Text { text: "Périphérique"; color: OTheme.text; font.pixelSize: 14 }
                Field { key: "audio_device"; Layout.fillWidth: true; placeholderText: "défaut (ex. pipewire, alsa/hw:1,0)" }
            }
        }

        Card {
            title: "yt-dlp"
            subtitle: "Outil utilisé pour YouTube. Une version à jour évite la plupart des erreurs : Orpheus peut gérer sa propre copie et la mettre à jour automatiquement à chaque démarrage."
            Text { text: backend.ytdlpInfo; color: OTheme.text; font.pixelSize: 13; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
            Text {
                text: backend.hasDeno ? "Moteur JavaScript deno : détecté ✓" : "Moteur JavaScript deno absent (requis par YouTube)"
                color: backend.hasDeno ? OTheme.muted : OTheme.warn
                font.pixelSize: 13
            }
            PillButton { text: "Installer / mettre à jour yt-dlp"; iconName: "sync"; onClicked: backend.updateYtDlp() }
        }
        Item { implicitHeight: 20 }
    }
}
