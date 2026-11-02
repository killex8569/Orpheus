import QtQuick

// Indicateur d'activité (icône qui tourne).
Icon {
    id: spin
    iconName: "loader"
    size: 18
    color: OTheme.accent
    RotationAnimator on rotation { running: spin.visible; from: 0; to: 360; duration: 900; loops: Animation.Infinite }
}
