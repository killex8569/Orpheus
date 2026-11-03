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

    // Navigateurs dont yt-dlp sait lire les cookies.
    readonly property var browsers: [["firefox", "Firefox"], ["chrome", "Chrome"], ["chromium", "Chromium"], ["brave", "Brave"],
                                      ["edge", "Edge"], ["vivaldi", "Vivaldi"], ["opera", "Opera"]]
                                     .concat(Qt.platform.os === "osx" ? [["safari", "Safari"]] : [])
    function browserLabel(v) {
        const b = browsers.find(o => o[0] === v)
        return b ? b[1] : v
    }
    property bool showManualCookie: false
    property bool showDevApp: false

    // Titre de section (regroupe plusieurs cartes).
    component SectionTitle: ColumnLayout {
        property string text
        property string icon
        property string help
        Layout.topMargin: 14
        spacing: 2
        RowLayout {
            spacing: 10
            Icon { visible: parent.parent.icon !== ""; iconName: parent.parent.icon; size: 20; color: OTheme.accent }
            Text { text: parent.parent.text; color: OTheme.text; font.pixelSize: 20; font.weight: Font.Bold }
        }
        Text {
            visible: parent.help !== ""
            text: parent.help
            color: OTheme.muted; font.pixelSize: 13; wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
    }

    // Pastille numérotée d'une étape.
    component StepNumber: Rectangle {
        property int n
        property bool done: false
        implicitWidth: 26; implicitHeight: 26; radius: 13
        color: done ? OTheme.accentStrong : OTheme.panel2
        border.color: done ? OTheme.accentStrong : OTheme.border
        Layout.alignment: Qt.AlignTop
        Text { anchors.centerIn: parent; text: parent.n; color: OTheme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
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

        // ---------- Spotify ----------
        SectionTitle {
            text: "Spotify"
            icon: "spotify"
            help: "Pour importer vos playlists Spotify, privées comprises, et vos titres likés. Les morceaux sont ensuite retrouvés sur YouTube."
        }

        Card {
            title: "Votre compte (gratuit ou Premium)"
            subtitle: "Aucune application développeur ni abonnement nécessaire : Orpheus utilise la session du lecteur web de Spotify, "
                      + "ouverte dans votre navigateur."

            // État de la connexion.
            Rectangle {
                implicitWidth: statusRow.implicitWidth + 24
                implicitHeight: 32
                radius: 16
                color: OTheme.panel2
                border.color: backend.spotifySession ? OTheme.ok : OTheme.border
                RowLayout {
                    id: statusRow
                    anchors.centerIn: parent
                    spacing: 8
                    Rectangle { implicitWidth: 9; implicitHeight: 9; radius: 5; color: backend.spotifySession ? OTheme.ok : OTheme.muted }
                    Text {
                        text: backend.spotifySession
                              ? "Connecté " + (backend.spotifySessionSource === "manual" ? "(cookie collé à la main)"
                                               : "via " + scroll.browserLabel(backend.spotifySessionSource))
                              : backend.spotifyConnecting ? "Connexion en cours…" : "Non connecté"
                        color: OTheme.text; font.pixelSize: 13
                    }
                }
            }

            // Connecté : accès direct à l'import.
            ColumnLayout {
                visible: backend.spotifySession
                spacing: 10
                Layout.fillWidth: true
                Text {
                    text: "Vos playlists et vos titres likés sont disponibles dans <b>Importer</b> : cochez celles que vous voulez, "
                          + "choisissez si les morceaux sont téléchargés, et dans quel dossier."
                    textFormat: Text.StyledText
                    color: OTheme.muted; font.pixelSize: 13; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                RowLayout {
                    spacing: 10
                    PillButton {
                        text: "Choisir les playlists à importer"; iconName: "download"; primary: true
                        onClicked: { backend.loadRemote("spotify"); ApplicationWindow.window.navigate("import", 0) }
                    }
                    PillButton { text: "Se déconnecter"; danger: true; onClicked: backend.spotifySessionLogout() }
                }
            }

            // Non connecté : trois étapes.
            ColumnLayout {
                visible: !backend.spotifySession
                spacing: 12
                Layout.fillWidth: true
                Layout.topMargin: 4

                RowLayout {
                    spacing: 12
                    Layout.fillWidth: true
                    StepNumber { n: 1 }
                    Text {
                        text: "Connectez-vous à Spotify dans votre navigateur (une fois suffit)."
                        color: OTheme.text; font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    PillButton { text: "Ouvrir open.spotify.com"; iconName: "link"; implicitHeight: 30
                                 onClicked: Qt.openUrlExternally("https://open.spotify.com") }
                }
                RowLayout {
                    spacing: 12
                    Layout.fillWidth: true
                    StepNumber { n: 2 }
                    Text { text: "Choisissez ce navigateur :"; color: OTheme.text; font.pixelSize: 14 }
                    Choice {
                        id: spBrowser
                        key: "spotify_cookies_browser"
                        implicitWidth: 200
                        options: scroll.browsers
                        // Par défaut, le navigateur déjà choisi pour YouTube.
                        currentIndex: Math.max(0, options.findIndex(o => o[0] === (backend.setting("spotify_cookies_browser")
                                                                                    || backend.setting("yt_cookies_browser"))))
                    }
                    Item { Layout.fillWidth: true }
                }
                RowLayout {
                    spacing: 12
                    Layout.fillWidth: true
                    StepNumber { n: 3 }
                    PillButton {
                        text: backend.spotifyConnecting ? "Connexion…" : "Se connecter"
                        primary: true
                        enabled: !backend.spotifyConnecting
                        onClicked: backend.spotifyConnectBrowser(spBrowser.options[spBrowser.currentIndex][0])
                    }
                    Spinner { visible: backend.spotifyConnecting }
                    Text {
                        text: "Orpheus lit le cookie de session « sp_dc » de ce navigateur. Il reste sur cet ordinateur "
                              + "et n'est envoyé qu'à Spotify."
                        color: OTheme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                }

                // Dernier échec, gardé à l'écran (un toast disparaît trop vite).
                Rectangle {
                    visible: backend.spotifyError !== "" && !backend.spotifyConnecting
                    Layout.fillWidth: true
                    implicitHeight: errText.implicitHeight + 20
                    radius: 8
                    color: "#2a1a1e"
                    border.color: OTheme.err
                    Text {
                        id: errText
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 12 }
                        text: "Échec : " + backend.spotifyError
                        color: OTheme.err; font.pixelSize: 13; wrapMode: Text.WordWrap
                    }
                }
                Text {
                    visible: Qt.platform.os === "windows"
                    text: "Sous Windows, Chrome, Edge, Brave et les autres navigateurs Chromium chiffrent leurs cookies : "
                          + "utilisez Firefox, ou collez la valeur du cookie ci-dessous."
                    color: OTheme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }

                // Solution de secours : valeur du cookie collée à la main.
                Text {
                    text: (scroll.showManualCookie ? "▾ " : "▸ ") + "Ça ne marche pas ? Collez la valeur du cookie à la main"
                    color: OTheme.accent; font.pixelSize: 13
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: scroll.showManualCookie = !scroll.showManualCookie }
                }
                ColumnLayout {
                    visible: scroll.showManualCookie
                    spacing: 6
                    Layout.fillWidth: true
                    Text {
                        text: "Dans le navigateur connecté à open.spotify.com : F12 → Stockage (Firefox) ou Application (Chrome, Brave, Edge) "
                              + "→ Cookies → https://open.spotify.com → ligne « sp_dc » : copiez sa valeur, puis collez-la ici."
                        color: OTheme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    RowLayout {
                        spacing: 10
                        Layout.fillWidth: true
                        TextField {
                            id: spDc
                            Layout.fillWidth: true
                            implicitHeight: 36
                            echoMode: TextInput.Password
                            placeholderText: "AQB…"
                            placeholderTextColor: OTheme.muted
                            color: OTheme.text
                            selectByMouse: true
                            leftPadding: 12
                            background: Rectangle { radius: 8; color: OTheme.panel2; border.color: spDc.activeFocus ? OTheme.accent : OTheme.border }
                            onAccepted: if (text.trim() !== "") { backend.spotifyConnectCookie(text); text = "" }
                        }
                        PillButton {
                            text: "Valider"
                            enabled: spDc.text.trim() !== "" && !backend.spotifyConnecting
                            onClicked: { backend.spotifyConnectCookie(spDc.text); spDc.text = "" }
                        }
                    }
                }
            }

            Text {
                Layout.topMargin: 4
                text: "Méthode non officielle (celle du lecteur web de Spotify) : si Spotify la modifie, une mise à jour d'Orpheus "
                      + "la rétablira. La session dure environ un an ; si elle expire, reconnectez-vous."
                color: OTheme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
        }

        Card {
            title: "Application développeur (facultatif, Premium)"
            subtitle: backend.spotifyConnected
                      ? "Connectée" + (backend.spotifyUser ? " en tant que <b>" + backend.spotifyUser + "</b>" : "")
                        + ". Sert à la recherche sur Spotify."
                      : "Utile seulement pour <b>rechercher</b> sur Spotify. Depuis février 2026, Spotify exige que le propriétaire "
                        + "de l'application ait Premium. Pour importer vos playlists, la connexion ci-dessus suffit."

            Text {
                visible: !backend.spotifyConnected
                text: (scroll.showDevApp ? "▾ " : "▸ ") + "Configurer une application développeur"
                color: OTheme.accent; font.pixelSize: 13
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: scroll.showDevApp = !scroll.showDevApp }
            }
            ColumnLayout {
                visible: scroll.showDevApp && !backend.spotifyConnected
                spacing: 10
                Layout.fillWidth: true
                Text {
                    text: "1. Créez une application sur <a href=\"https://developer.spotify.com/dashboard\">developer.spotify.com/dashboard</a> "
                          + "(cochez « Web API »).<br>2. Ajoutez la Redirect URI ci-dessous.<br>"
                          + "3. Collez le Client ID puis connectez-vous (votre navigateur s'ouvrira)."
                    color: OTheme.muted; font.pixelSize: 13; wrapMode: Text.WordWrap; textFormat: Text.StyledText
                    linkColor: OTheme.accent
                    Layout.fillWidth: true
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                }
                RowLayout {
                    spacing: 10
                    Text { text: "Redirect URI"; color: OTheme.text; font.pixelSize: 14 }
                    Rectangle {
                        implicitWidth: uri.implicitWidth + 20; implicitHeight: 30; radius: 6; color: OTheme.panel2
                        Text { id: uri; anchors.centerIn: parent; text: backend.spotifyRedirect; color: OTheme.accent; font.family: "monospace" }
                    }
                    PillButton { text: "Copier"; implicitHeight: 30; onClicked: backend.copyText(backend.spotifyRedirect) }
                }
                RowLayout {
                    spacing: 10
                    Layout.fillWidth: true
                    Text { text: "Client ID"; color: OTheme.text; font.pixelSize: 14 }
                    Field { id: clientId; key: "spotify_client_id"; Layout.fillWidth: true; placeholderText: "ex. 3f9a…" }
                    PillButton {
                        text: "Se connecter à Spotify"
                        onClicked: { backend.setSetting("spotify_client_id", clientId.text.trim()); backend.spotifyLogin() }
                    }
                }
            }
            PillButton { visible: backend.spotifyConnected; text: "Se déconnecter"; danger: true; onClicked: backend.spotifyLogout() }
        }

        // ---------- Général ----------
        SectionTitle { text: "Général"; icon: "settings" }

        Card {
            title: "Recherche"
            RowLayout {
                spacing: 12
                Text { text: "Source par défaut"; color: OTheme.text; font.pixelSize: 14 }
                Choice { key: "search_source"; options: [["youtube", "YouTube"], ["spotify", "Spotify (application développeur requise)"]] }
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
                text: backend.hasJsRuntime ? "Moteur JavaScript " + backend.jsRuntimeName + " : détecté ✓"
                                           : "Aucun moteur JavaScript (deno ou Node.js) : requis par YouTube"
                color: backend.hasJsRuntime ? OTheme.muted : OTheme.warn
                font.pixelSize: 13
            }
            PillButton { text: "Installer / mettre à jour yt-dlp"; iconName: "sync"; onClicked: backend.updateYtDlp() }
        }
        Item { implicitHeight: 20 }
    }
}
