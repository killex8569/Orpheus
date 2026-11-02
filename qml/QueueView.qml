import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    ColumnLayout {
        anchors.fill: parent
        spacing: 14
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true
                Text { text: "File d'attente"; color: OTheme.text; font.pixelSize: 28; font.weight: Font.Bold }
                Text { text: backend.queueModel.count + " morceaux"; color: OTheme.muted; font.pixelSize: 14 }
            }
            PillButton { text: "Vider la file"; iconName: "trash"; onClicked: backend.clearQueue() }
        }
        TrackList {
            Layout.fillWidth: true
            Layout.fillHeight: true
            listName: "queue"
            editable: true
            model: backend.queueModel
            emptyText: "La file est vide. Lancez un morceau ou une playlist."
        }
    }
}
