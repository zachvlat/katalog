#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkCookie;
class QNetworkReply;
class QNetworkRequest;
class QQuickWebEngineProfile;

// Signs in to flathub.org through an embedded browser and talks to the
// Flathub API with the resulting session cookie. The cookie is kept only in
// the web engine profile's own storage.
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
    void storeResponseCookies(QNetworkReply *reply);
    void setState(bool connected, const QString &displayName);

    QNetworkAccessManager *m_nam = nullptr;
    QQuickWebEngineProfile *m_profile = nullptr;
    QByteArray m_session;
    QByteArray m_storedSession;
    bool m_connected = false;
    bool m_checking = false;
    bool m_checkQueued = false;
    QString m_displayName;
};
