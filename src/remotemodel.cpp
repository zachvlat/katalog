#include "remotemodel.h"

RemoteModel::RemoteModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int RemoteModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_remotes.size();
}

QVariant RemoteModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_remotes.size())
        return QVariant();

    const RemoteData &remote = m_remotes.at(index.row());
    switch (role) {
    case NameRole: return remote.name;
    case TitleRole: return remote.title;
    case UrlRole: return remote.url;
    case DisabledRole: return remote.disabled;
    case IsUserRole: return remote.isUser;
    case ScopeRole: return remote.isUser ? QStringLiteral("user") : QStringLiteral("system");
    }
    return QVariant();
}

QHash<int, QByteArray> RemoteModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {TitleRole, "title"},
        {UrlRole, "url"},
        {DisabledRole, "disabled"},
        {IsUserRole, "isUser"},
        {ScopeRole, "scope"},
    };
}

void RemoteModel::setRemotes(const QList<RemoteData> &remotes)
{
    beginResetModel();
    m_remotes = remotes;
    endResetModel();
}
