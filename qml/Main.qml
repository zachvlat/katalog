import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigami.layouts as KL

Kirigami.ApplicationWindow {
    id: window
    width: 1280
    height: 800
    title: qsTr("Qute Store")

    readonly property var tx: backend.currentTransaction

    Component.onCompleted: {
        backend.loadExplore()

        // By default a pushed page becomes a second column beside the current
        // one (the panel is split in two). Single column mode makes the new page
        // take the panel over instead, exactly like a category does, while
        // keeping the previous page alive behind it.
        if (pageStack.columnView)
            pageStack.columnView.columnResizeMode = KL.ColumnView.SingleColumn

        // The detail page ships its own labelled back action, so there is no
        // need for the icon-only navigation buttons as well.
        if (pageStack.globalToolBar)
            pageStack.globalToolBar.showNavigationButtons = Kirigami.ApplicationHeaderStyle.NoNavigationButtons
    }

    function openDetails(appId) {
        backend.openApp(appId)
        // Drop a previously opened detail so pages never pile up in the stack.
        while (pageStack.depth > 1)
            pageStack.pop()
        const detail = pageStack.push(appDetailComponent)
        if (detail)
            detail.closeRequested.connect(window.closeDetails)
    }

    function closeDetails() {
        // Remove the detail page; the previous page slides back in.
        if (pageStack.depth > 1)
            pageStack.pop()
    }

    function openCategory(title, categoryId) {
        backend.loadCategory(categoryId)
        while (pageStack.depth > 1)
            pageStack.pop()
        // Replace the main-screen page so the category takes over the right panel
        // instead of appearing as a column beside the previous page.
        pageStack.replace(categoryPageComponent, { "title": title, "categoryTitle": title })
    }

    function navigateTo(page) {
        while (pageStack.depth > 1)
            pageStack.pop()
        if (pageStack.currentItem !== page)
            pageStack.replace(page)
    }

    // Ctrl+F opens the search screen and focuses its field.
    Shortcut {
        sequence: StandardKey.Find
        onActivated: {
            window.navigateTo(searchPage)
            if (pageStack.currentItem && pageStack.currentItem.focusSearch)
                pageStack.currentItem.focusSearch()
        }
    }

    Component {
        id: appDetailComponent
        AppDetailPage {}
    }

    Component {
        id: categoryPageComponent
        Kirigami.Page {
            id: categoryPage
            property string categoryTitle
            title: categoryTitle

            BusyIndicator {
                anchors.centerIn: parent
                running: backend.categoryLoading
                visible: backend.categoryLoading
            }

            GridView {
                anchors.fill: parent
                visible: !backend.categoryLoading
                clip: true
                cellWidth: Kirigami.Units.gridUnit * 13
                cellHeight: Kirigami.Units.gridUnit * 14
                model: backend.categoryApps
                delegate: exploreTile
            }
        }
    }

    pageStack.leftSidebar: sidebar

    // A sidebar row with an explicit icon and label. The Breeze QQC2 style's own
    // ItemDelegate icon ends up with no source and a zero size here, so the row
    // builds its content itself.
    component SidebarRow: ItemDelegate {
        id: control
        property string iconName

        Layout.fillWidth: true

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            property alias truncated: label.truncated

            Kirigami.Icon {
                source: control.iconName
                visible: control.iconName.length > 0
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
                Layout.alignment: Qt.AlignVCenter
            }

            Label {
                id: label
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: control.text
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                color: control.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
            }
        }
    }

    Kirigami.GlobalDrawer {
        id: sidebar
        modal: false
        width: Kirigami.Units.gridUnit * 14
        title: qsTr("Qute Store")

        topContent: [
            SidebarRow {
                text: qsTr("Explore")
                iconName: "view-grid"
                onClicked: window.navigateTo(explorePage)
            },
            SidebarRow {
                text: qsTr("Search")
                iconName: "search"
                onClicked: window.navigateTo(searchPage)
            },
            SidebarRow {
                text: qsTr("Installed")
                iconName: "system-software-installed"
                onClicked: window.navigateTo(installedPage)
            },
            SidebarRow {
                text: qsTr("Updates")
                iconName: "system-software-update"
                onClicked: window.navigateTo(updatesPage)
            },
            SidebarRow {
                text: qsTr("Repositories")
                iconName: "server-database"
                onClicked: window.navigateTo(repositoriesPage)
            },
            SidebarRow {
                text: qsTr("Leftover files")
                iconName: "trash-empty"
                onClicked: window.navigateTo(leftoverPage)
            },
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.smallSpacing
                Layout.bottomMargin: Kirigami.Units.smallSpacing
                implicitHeight: 1
                color: Kirigami.Theme.textColor
                opacity: 0.15
            },
            Kirigami.Heading {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.smallSpacing
                Layout.bottomMargin: Kirigami.Units.smallSpacing
                level: 4
                opacity: 0.7
                text: qsTr("Categories")
            },
            Repeater {
                id: categoryRepeater
                model: backend.exploreSections
                delegate: SidebarRow {
                    text: modelData.title
                    iconName: modelData.icon ? modelData.icon : ""
                    onClicked: window.openCategory(modelData.title, modelData.category)
                }
            }
        ]
    }

    Component {
        id: exploreTile
        Kirigami.AbstractCard {
            id: tile
            width: Kirigami.Units.gridUnit * 12
            height: Kirigami.Units.gridUnit * 13

            TapHandler {
                cursorShape: Qt.PointingHandCursor
                onTapped: window.openDetails(model.appId)
            }

            contentItem: Column {
                spacing: Kirigami.Units.smallSpacing

                Item {
                    readonly property real iconSize: Kirigami.Units.iconSizes.huge
                    width: tile.width
                    height: iconSize

                    // Installed apps have no AppStream icon, so fall back to a
                    // themed placeholder rather than leaving the tile blank.
                    Kirigami.Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: !model.icon || model.icon.length === 0
                        source: "application-x-executable"
                        width: parent.iconSize
                        height: parent.iconSize
                        opacity: 0.4
                    }
                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: model.icon && model.icon.length > 0
                        source: model.icon ? model.icon : ""
                        sourceSize.width: parent.iconSize
                        sourceSize.height: parent.iconSize
                        width: parent.iconSize
                        height: parent.iconSize
                        fillMode: Image.PreserveAspectFit
                    }
                }
                Label {
                    width: parent.width
                    text: model.name ? model.name : model.appId
                    font.bold: true
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }
                Label {
                    width: parent.width
                    text: model.summary
                    opacity: 0.7
                    wrapMode: Text.WordWrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                }
            }
        }
    }

    Component {
        id: explorePage
        Kirigami.ScrollablePage {
            id: exploreRoot
            title: qsTr("Explore")

            Component.onCompleted: backend.loadExplore()

            Column {
                width: exploreRoot.width
                spacing: Kirigami.Units.largeSpacing

                Row {
                    visible: backend.exploring
                    spacing: Kirigami.Units.smallSpacing
                    BusyIndicator {
                        running: backend.exploring
                        height: Kirigami.Units.iconSizes.medium
                        width: height
                    }
                    Label {
                        text: qsTr("Loading…")
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                // The Flathub-curated rows (Trending, Most Popular, New, Updated)
                // appear first, in that order. The category rows follow.
                Repeater {
                    model: backend.exploreCurated
                    delegate: Column {
                        width: exploreRoot.width
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Heading {
                            text: modelData.title
                            level: 2
                        }

                        ListView {
                            width: parent.width
                            height: Kirigami.Units.gridUnit * 13 + Kirigami.Units.largeSpacing
                            orientation: ListView.Horizontal
                            clip: true
                            spacing: Kirigami.Units.smallSpacing
                            model: modelData.model
                            delegate: exploreTile
                        }
                    }
                }

                Repeater {
                    model: backend.exploreSections
                    delegate: Column {
                        width: exploreRoot.width
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Heading {
                            text: modelData.title
                            level: 2
                        }
                        ListView {
                            width: parent.width
                            height: Kirigami.Units.gridUnit * 13 + Kirigami.Units.largeSpacing
                            orientation: ListView.Horizontal
                            clip: true
                            spacing: Kirigami.Units.smallSpacing
                            model: modelData.model
                            delegate: exploreTile
                        }
                    }
                }
            }
        }
    }

    Component {
        id: searchPage
        Kirigami.Page {
            id: searchRoot
            title: qsTr("Search")

            function focusSearch() {
                queryField.forceActiveFocus()
            }

            Column {
                anchors.fill: parent
                spacing: Kirigami.Units.smallSpacing
                Row {
                    width: parent.width
                    spacing: Kirigami.Units.smallSpacing
                    TextField {
                        id: queryField
                        width: parent.width - searchButton.width - Kirigami.Units.smallSpacing
                        placeholderText: qsTr("Search applications…")
                        onAccepted: backend.search(text)
                    }
                    Button {
                        id: searchButton
                        text: qsTr("Search")
                        enabled: !backend.searching
                        onClicked: backend.search(queryField.text)
                    }
                }
                BusyIndicator {
                    running: backend.searching
                    visible: backend.searching
                }
                GridView {
                    width: parent.width
                    height: parent.height - queryField.height - Kirigami.Units.smallSpacing
                    clip: true
                    cellWidth: Kirigami.Units.gridUnit * 13
                    cellHeight: Kirigami.Units.gridUnit * 14
                    model: backend.searchResults
                    delegate: exploreTile
                }
                Label {
                    width: parent.width
                    visible: !backend.searching && backend.searchResults.count === 0
                    text: qsTr("Search for applications to install.")
                    opacity: 0.7
                }
            }
        }
    }

    Component {
        id: installedPage
        Kirigami.Page {
            title: qsTr("Installed")
            Component.onCompleted: backend.refreshInstalled()
            GridView {
                anchors.fill: parent
                clip: true
                cellWidth: Kirigami.Units.gridUnit * 13
                cellHeight: Kirigami.Units.gridUnit * 14
                model: backend.installedApps
                delegate: exploreTile
            }
            Label {
                anchors.centerIn: parent
                visible: !backend.loadingInstalled && backend.installedApps.count === 0
                text: qsTr("Nothing installed yet.")
                opacity: 0.7
            }
            BusyIndicator {
                anchors.centerIn: parent
                running: backend.loadingInstalled && backend.installedApps.count === 0
                visible: running
            }
        }
    }

    Component {
        id: updatesPage
        Kirigami.Page {
            title: qsTr("Updates")
            Component.onCompleted: backend.refreshInstalled()
            GridView {
                anchors.fill: parent
                clip: true
                cellWidth: Kirigami.Units.gridUnit * 13
                cellHeight: Kirigami.Units.gridUnit * 14
                model: backend.updates
                delegate: exploreTile
            }
            Label {
                anchors.centerIn: parent
                visible: !backend.loadingInstalled && backend.updates.count === 0
                text: qsTr("All quiet here…")
                opacity: 0.7
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.3
            }
            BusyIndicator {
                anchors.centerIn: parent
                running: backend.loadingInstalled && backend.updates.count === 0
                visible: running
            }
        }
    }

    footer: Rectangle {
        visible: window.tx !== null && window.tx !== undefined
        implicitHeight: visible ? content.implicitHeight + Kirigami.Units.largeSpacing * 2 : 0
        height: implicitHeight
        color: Kirigami.Theme.backgroundColor

        Column {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            Row {
                spacing: Kirigami.Units.smallSpacing
                Label {
                    text: window.tx ? window.tx.appId : ""
                    font.bold: true
                }
                Label {
                    text: window.tx ? window.tx.status : ""
                    opacity: 0.7
                }
            }

            Loader {
                id: progressLoader
                width: parent.width
                height: active ? item.implicitHeight : 0
                active: window.tx !== null && window.tx !== undefined
                sourceComponent: ProgressBar {
                    width: progressLoader.width
                    indeterminate: window.tx && !window.tx.finished
                    value: window.tx && window.tx.finished ? 1 : 0
                }
            }

            Row {
                visible: window.tx && window.tx.finished
                spacing: Kirigami.Units.smallSpacing
                Label {
                    text: {
                        if (!window.tx)
                            return "";
                        return window.tx.success ? qsTr("Done") : window.tx.errorMessage;
                    }
                    color: window.tx && window.tx.success ? Kirigami.Theme.positiveTextColor
                                                          : Kirigami.Theme.negativeTextColor
                }
                Button {
                    text: qsTr("Dismiss")
                    onClicked: backend.dismissTransaction()
                }
            }
        }
    }

    Component {
        id: repositoriesPage
        RepositoriesPage {}
    }

    Component {
        id: leftoverPage
        LeftoverPage {}
    }

    pageStack.initialPage: explorePage
}
