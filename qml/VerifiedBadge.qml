import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: badge

    implicitWidth: Kirigami.Units.iconSizes.small
    implicitHeight: implicitWidth

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Kirigami.Theme.textColor
    }

    Kirigami.Icon {
        anchors.centerIn: parent
        width: Math.round(badge.width * 0.6)
        height: width
        source: "checkbox"
        color: Kirigami.Theme.backgroundColor
        isMask: true
    }

    Accessible.name: qsTr("Verified")
    Accessible.role: Accessible.StaticText
}
