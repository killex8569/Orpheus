import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Statistiques d'écoute : une écoute compte quand plus de la moitié du morceau a
// réellement été écoutée (pauses et sauts exclus).
ScrollView {
    id: scroll
    contentWidth: availableWidth
    clip: true
    property int days: 30
    readonly property var st: backend.stats

    // Rechargées à chaque affichage (et après chaque écoute comptée, côté C++).
    onVisibleChanged: if (visible) backend.loadStats(days)

    function fmt(n) { return Number(n || 0).toLocaleString(Qt.locale(), "f", 0) }

    component StatTile: Rectangle {
        id: tile
        property string label
        property string value
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        implicitHeight: 92
        radius: OTheme.radius
        color: OTheme.panel
        ColumnLayout {
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 18 }
            spacing: 4
            Text { text: tile.label; color: OTheme.muted; font.pixelSize: 13 }
            Text {
                text: tile.value
                color: OTheme.text
                font.pixelSize: 28
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }

    ColumnLayout {
        width: scroll.availableWidth
        spacing: 14
        WheelScroll { flickable: scroll.contentItem }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true
                Text { text: "Statistiques"; color: OTheme.text; font.pixelSize: 28; font.weight: Font.Bold }
                Text {
                    text: "Une écoute compte dès que plus de la moitié du morceau a été écoutée."
                    color: OTheme.muted; font.pixelSize: 14
                    wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
            }
            // Période
            Rectangle {
                implicitWidth: seg.implicitWidth + 8
                implicitHeight: 38
                radius: 19
                color: OTheme.panel2
                border.color: OTheme.border
                RowLayout {
                    id: seg
                    anchors.centerIn: parent
                    spacing: 2
                    Repeater {
                        model: [[7, "7 jours"], [30, "30 jours"], [365, "12 mois"], [0, "Tout"]]
                        delegate: AbstractButton {
                            id: segBtn
                            required property var modelData
                            readonly property bool on: scroll.days === modelData[0]
                            implicitWidth: segText.implicitWidth + 26
                            implicitHeight: 30
                            hoverEnabled: true
                            focusPolicy: Qt.NoFocus
                            onClicked: { scroll.days = modelData[0]; backend.loadStats(scroll.days) }
                            background: Rectangle { radius: 15; color: segBtn.on ? OTheme.hover : "transparent" }
                            contentItem: Text {
                                id: segText
                                text: segBtn.modelData[1]
                                color: segBtn.on || segBtn.hovered ? OTheme.text : OTheme.muted
                                font.pixelSize: 13
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }
        }

        // ---------- Chiffres clés ----------
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            StatTile { label: "Temps d'écoute"; value: scroll.st.time || "0 min" }
            StatTile { label: "Écoutes"; value: scroll.fmt(scroll.st.plays) }
            StatTile { label: "Morceaux différents"; value: scroll.fmt(scroll.st.tracks) }
            StatTile { label: "Artistes différents"; value: scroll.fmt(scroll.st.artists) }
        }

        Text {
            visible: (scroll.st.plays || 0) === 0
            Layout.topMargin: 30
            Layout.alignment: Qt.AlignHCenter
            text: "Aucune écoute comptée sur cette période pour le moment."
            color: OTheme.muted
            font.pixelSize: 14
        }

        // ---------- Top artistes ----------
        Text {
            visible: backend.statsArtists.length > 0
            Layout.topMargin: 10
            text: "Top artistes"
            color: OTheme.text; font.pixelSize: 17; font.weight: Font.DemiBold
        }
        Card {
            visible: backend.statsArtists.length > 0
            Repeater {
                model: backend.statsArtists
                delegate: RowLayout {
                    id: artistRow
                    required property int index
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 14
                    Text {
                        text: artistRow.index + 1
                        color: OTheme.muted; font.pixelSize: 12
                        horizontalAlignment: Text.AlignRight
                        Layout.preferredWidth: 20
                    }
                    Text {
                        text: artistRow.modelData.name
                        color: OTheme.text; font.pixelSize: 14
                        elide: Text.ElideRight
                        Layout.preferredWidth: 200
                    }
                    // Barre proportionnelle au nombre d'écoutes (le premier = pleine largeur).
                    Item {
                        id: barArea
                        Layout.fillWidth: true
                        implicitHeight: 22
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.max(4, barArea.width * artistRow.modelData.share)
                            height: 8
                            radius: 4
                            color: barHover.hovered ? OTheme.accentStrong : OTheme.accent
                            Behavior on width { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
                        }
                        HoverHandler { id: barHover }
                        ToolTip.visible: barHover.hovered
                        ToolTip.text: artistRow.modelData.name + " — " + artistRow.modelData.playsText + ", "
                                      + artistRow.modelData.time + ", " + artistRow.modelData.tracks + " morceau(x)"
                    }
                    Text {
                        text: artistRow.modelData.playsText + " · " + artistRow.modelData.time
                        color: OTheme.muted; font.pixelSize: 12
                        horizontalAlignment: Text.AlignRight
                        Layout.preferredWidth: 150
                    }
                }
            }
        }

        // ---------- Top morceaux ----------
        Text {
            visible: backend.statsTracks.count > 0
            Layout.topMargin: 10
            text: "Top morceaux"
            color: OTheme.text; font.pixelSize: 17; font.weight: Font.DemiBold
        }
        TrackList {
            visible: backend.statsTracks.count > 0
            Layout.fillWidth: true
            fitContent: true
            listName: "stats"
            model: backend.statsTracks
        }
        Item { implicitHeight: 20 }
    }
}
