#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QString>

struct RemoteData {
    QString name;
    QString title;
    QString url;
    bool disabled = false;
    bool isUser = false;
};

struct RemoteOpResult {
    bool succeeded = false;
    bool reloadExplore = false;
    QString message;
};

class RemoteModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        TitleRole,
        UrlRole,
        DisabledRole,
        IsUserRole,
        ScopeRole,
        InstalledAppsRole,
    };

    explicit RemoteModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRemotes(const QList<RemoteData> &remotes);
    void setInstalledCounts(const QHash<QString, int> &appsPerRemote);

private:
    QList<RemoteData> m_remotes;
    QHash<QString, int> m_appsPerRemote;
};
