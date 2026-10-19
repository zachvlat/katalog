#include "flathubclient.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

const char *const FlathubClient::TrendingKey = "trending";
const char *const FlathubClient::PopularKey = "popular";
const char *const FlathubClient::NewKey = "new";
const char *const FlathubClient::UpdatedKey = "updated";

namespace {

// How many tiles a curated row shows. The API returns the whole collection; we
// only need enough for a horizontal strip.
constexpr int kRowLimit = 24;

// The verified collection is paginated by Meilisearch; this is the largest page
// size the API accepts.
constexpr int kVerifiedPageSize = 250;

struct CollectionDef {
    const char *key;
    const char *path; // appended to the API base URL
};

const CollectionDef kCollections[] = {
    {FlathubClient::TrendingKey, "collection/trending"},
    {FlathubClient::PopularKey, "collection/popular"},
    {FlathubClient::NewKey, "collection/recently-added"},
    {FlathubClient::UpdatedKey, "collection/recently-updated"},
};

QList<EntryData> parseCollection(const QByteArray &data)
{
    QList<EntryData> entries;

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "Flathub: could not parse collection:" << parseError.errorString();
        return entries;
    }

    const QJsonArray hits = doc.object().value(QStringLiteral("hits")).toArray();
    for (const QJsonValue &value : hits) {
        if (entries.size() >= kRowLimit)
            break;
        const QJsonObject object = value.toObject();
        const QString appId = object.value(QStringLiteral("app_id")).toString();
        if (appId.isEmpty())
            continue;

        EntryData entry;
        entry.appId = appId;
        entry.name = object.value(QStringLiteral("name")).toString();
        if (entry.name.isEmpty())
            entry.name = appId;
        entry.summary = object.value(QStringLiteral("summary")).toString();
        entry.icon = object.value(QStringLiteral("icon")).toString();
        entry.verified = object.value(QStringLiteral("verification_verified")).toBool();
        entries.append(entry);
    }
    return entries;
}

} // namespace

FlathubClient::FlathubClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void FlathubClient::fetchExplore()
{
    m_collections.clear();
    m_pending = 0;

    for (const CollectionDef &definition : kCollections) {
        const QString key = QString::fromLatin1(definition.key);
        QNetworkRequest request(
            QUrl(QStringLiteral("https://flathub.org/api/v2/") + QLatin1String(definition.path)));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(15000);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("QuteStore/1.0"));

        QNetworkReply *reply = m_nam->get(request);
        ++m_pending;
        connect(reply, &QNetworkReply::finished, this, [this, key, reply]() { handleReply(key, reply); });
    }

    if (m_pending == 0)
        Q_EMIT allFinished();
}

void FlathubClient::fetchVerifiedApps()
{
    if (m_verifiedLoading || m_verifiedLoaded)
        return;

    m_verifiedLoading = true;
    m_verifiedAppIds.clear();
    m_verifiedPagesPending = 0;
    requestVerifiedPage(1);
}

void FlathubClient::requestVerifiedPage(int page)
{
    QNetworkRequest request(
        QUrl(QStringLiteral("https://flathub.org/api/v2/collection/verified?page=%1&per_page=%2")
                 .arg(page)
                 .arg(kVerifiedPageSize)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("QuteStore/1.0"));

    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, page, reply]() { handleVerifiedPage(page, reply); });
}

void FlathubClient::handleVerifiedPage(int page, QNetworkReply *reply)
{
    reply->deleteLater();

    QJsonDocument doc;
    if (reply->error() == QNetworkReply::NoError) {
        QJsonParseError parseError{};
        doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning() << "Flathub: could not parse verified apps:" << parseError.errorString();
            doc = QJsonDocument();
        }
    } else {
        qWarning() << "Flathub: request for verified apps failed:" << reply->errorString();
    }

    if (doc.isObject()) {
        const QJsonArray hits = doc.object().value(QStringLiteral("hits")).toArray();
        for (const QJsonValue &value : hits) {
            const QJsonObject object = value.toObject();
            const QString appId = object.value(QStringLiteral("app_id")).toString();
            if (!appId.isEmpty() && object.value(QStringLiteral("verification_verified")).toBool())
                m_verifiedAppIds.insert(appId);
        }

        if (page == 1) {
            const int totalPages = doc.object().value(QStringLiteral("totalPages")).toInt();
            m_verifiedPagesPending = qMax(0, totalPages - 1);
            if (m_verifiedPagesPending == 0) {
                finishVerifiedApps();
                return;
            }
            for (int next = 2; next <= totalPages; ++next)
                requestVerifiedPage(next);
            return;
        }
    } else if (page == 1) {
        // Nothing usable at all; allow a later retry.
        m_verifiedLoading = false;
        Q_EMIT verifiedAppsReady();
        return;
    }

    if (--m_verifiedPagesPending <= 0)
        finishVerifiedApps();
}

void FlathubClient::finishVerifiedApps()
{
    m_verifiedLoading = false;
    m_verifiedLoaded = true;
    Q_EMIT verifiedAppsReady();
}

void FlathubClient::handleReply(const QString &key, QNetworkReply *reply)
{
    reply->deleteLater();

    QList<EntryData> entries;
    if (reply->error() == QNetworkReply::NoError) {
        entries = parseCollection(reply->readAll());
    } else {
        qWarning() << "Flathub: request for" << key << "failed:" << reply->errorString();
    }

    m_collections.insert(key, entries);
    Q_EMIT collectionReady(key, entries);

    if (--m_pending <= 0)
        Q_EMIT allFinished();
}
