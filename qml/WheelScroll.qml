import QtQuick

// Défilement à la molette plus rapide et fluide pour un Flickable (ListView, ScrollView…).
// Par défaut, Qt Quick n'avance que de 24 pixels par cran de molette.
WheelHandler {
    id: wh
    // Le Flickable à faire défiler. Déclaré dans un Flickable/ListView, le handler est
    // rattaché à son contentItem : on remonte alors au Flickable lui-même.
    property Item flickable: parent && parent.parent && parent.parent.contentItem === parent ? parent.parent : parent
    property real step: 120          // pixels par cran de molette (≈ 2 lignes)
    property real dest: 0
    property real lastRotation: 0

    target: null
    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad

    readonly property NumberAnimation anim: NumberAnimation {
        target: wh.flickable
        property: "contentY"
        duration: 160
        easing.type: Easing.OutCubic
    }

    onWheel: (event) => {
        // `rotation` cumule les crans (15° chacun) ; on n'en garde que la variation.
        const degrees = rotation - lastRotation
        lastRotation = rotation
        const f = flickable
        if (!f || degrees === 0)
            return
        const minY = f.originY - f.topMargin
        const maxY = Math.max(minY, f.originY + f.contentHeight + f.bottomMargin - f.height)
        const base = anim.running ? dest : f.contentY
        dest = Math.max(minY, Math.min(maxY, base - degrees / 15 * step))
        anim.stop()
        if (Math.abs(degrees) < 15) {
            // Pavé tactile (petits pas fréquents) : déplacement direct, sans animation.
            f.contentY = dest
        } else {
            anim.from = f.contentY
            anim.to = dest
            anim.start()
        }
        event.accepted = true
    }
}
