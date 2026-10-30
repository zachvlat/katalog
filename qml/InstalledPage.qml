import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    title: qsTr("Installed")
    Component.onCompleted: backend.ensureInstalled()

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Row {
            Layout.fillWidth: true
            visible: backend.loadingInstalled
            spacing: Kirigami.Units.smallSpacing
            BusyIndicator {
                running: backend.loadingInstalled
                height: Kirigami.Units.iconSizes.medium
                width: height
            }
            Label {
                text: qsTr("Loading…")
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: backend.installedApps && backend.installedApps.count > 0
            spacing: Kirigami.Units.smallSpacing
            Kirigami.Heading {
                text: qsTr("Applications")
                level: 2
            }
            GridView {
                Layout.fillWidth: true
                Layout.preferredHeight: backend.installedApps ? Math.ceil(backend.installedApps.count / Math.max(1, Math.floor(width / cellWidth))) * cellHeight : 0
                visible: backend.installedApps && backend.installedApps.count > 0
                clip: true
                interactive: false
                cellWidth: Kirigami.Units.gridUnit * 13
                cellHeight: window.tileHeight + Kirigami.Units.gridUnit
                model: backend.installedApps
                delegate: exploreTile
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: backend.installedRuntimes && backend.installedRuntimes.count > 0
            spacing: Kirigami.Units.smallSpacing
            Kirigami.Heading {
                text: qsTr("Runtimes")
                level: 2
            }
            GridView {
                Layout.fillWidth: true
                Layout.preferredHeight: backend.installedRuntimes ? Math.ceil(backend.installedRuntimes.count / Math.max(1, Math.floor(width / cellWidth))) * cellHeight : 0
                visible: backend.installedRuntimes && backend.installedRuntimes.count > 0
                clip: true
                interactive: false
                cellWidth: Kirigami.Units.gridUnit * 13
                cellHeight: window.tileHeight + Kirigami.Units.gridUnit
                model: backend.installedRuntimes
                delegate: exploreTile
            }
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Kirigami.Units.largeSpacing
            visible: !backend.loadingInstalled && (backend.installedApps ? backend.installedApps.count === 0 : true) && (backend.installedRuntimes ? backend.installedRuntimes.count === 0 : true)
            text: qsTr("Nothing installed yet.")
            opacity: 0.7
        }
    }
}
