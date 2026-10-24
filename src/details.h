#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

struct ScreenshotData {
    QString url;
    QString caption;
};

struct ReleaseData {
    QString version;
    QString date;
    QString description;
};

struct AppDetailData {
    bool found = false;

    QString appId;
    QString name;
    QString developer;
    QString summary;
    QString description;
    QString license;
    QString version;
    QString runtime;
    QString icon;

    QString homepage;
    QString bugtracker;
    QString help;
    QString translate;
    QString donation;
    QString contact;

    QString downloadSize;
    QString installedSize;
    QStringList permissions;
    QStringList categories;

    QList<ScreenshotData> screenshots;
    QList<ReleaseData> releases;

    bool installed = false;
    bool updateAvailable = false;
    bool verified = false;
    QString ref;
    bool userInstallation = false;
    bool isRuntime = false;

    quint64 generation = 0;
};

class Details : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool found READ found NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString appId READ appId NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString developer READ developer NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString description READ description NOTIFY changed)
    Q_PROPERTY(QString license READ license NOTIFY changed)
    Q_PROPERTY(QString version READ version NOTIFY changed)
    Q_PROPERTY(QString runtime READ runtime NOTIFY changed)
    Q_PROPERTY(QString icon READ icon NOTIFY changed)
    Q_PROPERTY(QString homepage READ homepage NOTIFY changed)
    Q_PROPERTY(QString bugtracker READ bugtracker NOTIFY changed)
    Q_PROPERTY(QString help READ help NOTIFY changed)
    Q_PROPERTY(QString translate READ translate NOTIFY changed)
    Q_PROPERTY(QString donation READ donation NOTIFY changed)
    Q_PROPERTY(QString contact READ contact NOTIFY changed)
    Q_PROPERTY(QString downloadSize READ downloadSize NOTIFY changed)
    Q_PROPERTY(QString installedSize READ installedSize NOTIFY changed)
    Q_PROPERTY(QStringList permissions READ permissions NOTIFY changed)
    Q_PROPERTY(QStringList categories READ categories NOTIFY changed)
    Q_PROPERTY(QVariantList screenshots READ screenshots NOTIFY changed)
    Q_PROPERTY(QVariantList releases READ releases NOTIFY changed)
    Q_PROPERTY(bool installed READ installed NOTIFY changed)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY changed)
    Q_PROPERTY(bool verified READ verified NOTIFY changed)
    Q_PROPERTY(QString ref READ ref NOTIFY changed)
    Q_PROPERTY(bool userInstallation READ userInstallation NOTIFY changed)
    Q_PROPERTY(bool isRuntime READ isRuntime NOTIFY changed)
public:
    explicit Details(QObject *parent = nullptr);

    bool found() const { return m_data.found; }
    bool loading() const { return m_loading; }
    QString appId() const { return m_data.appId; }
    QString name() const { return m_data.name; }
    QString developer() const { return m_data.developer; }
    QString summary() const { return m_data.summary; }
    QString description() const { return m_data.description; }
    QString license() const { return m_data.license; }
    QString version() const { return m_data.version; }
    QString runtime() const { return m_data.runtime; }
    QString icon() const { return m_data.icon; }
    QString homepage() const { return m_data.homepage; }
    QString bugtracker() const { return m_data.bugtracker; }
    QString help() const { return m_data.help; }
    QString translate() const { return m_data.translate; }
    QString donation() const { return m_data.donation; }
    QString contact() const { return m_data.contact; }
    QString downloadSize() const { return m_data.downloadSize; }
    QString installedSize() const { return m_data.installedSize; }
    QStringList permissions() const { return m_data.permissions; }
    QStringList categories() const { return m_data.categories; }
    QVariantList screenshots() const;
    QVariantList releases() const;
    bool installed() const { return m_data.installed; }
    bool updateAvailable() const { return m_data.updateAvailable; }
    bool verified() const { return m_data.verified; }
    QString ref() const { return m_data.ref; }
    bool userInstallation() const { return m_data.userInstallation; }
    bool isRuntime() const { return m_data.isRuntime; }

    void setLoading(bool loading);
    void setVerified(bool verified);
    void reset(const QString &appId);
    void apply(const AppDetailData &data);

Q_SIGNALS:
    void changed();

private:
    AppDetailData m_data;
    bool m_loading = false;
};
