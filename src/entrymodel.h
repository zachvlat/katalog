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
