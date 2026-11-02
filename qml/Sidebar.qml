import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property string view
    property var playlistId
    signal navigate(string view, var id)
    signal newPlaylist()

    color: OTheme.panel

    component NavItem: ItemDelegate {
        id: item
        property string iconName
        property string label
        property bool active
        property int count: -1
        Layout.fillWidth: true
        width: ListView.view ? ListView.view.width : implicitWidth
        height: 38
        hoverEnabled: true
        leftPadding: 12
        rightPadding: 12
        background: Rectangle {
            radius: 8
            color: item.active ? OTheme.panel2 : (item.hovered ? OTheme.hover : "transparent")
        }
        contentItem: RowLayout {
            spacing: 12
            Icon {
                iconName: item.iconName
                size: 17
                color: item.active || item.hovered ? OTheme.text : OTheme.muted
            }
            Text {
                text: item.label
                color: item.active || item.hovered ? OTheme.text : OTheme.muted
                font.pixelSize: 14
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                visible: item.count >= 0
                text: item.count
                color: OTheme.muted
                font.pixelSize: 11
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 2

        RowLayout {
            Layout.leftMargin: 8
            Layout.topMargin: 6
            Layout.bottomMargin: 18
            spacing: 10
            Image { source: OTheme.icon("orpheus"); sourceSize: Qt.size(56, 56)
                    Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
            Text { text: "Orpheus"; color: OTheme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
        }

        NavItem { iconName: "search"; label: "Rechercher"; active: root.view === "search"; onClicked: root.navigate("search", 0) }
        NavItem { iconName: "queue"; label: "File d'attente"; active: root.view === "queue"; onClicked: root.navigate("queue", 0) }
        NavItem { iconName: "lyrics"; label: "Paroles"; active: root.view === "lyrics"; onClicked: root.navigate("lyrics", 0) }
        NavItem { iconName: "stats"; label: "Statistiques"; active: root.view === "stats"; onClicked: root.navigate("stats", 0) }
        NavItem { iconName: "download"; label: "Importer"; active: root.view === "import"; onClicked: root.navigate("import", 0) }
        NavItem { iconName: "settings"; label: "Paramètres"; active: root.view === "settings"; onClicked: root.navigate("settings", 0) }

        RowLayout {
            Layout.topMargin: 20
            Layout.leftMargin: 12
            Text {
                text: "PLAYLISTS"
                color: OTheme.muted
                font.pixelSize: 11
                font.letterSpacing: 1
                Layout.fillWidth: true
            }
            IconButton { iconName: "plus"; iconSize: 15; tip: "Nouvelle playlist"; onClicked: root.newPlaylist() }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: backend.playlists
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            WheelScroll {}
            delegate: NavItem {
                required property var modelData
                iconName: ({ "cache": "disk", "youtube": "youtube", "spotify": "spotify" })[modelData.kind] || "music"
                label: modelData.name
                count: modelData.trackCount
                active: root.view === "playlist" && root.playlistId === modelData.id
                onClicked: root.navigate("playlist", modelData.id)
            }
        }

        // Téléchargements en cours.
        Rectangle {
            visible: backend.downloadsCount > 0
            Layout.fillWidth: true
            implicitHeight: dlText.implicitHeight + 20
            radius: 8
            color: OTheme.panel2
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10
                Icon {
                    id: dlSpin
                    iconName: "loader"; size: 16; color: OTheme.accent2
                    RotationAnimator on rotation { running: dlSpin.visible; from: 0; to: 360; duration: 900; loops: Animation.Infinite }
                }
                ColumnLayout {
                    id: dlText
                    Layout.fillWidth: true
                    spacing: 1
                    Repeater {
                        model: backend.downloadsText.split("\n")
                        delegate: Text {
                            required property string modelData
                            required property int index
                            text: modelData
                            color: index === 0 ? OTheme.text : OTheme.muted
                            font.pixelSize: index === 0 ? 12 : 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }
    }
}
