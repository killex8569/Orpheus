import QtQuick
import QtQuick.Controls.impl

// Icône SVG teintée.
IconImage {
    property string iconName
    property int size: 18
    source: iconName ? OTheme.icon(iconName) : ""
    sourceSize: Qt.size(size, size)
    width: size
    height: size
    color: OTheme.text
}
