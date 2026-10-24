#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

struct LeftoverData {
    QString appId;
    QString path;
    QString sizeText;
    qint64 size = 0;
    bool isUser = true;
};

struct LeftoverScan {
    QList<LeftoverData> items;
    QString error;
};

class LeftoverModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
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

    QString appIdForPath(const QString &path) const;

Q_SIGNALS:
    void countChanged();

private:
    QList<LeftoverData> m_leftovers;
};
