#include "flathubaccount.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QNetworkRequest>

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
#ifdef HAVE_WEBENGINE
    m_profile = new QQuickWebEngineProfile(this);
    m_profile->setStorageName(QStringLiteral("flathub"));
    m_profile->setOffTheRecord(false);
    m_profile->setPersistentCookiesPolicy(QQuickWebEngineProfile::ForcePersistentCookies);

    QWebEngineCookieStore *store = m_profile->cookieStore();
    connect(store, &QWebEngineCookieStore::cookieAdded, this, &FlathubAccount::onCookieAdded);
    connect(store, &QWebEngineCookieStore::cookieRemoved, this, &FlathubAccount::onCookieRemoved);
    store->loadAllCookies();
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
    if (!isSessionCookie(cookie))
        return;
    m_session = cookie.value();
    // Cookies we copied over from our own API replies need no new check.
    if (m_session == m_storedSession)
        return;
    checkSession();
}

void FlathubAccount::onCookieRemoved(const QNetworkCookie &cookie)
{
    if (!isSessionCookie(cookie) || cookie.value() != m_session)
        return;
    m_session.clear();
    setState(false, QString());
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

// Flathub renews the session cookie on every reply; keep the web profile's copy
// current so the login survives restarts.
void FlathubAccount::storeResponseCookies(QNetworkReply *reply)
{
    const auto cookies = reply->header(QNetworkRequest::SetCookieHeader).value<QList<QNetworkCookie>>();
    for (const QNetworkCookie &cookie : cookies) {
        if (!isSessionCookie(cookie) || cookie.value().isEmpty() || cookie.value() == "null")
            continue;
        m_storedSession = cookie.value();
        m_session = cookie.value();
#ifdef HAVE_WEBENGINE
        m_profile->cookieStore()->setCookie(cookie, QUrl(QStringLiteral("https://flathub.org")));
#endif
    }
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

    QNetworkReply *reply = m_nam->get(apiRequest(QStringLiteral("/auth/userinfo")));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_checking = false;

        bool connected = false;
        QString name;
        if (reply->error() == QNetworkReply::NoError && httpStatus(reply) == 200) {
            const QJsonObject info = QJsonDocument::fromJson(reply->readAll()).object();
            if (!info.isEmpty()) {
                connected = true;
                storeResponseCookies(reply);
                name = info.value(QStringLiteral("displayname")).toString();
                if (name.isEmpty())
                    name = info.value(QStringLiteral("default_account")).toObject().value(QStringLiteral("login")).toString();
                if (name.isEmpty())
                    name = tr("Flathub user");
            }
        } else if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Flathub: could not check the login:" << reply->errorString();
        }
        setState(connected, name);

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
#ifdef HAVE_WEBENGINE
    m_profile->cookieStore()->deleteAllCookies();
#endif
    m_session.clear();
    m_storedSession.clear();
    setState(false, QString());
}
