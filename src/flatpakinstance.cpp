#ifdef HAVE_FLATPAK
#include <flatpak/flatpak.h>
#endif

#include "flatpakinstance.h"

#include <QDebug>

#ifdef HAVE_FLATPAK
namespace {

// Locations that commonly hold the real Flatpak installation when the app runs
// inside a container (e.g. distrobox), where /var/lib/flatpak is not mounted.
const char *const kHostSystemDirs[] = {
    "/run/host/var/lib/flatpak",
    nullptr,
};

bool hasRemotes(FlatpakInstallation *installation)
{
    if (!installation)
        return false;
    GError *error = nullptr;
    g_autoptr(GPtrArray) remotes = flatpak_installation_list_remotes(installation, nullptr, &error);
    if (error) {
        g_clear_error(&error);
        return false;
    }
    return remotes && remotes->len > 0;
}

// If the default system installation is empty (container case), fall back to a
// host path that contains a usable installation.
FlatpakInstallation *detectSystemInstallation()
{
    GError *error = nullptr;
    FlatpakInstallation *installation = flatpak_installation_new_system(nullptr, &error);
    if (error) {
        qWarning() << "Could not open system installation:" << error->message;
        g_clear_error(&error);
    }
    if (hasRemotes(installation))
        return installation;

    for (int i = 0; kHostSystemDirs[i]; ++i) {
        const char *path = kHostSystemDirs[i];
        g_autoptr(GFile) file = g_file_new_for_path(path);
        if (!g_file_query_exists(file, nullptr))
            continue;

        error = nullptr;
        FlatpakInstallation *candidate = flatpak_installation_new_for_path(file, FALSE, nullptr, &error);
        if (error) {
            qWarning() << "Could not open installation at" << path << ":" << error->message;
            g_clear_error(&error);
        }
        if (candidate && hasRemotes(candidate)) {
            if (installation)
                g_object_unref(installation);
            qInfo() << "Using host Flatpak installation at" << path;
            return candidate;
        }
        if (candidate)
            g_object_unref(candidate);
    }

    return installation;
}

} // namespace
#endif

FlatpakManager::FlatpakManager(QObject *parent)
    : QObject(parent)
{
#ifdef HAVE_FLATPAK
    m_system = detectSystemInstallation();

    GError *error = nullptr;
    m_user = flatpak_installation_new_user(nullptr, &error);
    if (error) {
        qWarning() << "Could not open user installation:" << error->message;
        g_clear_error(&error);
    }
#endif
}

FlatpakManager::~FlatpakManager()
{
#ifdef HAVE_FLATPAK
    if (m_system) g_object_unref(m_system);
    if (m_user) g_object_unref(m_user);
#endif
}

bool FlatpakManager::hasSystem() const
{
#ifdef HAVE_FLATPAK
    return m_system != nullptr;
#else
    return false;
#endif
}

bool FlatpakManager::hasUser() const
{
#ifdef HAVE_FLATPAK
    return m_user != nullptr;
#else
    return false;
#endif
}

FlatpakInstallation *FlatpakManager::systemInstallation() const
{
#ifdef HAVE_FLATPAK
    return m_system;
#else
    return nullptr;
#endif
}

FlatpakInstallation *FlatpakManager::userInstallation() const
{
#ifdef HAVE_FLATPAK
    return m_user;
#else
    return nullptr;
#endif
}
