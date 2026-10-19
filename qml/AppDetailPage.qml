import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: page

    property var app: backend.details
    property string selectedShot: ""
    readonly property bool busy: backend.currentTransaction !== null
                                  && backend.currentTransaction !== undefined
                                  && backend.currentTransaction.running

    title: app && app.name ? app.name : (app ? app.appId : "")

    // The page takes over the main panel (the panel shows a single column at a
    // time, see Main.qml), the same way a category does. PageRow's "back" only
    // moves between columns without ever removing one, so the page carries an
    // explicit action that really pops itself.
    signal closeRequested()

    actions: [
        Kirigami.Action {
            text: qsTr("Back")
            icon.name: "go-previous"
            onTriggered: page.closeRequested()
        }
    ]

    // Keep the standard Back shortcut (Alt+Left) working too.
    onBackRequested: event => {
        event.accepted = true
        page.closeRequested()
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroll.availableWidth
            spacing: Kirigami.Units.largeSpacing

            // ----- Loading -----
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.largeSpacing
                visible: page.app && page.app.loading
                spacing: Kirigami.Units.largeSpacing
                BusyIndicator {
                    running: true
                }
                Label {
                    text: qsTr("Loading application details…")
                    opacity: 0.7
                }
            }

            // ----- Header -----
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.largeSpacing
                visible: page.app && !page.app.loading
                spacing: Kirigami.Units.largeSpacing

                Item {
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: 128
                    Layout.preferredHeight: 128
                    // AppStream may return either an icon URL or a themed name.
                    readonly property string iconSource: page.app && page.app.icon ? page.app.icon : ""
                    readonly property bool iconIsUrl: iconSource.indexOf("://") !== -1

                    Image {
                        id: headerImage
                        anchors.fill: parent
                        source: parent.iconIsUrl ? parent.iconSource : ""
                        sourceSize.width: 128
                        sourceSize.height: 128
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        visible: parent.iconIsUrl && status === Image.Ready
                    }
                    // Themed icon, or a placeholder while a URL loads / on failure.
                    Kirigami.Icon {
                        anchors.fill: parent
                        visible: !headerImage.visible
                        source: (parent.iconIsUrl || parent.iconSource.length === 0) ? "application-x-executable" : parent.iconSource
                        opacity: (parent.iconIsUrl || parent.iconSource.length === 0) ? 0.4 : 1.0
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        Kirigami.Heading {
                            id: appTitle
                            level: 1
                            wrapMode: Text.WordWrap
                            text: page.app ? page.app.name : ""
                            Layout.fillWidth: true
                            // Grow (and wrap) for long titles, but never past the
                            // text's natural width so the badge stays next to it.
                            Layout.maximumWidth: implicitWidth
                        }
                        VerifiedBadge {
                            id: verifiedBadge
                            visible: page.app && page.app.verified
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        opacity: 0.8
                        text: page.app ? page.app.developer : ""
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        wrapMode: Text.WordWrap
                        text: page.app ? page.app.summary : ""
                    }
                    RowLayout {
                        Layout.topMargin: Kirigami.Units.smallSpacing
                        spacing: Kirigami.Units.smallSpacing

                        Button {
                            visible: page.app && !page.app.installed
                            text: qsTr("Install")
                            icon.name: "install"
                            enabled: !page.busy
                            onClicked: backend.installApp(page.app.appId)
                        }
                        Button {
                            visible: page.app && page.app.installed
                            text: qsTr("Launch")
                            icon.name: "system-run"
                            onClicked: backend.launchApp(page.app.appId)
                        }
                        Button {
                            visible: page.app && page.app.installed && page.app.updateAvailable
                            text: qsTr("Update")
                            icon.name: "system-software-update"
                            enabled: !page.busy
                            onClicked: backend.updateApp(page.app.appId)
                        }
                        Button {
                            visible: page.app && page.app.installed
                            text: qsTr("Remove")
                            icon.name: "delete"
                            enabled: !page.busy
                            onClicked: {
                                deleteDataBox.checked = false
                                removeDialog.open()
                            }
                        }
                    }
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
                visible: page.app && !page.app.loading
            }

            // ----- Screenshots -----
            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: 260
                visible: page.app && page.app.screenshots.length > 0
                contentWidth: shotsRow.width
                contentHeight: height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Row {
                    id: shotsRow
                    spacing: Kirigami.Units.smallSpacing
                    height: parent.height

                    Repeater {
                        model: page.app ? page.app.screenshots : []
                        delegate: Rectangle {
                            width: 400
                            height: shotsRow.height
                            radius: Kirigami.Units.smallSpacing
                            color: Kirigami.Theme.alternateBackgroundColor
                            clip: true

                            TapHandler {
                                cursorShape: Qt.PointingHandCursor
                                onTapped: page.selectedShot = modelData.url
                            }

                            Image {
                                anchors.fill: parent
                                anchors.margins: 1
                                source: modelData.url
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                            }
                        }
                    }
                }
            }

            // ----- Description -----
            Label {
                Layout.fillWidth: true
                visible: page.app && !page.app.loading && text.length > 0
                text: page.app ? page.app.description : ""
                textFormat: Text.RichText
                wrapMode: Text.WordWrap
                onLinkActivated: link => Qt.openUrlExternally(link)
            }

            // ----- Details grid -----
            GridLayout {
                Layout.fillWidth: true
                visible: page.app && !page.app.loading
                columns: 2
                columnSpacing: Kirigami.Units.largeSpacing
                rowSpacing: Kirigami.Units.smallSpacing

                Label {
                    text: qsTr("Version")
                    font.bold: true
                    visible: versionValue.text.length > 0
                }
                Label {
                    id: versionValue
                    text: page.app ? page.app.version : ""
                    visible: text.length > 0
                }

                Label {
                    text: qsTr("License")
                    font.bold: true
                    visible: licenseValue.text.length > 0
                }
                Label {
                    id: licenseValue
                    text: page.app ? page.app.license : ""
                    visible: text.length > 0
                }

                Label {
                    text: qsTr("Download size")
                    font.bold: true
                    visible: downloadValue.text.length > 0
                }
                Label {
                    id: downloadValue
                    text: page.app ? page.app.downloadSize : ""
                    visible: text.length > 0
                }

                Label {
                    text: qsTr("Installed size")
                    font.bold: true
                    visible: installedValue.text.length > 0
                }
                Label {
                    id: installedValue
                    text: page.app ? page.app.installedSize : ""
                    visible: text.length > 0
                }

                Label {
                    text: qsTr("Runtime")
                    font.bold: true
                    visible: runtimeValue.text.length > 0
                }
                Label {
                    id: runtimeValue
                    text: page.app ? page.app.runtime : ""
                    visible: text.length > 0
                }

                Label {
                    text: qsTr("Categories")
                    font.bold: true
                    visible: categoriesValue.text.length > 0
                }
                Label {
                    id: categoriesValue
                    text: page.app ? page.app.categories.join(", ") : ""
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    visible: text.length > 0
                }

                Label {
                    text: qsTr("Homepage")
                    font.bold: true
                    visible: page.app && page.app.homepage.length > 0
                }
                Label {
                    visible: page.app && page.app.homepage.length > 0
                    text: page.app ? page.app.homepage : ""
                    color: Kirigami.Theme.linkColor
                    onLinkActivated: link => Qt.openUrlExternally(link)
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Qt.openUrlExternally(page.app.homepage)
                    }
                }
            }

            // ----- Permissions -----
            ColumnLayout {
                Layout.fillWidth: true
                visible: page.app && !page.app.loading && page.app.permissions.length > 0
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Heading {
                    level: 3
                    text: qsTr("Permissions")
                }
                Repeater {
                    model: page.app ? page.app.permissions : []
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        Kirigami.Icon {
                            source: "security-medium"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: modelData
                        }
                    }
                }
            }

            // ----- Releases -----
            ColumnLayout {
                Layout.fillWidth: true
                visible: page.app && !page.app.loading && page.app.releases.length > 0
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Heading {
                    level: 3
                    text: qsTr("What's new")
                }
                Repeater {
                    model: page.app ? page.app.releases : []
                    delegate: ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        RowLayout {
                            spacing: Kirigami.Units.smallSpacing
                            Label {
                                font.bold: true
                                text: modelData.version ? modelData.version : qsTr("Release")
                            }
                            Label {
                                opacity: 0.6
                                text: modelData.date ? modelData.date : ""
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: modelData.description
                            textFormat: Text.RichText
                            wrapMode: Text.WordWrap
                            onLinkActivated: link => Qt.openUrlExternally(link)
                        }
                    }
                }
            }

            // ----- Not found -----
            Label {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.largeSpacing
                visible: page.app && !page.app.loading && !page.app.found
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTr("No details are available for “%1”.").arg(page.app ? page.app.appId : "")
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Kirigami.Units.largeSpacing
            }
        }
    }

    // Removal confirmation. The app's own data (~/.var/app/<id>: settings,
    // profiles, saves) is kept unless the user explicitly opts in.
    Dialog {
        id: removeDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(page.width * 0.9, Kirigami.Units.gridUnit * 28)
        modal: true
        title: qsTr("Remove %1?").arg(page.app ? page.app.name : "")

        ColumnLayout {
            width: removeDialog.availableWidth
            spacing: Kirigami.Units.largeSpacing

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("The application will be uninstalled. Its settings and data are kept, so they are still there if you install it again.")
            }
            CheckBox {
                id: deleteDataBox
                Layout.fillWidth: true
                text: qsTr("Also delete its settings and data")
            }
            Label {
                Layout.fillWidth: true
                visible: deleteDataBox.checked
                wrapMode: Text.WordWrap
                color: Kirigami.Theme.negativeTextColor
                text: qsTr("This permanently deletes ~/.var/app/%1 and cannot be undone.").arg(page.app ? page.app.appId : "")
            }
        }

        footer: DialogButtonBox {
            Button {
                text: qsTr("Remove")
                icon.name: "delete"
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }

        onAccepted: backend.removeApp(page.app.appId, deleteDataBox.checked)
    }

    // Full-size view of a screenshot, opened by clicking one in the strip.
    Popup {
        id: shotViewer
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(page.width * 0.85, 1200)
        height: page.height * 0.85
        modal: true
        padding: Kirigami.Units.smallSpacing
        visible: page.selectedShot.length > 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: page.selectedShot = ""

        background: Rectangle {
            color: Kirigami.Theme.backgroundColor
            radius: Kirigami.Units.smallSpacing
            border.width: 1
            border.color: Kirigami.Theme.disabledTextColor
        }

        contentItem: Image {
            source: page.selectedShot
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: false
        }
    }
}
