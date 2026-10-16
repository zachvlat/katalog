#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

// A data directory that belongs to an application that is no longer installed,
// as shown on the Leftover Files page.
struct LeftoverData {
    QString appId;
    QString path;
    QString sizeText;
    qint64 size = 0;
    bool isUser = true;
};

class LeftoverModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        AppIdRole = Qt::UserRole + 1,
        PathRole,
        SizeTextRole,
        SizeRole,
        IsUserRole,
        ScopeRole,
    };

    explicit LeftoverModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setLeftovers(const QList<LeftoverData> &leftovers);

private:
    QList<LeftoverData> m_leftovers;
};
