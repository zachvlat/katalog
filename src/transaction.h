#pragma once

#include <QObject>
#include <QString>

#ifdef HAVE_FLATPAK
struct _FlatpakInstallation;
typedef struct _FlatpakInstallation FlatpakInstallation;
#endif

class FlatpakManager;

class Transaction : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString appId READ appId CONSTANT)
    Q_PROPERTY(QString operation READ operation CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool finished READ finished NOTIFY finishedChanged)
    Q_PROPERTY(bool success READ success NOTIFY finishedChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY progressChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY finishedChanged)
public:
    enum Kind {
        Install,
        Update,
        Remove,
    };
    Q_ENUM(Kind)

    Transaction(FlatpakManager *manager, Kind kind, const QString &appId, const QString &ref = QString(),
                bool userInstallation = false, QObject *parent = nullptr);

    QString appId() const { return m_appId; }
    QString operation() const;
    bool running() const { return m_running; }
    bool finished() const { return m_finished; }
    bool success() const { return m_success; }
    int progress() const { return m_progress; }
    QString status() const { return m_status; }
    QString errorMessage() const { return m_errorMessage; }

    Q_INVOKABLE void start();

    void publishProgress(int progress, const QString &status);
    void setPendingError(const QString &message);

Q_SIGNALS:
    void runningChanged();
    void finishedChanged();
    void progressChanged();
    void completed();

private:
    void runWorker();

    FlatpakManager *m_manager = nullptr;
    QString m_pendingError;
    Kind m_kind;
    QString m_appId;
    QString m_ref;
    bool m_userInstallation = false;

    bool m_running = false;
    bool m_finished = false;
    bool m_success = false;
    int m_progress = 0;
    QString m_status;
    QString m_errorMessage;
};
