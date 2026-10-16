#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include "details.h"
#include "entrymodel.h"
#include "flatpakinstance.h"
#include "flathubclient.h"
#include "leftovermodel.h"
#include "remotemodel.h"
#include "transaction.h"

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(FlatpakManager *flatpakInstance READ flatpakInstance CONSTANT)
    Q_PROPERTY(EntryModel *searchResults READ searchResults CONSTANT)
    Q_PROPERTY(EntryModel *installedApps READ installedApps CONSTANT)
    Q_PROPERTY(EntryModel *updates READ updates CONSTANT)
    Q_PROPERTY(QVariantList exploreCurated READ exploreCurated NOTIFY exploreChanged)
    Q_PROPERTY(QVariantList exploreSections READ exploreSections NOTIFY exploreChanged)
    Q_PROPERTY(bool exploring READ exploring NOTIFY exploreChanged)
    Q_PROPERTY(EntryModel *categoryApps READ categoryApps CONSTANT)
    Q_PROPERTY(bool categoryLoading READ categoryLoading NOTIFY categoryChanged)
    Q_PROPERTY(Details *details READ details CONSTANT)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(Transaction *currentTransaction READ currentTransaction NOTIFY currentTransactionChanged)
    Q_PROPERTY(RemoteModel *remotes READ remotes CONSTANT)
    Q_PROPERTY(LeftoverModel *leftovers READ leftovers CONSTANT)
    Q_PROPERTY(bool scanningLeftovers READ scanningLeftovers NOTIFY leftoversChanged)
public:
    explicit Backend(QObject *parent = nullptr);

    FlatpakManager *flatpakInstance() const { return m_flatpak; }
    EntryModel *searchResults() const { return m_search; }
    EntryModel *installedApps() const { return m_installed; }
    EntryModel *updates() const { return m_updates; }
    QVariantList exploreCurated() const { return m_exploreCurated; }
    QVariantList exploreSections() const { return m_exploreSections; }
    bool exploring() const { return m_exploring; }
    EntryModel *categoryApps() const { return m_category; }
    bool categoryLoading() const { return m_categoryLoading; }
    Details *details() const { return m_details; }
    bool searching() const { return m_searching; }
    Transaction *currentTransaction() const { return m_transaction; }
    RemoteModel *remotes() const { return m_remotes; }
    LeftoverModel *leftovers() const { return m_leftovers; }
    bool scanningLeftovers() const { return m_scanningLeftovers; }

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

    // Repositories (Flatpak remotes).
    Q_INVOKABLE void refreshRemotes();
    Q_INVOKABLE void addRemote(const QString &name, const QString &url, bool user);
    Q_INVOKABLE void removeRemote(const QString &name, bool user);
    Q_INVOKABLE void setRemoteEnabled(const QString &name, bool user, bool enabled);

    // Leftover data directories of uninstalled apps.
    Q_INVOKABLE void refreshLeftovers();
    Q_INVOKABLE void removeLeftover(const QString &path);

    // Manually trigger the background update check (also runs on a timer).
    Q_INVOKABLE void checkForUpdates();

Q_SIGNALS:
    void searchingChanged();
    void exploreChanged();
    void categoryChanged();
    void currentTransactionChanged();
    void leftoversChanged();

private:
    void startTransaction(int kind, const QString &appId);
    void explorePartDone();
    void buildExplore();
    void notifyUpdates(int count);

    FlatpakManager *m_flatpak = nullptr;
    EntryModel *m_search = nullptr;
    EntryModel *m_installed = nullptr;
    EntryModel *m_updates = nullptr;
    EntryModel *m_category = nullptr;
    Details *m_details = nullptr;
    Transaction *m_transaction = nullptr;
    RemoteModel *m_remotes = nullptr;
    LeftoverModel *m_leftovers = nullptr;
    FlathubClient *m_flathub = nullptr;

    QVariantList m_exploreCurated;
    QVariantList m_exploreSections;
    QList<EntryModel *> m_exploreModels;
    QList<ExploreSectionData> m_exploreCategories;
    QList<EntryData> m_recentNew;
    QList<EntryData> m_recentUpdated;
    int m_explorePending = 0;
    bool m_exploring = false;
    bool m_categoryLoading = false;

    QTimer *m_updateTimer = nullptr;
    int m_lastNotifiedUpdates = -1;

    bool m_scanningLeftovers = false;

    QFutureWatcher<QList<EntryData>> *m_watcher = nullptr;
    QFutureWatcher<AppDetailData> *m_detailsWatcher = nullptr;
    QFutureWatcher<ExploreResult> *m_exploreWatcher = nullptr;
    QFutureWatcher<QList<EntryData>> *m_categoryWatcher = nullptr;
    QFutureWatcher<QList<LeftoverData>> *m_leftoverWatcher = nullptr;
    quint64 m_searchGeneration = 0;
    quint64 m_detailsGeneration = 0;
    quint64 m_categoryGeneration = 0;
    bool m_searching = false;
};
