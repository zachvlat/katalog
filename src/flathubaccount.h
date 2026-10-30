#pragma once

#include <QByteArray>
#include <QNetworkCookie>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QQuickWebEngineProfile;

// Signs in to flathub.org through an embedded browser and talks to the
// Flathub API with the resulting session cookie. The web engine profile keeps
// its own copy of the cookie; a private fallback file keeps the login across
// restarts even when the profile's cookie storage does not survive the exit.
class FlathubAccount : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QObject *profile READ profile CONSTANT)
    Q_PROPERTY(QUrl loginUrl READ loginUrl CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY stateChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY stateChanged)
public:
    explicit FlathubAccount(QObject *parent = nullptr);

    bool available() const;
    QObject *profile() const;
    QUrl loginUrl() const;
    bool connected() const { return m_connected; }
    bool checking() const { return m_checking; }
    QString displayName() const { return m_displayName; }

    Q_INVOKABLE void logout();

    void fetchFavorites();

Q_SIGNALS:
    void stateChanged();
    void favoritesReady(const QStringList &appIds);
    void favoritesFailed(const QString &message);

private:
    bool isSessionCookie(const QNetworkCookie &cookie) const;
    void onCookieAdded(const QNetworkCookie &cookie);
    void onCookieRemoved(const QNetworkCookie &cookie);
    void checkSession();
    QNetworkRequest apiRequest(const QString &path) const;
    bool storeResponseCookies(QNetworkReply *reply);
    void restoreSession();
    void saveSessionCookie(const QNetworkCookie &cookie);
    void clearSession();
    void expireSession();
    void syncSessionCookieToProfile();
    void setState(bool connected, const QString &displayName);

    static QString sessionFilePath();

    QNetworkAccessManager *m_nam = nullptr;
    QQuickWebEngineProfile *m_profile = nullptr;
    QByteArray m_session;
    // Cookie read from the fallback file at startup. If it shows up in the web
    // profile after a newer server cookie was accepted, it is stale and ignored.
    QNetworkCookie m_staleCookie;
    // Cookie value we just handed to the web profile ourselves. Its removal
    // event is part of that write and must not be mistaken for a logout.
    QByteArray m_pendingCookieWrite;
    bool m_connected = false;
    bool m_checking = false;
    bool m_checkQueued = false;
    bool m_restoreCookieAfterCheck = false;
    bool m_restoredSessionUnverified = false;
    QString m_displayName;
};
