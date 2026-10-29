#include "leftovermodel.h"

LeftoverModel::LeftoverModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LeftoverModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_leftovers.size();
}

QVariant LeftoverModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_leftovers.size())
        return QVariant();

    const LeftoverData &leftover = m_leftovers.at(index.row());
    switch (role) {
    case AppIdRole: return leftover.appId;
    case PathRole: return leftover.path;
    case SizeTextRole: return leftover.sizeText;
    case SizeRole: return leftover.size;
    case IsUserRole: return leftover.isUser;
    case ScopeRole: return leftover.isUser ? QStringLiteral("user") : QStringLiteral("system");
    }
    return QVariant();
}

QHash<int, QByteArray> LeftoverModel::roleNames() const
{
    return {
        {AppIdRole, "appId"},
        {PathRole, "path"},
        {SizeTextRole, "sizeText"},
        {SizeRole, "size"},
        {IsUserRole, "isUser"},
        {ScopeRole, "scope"},
    };
}

void LeftoverModel::setLeftovers(const QList<LeftoverData> &leftovers)
{
    beginResetModel();
    m_leftovers = leftovers;
    endResetModel();
    Q_EMIT countChanged();
}

QString LeftoverModel::appIdForPath(const QString &path) const
{
    for (const LeftoverData &leftover : m_leftovers) {
        if (leftover.path == path)
            return leftover.appId;
    }
    return QString();
}
