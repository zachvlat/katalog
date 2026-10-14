#pragma once

#include <QFutureWatcher>
#include <QObject>
#include "details.h"
#include "entrymodel.h"
#include "flatpakinstance.h"
#include "transaction.h"

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(FlatpakManager *flatpakInstance READ flatpakInstance CONSTANT)
    Q_PROPERTY(EntryModel *searchResults READ searchResults CONSTANT)
    Q_PROPERTY(EntryModel *installedApps READ installedApps CONSTANT)
    Q_PROPERTY(EntryModel *updates READ updates CONSTANT)
    Q_PROPERTY(QVariantList exploreSections READ exploreSections NOTIFY exploreChanged)
    Q_PROPERTY(bool exploring READ exploring NOTIFY exploreChanged)
    Q_PROPERTY(EntryModel *categoryApps READ categoryApps CONSTANT)
    Q_PROPERTY(bool categoryLoading READ categoryLoading NOTIFY categoryChanged)
    Q_PROPERTY(Details *details READ details CONSTANT)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(Transaction *currentTransaction READ currentTransaction NOTIFY currentTransactionChanged)
public:
    explicit Backend(QObject *parent = nullptr);

    FlatpakManager *flatpakInstance() const { return m_flatpak; }
    EntryModel *searchResults() const { return m_search; }
    EntryModel *installedApps() const { return m_installed; }
    EntryModel *updates() const { return m_updates; }
    QVariantList exploreSections() const { return m_exploreSections; }
    bool exploring() const { return m_exploring; }
    EntryModel *categoryApps() const { return m_category; }
    bool categoryLoading() const { return m_categoryLoading; }
    Details *details() const { return m_details; }
    bool searching() const { return m_searching; }
    Transaction *currentTransaction() const { return m_transaction; }

    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void refreshInstalled();
    Q_INVOKABLE void loadExplore();
    Q_INVOKABLE void loadCategory(const QString &category);
    Q_INVOKABLE void openApp(const QString &appId);
    Q_INVOKABLE void installApp(const QString &appId);
    Q_INVOKABLE void updateApp(const QString &appId);
    Q_INVOKABLE void removeApp(const QString &appId);
    Q_INVOKABLE void launchApp(const QString &appId);
    Q_INVOKABLE void dismissTransaction();

Q_SIGNALS:
    void searchingChanged();
    void exploreChanged();
    void categoryChanged();
    void currentTransactionChanged();

private:
    void startTransaction(int kind, const QString &appId);

    FlatpakManager *m_flatpak = nullptr;
    EntryModel *m_search = nullptr;
    EntryModel *m_installed = nullptr;
    EntryModel *m_updates = nullptr;
    EntryModel *m_category = nullptr;
    Details *m_details = nullptr;
    Transaction *m_transaction = nullptr;

    QVariantList m_exploreSections;
    QList<EntryModel *> m_exploreModels;
    bool m_exploring = false;
    bool m_categoryLoading = false;

    QFutureWatcher<QList<EntryData>> *m_watcher = nullptr;
    QFutureWatcher<AppDetailData> *m_detailsWatcher = nullptr;
    QFutureWatcher<QList<ExploreSectionData>> *m_exploreWatcher = nullptr;
    QFutureWatcher<QList<EntryData>> *m_categoryWatcher = nullptr;
    quint64 m_searchGeneration = 0;
    quint64 m_detailsGeneration = 0;
    quint64 m_categoryGeneration = 0;
    bool m_searching = false;
};
