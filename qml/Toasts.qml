import QtQuick
import QtQuick.Layouts

// Notifications temporaires en bas de l'écran.
Column {
    id: root
    spacing: 8
    function show(message, error) {
        model.append({ message: message, error: error })
        if (model.count > 4)
            model.remove(0)
    }
    ListModel { id: model }
    Repeater {
        model: model
        delegate: Rectangle {
            id: toast
            required property int index
            required property string message
            required property bool error
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(label.implicitWidth + 36, 640)
            height: label.implicitHeight + 22
            radius: 8
            color: error ? "#4a1f25" : "#2a2a38"
            opacity: 0
            Text {
                id: label
                anchors.centerIn: parent
                width: Math.min(implicitWidth, 604, root.width - 68)
                text: toast.message
                color: OTheme.text
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
            Component.onCompleted: opacity = 1
            Behavior on opacity { NumberAnimation { duration: 180 } }
            Timer {
                running: true
                interval: toast.error ? 6000 : 2800
                onTriggered: { toast.opacity = 0; removeTimer.start() }
            }
            Timer { id: removeTimer; interval: 200; onTriggered: model.remove(toast.index) }
        }
    }
}
