#include "flathubaccount.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>

#ifdef HAVE_WEBENGINE
#include <QQuickWebEngineProfile>
#include <QWebEngineCookieStore>
#endif

namespace {

const QLatin1String kFlathubHost("flathub.org");

int httpStatus(QNetworkReply *reply)
{
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

}

FlathubAccount::FlathubAccount(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    restoreSession();

#ifdef HAVE_WEBENGINE
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    // A profile constructed without a storage name is off-the-record, and
    // changing that afterwards does not make its cookies survive restarts.
    m_profile = new QQuickWebEngineProfile(QStringLiteral("flathub"), this);
#else
    // Older Qt versions can only create off-the-record profiles directly.
    m_profile = new QQuickWebEngineProfile(this);
    m_profile->setStorageName(QStringLiteral("flathub"));
    m_profile->setOffTheRecord(false);
#endif
    m_profile->setPersistentCookiesPolicy(QQuickWebEngineProfile::ForcePersistentCookies);

    QWebEngineCookieStore *store = m_profile->cookieStore();
    connect(store, &QWebEngineCookieStore::cookieAdded, this, &FlathubAccount::onCookieAdded);
    connect(store, &QWebEngineCookieStore::cookieRemoved, this, &FlathubAccount::onCookieRemoved);
    store->loadAllCookies();

    if (!m_staleCookie.value().isEmpty()) {
        m_pendingCookieWrite = m_staleCookie.value();
        store->setCookie(m_staleCookie, QUrl(QStringLiteral("https://flathub.org")));
        checkSession();
    } else if (!m_session.isEmpty()) {
        checkSession();
    }
#else
    if (!m_session.isEmpty())
        checkSession();
#endif
}

QString FlathubAccount::sessionFilePath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.local/share/qutestore");
    return dir + QStringLiteral("/flathub-session");
}

void FlathubAccount::restoreSession()
{
    const QString path = sessionFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QByteArray raw = file.read(8192).trimmed();
    file.close();
    if (raw.isEmpty()) {
        QFile::remove(path);
        return;
    }

    const QList<QNetworkCookie> cookies = QNetworkCookie::parseCookies(raw);
    for (const QNetworkCookie &cookie : cookies) {
        if (!isSessionCookie(cookie) || cookie.value().isEmpty() || cookie.value() == "null")
            continue;

        const QDateTime expiry = cookie.expirationDate();
        if (expiry.isValid() && expiry <= QDateTime::currentDateTimeUtc()) {
            QFile::remove(path);
            return;
        }

        m_session = cookie.value();
        m_staleCookie = cookie;
        m_restoredSessionUnverified = true;
        return;
    }

    QFile::remove(path);
}

void FlathubAccount::saveSessionCookie(const QNetworkCookie &cookie)
{
    if (!isSessionCookie(cookie) || cookie.value().isEmpty() || cookie.value() == "null")
        return;

    const QString path = sessionFilePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qWarning() << "Flathub: could not create the folder for the saved login";
        return;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Flathub: could not save the login";
        return;
    }
    file.write(cookie.toRawForm(QNetworkCookie::Full));
    file.write("\n");
    if (!file.commit()) {
        qWarning() << "Flathub: could not finish saving the login";
        return;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

void FlathubAccount::clearSession()
{
    m_session.clear();
    m_restoreCookieAfterCheck = false;
    m_restoredSessionUnverified = false;
    QFile::remove(sessionFilePath());
}

void FlathubAccount::expireSession()
{
#ifdef HAVE_WEBENGINE
    if (m_profile)
        m_profile->cookieStore()->deleteAllCookies();
#endif
    clearSession();
}

void FlathubAccount::syncSessionCookieToProfile()
{
#ifdef HAVE_WEBENGINE
    if (!m_profile || m_session.isEmpty())
        return;

    QNetworkCookie cookie = m_staleCookie;
    if (cookie.value().isEmpty())
        cookie = QNetworkCookie(QByteArrayLiteral("session"), m_session);
    cookie.setValue(m_session);
    // The rotated cookie is a fresh session; its old expiry no longer applies.
    cookie.setExpirationDate(QDateTime());
    m_pendingCookieWrite = m_session;
    m_profile->cookieStore()->setCookie(cookie, QUrl(QStringLiteral("https://flathub.org")));
#endif
}

bool FlathubAccount::available() const
{
#ifdef HAVE_WEBENGINE
    return true;
#else
    return false;
#endif
}

QObject *FlathubAccount::profile() const
{
#ifdef HAVE_WEBENGINE
    return m_profile;
#else
    return nullptr;
#endif
}

QUrl FlathubAccount::loginUrl() const
{
    return QUrl(QStringLiteral("https://flathub.org/login"));
}

bool FlathubAccount::isSessionCookie(const QNetworkCookie &cookie) const
{
    if (cookie.name() != "session")
        return false;
    const QString domain = cookie.domain();
    return domain.isEmpty() || domain == kFlathubHost || domain == QLatin1String(".flathub.org");
}

void FlathubAccount::onCookieAdded(const QNetworkCookie &cookie)
{
    if (!isSessionCookie(cookie) || cookie.value().isEmpty() || cookie.value() == "null")
        return;

    if (cookie.value() == m_pendingCookieWrite)
        m_pendingCookieWrite.clear();

    const bool restoredValue = !m_staleCookie.value().isEmpty()
            && cookie.value() == m_staleCookie.value();

    if (!restoredValue && m_restoredSessionUnverified) {
        // The web profile may still hold an older cookie from before the app
        // was closed. The saved session wins until the server has checked it.
        syncSessionCookieToProfile();
        return;
    }

    // The cookie restored from disk may be replayed by the web profile after a
    // response already handed us a newer one. Never let the old value win.
    if (restoredValue) {
        if (m_session != cookie.value())
            syncSessionCookieToProfile();
        return;
    }

    const bool changed = m_session != cookie.value();
    if (changed) {
        m_session = cookie.value();
        saveSessionCookie(cookie);
    }

    if (!changed && (m_connected || m_checking))
        return;
    checkSession();
}

void FlathubAccount::onCookieRemoved(const QNetworkCookie &cookie)
{
    if (!isSessionCookie(cookie) || m_session.isEmpty() || cookie.value() != m_session)
        return;
    // Ignore the removal that is part of a cookie the app just wrote itself.
    if (cookie.value() == m_pendingCookieWrite)
        return;
    // The web profile may drop a cookie that the server still accepts (for
    // example when its short-lived copy expires). Ask the server before
    // signing out and put the cookie back if the login still holds.
    m_restoreCookieAfterCheck = true;
    checkSession();
}

QNetworkRequest FlathubAccount::apiRequest(const QString &path) const
{
    QNetworkRequest request(QUrl(QStringLiteral("https://flathub.org/api/v2") + path));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Katalog/1.0"));
    if (!m_session.isEmpty()) {
        const QList<QNetworkCookie> cookies{QNetworkCookie(QByteArrayLiteral("session"), m_session)};
        request.setHeader(QNetworkRequest::CookieHeader, QVariant::fromValue(cookies));
    }
    return request;
}

// Flathub renews the session cookie on every reply; keep the fallback file and
// the web profile's copy current so the login survives restarts.
bool FlathubAccount::storeResponseCookies(QNetworkReply *reply)
{
    bool stored = false;
    const auto cookies = reply->header(QNetworkRequest::SetCookieHeader).value<QList<QNetworkCookie>>();
    for (const QNetworkCookie &cookie : cookies) {
        if (!isSessionCookie(cookie) || cookie.value().isEmpty() || cookie.value() == "null")
            continue;
        m_session = cookie.value();
        saveSessionCookie(cookie);
        stored = true;
#ifdef HAVE_WEBENGINE
        m_pendingCookieWrite = cookie.value();
        m_profile->cookieStore()->setCookie(cookie, QUrl(QStringLiteral("https://flathub.org")));
#endif
    }
    return stored;
}

void FlathubAccount::checkSession()
{
    if (m_checking) {
        m_checkQueued = true;
        return;
    }
    if (m_session.isEmpty()) {
        setState(false, QString());
        return;
    }

    m_checking = true;
    Q_EMIT stateChanged();

    const QByteArray session = m_session;
    QNetworkReply *reply = m_nam->get(apiRequest(QStringLiteral("/auth/userinfo")));
    connect(reply, &QNetworkReply::finished, this, [this, reply, session]() {
        reply->deleteLater();
        m_checking = false;

        if (m_session != session) {
            // Another cookie arrived while this request was in flight; its
            // result is about to be obsolete. Check the current one instead.
            m_restoreCookieAfterCheck = false;
            m_checkQueued = true;
        } else {
            const int status = httpStatus(reply);
            bool connected = false;
            QString name;
            bool storedCookie = false;
            if (reply->error() == QNetworkReply::NoError && status == 200) {
                const QJsonObject info = QJsonDocument::fromJson(reply->readAll()).object();
                if (!info.isEmpty()) {
                    connected = true;
                    storedCookie = storeResponseCookies(reply);
                    name = info.value(QStringLiteral("displayname")).toString();
                    if (name.isEmpty())
                        name = info.value(QStringLiteral("default_account")).toObject().value(QStringLiteral("login")).toString();
                    if (name.isEmpty())
                        name = tr("Flathub user");
                }
            } else if (status == 401 || status == 403) {
                expireSession();
            } else if (reply->error() != QNetworkReply::NoError) {
                qWarning() << "Flathub: could not check the login:" << reply->errorString();
            }
            if (m_restoreCookieAfterCheck && connected && !storedCookie)
                syncSessionCookieToProfile();
            m_restoreCookieAfterCheck = false;
            setState(connected, name);
        }

        if (m_restoredSessionUnverified && session == m_staleCookie.value())
            m_restoredSessionUnverified = false;

        if (m_checkQueued) {
            m_checkQueued = false;
            checkSession();
        }
    });
}

void FlathubAccount::setState(bool connected, const QString &displayName)
{
    m_connected = connected;
    m_displayName = displayName;
    Q_EMIT stateChanged();
}

void FlathubAccount::fetchFavorites()
{
    if (m_session.isEmpty()) {
        Q_EMIT favoritesFailed(tr("You are not signed in to Flathub."));
        return;
    }

    QNetworkReply *reply = m_nam->get(apiRequest(QStringLiteral("/favorites")));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        const int status = httpStatus(reply);
        if (status == 401 || status == 403) {
            expireSession();
            setState(false, QString());
            Q_EMIT favoritesFailed(tr("Your Flathub login has expired. Connect again in Settings."));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT favoritesFailed(tr("Could not load your Flathub bookmarks: %1").arg(reply->errorString()));
            return;
        }
        storeResponseCookies(reply);

        QStringList appIds;
        const QJsonArray favorites = QJsonDocument::fromJson(reply->readAll()).array();
        for (const QJsonValue &value : favorites) {
            const QString appId = value.toObject().value(QStringLiteral("app_id")).toString();
            if (!appId.isEmpty() && !appIds.contains(appId))
                appIds << appId;
        }
        Q_EMIT favoritesReady(appIds);
    });
}

void FlathubAccount::logout()
{
    if (!m_session.isEmpty()) {
        QNetworkReply *reply = m_nam->post(apiRequest(QStringLiteral("/auth/logout")), QByteArray());
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }
    expireSession();
    setState(false, QString());
}
