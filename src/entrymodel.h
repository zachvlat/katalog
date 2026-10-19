#pragma once

#include <QAbstractListModel>
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
};

// A titled group of entries, as shown on the Explore page.
struct ExploreSectionData {
    QString title;
    QString category;
    QString icon;
    QList<EntryData> entries;
};

// Everything the Explore page needs from a single (AppStream) worker pass. The
// curated Flathub API rows are fetched separately over the network; the two
// "recent" lists are the offline fallback derived from AppStream release dates.
struct ExploreResult {
    QList<ExploreSectionData> categories;
    QList<EntryData> newest;
    QList<EntryData> updated;
};

// The Installed and Updates lists, produced together by one worker pass.
struct InstalledLists {
    QList<EntryData> installed;
    QList<EntryData> updates;
};

class EntryModel : public QAbstractListModel {
    Q_OBJECT
    // Row count for QML bindings (C++ models have no built-in "count").
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles { AppIdRole = Qt::UserRole + 1, NameRole, SummaryRole, IconRole, InstalledRole, VerifiedRole };

    explicit EntryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void append(const EntryData &entry);
    void clear();
    void setEntries(const QList<EntryData> &entries);

    // Updates the verified flag of the rows whose app id is in the set. Used
    // when the Flathub verification list arrives after the rows were built.
    void setVerifiedIds(const QSet<QString> &ids);

    // Read-only access to the rows themselves, for code that has to walk them
    // (e.g. the backend's "update all" queue).
    const QList<EntryData> &entries() const { return m_entries; }

Q_SIGNALS:
    void countChanged();

private:
    QList<EntryData> m_entries;
};
