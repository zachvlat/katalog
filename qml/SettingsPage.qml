import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: settingsPage
    title: qsTr("Settings")

    ColumnLayout {
        width: settingsPage.width
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Heading {
            text: qsTr("Appearance")
            level: 2
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing
            visible: backend.colorSchemes !== null

            Label {
                text: qsTr("Color scheme")
            }
            ComboBox {
                Layout.fillWidth: true
                model: backend.colorSchemes
                textRole: "display"
                currentIndex: backend.colorSchemeIndex
                onActivated: index => backend.setColorSchemeIndex(index)
            }
        }

        Label {
            Layout.fillWidth: true
            visible: backend.colorSchemes !== null
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: qsTr("Default follows your system's light or dark preference.")
        }
    }
}
