import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: reposPage
    title: qsTr("Repositories")

    Component.onCompleted: backend.refreshRemotes()

    ColumnLayout {
        width: reposPage.width
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Heading {
            text: qsTr("Flatpak remotes")
            level: 2
            Layout.fillWidth: true
        }

        Label {
            text: qsTr("These are the repositories Qute Store uses to search for and install applications.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            opacity: 0.7
        }

        Repeater {
            model: backend.remotes

            delegate: Kirigami.AbstractCard {
                Layout.fillWidth: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing
                    width: parent.width

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing / 2

                            Label {
                                text: model.title && model.title.length > 0 ? model.title : model.name
                                font.bold: true
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            Label {
                                text: model.name + " • " + model.url
                                opacity: 0.7
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                Layout.fillWidth: true
                                wrapMode: Text.WrapAnywhere
                            }
                            Label {
                                text: model.disabled ? qsTr("Disabled") : qsTr("Enabled")
                                opacity: model.disabled ? 0.5 : 0.9
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                            }
                        }

                        Button {
                            text: model.disabled ? qsTr("Enable") : qsTr("Disable")
                            enabled: !backend.remoteBusy
                            onClicked: backend.setRemoteEnabled(model.name, model.isUser, model.disabled)
                        }

                        Button {
                            text: qsTr("Remove")
                            enabled: !backend.remoteBusy
                            onClicked: backend.removeRemote(model.name, model.isUser)
                        }
                    }
                }
            }
        }

        Kirigami.Heading {
            text: qsTr("Add a remote")
            level: 2
            Layout.fillWidth: true
        }

        GridLayout {
            columns: 2
            rowSpacing: Kirigami.Units.smallSpacing
            columnSpacing: Kirigami.Units.smallSpacing
            Layout.fillWidth: true

            Label {
                text: qsTr("Name")
            }
            TextField {
                id: newRemoteName
                placeholderText: qsTr("e.g. flathub")
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("URL")
            }
            TextField {
                id: newRemoteUrl
                placeholderText: qsTr("e.g. https://dl.flathub.org/repo/flathub.flatpakrepo")
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("Installation")
            }
            RowLayout {
                spacing: Kirigami.Units.smallSpacing
                RadioButton {
                    id: newRemoteUser
                    text: qsTr("User")
                    checked: true
                }
                RadioButton {
                    id: newRemoteSystem
                    text: qsTr("System")
                }
            }

            Item {
                Layout.columnSpan: 2
                Layout.fillWidth: true
            }

            Button {
                text: qsTr("Add Flathub")
                onClicked: {
                    newRemoteName.text = "flathub"
                    newRemoteUrl.text = "https://dl.flathub.org/repo/flathub.flatpakrepo"
                    newRemoteUser.checked = true
                }
            }

            Button {
                text: qsTr("Add Flathub Beta")
                onClicked: {
                    newRemoteName.text = "flathub-beta"
                    newRemoteUrl.text = "https://dl.flathub.org/beta-repo/flathub-beta.flatpakrepo"
                    newRemoteUser.checked = true
                }
            }

            Button {
                text: qsTr("Add remote")
                enabled: !backend.remoteBusy && newRemoteName.text.length > 0 && newRemoteUrl.text.length > 0
                onClicked: {
                    backend.addRemote(newRemoteName.text.trim(), newRemoteUrl.text.trim(), newRemoteUser.checked)
                    newRemoteName.text = ""
                    newRemoteUrl.text = ""
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: backend.remoteBusy
            spacing: Kirigami.Units.smallSpacing
            BusyIndicator {
                running: backend.remoteBusy
                Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                Layout.preferredWidth: Kirigami.Units.iconSizes.medium
            }
            Label {
                text: qsTr("Updating repositories…")
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !backend.remoteBusy && backend.remoteMessage.length > 0
            type: backend.remoteSucceeded ? Kirigami.MessageType.Positive
                                                                  : Kirigami.MessageType.Error
            text: backend.remoteMessage
        }
    }
}