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
    case VerifiedRole: return e.verified;
    case KindRole: return e.kind;
    case RefRole: return e.ref;
    case UserInstallationRole: return e.userInstallation;
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
        {InstalledRole, "installed"},
        {VerifiedRole, "verified"},
        {KindRole, "kind"},
        {RefRole, "ref"},
        {UserInstallationRole, "userInstallation"}
    };
}

void EntryModel::append(const EntryData &entry)
{
    beginInsertRows(QModelIndex(), m_entries.size(), m_entries.size());
    m_entries.append(entry);
    endInsertRows();
    Q_EMIT countChanged();
}

void EntryModel::clear()
{
    beginResetModel();
    m_entries.clear();
    endResetModel();
    Q_EMIT countChanged();
}

void EntryModel::setEntries(const QList<EntryData> &entries)
{
    beginResetModel();
    m_entries = entries;
    endResetModel();
    Q_EMIT countChanged();
}

void EntryModel::setVerifiedIds(const QSet<QString> &ids)
{
    QList<int> changed;
    for (int row = 0; row < m_entries.size(); ++row) {
        const bool verified = m_entries.at(row).verified || ids.contains(m_entries.at(row).appId);
        if (m_entries[row].verified != verified) {
            m_entries[row].verified = verified;
            changed.append(row);
        }
    }
    for (int row : changed)
        Q_EMIT dataChanged(index(row, 0), index(row, 0), {VerifiedRole});
}

void EntryModel::setInstalledIds(const QSet<QString> &ids)
{
    QList<int> changed;
    for (int row = 0; row < m_entries.size(); ++row) {
        const bool installed = ids.contains(m_entries.at(row).appId);
        if (m_entries[row].installed != installed) {
            m_entries[row].installed = installed;
            changed.append(row);
        }
    }
    for (int row : changed)
        Q_EMIT dataChanged(index(row, 0), index(row, 0), {InstalledRole});
}
