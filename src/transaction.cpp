#ifdef HAVE_FLATPAK
#include <flatpak/flatpak.h>
#endif

#include "transaction.h"
#include "flatpakinstance.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QDebug>

#ifdef HAVE_FLATPAK
namespace {

struct ResolvedOperation {
    FlatpakInstallation *installation = nullptr;
    QString installedRef;
    QString remoteName;
    QString remoteRef;
};

FlatpakRef *findAppRef(GPtrArray *refs, const QString &appId)
{
    constexpr qsizetype desktopSuffixLength = 8;
    const QString wanted =
        appId.endsWith(QLatin1String(".desktop")) ? appId.chopped(desktopSuffixLength) : appId;

    for (guint i = 0; i < refs->len; ++i) {
        auto *ref = FLATPAK_REF(g_ptr_array_index(refs, i));
        const auto kind = flatpak_ref_get_kind(ref);
        if (kind != FLATPAK_REF_KIND_APP && kind != FLATPAK_REF_KIND_RUNTIME)
            continue;
        if (wanted == QString::fromUtf8(flatpak_ref_get_name(ref)))
            return ref;
    }
    return nullptr;
}

bool resolveOperation(FlatpakManager *manager, Transaction::Kind kind, const QString &appId, const QString &exactRef,
                      bool userInstallation, ResolvedOperation &out)
{
    if (!exactRef.isEmpty()) {
        FlatpakInstallation *installation =
            userInstallation ? manager->userInstallation() : manager->systemInstallation();
        if (!installation)
            return false;
        g_autoptr(GPtrArray) refs = flatpak_installation_list_installed_refs(installation, nullptr, nullptr);
        if (!refs)
            return false;
        for (guint i = 0; i < refs->len; ++i) {
            g_autofree char *formatted = flatpak_ref_format_ref(FLATPAK_REF(g_ptr_array_index(refs, i)));
            if (exactRef == QString::fromUtf8(formatted)) {
                out.installation = installation;
                out.installedRef = exactRef;
                return true;
            }
        }
        return false;
    }

    QList<FlatpakInstallation *> installations;
    if (auto *system = manager->systemInstallation())
        installations << system;
    if (auto *user = manager->userInstallation())
        installations << user;

    for (FlatpakInstallation *installation : installations) {
        g_autoptr(GPtrArray) refs = flatpak_installation_list_installed_refs(installation, nullptr, nullptr);
        if (!refs)
            continue;
        if (FlatpakRef *ref = findAppRef(refs, appId)) {
            g_autofree char *formatted = flatpak_ref_format_ref(ref);
            out.installation = installation;
            out.installedRef = QString::fromUtf8(formatted);
            break;
        }
    }

    if (!out.installedRef.isEmpty())
        return true;

    if (kind != Transaction::Install)
        return false;

    for (FlatpakInstallation *installation : installations) {
        g_autoptr(GPtrArray) remotes = flatpak_installation_list_remotes(installation, nullptr, nullptr);
        if (!remotes)
            continue;
        for (guint i = 0; i < remotes->len; ++i) {
            auto *remote = FLATPAK_REMOTE(g_ptr_array_index(remotes, i));
            const QString remoteName = QString::fromUtf8(flatpak_remote_get_name(remote));
            GError *error = nullptr;
            g_autoptr(GPtrArray) refs = flatpak_installation_list_remote_refs_sync(
                installation, remoteName.toUtf8().constData(), nullptr, &error);
            if (!refs) {
                g_clear_error(&error);
                continue;
            }
            if (FlatpakRef *ref = findAppRef(refs, appId)) {
                g_autofree char *formatted = flatpak_ref_format_ref(ref);
                out.installation = installation;
                out.remoteName = remoteName;
                out.remoteRef = QString::fromUtf8(formatted);
                return true;
            }
        }
    }
    return false;
}

}

static void onNewOperation(FlatpakTransaction *, FlatpakTransactionOperation *operation,
                           FlatpakTransactionProgress *progress, gpointer user_data)
{
    auto *self = static_cast<Transaction *>(user_data);
    const char *ref = flatpak_transaction_operation_get_ref(operation);
    self->publishProgress(0, QString::fromUtf8(ref ? ref : ""));
    flatpak_transaction_progress_set_update_frequency(progress, 200);
    g_signal_connect(progress, "changed", G_CALLBACK(+[](FlatpakTransactionProgress *p, gpointer data) {
        auto *instance = static_cast<Transaction *>(data);
        const int value = flatpak_transaction_progress_get_progress(p);
        g_autofree char *status = flatpak_transaction_progress_get_status(p);
        instance->publishProgress(value, QString::fromUtf8(status ? status : ""));
    }), self);
}

static void onOperationDone(FlatpakTransaction *, FlatpakTransactionOperation *operation,
                            const char *, FlatpakTransactionResult, gpointer user_data)
{
    auto *self = static_cast<Transaction *>(user_data);
    const char *ref = flatpak_transaction_operation_get_ref(operation);
    self->publishProgress(0, QString::fromUtf8(ref ? ref : ""));
}

static gboolean onOperationError(FlatpakTransaction *, FlatpakTransactionOperation *operation,
                                 const GError *error, FlatpakTransactionErrorDetails, gpointer user_data)
{
    auto *self = static_cast<Transaction *>(user_data);
    const char *ref = flatpak_transaction_operation_get_ref(operation);
    const QString message = QStringLiteral("%1: %2")
                                .arg(QString::fromUtf8(ref ? ref : ""),
                                     QString::fromUtf8(error->message ? error->message : "unknown error"));
    self->setPendingError(message);
    return FALSE;
}
#endif

Transaction::Transaction(FlatpakManager *manager, Kind kind, const QString &appId, const QString &ref,
                         bool userInstallation, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
    , m_kind(kind)
    , m_appId(appId)
    , m_ref(ref)
    , m_userInstallation(userInstallation)
{
}

QString Transaction::operation() const
{
    switch (m_kind) {
    case Install:
        return QStringLiteral("install");
    case Update:
        return QStringLiteral("update");
    case Remove:
        return QStringLiteral("remove");
    }
    return QString();
}

void Transaction::start()
{
    if (m_running)
        return;
    m_running = true;
    Q_EMIT runningChanged();
    QFuture<void> future = QtConcurrent::run([this]() { runWorker(); });
    Q_UNUSED(future)
}

void Transaction::publishProgress(int progress, const QString &status)
{
    QMetaObject::invokeMethod(
        this,
        [this, progress, status]() {
            m_progress = progress;
            if (!status.isEmpty())
                m_status = status;
            Q_EMIT progressChanged();
        },
        Qt::QueuedConnection);
}

void Transaction::setPendingError(const QString &message)
{
    m_pendingError = message;
}

void Transaction::runWorker()
{
    bool success = false;
    QString errorMessage;

#ifdef HAVE_FLATPAK
    ResolvedOperation resolved;
    if (!resolveOperation(m_manager, m_kind, m_appId, m_ref, m_userInstallation, resolved) || !resolved.installation) {
        errorMessage = tr("Could not find “%1” in any remote.").arg(m_appId);
    } else {
        GError *error = nullptr;
        FlatpakTransaction *txn = flatpak_transaction_new_for_installation(resolved.installation, nullptr, &error);
        if (!txn) {
            errorMessage = QString::fromUtf8(error ? error->message : "Could not create transaction");
            g_clear_error(&error);
        } else {
            flatpak_transaction_set_no_interaction(txn, FALSE);
            flatpak_transaction_add_default_dependency_sources(txn);

            g_signal_connect(txn, "new-operation", G_CALLBACK(onNewOperation), this);
            g_signal_connect(txn, "operation-done", G_CALLBACK(onOperationDone), this);
            g_signal_connect(txn, "operation-error", G_CALLBACK(onOperationError), this);

            gboolean added = FALSE;
            if (m_kind == Remove) {
                added = flatpak_transaction_add_uninstall(txn, resolved.installedRef.toUtf8().constData(), &error);
            } else if (m_kind == Update || !resolved.installedRef.isEmpty()) {
                added = flatpak_transaction_add_update(txn, resolved.installedRef.toUtf8().constData(),
                                                       nullptr, nullptr, &error);
            } else {
                added = flatpak_transaction_add_install(txn, resolved.remoteName.toUtf8().constData(),
                                                        resolved.remoteRef.toUtf8().constData(), nullptr, &error);
            }

            if (!added) {
                errorMessage = QString::fromUtf8(error ? error->message : "Could not add operation");
                g_clear_error(&error);
            } else {
                success = flatpak_transaction_run(txn, nullptr, &error);
                if (!success) {
                    errorMessage = !m_pendingError.isEmpty()
                                       ? m_pendingError
                                       : QString::fromUtf8(error ? error->message : "Transaction failed");
                    g_clear_error(&error);
                }
            }
            g_object_unref(txn);
        }
    }
#else
    errorMessage = tr("Flatpak support is not available in this build.");
#endif

    const int finalProgress = success ? 100 : 0;
    const QString finalStatus = success ? tr("Done") : errorMessage;
    QMetaObject::invokeMethod(
        this,
        [this, success, finalProgress, finalStatus, errorMessage]() {
            m_progress = finalProgress;
            m_status = finalStatus;
            m_running = false;
            m_finished = true;
            m_success = success;
            if (!success)
                m_errorMessage = errorMessage;
            Q_EMIT runningChanged();
            Q_EMIT progressChanged();
            Q_EMIT finishedChanged();
            if (success)
                Q_EMIT completed();
        },
        Qt::QueuedConnection);
}
