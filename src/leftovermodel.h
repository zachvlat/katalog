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

// The result of one scan. When error is set the scan was refused (it could not
// tell which apps are installed) and items is empty.
struct LeftoverScan {
    QList<LeftoverData> items;
    QString error;
};

class LeftoverModel : public QAbstractListModel {
    Q_OBJECT
    // Row count for QML bindings (C++ models have no built-in "count").
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

    // The app id of the listed leftover at path, or an empty string if the
    // path is not one of the listed leftovers.
    QString appIdForPath(const QString &path) const;

Q_SIGNALS:
    void countChanged();

private:
    QList<LeftoverData> m_leftovers;
};
