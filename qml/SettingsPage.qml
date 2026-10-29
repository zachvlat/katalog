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

        Kirigami.Heading {
            text: qsTr("Flathub account")
            level: 2
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing
            visible: backend.flathubAccount.available

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: {
                    if (backend.flathubAccount.checking)
                        return qsTr("Checking your Flathub login…")
                    if (backend.flathubAccount.connected)
                        return qsTr("Connected as %1").arg(backend.flathubAccount.displayName)
                    return qsTr("Connect your Flathub account to see the apps you bookmarked on flathub.org. It only works with developer login method.")
                }
            }
            Button {
                text: backend.flathubAccount.connected ? qsTr("Disconnect") : qsTr("Connect to Flathub")
                icon.name: backend.flathubAccount.connected ? "system-log-out" : "im-user-online"
                enabled: !backend.flathubAccount.checking
                onClicked: {
                    if (backend.flathubAccount.connected)
                        backend.flathubAccount.logout()
                    else
                        applicationWindow().pageStack.push(Qt.resolvedUrl("FlathubLoginPage.qml"))
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: !backend.flathubAccount.available
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: qsTr("Signing in to Flathub needs QtWebEngine, which this build of Katalog does not include.")
        }
    }
}
