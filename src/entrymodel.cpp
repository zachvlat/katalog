#include "entrymodel.h"

EntryModel::EntryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int EntryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_entries.size();
}

QVariant EntryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return QVariant();
    const auto &e = m_entries.at(index.row());
    switch (role) {
    case AppIdRole: return e.appId;
    case NameRole: return e.name;
    case SummaryRole: return e.summary;
    case IconRole: return e.icon;
    case InstalledRole: return e.installed;
    }
    return QVariant();
}

QHash<int, QByteArray> EntryModel::roleNames() const
{
    return {
        {AppIdRole, "appId"},
        {NameRole, "name"},
        {SummaryRole, "summary"},
        {IconRole, "icon"},
        {InstalledRole, "installed"}
    };
}

void EntryModel::append(const EntryData &entry)
{
    beginInsertRows(QModelIndex(), m_entries.size(), m_entries.size());
    m_entries.append(entry);
    endInsertRows();
}

void EntryModel::clear()
{
    beginResetModel();
    m_entries.clear();
    endResetModel();
}
