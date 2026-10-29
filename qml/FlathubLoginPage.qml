import QtQuick
import QtQuick.Controls
import QtWebEngine
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: loginPage
    title: qsTr("Connect to Flathub")
    padding: 0

    function close() {
        if (applicationWindow().pageStack.currentItem === loginPage)
            applicationWindow().pageStack.pop()
    }

    actions: [
        Kirigami.Action {
            text: qsTr("Cancel")
            icon.name: "dialog-cancel"
            onTriggered: loginPage.close()
        }
    ]

    Connections {
        target: backend.flathubAccount
        function onStateChanged() {
            if (backend.flathubAccount.connected)
                loginPage.close()
        }
    }

    WebEngineView {
        id: loginView
        anchors.fill: parent
        profile: backend.flathubAccount.profile
        url: backend.flathubAccount.loginUrl
        onNewWindowRequested: request => loginView.url = request.requestedUrl
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: loginView.loading
        visible: running
    }
}
