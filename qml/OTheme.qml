pragma Singleton
import QtQuick

QtObject {
    readonly property color bg: "#0f0f14"
    readonly property color panel: "#16161e"
    readonly property color panel2: "#1d1d27"
    readonly property color hover: "#262633"
    readonly property color current: "#221f33"
    readonly property color border: "#2a2a38"
    readonly property color text: "#ececf1"
    readonly property color muted: "#9a9aab"
    readonly property color accent: "#a78bfa"
    readonly property color accentStrong: "#8b5cf6"
    readonly property color accent2: "#22d3ee"
    readonly property color ok: "#4ade80"
    readonly property color warn: "#fbbf24"
    readonly property color err: "#f87171"
    readonly property int radius: 10
    readonly property string font: "Inter"
    // Téléphone (Android) : tiroir au lieu de la barre latérale, lecteur compact, gestes tactiles.
    readonly property bool mobile: Qt.platform.os === "android"

    function icon(name) { return "qrc:/qt/qml/Orpheus/icons/" + name + ".svg" }
}
