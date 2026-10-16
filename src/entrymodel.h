#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

struct EntryData {
    QString appId;
    QString name;
    QString summary;
    QString icon;
    bool installed = false;
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

class EntryModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { AppIdRole = Qt::UserRole + 1, NameRole, SummaryRole, IconRole, InstalledRole };

    explicit EntryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void append(const EntryData &entry);
    void clear();

private:
    QList<EntryData> m_entries;
};
