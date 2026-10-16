#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include "entrymodel.h"

class QNetworkAccessManager;
class QNetworkReply;

// Fetches the curated "Explore" collections (trending, most popular, new and
// recently updated) from the Flathub web API. Each collection is a list of
// application ids with enough metadata (name, summary, icon) to build the tiles
// directly, so no AppStream round-trip is needed.
class FlathubClient : public QObject {
    Q_OBJECT
public:
    explicit FlathubClient(QObject *parent = nullptr);

    // Keys used in the bookkeeping and by the backend's row definitions.
    static const char *const TrendingKey;
    static const char *const PopularKey;
    static const char *const NewKey;
    static const char *const UpdatedKey;

    // Kick off all collection requests. Emits collectionReady() once per
    // collection and allFinished() after the last reply (success or failure).
    void fetchExplore();

    QList<EntryData> collection(const QString &key) const { return m_collections.value(key); }

Q_SIGNALS:
    void collectionReady(const QString &key, const QList<EntryData> &entries);
    void allFinished();

private:
    void handleReply(const QString &key, QNetworkReply *reply);

    QNetworkAccessManager *m_nam = nullptr;
    QHash<QString, QList<EntryData>> m_collections;
    int m_pending = 0;
};
