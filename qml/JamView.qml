import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Jam : écouter la même musique en même temps, sur le même réseau.
Item {
    id: root
    readonly property var jam: backend.jam
    property bool searchOpen: false

    function runSearch() {
        if (jamSearch.text.trim() === "")
            return
        backend.search(jamSearch.text, backend.searchSource)
        searchOpen = true
    }
    function closeSearch() { searchOpen = false; jamSearch.text = "" }

    // Recherche des Jams du réseau seulement quand la vue est affichée.
    function updateDiscovery() { jam.setDiscovering(visible && !jam.active) }
    onVisibleChanged: updateDiscovery()
    Component.onCompleted: updateDiscovery()
    Connections {
        target: root.jam
        function onChanged() {
            root.updateDiscovery()
            if (!root.jam.active)
                root.searchOpen = false
        }
    }

    component Field: TextField {
        id: tf
        implicitHeight: 38
        color: OTheme.text
        placeholderTextColor: OTheme.muted
        selectByMouse: true
        leftPadding: 12
        background: Rectangle { radius: 8; color: OTheme.panel2; border.color: tf.activeFocus ? OTheme.accent : OTheme.border }
    }

    // Code d'accès : exactement 4 chiffres.
    component CodeField: Field {
        implicitWidth: 120
        maximumLength: 4
        horizontalAlignment: TextInput.AlignHCenter
        leftPadding: 12
        font.pixelSize: 18
        font.letterSpacing: 6
        inputMethodHints: Qt.ImhDigitsOnly
        validator: RegularExpressionValidator { regularExpression: /[0-9]{0,4}/ }
        placeholderText: "0000"
        readonly property bool complete: root.jam.isValidCode(text)
    }

    // ---------- Pas de Jam en cours : créer ou rejoindre ----------
    ScrollView {
        id: lobby
        anchors.fill: parent
        visible: !root.jam.active
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: lobby.availableWidth
            spacing: 14
            WheelScroll { flickable: lobby.contentItem }

            ColumnLayout {
                spacing: 2
                Text { text: "Jam"; color: OTheme.text; font.pixelSize: 28; font.weight: Font.Bold }
                Text {
                    text: "Écoutez la même musique en même temps que vos amis connectés au même réseau (Wi-Fi). "
                          + "Seule la synchronisation passe par le réseau : chacun lit le morceau depuis son disque, ou le télécharge s'il ne l'a pas encore."
                    color: OTheme.muted; font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
            }

            Card {
                title: "Votre nom"
                subtitle: "Affiché aux autres participants."
                Field {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 280
                    text: root.jam.myName
                    placeholderText: "ex. Alex"
                    maximumLength: 40
                    onEditingFinished: root.jam.myName = text
                }
            }

            Card {
                title: "Créer un Jam"
                subtitle: "Vous devenez l'hôte. Tout le monde peut ajouter des morceaux, mettre en pause ou passer au suivant, pour tous à la fois. "
                          + "Vos amis rejoignent avec le nom du Jam et le code à 4 chiffres : pas d'adresse IP à taper."
                GridLayout {
                    columns: OTheme.mobile ? 1 : 2
                    columnSpacing: 12
                    rowSpacing: 10
                    Text { text: "Nom du Jam"; color: OTheme.text; font.pixelSize: 14 }
                    Field {
                        id: createName
                        Layout.fillWidth: OTheme.mobile
                        Layout.preferredWidth: 280
                        placeholderText: root.jam.defaultJamName
                        maximumLength: 40
                    }
                    Text { text: "Code d'accès"; color: OTheme.text; font.pixelSize: 14 }
                    RowLayout {
                        spacing: 12
                        CodeField { id: createCode; onAccepted: if (complete && !root.jam.lookingUp) root.jam.create(text, createName.text) }
                        PillButton {
                            text: root.jam.lookingUp ? "Vérification du nom…" : "Créer un Jam"; iconName: "jam"; primary: true
                            enabled: createCode.complete && !root.jam.lookingUp
                            onClicked: root.jam.create(createCode.text, createName.text)
                        }
                    }
                }
            }

            Card {
                title: "Rejoindre un Jam"
                subtitle: "Demandez le code à 4 chiffres à l'hôte. Après 3 codes incorrects, le Jam bloque votre appareil."

                RowLayout {
                    spacing: 12
                    Text { text: "Code du Jam"; color: OTheme.text; font.pixelSize: 14 }
                    CodeField { id: joinCode; echoMode: TextInput.PasswordEchoOnEdit }
                }
                Text {
                    visible: root.jam.discovered.length > 0
                    text: "Jams trouvés sur votre réseau :"
                    color: OTheme.muted; font.pixelSize: 13; Layout.topMargin: 4
                }

                RowLayout {
                    visible: root.jam.discovered.length === 0
                    spacing: 10
                    Spinner { size: 16 }
                    Text { text: "Recherche des Jams sur le réseau…"; color: OTheme.muted; font.pixelSize: 13 }
                }

                Repeater {
                    model: root.jam.discovered
                    delegate: Rectangle {
                        id: drow
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        implicitHeight: 54
                        radius: 8
                        color: dHover.hovered ? OTheme.hover : OTheme.panel2
                        HoverHandler { id: dHover }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 10
                            spacing: 12
                            Icon { iconName: "jam"; size: 18; color: OTheme.accent }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text { text: drow.modelData.name; color: OTheme.text; font.pixelSize: 14; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text {
                                    text: drow.modelData.members + " participant" + (drow.modelData.members > 1 ? "s" : "") + " · " + drow.modelData.address
                                    color: OTheme.muted; font.pixelSize: 12
                                }
                            }
                            PillButton {
                                text: "Rejoindre"; primary: true
                                enabled: joinCode.complete
                                onClicked: root.jam.joinDiscovered(drow.index, joinCode.text)
                            }
                        }
                    }
                }

                Text {
                    text: "Pas dans la liste ? Saisissez le nom du Jam (ou, à défaut, l'adresse IP affichée chez l'hôte) :"
                    color: OTheme.muted; font.pixelSize: 13; Layout.topMargin: 6
                    wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                RowLayout {
                    spacing: 10
                    Field {
                        id: addr
                        Layout.fillWidth: OTheme.mobile
                        Layout.preferredWidth: 260
                        placeholderText: "ex. Soirée chez Alex, ou 192.168.1.20"
                        maximumLength: 80
                        onAccepted: if (joinCode.complete && !root.jam.lookingUp) root.jam.join(text, joinCode.text)
                    }
                    PillButton {
                        text: root.jam.lookingUp ? "Recherche…" : "Rejoindre"
                        enabled: addr.text.trim() !== "" && joinCode.complete && !root.jam.lookingUp
                        onClicked: root.jam.join(addr.text, joinCode.text)
                    }
                }
            }
            Item { implicitHeight: 10 }
        }
    }

    // ---------- Jam en cours ----------
    ColumnLayout {
        anchors.fill: parent
        visible: root.jam.active
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: root.jam.isHost ? "Vous êtes l'hôte" : root.jam.connecting ? "Connexion à " + root.jam.address + "…" : "Connecté à " + root.jam.address
                    color: OTheme.muted; font.pixelSize: 12
                }
                Text { text: root.jam.name || "Jam"; color: OTheme.text; font.pixelSize: 28; font.weight: Font.Bold; elide: Text.ElideRight; Layout.fillWidth: true }
                Text {
                    text: root.jam.statusText
                    color: OTheme.accent; font.pixelSize: 14
                }
            }
            PillButton {
                text: root.jam.isHost ? "Terminer le Jam" : "Quitter le Jam"
                iconName: "close"; danger: true
                onClicked: root.jam.leave()
            }
        }

        // Adresse à partager (si la découverte automatique est bloquée par le réseau).
        Rectangle {
            visible: root.jam.isHost && root.jam.address !== ""
            Layout.fillWidth: true
            implicitHeight: shareRow.implicitHeight + 20
            radius: 8
            color: OTheme.panel
            RowLayout {
                id: shareRow
                anchors.fill: parent
                anchors.margins: 10
                anchors.leftMargin: 14
                spacing: 10
                Icon { iconName: "link"; size: 15; color: OTheme.muted }
                Text {
                    text: "Pour rejoindre : le nom <b>" + root.jam.name + "</b> et le code <b>" + root.jam.code + "</b>. "
                          + "Vos amis voient aussi ce Jam dans leur onglet Jam. En dernier recours, l'adresse : <b>" + root.jam.address + "</b>"
                    textFormat: Text.StyledText
                    color: OTheme.muted; font.pixelSize: 13; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                PillButton { text: "Copier"; implicitHeight: 30; onClicked: backend.copyText(root.jam.address.split(" · ")[0]) }
            }
        }

        // Participants (point vert : morceau prêt).
        Flow {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: root.jam.members
                delegate: Rectangle {
                    required property var modelData
                    implicitWidth: chip.implicitWidth + 24
                    implicitHeight: 30
                    radius: 15
                    color: OTheme.panel2
                    border.color: modelData.me ? OTheme.accent : OTheme.border
                    RowLayout {
                        id: chip
                        anchors.centerIn: parent
                        spacing: 7
                        Rectangle { width: 8; height: 8; radius: 4; color: parent.parent.modelData.ready ? OTheme.ok : OTheme.muted }
                        Text {
                            text: parent.parent.modelData.name + (parent.parent.modelData.host ? " (hôte)" : "") + (parent.parent.modelData.me ? " — vous" : "")
                            color: OTheme.text; font.pixelSize: 13
                        }
                    }
                }
            }
        }

        // Compensation de latence : casque Bluetooth, enceinte connectée…
        RowLayout {
            spacing: 10
            Text { text: "Décalage audio"; color: OTheme.text; font.pixelSize: 13 }
            SpinBox {
                id: latency
                from: -1000; to: 1000; stepSize: 20
                editable: true
                value: root.jam.latencyMs
                onValueModified: root.jam.latencyMs = value
                textFromValue: (v) => (v > 0 ? "+" : "") + v + " ms"
                valueFromText: (t) => parseInt(t)
                implicitHeight: 32
            }
            Text {
                visible: !OTheme.mobile
                text: "En retard sur les autres (ex. casque Bluetooth) ? Augmentez la valeur."
                color: OTheme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
        }

        // Recherche de morceaux à ajouter au Jam (mêmes sources que l'onglet Rechercher).
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            TextField {
                id: jamSearch
                Layout.fillWidth: true
                implicitHeight: 40
                placeholderText: "Rechercher un morceau à ajouter au Jam…"
                placeholderTextColor: OTheme.muted
                color: OTheme.text
                font.pixelSize: 14
                leftPadding: 42
                selectByMouse: true
                onAccepted: root.runSearch()
                Keys.onEscapePressed: root.closeSearch()
                background: Rectangle {
                    radius: 20
                    color: OTheme.panel2
                    border.color: jamSearch.activeFocus ? OTheme.accent : OTheme.border
                    Icon { iconName: "search"; size: 16; color: OTheme.muted; anchors.verticalCenter: parent.verticalCenter; x: 15 }
                }
            }
            PillButton { text: "Chercher"; primary: true; implicitHeight: 40; enabled: jamSearch.text.trim() !== ""; onClicked: root.runSearch() }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 18

            // File commune (téléphone : remplacée par les résultats pendant une recherche).
            ColumnLayout {
                visible: !(OTheme.mobile && root.searchOpen)
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "File du Jam"; color: OTheme.text; font.pixelSize: 17; font.weight: Font.DemiBold; Layout.fillWidth: true }
                    Text { text: root.jam.queue.count + " morceaux"; color: OTheme.muted; font.pixelSize: 13 }
                }
                TrackList {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    listName: "jam"
                    model: root.jam.queue
                    emptyText: "File vide : cherchez un morceau ci-dessus,\nou ajoutez-en depuis vos playlists."
                }
            }

            // Résultats de recherche (double-clic : ajouter au Jam).
            ColumnLayout {
                visible: root.searchOpen
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text { text: "Ajouter au Jam"; color: OTheme.text; font.pixelSize: 17; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        Text { text: "Double-clic sur un morceau pour l'ajouter"; color: OTheme.muted; font.pixelSize: 12 }
                    }
                    IconButton { iconName: "close"; iconSize: 15; tip: "Fermer la recherche (Échap)"; onClicked: root.closeSearch() }
                }
                RowLayout {
                    visible: backend.searching
                    spacing: 10
                    Spinner { size: 16 }
                    Text { text: "Recherche…"; color: OTheme.muted; font.pixelSize: 13 }
                }
                Text {
                    visible: !backend.searching && backend.searchError !== ""
                    text: backend.searchError
                    color: OTheme.err; font.pixelSize: 13
                    wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                Text {
                    visible: !backend.searching && backend.searchError === ""
                             && backend.searchResults.count === 0 && backend.localResults.count === 0
                    text: "Aucun résultat."
                    color: OTheme.muted; font.pixelSize: 13
                }
                ScrollView {
                    id: results
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !backend.searching
                    contentWidth: availableWidth
                    clip: true
                    ColumnLayout {
                        width: results.availableWidth
                        spacing: 6
                        WheelScroll { flickable: results.contentItem }
                        Text {
                            visible: backend.localResults.count > 0
                            text: "Déjà en local"
                            color: OTheme.muted; font.pixelSize: 13; font.weight: Font.DemiBold
                        }
                        TrackList {
                            visible: backend.localResults.count > 0
                            Layout.fillWidth: true
                            fitContent: true
                            listName: "local"
                            model: backend.localResults
                        }
                        Text {
                            visible: backend.searchResults.count > 0
                            Layout.topMargin: backend.localResults.count > 0 ? 8 : 0
                            text: backend.searchSource === "spotify" ? "Spotify" : "YouTube"
                            color: OTheme.muted; font.pixelSize: 13; font.weight: Font.DemiBold
                        }
                        TrackList {
                            visible: backend.searchResults.count > 0
                            Layout.fillWidth: true
                            fitContent: true
                            listName: "search"
                            model: backend.searchResults
                        }
                    }
                }
            }
        }
    }
}
