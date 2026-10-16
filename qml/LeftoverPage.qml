import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: leftoverPage
    title: qsTr("Leftover files")

    Component.onCompleted: backend.refreshLeftovers()

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
                            onClicked: backend.removeLeftover(model.path)
                        }
                    }
                }
            }
        }

        Label {
            visible: !backend.scanningLeftovers && backend.leftovers.count === 0
            text: qsTr("No leftover files found.")
            opacity: 0.7
        }
    }
}