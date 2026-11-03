import QtQuick
import QtQuick.Controls

// Case à cocher aux couleurs d'Orpheus (états coché, partiel et décoché).
CheckBox {
    id: root
    spacing: 10
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    opacity: enabled ? 1 : 0.5

    indicator: Rectangle {
        implicitWidth: 18
        implicitHeight: 18
        x: root.leftPadding
        y: root.topPadding + (root.availableHeight - height) / 2
        radius: 5
        color: root.checkState !== Qt.Unchecked ? OTheme.accentStrong : (root.hovered ? OTheme.hover : OTheme.panel2)
        border.color: root.checkState !== Qt.Unchecked ? OTheme.accentStrong : OTheme.border
        Text {
            anchors.centerIn: parent
            visible: root.checkState !== Qt.Unchecked
            text: root.checkState === Qt.PartiallyChecked ? "–" : "✓"
            color: "white"
            font.pixelSize: 13
            font.bold: true
        }
    }
    contentItem: Text {
        leftPadding: root.text !== "" ? root.indicator.width + root.spacing : root.indicator.width
        text: root.text
        color: OTheme.text
        font.pixelSize: 14
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
    }
}
