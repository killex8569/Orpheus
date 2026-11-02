import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Paroles du morceau en cours : synchronisées (ligne active mise en avant, clic = aller
// à ce passage) ou texte seul. Gardées en base : disponibles hors ligne.
Item {
    id: root
    readonly property string status: backend.lyricsStatus

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true
            Text { text: "Paroles"; color: OTheme.muted; font.pixelSize: 12 }
            Text {
                text: backend.hasTrack ? backend.title : "Paroles"
                color: OTheme.text; font.pixelSize: 28; font.weight: Font.Bold
                elide: Text.ElideRight; Layout.fillWidth: true
            }
            Text {
                visible: backend.hasTrack && backend.artist !== ""
                text: backend.artist
                color: OTheme.muted; font.pixelSize: 14
                elide: Text.ElideRight; Layout.fillWidth: true
            }
        }

        // ---------- Paroles synchronisées ----------
        ListView {
            id: lines
            visible: root.status === "synced"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: backend.lyricsLines
            spacing: 6
            // La ligne en cours reste vers le haut du tiers central ; on peut faire défiler
            // librement, la vue se recale à la ligne suivante.
            currentIndex: backend.lyricsIndex
            highlight: Item {}
            highlightFollowsCurrentItem: true
            highlightMoveDuration: 350
            highlightRangeMode: ListView.ApplyRange
            preferredHighlightBegin: height * 0.35
            preferredHighlightEnd: height * 0.35 + 50
            topMargin: height * 0.35
            bottomMargin: height * 0.5
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            WheelScroll {}

            delegate: Text {
                id: line
                required property int index
                required property var modelData
                readonly property bool active: index === lines.currentIndex
                width: ListView.view.width - 24
                text: modelData.text !== "" ? modelData.text : "♪"
                wrapMode: Text.WordWrap
                font.pixelSize: 26
                font.weight: Font.Bold
                color: active ? OTheme.text : (lineHover.hovered ? OTheme.text : OTheme.muted)
                opacity: active ? 1 : (index < lines.currentIndex ? 0.45 : 0.7)
                Behavior on opacity { NumberAnimation { duration: 200 } }
                HoverHandler { id: lineHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: backend.seek(line.modelData.time) }
            }
        }

        // ---------- Texte seul ----------
        ScrollView {
            id: plainScroll
            visible: root.status === "plain"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: plainScroll.availableWidth
                spacing: 10
                WheelScroll { flickable: plainScroll.contentItem }
                Text {
                    text: "Paroles non synchronisées"
                    color: OTheme.muted; font.pixelSize: 12
                }
                Text {
                    text: backend.lyricsPlain
                    color: OTheme.text
                    font.pixelSize: 20
                    lineHeight: 1.25
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Item { implicitHeight: 20 }
            }
        }

        // ---------- États ----------
        Item {
            visible: root.status !== "synced" && root.status !== "plain"
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 12
                Spinner { visible: root.status === "loading"; Layout.alignment: Qt.AlignHCenter }
                Icon {
                    visible: root.status !== "loading"
                    iconName: "lyrics"; size: 40; color: OTheme.muted
                    Layout.alignment: Qt.AlignHCenter
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: root.width * 0.7
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: OTheme.muted
                    font.pixelSize: 15
                    text: !backend.hasTrack ? "Lancez un morceau pour afficher ses paroles."
                        : ({
                            "loading": "Recherche des paroles…",
                            "instrumental": "Morceau instrumental",
                            "missing": "Paroles introuvables pour ce morceau.",
                            "offline": "Paroles momentanément indisponibles (pas de connexion, ou service LRCLIB occupé). Nouvel essai automatique."
                        })[root.status] || ""
                }
            }
        }

        Text {
            visible: root.status === "synced" || root.status === "plain"
            text: "Paroles : LRCLIB · conservées pour l'écoute hors ligne · cliquez sur une ligne pour y aller"
            color: OTheme.muted
            font.pixelSize: 11
        }
    }
}
