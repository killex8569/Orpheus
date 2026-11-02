import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Bouton arrondi (principal ou secondaire), icône facultative.
AbstractButton {
    id: root
    property string iconName
    property bool primary: false
    property bool danger: false

    implicitHeight: 36
    implicitWidth: row.implicitWidth + 32
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    opacity: enabled ? 1 : 0.5

    background: Rectangle {
        radius: height / 2
        color: root.primary ? (root.hovered ? "#9b73fb" : OTheme.accentStrong)
                            : (root.hovered ? (root.danger ? "#3a1f24" : OTheme.hover) : OTheme.panel2)
        border.color: root.primary ? "transparent" : (root.checked ? OTheme.accent : OTheme.border)
        Behavior on color { ColorAnimation { duration: 100 } }
    }
    contentItem: Item {
        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: 8
            Icon {
                visible: root.iconName !== ""
                iconName: root.iconName
                size: 16
                color: root.primary ? "white" : root.checked ? OTheme.accent : (root.danger && root.hovered ? OTheme.err : OTheme.text)
            }
            Text {
                text: root.text
                color: root.primary ? "white" : root.checked ? OTheme.accent : (root.danger && root.hovered ? OTheme.err : OTheme.text)
                font.pixelSize: 14
                font.weight: root.primary ? Font.DemiBold : Font.Normal
            }
        }
    }
}
