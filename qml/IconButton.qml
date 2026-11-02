import QtQuick
import QtQuick.Controls

// Bouton rond à icône, avec info-bulle.
AbstractButton {
    id: root
    property string iconName
    property int iconSize: 18
    property color color: OTheme.muted
    property color activeColor: OTheme.text
    property bool active: false
    property string tip

    implicitWidth: iconSize + 16
    implicitHeight: iconSize + 16
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    background: Rectangle {
        radius: width / 2
        color: root.down ? OTheme.border : root.hovered ? OTheme.hover : "transparent"
        Behavior on color { ColorAnimation { duration: 100 } }
    }
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            iconName: root.iconName
            size: root.iconSize
            color: root.active ? OTheme.accent : (root.hovered ? root.activeColor : root.color)
        }
    }
    ToolTip.visible: tip !== "" && hovered
    ToolTip.text: tip
    ToolTip.delay: 600
}
