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
