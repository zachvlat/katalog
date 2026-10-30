import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: leftoverPage
    title: qsTr("Leftover files")

    Component.onCompleted: backend.ensureLeftovers()

    ColumnLayout {
        width: leftoverPage.width
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Heading {
            text: qsTr("Orphaned data directories")
            level: 2
            Layout.fillWidth: true
        }

        Label {
            text: qsTr("These are application data directories left behind by apps that are no longer installed. You can safely delete them to free up disk space.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            opacity: 0.7
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Label {
                text: qsTr("Items found: ") + backend.leftovers.count
                opacity: 0.7
                Layout.fillWidth: true
            }

            Button {
                text: qsTr("Scan again")
                enabled: !backend.scanningLeftovers
                onClicked: backend.refreshLeftovers()
            }
        }

        Row {
            visible: backend.scanningLeftovers
            spacing: Kirigami.Units.smallSpacing
            Layout.fillWidth: true

            BusyIndicator {
                running: backend.scanningLeftovers
                height: Kirigami.Units.iconSizes.medium
                width: height
            }
            Label {
                text: qsTr("Scanning…")
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Repeater {
            model: backend.leftovers

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
                                text: model.appId
                                font.bold: true
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            Label {
                                text: model.path
                                opacity: 0.7
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                Layout.fillWidth: true
                                wrapMode: Text.WrapAnywhere
                            }
                            Label {
                                text: model.sizeText + " • " + model.scope
                                opacity: 0.5
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                            }
                        }

                        Button {
                            text: qsTr("Delete")
                            enabled: !backend.scanningLeftovers
                            onClicked: {
                                confirmDialog.appId = model.appId
                                confirmDialog.path = model.path
                                confirmDialog.sizeText = model.sizeText
                                confirmDialog.open()
                            }
                        }
                    }
                }
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !backend.scanningLeftovers && backend.leftoverError.length > 0
            type: Kirigami.MessageType.Warning
            text: backend.leftoverError
        }

        Label {
            visible: !backend.scanningLeftovers && backend.leftoverError.length === 0
                     && backend.leftovers.count === 0
            text: qsTr("No leftover files found.")
            opacity: 0.7
        }
    }

    Dialog {
        id: confirmDialog
        property string appId
        property string path
        property string sizeText

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(leftoverPage.width * 0.9, Kirigami.Units.gridUnit * 28)
        modal: true
        title: qsTr("Delete data of %1?").arg(confirmDialog.appId)

        Label {
            width: confirmDialog.availableWidth
            wrapMode: Text.WordWrap
            text: qsTr("%1 (%2) will be permanently deleted. This cannot be undone.")
                      .arg(confirmDialog.path).arg(confirmDialog.sizeText)
        }

        footer: DialogButtonBox {
            Button {
                text: qsTr("Delete")
                icon.name: "delete"
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }

        onAccepted: backend.removeLeftover(confirmDialog.path)
    }
}