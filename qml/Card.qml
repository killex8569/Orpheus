import QtQuick
import QtQuick.Layouts

// Bloc de contenu sur fond légèrement surélevé.
Rectangle {
    id: root
    default property alias content: column.data
    property string title
    property string subtitle

    color: OTheme.panel
    radius: OTheme.radius
    implicitHeight: column.implicitHeight + 36
    Layout.fillWidth: true

    ColumnLayout {
        id: column
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 18 }
        spacing: 10
        Text {
            visible: root.title !== ""
            text: root.title
            color: OTheme.text
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }
        Text {
            visible: root.subtitle !== ""
            text: root.subtitle
            color: OTheme.muted
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            textFormat: Text.StyledText
            Layout.fillWidth: true
            onLinkActivated: (link) => Qt.openUrlExternally(link)
        }
    }
}
