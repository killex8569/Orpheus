import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property string source: backend.searchSource
    function focusSearch() { field.forceActiveFocus(); field.selectAll() }
    function run() { if (field.text.trim() !== "") backend.search(field.text, root.source) }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        ColumnLayout {
            spacing: 2
            Text { text: "Rechercher"; color: OTheme.text; font.pixelSize: 28; font.weight: Font.Bold }
            Text { text: "Titre, artiste, ou lien YouTube"; color: OTheme.muted; font.pixelSize: 14 }
        }

        GridLayout {
            columns: 3
            columnSpacing: 10
            rowSpacing: 10
            Layout.fillWidth: true
            TextField {
                id: field
                Layout.fillWidth: true
                Layout.columnSpan: OTheme.mobile ? 3 : 1
                implicitHeight: 42
                placeholderText: "Que voulez-vous écouter ?"
                placeholderTextColor: OTheme.muted
                color: OTheme.text
                font.pixelSize: 15
                leftPadding: 44
                selectByMouse: true
                onAccepted: root.run()
                Component.onCompleted: if (!OTheme.mobile) forceActiveFocus()
                background: Rectangle {
                    radius: 21
                    color: OTheme.panel2
                    border.color: field.activeFocus ? OTheme.accent : OTheme.border
                    Icon { iconName: "search"; size: 17; color: OTheme.muted; anchors.verticalCenter: parent.verticalCenter; x: 16 }
                }
            }
            Rectangle {
                implicitWidth: seg.implicitWidth + 8
                implicitHeight: 42
                radius: 21
                color: OTheme.panel2
                border.color: OTheme.border
                RowLayout {
                    id: seg
                    anchors.centerIn: parent
                    spacing: 2
                    Repeater {
                        model: [["youtube", "YouTube"], ["spotify", "Spotify"]]
                        delegate: AbstractButton {
                            id: segBtn
                            required property var modelData
                            readonly property bool on: root.source === modelData[0]
                            implicitWidth: segText.implicitWidth + 28
                            implicitHeight: 34
                            hoverEnabled: true
                            focusPolicy: Qt.NoFocus
                            onClicked: { root.source = modelData[0]; root.run() }
                            background: Rectangle { radius: 17; color: segBtn.on ? OTheme.hover : "transparent" }
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
            PillButton { text: "Chercher"; primary: true; implicitHeight: 42; onClicked: root.run() }
            Item { visible: OTheme.mobile; Layout.fillWidth: true }  // téléphone : le champ prend toute la largeur
        }

        RowLayout {
            visible: backend.searching
            spacing: 10
            Spinner {}
            Text { text: "Recherche…"; color: OTheme.muted; font.pixelSize: 14 }
        }
        Text {
            visible: !backend.searching && backend.searchError !== ""
            text: backend.searchError
            color: OTheme.err
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !backend.searching
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: scroll.availableWidth
                spacing: 8
                WheelScroll { flickable: scroll.contentItem }

                Text {
                    visible: backend.localResults.count > 0
                    text: "Déjà en local"
                    color: OTheme.text
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
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
                    Layout.topMargin: backend.localResults.count > 0 ? 14 : 0
                    text: backend.searchSource === "spotify" ? "Résultats Spotify" : "Résultats YouTube"
                    color: OTheme.text
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
                }
                TrackList {
                    visible: backend.searchResults.count > 0
                    Layout.fillWidth: true
                    fitContent: true
                    listName: "search"
                    model: backend.searchResults
                }
                Text {
                    visible: backend.searchResults.count === 0 && backend.localResults.count === 0 && backend.searchError === ""
                             && !OTheme.mobile  // raccourcis clavier
                    Layout.topMargin: 40
                    Layout.alignment: Qt.AlignHCenter
                    text: "Astuce : Ctrl+K pour rechercher depuis n'importe où, Espace pour lecture / pause."
                    color: OTheme.muted
                }
            }
        }
    }
}
