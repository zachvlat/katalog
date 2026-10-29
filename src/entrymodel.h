#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

struct EntryData {
    QString appId;
    QString name;
    QString summary;
    QString icon;
    bool installed = false;
    bool verified = false;
    int kind = 0;
    QString ref;
    bool userInstallation = false;
};

struct ExploreSectionData {
    QString title;
    QString category;
    QString icon;
    QList<EntryData> entries;
};

struct ExploreResult {
    QList<ExploreSectionData> categories;
    QList<EntryData> newest;
    QList<EntryData> updated;
};

struct InstalledLists {
    QList<EntryData> installed;
    QList<EntryData> updates;
    QHash<QString, int> appsPerRemote;
};

class EntryModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles { AppIdRole = Qt::UserRole + 1, NameRole, SummaryRole, IconRole, InstalledRole, VerifiedRole, KindRole, RefRole,
                 UserInstallationRole };

    explicit EntryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void append(const EntryData &entry);
    void clear();
    void setEntries(const QList<EntryData> &entries);

    void setVerifiedIds(const QSet<QString> &ids);
    void setInstalledIds(const QSet<QString> &ids);

    const QList<EntryData> &entries() const { return m_entries; }

Q_SIGNALS:
    void countChanged();

private:
    QList<EntryData> m_entries;
};
