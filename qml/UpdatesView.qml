import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Mises à jour : version installée comparée à la dernière release publiée sur GitHub.
ScrollView {
    id: scroll
    readonly property var up: backend.updates
    contentWidth: availableWidth
    clip: true

    ColumnLayout {
        width: scroll.availableWidth
        spacing: 14
        WheelScroll { flickable: scroll.contentItem }

        ColumnLayout {
            spacing: 2
            Text { text: "Mises à jour"; color: OTheme.text; font.pixelSize: 28; font.weight: Font.Bold }
            Text {
                text: "Orpheus vérifie une fois au lancement s'il existe une version plus récente parmi les releases GitHub."
                color: OTheme.muted; font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
        }

        Card {
            title: "Version"
            GridLayout {
                columns: 2
                columnSpacing: 24
                rowSpacing: 6
                Text { text: "Installée"; color: OTheme.muted; font.pixelSize: 14 }
                Text { text: scroll.up.currentVersion; color: OTheme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                Text { text: "Dernière sur GitHub"; color: OTheme.muted; font.pixelSize: 14 }
                Text {
                    text: scroll.up.latestVersion === "" ? "—"
                          : scroll.up.latestVersion + (scroll.up.publishedText !== "" ? "  (publiée le " + scroll.up.publishedText + ")" : "")
                    color: OTheme.text; font.pixelSize: 14; font.weight: Font.DemiBold
                }
            }

            // État de la dernière vérification.
            RowLayout {
                Layout.topMargin: 4
                spacing: 10
                Spinner { visible: scroll.up.status === "checking"; size: 16 }
                Rectangle {
                    visible: scroll.up.status !== "checking" && scroll.up.status !== ""
                    width: 10; height: 10; radius: 5
                    color: scroll.up.status === "available" ? OTheme.accent
                         : scroll.up.status === "uptodate" ? OTheme.ok : OTheme.err
                }
                Text {
                    text: ({
                        "": "Pas encore vérifié.",
                        "checking": "Recherche de mises à jour…",
                        "uptodate": "Orpheus est à jour.",
                        "available": "Nouvelle version disponible : " + scroll.up.latestVersion,
                        "error": scroll.up.error
                    })[scroll.up.status] || ""
                    color: scroll.up.status === "error" ? OTheme.err : OTheme.text
                    font.pixelSize: 14
                    font.weight: scroll.up.status === "available" ? Font.DemiBold : Font.Normal
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
            RowLayout {
                spacing: 12
                PillButton {
                    text: "Rechercher les mises à jour"; iconName: "sync"
                    enabled: scroll.up.status !== "checking"
                    onClicked: scroll.up.check()
                }
                Text {
                    visible: scroll.up.lastCheckText !== ""
                    text: "Dernière vérification à " + scroll.up.lastCheckText
                    color: OTheme.muted; font.pixelSize: 12
                }
            }

            RowLayout {
                Layout.topMargin: 6
                spacing: 12
                Switch {
                    id: auto
                    checked: backend.settingBool("update_check", true)
                    onToggled: backend.setSetting("update_check", checked ? "true" : "false")
                    indicator: Rectangle {
                        implicitWidth: 40; implicitHeight: 22; radius: 11
                        y: auto.topPadding + (auto.availableHeight - height) / 2
                        color: auto.checked ? OTheme.accentStrong : OTheme.hover
                        Rectangle {
                            x: auto.checked ? parent.width - width - 3 : 3
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16; height: 16; radius: 8; color: "white"
                            Behavior on x { NumberAnimation { duration: 120 } }
                        }
                    }
                }
                Text { text: "Vérifier au lancement d'Orpheus"; color: OTheme.text; font.pixelSize: 14 }
            }
        }

        // Nouvelle version : notes et marche à suivre.
        Card {
            visible: scroll.up.updateAvailable
            title: "Nouveautés de la " + scroll.up.latestVersion
            Text {
                visible: scroll.up.releaseNotes !== ""
                text: scroll.up.releaseNotes
                textFormat: Text.MarkdownText
                color: OTheme.text; font.pixelSize: 13
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
            PillButton {
                text: "Voir la release sur GitHub"; iconName: "link"
                onClicked: Qt.openUrlExternally(scroll.up.releaseUrl)
            }
        }

        // Windows / macOS : pas de commande, on réinstalle avec l'installeur de la release.
        Card {
            visible: scroll.up.updateAvailable && scroll.up.updateCommand === ""
            title: "Installer la mise à jour"
            subtitle: "Téléchargez l'installeur de la nouvelle version dans la release (.exe pour Windows, .dmg pour macOS, .rpm pour Fedora), puis ouvrez-le : "
                      + "il remplace la version actuelle sans toucher à vos morceaux ni à vos playlists. Fermez Orpheus avant. "
                      + "Fedora : sudo dnf install ./orpheus-*.rpm"
            PillButton {
                text: "Télécharger l'installeur"; iconName: "download"
                onClicked: Qt.openUrlExternally(scroll.up.releaseUrl)
            }
        }

        Card {
            visible: scroll.up.updateAvailable && scroll.up.updateCommand !== ""
            title: "Installer la mise à jour"
            subtitle: "Dans un terminal, lancez la commande ci-dessous (elle récupère la nouvelle version, la compile et l'installe), puis relancez Orpheus."
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: cmd.implicitHeight + 16
                    radius: 6
                    color: OTheme.panel2
                    Text {
                        id: cmd
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                        text: scroll.up.updateCommand
                        color: OTheme.accent; font.family: "monospace"; font.pixelSize: 12
                        wrapMode: Text.WrapAnywhere
                    }
                }
                PillButton {
                    text: "Copier"
                    onClicked: backend.copyText(scroll.up.updateCommand)
                }
            }
        }
        Item { implicitHeight: 10 }
    }
}
