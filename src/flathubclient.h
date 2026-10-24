#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include "entrymodel.h"

class QNetworkAccessManager;
class QNetworkReply;

class FlathubClient : public QObject {
    Q_OBJECT
public:
    explicit FlathubClient(QObject *parent = nullptr);

    static const char *const TrendingKey;
    static const char *const PopularKey;
    static const char *const NewKey;
    static const char *const UpdatedKey;

    void fetchExplore();

    void fetchVerifiedApps();
    const QSet<QString> &verifiedAppIds() const { return m_verifiedAppIds; }

    QList<EntryData> collection(const QString &key) const { return m_collections.value(key); }

Q_SIGNALS:
    void collectionReady(const QString &key, const QList<EntryData> &entries);
    void allFinished();
    void verifiedAppsReady();

private:
    void handleReply(const QString &key, QNetworkReply *reply);
    void requestVerifiedPage(int page);
    void handleVerifiedPage(int page, QNetworkReply *reply);
    void finishVerifiedApps();

    QNetworkAccessManager *m_nam = nullptr;
    QHash<QString, QList<EntryData>> m_collections;
    int m_pending = 0;

    QSet<QString> m_verifiedAppIds;
    int m_verifiedPagesPending = 0;
    bool m_verifiedLoading = false;
    bool m_verifiedLoaded = false;
};
