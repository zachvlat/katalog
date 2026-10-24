#pragma once

#include <QAbstractItemModel>
#include <QFutureWatcher>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <QVariantList>

#include "details.h"
#include "entrymodel.h"
#include "flatpakinstance.h"
#include "flathubclient.h"
#include "leftovermodel.h"
#include "remotemodel.h"
#include "transaction.h"

class QNetworkAccessManager;
class QSortFilterProxyModel;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(FlatpakManager *flatpakInstance READ flatpakInstance CONSTANT)
    Q_PROPERTY(EntryModel *searchResults READ searchResults CONSTANT)
    Q_PROPERTY(EntryModel *installedApps READ installedApps CONSTANT)
    Q_PROPERTY(EntryModel *installedRuntimes READ installedRuntimes CONSTANT)
    Q_PROPERTY(EntryModel *updates READ updates CONSTANT)
    Q_PROPERTY(EntryModel *updatesApps READ updatesApps CONSTANT)
    Q_PROPERTY(EntryModel *updatesRuntimes READ updatesRuntimes CONSTANT)
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
    Q_PROPERTY(bool loadingInstalled READ loadingInstalled NOTIFY installedChanged)
    Q_PROPERTY(QAbstractItemModel *colorSchemes READ colorSchemes CONSTANT)
    Q_PROPERTY(int colorSchemeIndex READ colorSchemeIndex NOTIFY colorSchemeChanged)
    Q_PROPERTY(bool scanningLeftovers READ scanningLeftovers NOTIFY leftoversChanged)
    Q_PROPERTY(QString leftoverError READ leftoverError NOTIFY leftoversChanged)
    Q_PROPERTY(QString remoteMessage READ remoteMessage NOTIFY remoteMessageChanged)
    Q_PROPERTY(bool remoteBusy READ remoteBusy NOTIFY remoteMessageChanged)
    Q_PROPERTY(bool remoteSucceeded READ remoteSucceeded NOTIFY remoteMessageChanged)
public:
    explicit Backend(QObject *parent = nullptr);

    FlatpakManager *flatpakInstance() const { return m_flatpak; }
    EntryModel *searchResults() const { return m_search; }
    EntryModel *installedApps() const { return m_installed; }
    EntryModel *installedRuntimes() const { return m_installedRuntimes; }
    EntryModel *updates() const { return m_updates; }
    EntryModel *updatesApps() const { return m_updatesApps; }
    EntryModel *updatesRuntimes() const { return m_updatesRuntimes; }
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
    bool loadingInstalled() const { return m_loadingInstalled; }
    QAbstractItemModel *colorSchemes() const;
    int colorSchemeIndex() const;
    Q_INVOKABLE void setColorSchemeIndex(int row);
    bool scanningLeftovers() const { return m_scanningLeftovers; }
    QString leftoverError() const { return m_leftoverError; }
    QString remoteMessage() const { return m_remoteMessage; }
    bool remoteBusy() const { return m_remoteBusy; }
    bool remoteSucceeded() const { return m_remoteSucceeded; }

    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void refreshInstalled();
    Q_INVOKABLE void loadExplore();
    Q_INVOKABLE void loadCategory(const QString &category);
    Q_INVOKABLE void openApp(const QString &appId, const QString &ref = QString(), bool userInstallation = false);
    Q_INVOKABLE void installApp(const QString &appId);
    Q_INVOKABLE void updateApp(const QString &appId, const QString &ref = QString(), bool userInstallation = false);
    Q_INVOKABLE void updateAll();
    Q_INVOKABLE void removeApp(const QString &appId, bool deleteData = false, const QString &ref = QString(),
                               bool userInstallation = false);
    Q_INVOKABLE void launchApp(const QString &appId);
    Q_INVOKABLE void dismissTransaction();

    Q_INVOKABLE void refreshRemotes();
    Q_INVOKABLE void addRemote(const QString &name, const QString &url, bool user);
    Q_INVOKABLE void removeRemote(const QString &name, bool user);
    Q_INVOKABLE void setRemoteEnabled(const QString &name, bool user, bool enabled);

    Q_INVOKABLE void refreshLeftovers();
    Q_INVOKABLE void removeLeftover(const QString &path);

    Q_INVOKABLE void checkForUpdates();

Q_SIGNALS:
    void searchingChanged();
    void exploreChanged();
    void categoryChanged();
    void currentTransactionChanged();
    void installedChanged();
    void colorSchemeChanged();
    void leftoversChanged();
    void remoteMessageChanged();

private:
    void startTransaction(int kind, const QString &appId, bool deleteData = false, const QString &ref = QString(),
                          bool userInstallation = false);
    void startNextQueuedUpdate();
    bool beginRemoteOperation();
    void runRemoteOperation(bool user, std::function<RemoteOpResult(FlatpakInstallation *)> operation);
    void finishRemoteOperation(const RemoteOpResult &result);
    void explorePartDone();
    void buildExplore();
    void notifyUpdates(int count);
    void applyInstalledLists(const InstalledLists &lists);
    void applyVerifiedApps();
    void setUpColorSchemes();

    FlatpakManager *m_flatpak = nullptr;
    EntryModel *m_search = nullptr;
    EntryModel *m_installed = nullptr;
    EntryModel *m_installedRuntimes = nullptr;
    EntryModel *m_updates = nullptr;
    EntryModel *m_updatesApps = nullptr;
    EntryModel *m_updatesRuntimes = nullptr;
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
    QList<EntryData> m_updateQueue;
    int m_lastNotifiedUpdates = -1;
    bool m_loadingInstalled = false;
    bool m_installedRefreshQueued = false;
    bool m_notifyAfterRefresh = false;

    bool m_scanningLeftovers = false;
    QString m_leftoverError;

    QNetworkAccessManager *m_nam = nullptr;
    QSortFilterProxyModel *m_colorSchemes = nullptr;
    QString m_remoteMessage;
    bool m_remoteBusy = false;
    bool m_remoteSucceeded = false;

    QFutureWatcher<QList<EntryData>> *m_watcher = nullptr;
    QFutureWatcher<AppDetailData> *m_detailsWatcher = nullptr;
    QFutureWatcher<ExploreResult> *m_exploreWatcher = nullptr;
    QFutureWatcher<QList<EntryData>> *m_categoryWatcher = nullptr;
    QFutureWatcher<LeftoverScan> *m_leftoverWatcher = nullptr;
    QFutureWatcher<InstalledLists> *m_installedWatcher = nullptr;
    QFutureWatcher<RemoteOpResult> *m_remoteWatcher = nullptr;
    quint64 m_searchGeneration = 0;
    quint64 m_detailsGeneration = 0;
    quint64 m_categoryGeneration = 0;
    bool m_searching = false;
};
