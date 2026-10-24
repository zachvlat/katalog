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

    // Padding around the contents of every app tile, and the resulting tile
    // height: one icon tall plus the padding, so the row layout stays compact.
    readonly property real tilePadding: Kirigami.Units.largeSpacing * 2
    readonly property real tileHeight: Kirigami.Units.iconSizes.huge + tilePadding * 2

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
                cellHeight: window.tileHeight + Kirigami.Units.gridUnit
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
                iconName: "package-symbolic"
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
            },
            SidebarRow {
                text: qsTr("KDE apps")
                iconName: "kde-symbolic"
                onClicked: window.openCategory(qsTr("KDE apps"), "kde")
            }
        ]
    }

    Component {
        id: exploreTile
        Kirigami.AbstractCard {
            id: tile
            width: Kirigami.Units.gridUnit * 12
            height: window.tileHeight
            padding: window.tilePadding

            TapHandler {
                cursorShape: Qt.PointingHandCursor
                onTapped: window.openDetails(model.appId)
            }

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Item {
                    id: iconBox
                    readonly property real iconSize: Kirigami.Units.iconSizes.huge
                    // AppStream may return either an icon URL or a themed name.
                    readonly property string iconSource: model.icon ? model.icon : ""
                    readonly property bool iconIsUrl: iconSource.indexOf("://") !== -1
                    Layout.preferredWidth: iconSize
                    Layout.preferredHeight: iconSize
                    Layout.alignment: Qt.AlignVCenter

                    Image {
                        id: tileImage
                        anchors.centerIn: parent
                        source: parent.iconIsUrl ? parent.iconSource : ""
                        sourceSize.width: parent.iconSize
                        sourceSize.height: parent.iconSize
                        width: parent.iconSize
                        height: parent.iconSize
                        fillMode: Image.PreserveAspectFit
                        visible: parent.iconIsUrl && status === Image.Ready
                    }
                    // Covers the themed-name case, and keeps the tile from going
                    // blank while a URL loads or when it fails to load.
                    Kirigami.Icon {
                        anchors.centerIn: parent
                        visible: !tileImage.visible
                        source: (parent.iconIsUrl || parent.iconSource.length === 0) ? "application-x-executable" : parent.iconSource
                        width: parent.iconSize
                        height: parent.iconSize
                        opacity: (parent.iconIsUrl || parent.iconSource.length === 0) ? 0.4 : 1.0
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        Label {
                            Layout.fillWidth: true
                            text: model.name ? model.name : model.appId
                            font.bold: true
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                        VerifiedBadge {
                            visible: model.verified
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: model.summary
                        opacity: 0.7
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }
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
                            height: window.tileHeight + Kirigami.Units.largeSpacing
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
                            height: window.tileHeight + Kirigami.Units.largeSpacing
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
                    cellHeight: window.tileHeight + Kirigami.Units.gridUnit
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
        InstalledPage {}
    }

    Component {
        id: updatesPage
        Kirigami.Page {
            title: qsTr("Updates")
            Component.onCompleted: backend.refreshInstalled()

            // Update every pending app in one go; the updates run one
            // transaction after the next (see Backend::updateAll).
            actions: [
                Kirigami.Action {
                    text: qsTr("Update All")
                    icon.name: "download"
                    enabled: backend.updates.count > 0
                             && (!backend.currentTransaction
                                 || !backend.currentTransaction.running)
                    onTriggered: backend.updateAll()
                }
            ]

            Column {
                anchors.fill: parent
                spacing: Kirigami.Units.largeSpacing

                Column {
                    width: parent.width
                    spacing: Kirigami.Units.smallSpacing
                    visible: backend.updatesApps && backend.updatesApps.count > 0
                    Kirigami.Heading { text: qsTr("Applications"); level: 2 }
                    GridView {
                        width: parent.width
                        height: backend.updatesApps ? Math.ceil(backend.updatesApps.count / Math.floor(width / cellWidth)) * cellHeight : 0
                        clip: true
                        interactive: false
                        cellWidth: Kirigami.Units.gridUnit * 13
                        cellHeight: window.tileHeight + Kirigami.Units.gridUnit
                        model: backend.updatesApps
                        delegate: exploreTile
                    }
                }

                Column {
                    width: parent.width
                    spacing: Kirigami.Units.smallSpacing
                    visible: backend.updatesRuntimes && backend.updatesRuntimes.count > 0
                    Kirigami.Heading { text: qsTr("Runtimes"); level: 2 }
                    GridView {
                        width: parent.width
                        height: backend.updatesRuntimes ? Math.ceil(backend.updatesRuntimes.count / Math.floor(width / cellWidth)) * cellHeight : 0
                        clip: true
                        interactive: false
                        cellWidth: Kirigami.Units.gridUnit * 13
                        cellHeight: window.tileHeight + Kirigami.Units.gridUnit
                        model: backend.updatesRuntimes
                        delegate: exploreTile
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: !backend.loadingInstalled && (backend.updatesApps ? backend.updatesApps.count === 0 : true) && (backend.updatesRuntimes ? backend.updatesRuntimes.count === 0 : true)
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
