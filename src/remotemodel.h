#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

// A Flatpak remote (repository), as shown on the Repositories page.
struct RemoteData {
    QString name;
    QString title;
    QString url;
    bool disabled = false;
    bool isUser = false;
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
    };

    explicit RemoteModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRemotes(const QList<RemoteData> &remotes);

private:
    QList<RemoteData> m_remotes;
};
