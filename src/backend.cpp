#ifdef HAVE_FLATPAK
#include <flatpak/flatpak.h>
#endif

#ifdef HAVE_APPSTREAM
#include <appstream.h>
#endif

#include "backend.h"
#include "transaction.h"

#ifdef HAVE_KNOTIFICATIONS
#include <KNotification>
#endif

#include <QtConcurrent/QtConcurrentRun>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>

namespace {

QString humanSize(quint64 bytes)
{
    if (bytes == 0)
        return QString();
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0)
        return QStringLiteral("%1 B").arg(bytes);
    return QStringLiteral("%1 %2").arg(QString::number(value, 'f', 1)).arg(QLatin1String(units[unit]));
}

#ifdef HAVE_FLATPAK
EntryData entryFromRef(FlatpakRef *ref, const QString &summary, bool installed)
{
    EntryData entry;
    entry.appId = QString::fromUtf8(flatpak_ref_get_name(ref));
    entry.name = entry.appId;
    entry.summary = summary;
    entry.installed = installed;
    return entry;
}

// Resolve the human-readable application name of an installed ref. Prefer the
// appdata <name> (e.g. "Firefox") and fall back to the ref name (the reverse-DNS
// app id, e.g. "org.mozilla.firefox") only when no appdata name is available.
QString installedDisplayName(FlatpakRef *ref)
{
    if (FLATPAK_IS_INSTALLED_REF(ref)) {
        if (const char *appdataName = flatpak_installed_ref_get_appdata_name(FLATPAK_INSTALLED_REF(ref))) {
            const QString name = QString::fromUtf8(appdataName);
            if (!name.isEmpty())
                return name;
        }
    }
    return QString::fromUtf8(flatpak_ref_get_name(ref));
}
#endif

} // namespace

#ifdef HAVE_APPSTREAM
namespace {

void addAppStreamLocation(AsPool *pool, const QString &directory)
{
    if (!directory.isEmpty())
        as_pool_add_extra_data_location(pool, directory.toUtf8().constData(), AS_FORMAT_STYLE_CATALOG);
}

void configureAppStreamPool(AsPool *pool, FlatpakManager *manager)
{
#ifdef HAVE_FLATPAK
    // Mirror Bazaar: read the AppStream bundle of every remote directly, so we
    // do not depend on libappstream's own Flatpak discovery.
    QList<FlatpakInstallation *> installations;
    if (auto *system = manager->systemInstallation())
        installations << system;
    if (auto *user = manager->userInstallation())
        installations << user;

    int added = 0;
    for (FlatpakInstallation *installation : installations) {
        g_autoptr(GPtrArray) remotes = flatpak_installation_list_remotes(installation, nullptr, nullptr);
        if (!remotes)
            continue;
        for (guint i = 0; i < remotes->len; ++i) {
            auto *remote = FLATPAK_REMOTE(g_ptr_array_index(remotes, i));
            g_autoptr(GFile) directory = flatpak_remote_get_appstream_dir(remote, nullptr);
            if (!directory)
                continue;
            g_autofree char *path = g_file_get_path(directory);
            addAppStreamLocation(pool, QString::fromUtf8(path));
            ++added;
        }
    }

    if (added == 0) {
        // Fall back to libappstream's built-in Flatpak discovery.
        as_pool_set_flags(pool, AS_POOL_FLAG_LOAD_FLATPAK);
    }
#else
    Q_UNUSED(manager)
#endif

    // Development/container escape hatch (e.g. when running inside a distrobox
    // where /var/lib/flatpak is not mounted).
    const QByteArray extra = qgetenv("QUTESTORE_APPSTREAM_DIRS");
    if (!extra.isEmpty()) {
        for (const QByteArray &dir : extra.split(':'))
            addAppStreamLocation(pool, QString::fromUtf8(dir));
    }
}

QString componentIconUrl(AsComponent *component)
{
    if (AsIcon *icon = as_component_get_icon_by_size(component, 128, 128)) {
        if (const gchar *url = as_icon_get_url(icon))
            return QString::fromUtf8(url);
        if (const gchar *name = as_icon_get_name(icon))
            return QString::fromUtf8(name);
    }
    return QString();
}

EntryData entryFromComponent(AsComponent *component)
{
    EntryData entry;
    const gchar *id = as_component_get_id(component);
    entry.appId = id ? QString::fromUtf8(id) : QString();
    const gchar *name = as_component_get_name(component);
    entry.name = name ? QString::fromUtf8(name) : entry.appId;
    const gchar *summary = as_component_get_summary(component);
    entry.summary = summary ? QString::fromUtf8(summary) : QString();
    entry.icon = componentIconUrl(component);
    return entry;
}

QList<EntryData> appStreamSearch(FlatpakManager *manager, const QString &query)
{
    QList<EntryData> results;

    AsPool *pool = as_pool_new();
    configureAppStreamPool(pool, manager);

    GError *error = nullptr;
    if (!as_pool_load(pool, nullptr, &error)) {
        qWarning() << "AppStream: failed to load pool:" << (error ? error->message : "unknown error");
        g_clear_error(&error);
        g_object_unref(pool);
        return results;
    }

    AsComponentBox *box = as_pool_search(pool, query.toUtf8().constData());
    if (box) {
        const guint count = as_component_box_len(box);
        for (guint i = 0; i < count; ++i) {
            AsComponent *component = as_component_box_index(box, i);
            const AsComponentKind kind = as_component_get_kind(component);
            if (kind != AS_COMPONENT_KIND_DESKTOP_APP && kind != AS_COMPONENT_KIND_GENERIC)
                continue;

            if (!as_component_get_id(component))
                continue;

            EntryData entry = entryFromComponent(component);
            entry.installed = false;
            results.append(entry);
        }
        g_object_unref(box);
    } else {
        qWarning() << "AppStream: search failed for" << query;
    }

    g_object_unref(pool);
    return results;
}

QString bestScreenshotUrl(AsScreenshot *screenshot)
{
    GPtrArray *images = as_screenshot_get_images_all(screenshot);
    if (!images)
        return QString();

    const gchar *best = nullptr;
    guint bestWidth = 0;
    for (guint i = 0; i < images->len; ++i) {
        auto *image = AS_IMAGE(g_ptr_array_index(images, i));
        const guint width = as_image_get_width(image);
        const gchar *url = as_image_get_url(image);
        if (!url)
            continue;
        // Prefer the largest bitmap; skip unknown-size (0x0) entries unless we
        // have nothing else.
        if (width > bestWidth) {
            bestWidth = width;
            best = url;
        } else if (!best) {
            best = url;
        }
    }
    return best ? QString::fromUtf8(best) : QString();
}

AppDetailData appStreamDetails(FlatpakManager *manager, const QString &appId)
{
    AppDetailData data;

    AsPool *pool = as_pool_new();
    configureAppStreamPool(pool, manager);

    GError *error = nullptr;
    if (!as_pool_load(pool, nullptr, &error)) {
        qWarning() << "AppStream: failed to load pool:" << (error ? error->message : "unknown error");
        g_clear_error(&error);
        g_object_unref(pool);
        return data;
    }

    AsComponentBox *box = as_pool_get_components_by_id(pool, appId.toUtf8().constData());
    if (box && as_component_box_len(box) > 0) {
        AsComponent *component = as_component_box_index(box, 0);

        data.found = true;
        data.appId = appId;

        if (const gchar *v = as_component_get_name(component))
            data.name = QString::fromUtf8(v);
        if (const gchar *v = as_component_get_summary(component))
            data.summary = QString::fromUtf8(v);
        if (const gchar *v = as_component_get_description(component))
            data.description = QString::fromUtf8(v);
        if (const gchar *v = as_component_get_project_license(component))
            data.license = QString::fromUtf8(v);
        if (AsDeveloper *dev = as_component_get_developer(component)) {
            if (const gchar *v = as_developer_get_name(dev))
                data.developer = QString::fromUtf8(v);
        }

        const struct {
            AsUrlKind kind;
            QString *target;
        } urlKinds[] = {
            {AS_URL_KIND_HOMEPAGE, &data.homepage},
            {AS_URL_KIND_BUGTRACKER, &data.bugtracker},
            {AS_URL_KIND_HELP, &data.help},
            {AS_URL_KIND_TRANSLATE, &data.translate},
            {AS_URL_KIND_DONATION, &data.donation},
            {AS_URL_KIND_CONTACT, &data.contact},
        };
        for (const auto &entry : urlKinds) {
            if (const gchar *v = as_component_get_url(component, entry.kind))
                *entry.target = QString::fromUtf8(v);
        }

        if (GPtrArray *categories = as_component_get_categories(component)) {
            for (guint i = 0; i < categories->len; ++i)
                data.categories << QString::fromUtf8(static_cast<const gchar *>(g_ptr_array_index(categories, i)));
        }

        if (AsIcon *icon = as_component_get_icon_by_size(component, 128, 128)) {
            if (const gchar *url = as_icon_get_url(icon))
                data.icon = QString::fromUtf8(url);
            else if (const gchar *name = as_icon_get_name(icon))
                data.icon = QString::fromUtf8(name);
        }

        if (GPtrArray *screenshots = as_component_get_screenshots_all(component)) {
            for (guint i = 0; i < screenshots->len; ++i) {
                auto *screenshot = AS_SCREENSHOT(g_ptr_array_index(screenshots, i));
                ScreenshotData shot;
                shot.url = bestScreenshotUrl(screenshot);
                if (const gchar *caption = as_screenshot_get_caption(screenshot))
                    shot.caption = QString::fromUtf8(caption);
                if (!shot.url.isEmpty())
                    data.screenshots << shot;
            }
        }

        if (AsReleaseList *releases = as_component_get_releases_plain(component)) {
            const guint count = as_release_list_get_size(releases);
            for (guint i = 0; i < count && i < 20; ++i) {
                AsRelease *release = as_release_list_index_safe(releases, i);
                if (!release)
                    continue;
                ReleaseData entry;
                if (const gchar *v = as_release_get_version(release))
                    entry.version = QString::fromUtf8(v);
                if (const gchar *v = as_release_get_date(release))
                    entry.date = QString::fromUtf8(v);
                if (const gchar *v = as_release_get_description(release))
                    entry.description = QString::fromUtf8(v);
                if (entry.version.isEmpty() && entry.date.isEmpty() && entry.description.isEmpty())
                    continue;
                data.releases << entry;
            }
            if (!data.releases.isEmpty() && data.version.isEmpty())
                data.version = data.releases.first().version;
        }

        g_object_unref(box);
    } else {
        qWarning() << "AppStream: no component found for" << appId;
    }

    g_object_unref(pool);
    return data;
}

ExploreResult appStreamExplore(FlatpakManager *manager)
{
    ExploreResult result;

    AsPool *pool = as_pool_new();
    configureAppStreamPool(pool, manager);

    GError *error = nullptr;
    if (!as_pool_load(pool, nullptr, &error)) {
        qWarning() << "AppStream: failed to load pool:" << (error ? error->message : "unknown error");
        g_clear_error(&error);
        g_object_unref(pool);
        return result;
    }

    // The curated rows shown on the Explore page. The category names are plain
    // XDG categories, which is what as_pool_get_components_by_categories wants.
    // The icons are the standard XDG category icons from Breeze.
    static const struct {
        const char *category;
        const char *title;
        const char *icon;
    } definitions[] = {
        {"AudioVideo", "Audio & Video", "applications-multimedia"},
        {"Development", "Developer Tools", "applications-development"},
        {"Education", "Education", "applications-education"},
        {"Game", "Games", "applications-games"},
        {"Graphics", "Graphics & Photography", "applications-graphics"},
        {"Network", "Communication & News", "applications-internet"},
        {"Office", "Office & Productivity", "applications-office"},
        {"Science", "Science", "applications-science"},
        {"System", "System", "applications-system"},
        {"Utility", "Utilities", "applications-utilities"},
    };

    constexpr int kMaxPerSection = 24;

    for (const auto &definition : definitions) {
        gchar *categories[2] = {const_cast<gchar *>(definition.category), nullptr};
        AsComponentBox *box = as_pool_get_components_by_categories(pool, categories);
        if (!box)
            continue;

        ExploreSectionData section;
        section.title = QString::fromUtf8(definition.title);
        section.category = QString::fromUtf8(definition.category);
        section.icon = QString::fromUtf8(definition.icon);
        QSet<QString> seen;

        const guint count = as_component_box_len(box);
        for (guint i = 0; i < count && section.entries.size() < kMaxPerSection; ++i) {
            AsComponent *component = as_component_box_index(box, i);
            if (as_component_get_kind(component) != AS_COMPONENT_KIND_DESKTOP_APP)
                continue;

            const gchar *id = as_component_get_id(component);
            if (!id)
                continue;
            const QString appId = QString::fromUtf8(id);
            if (seen.contains(appId))
                continue;
            seen.insert(appId);

            section.entries.append(entryFromComponent(component));
        }
        g_object_unref(box);

        if (!section.entries.isEmpty())
            result.categories.append(section);
    }

    // Offline fallback for the "New"/"Recently updated" Explore rows: order
    // every desktop app by its most recent release date. Only used when the
    // Flathub API rows are unavailable.
    if (AsComponentBox *all = as_pool_get_components(pool)) {
        struct DatedEntry {
            quint64 timestamp;
            EntryData entry;
        };
        QList<DatedEntry> dated;

        const guint count = as_component_box_len(all);
        for (guint i = 0; i < count; ++i) {
            AsComponent *component = as_component_box_index(all, i);
            if (as_component_get_kind(component) != AS_COMPONENT_KIND_DESKTOP_APP)
                continue;
            if (!as_component_get_id(component))
                continue;

            quint64 newest = 0;
            if (AsReleaseList *releases = as_component_get_releases_plain(component)) {
                const guint releaseCount = as_release_list_get_size(releases);
                for (guint r = 0; r < releaseCount; ++r) {
                    AsRelease *release = as_release_list_index_safe(releases, r);
                    if (release)
                        newest = qMax(newest, as_release_get_timestamp(release));
                }
            }
            if (newest == 0)
                continue;
            dated.append({newest, entryFromComponent(component)});
        }

        std::sort(dated.begin(), dated.end(), [](const DatedEntry &a, const DatedEntry &b) {
            return a.timestamp > b.timestamp;
        });

        constexpr int kRecentLimit = 24;
        const quint64 recentCutoff =
            static_cast<quint64>(QDateTime::currentSecsSinceEpoch()) - (quint64(120) * 24 * 60 * 60);
        for (int i = 0; i < dated.size(); ++i) {
            if (i < kRecentLimit)
                result.updated.append(dated.at(i).entry);
            if (dated.at(i).timestamp >= recentCutoff && result.newest.size() < kRecentLimit)
                result.newest.append(dated.at(i).entry);
        }

        g_object_unref(all);
    }

    g_object_unref(pool);
    return result;
}

QList<EntryData> appStreamCategory(FlatpakManager *manager, const QString &category)
{
    QList<EntryData> results;

    AsPool *pool = as_pool_new();
    configureAppStreamPool(pool, manager);

    GError *error = nullptr;
    if (!as_pool_load(pool, nullptr, &error)) {
        qWarning() << "AppStream: failed to load pool:" << (error ? error->message : "unknown error");
        g_clear_error(&error);
        g_object_unref(pool);
        return results;
    }

    const QByteArray categoryUtf8 = category.toUtf8();
    gchar *categories[2] = {const_cast<gchar *>(categoryUtf8.constData()), nullptr};
    AsComponentBox *box = as_pool_get_components_by_categories(pool, categories);
    if (box) {
        QSet<QString> seen;
        const guint count = as_component_box_len(box);
        for (guint i = 0; i < count; ++i) {
            AsComponent *component = as_component_box_index(box, i);
            if (as_component_get_kind(component) != AS_COMPONENT_KIND_DESKTOP_APP)
                continue;
            const gchar *id = as_component_get_id(component);
            if (!id)
                continue;
            const QString appId = QString::fromUtf8(id);
            if (seen.contains(appId))
                continue;
            seen.insert(appId);
            results.append(entryFromComponent(component));
        }
        g_object_unref(box);
    }

    g_object_unref(pool);
    return results;
}

} // namespace
#endif

#ifdef HAVE_FLATPAK
namespace {

QStringList splitSemicolons(const QString &value)
{
    return value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
}

void addUnique(QStringList &list, const QString &value)
{
    if (!value.isEmpty() && !list.contains(value))
        list << value;
}

QString describeFilesystem(const QString &entry)
{
    QString path = entry;
    bool readOnly = false;
    bool denied = false;
    if (path.startsWith(QLatin1Char('!'))) {
        denied = true;
        path.remove(0, 1);
    }
    if (path.endsWith(QLatin1String(":ro"))) {
        readOnly = true;
        path.chop(3);
    } else if (path.endsWith(QLatin1String(":rw"))) {
        path.chop(3);
    }

    static const QHash<QString, QString> names = {
        {QStringLiteral("host"), QStringLiteral("Full host filesystem")},
        {QStringLiteral("home"), QStringLiteral("Home folder")},
        {QStringLiteral("xdg-desktop"), QStringLiteral("Desktop folder")},
        {QStringLiteral("xdg-documents"), QStringLiteral("Documents folder")},
        {QStringLiteral("xdg-download"), QStringLiteral("Downloads folder")},
        {QStringLiteral("xdg-music"), QStringLiteral("Music folder")},
        {QStringLiteral("xdg-pictures"), QStringLiteral("Pictures folder")},
        {QStringLiteral("xdg-videos"), QStringLiteral("Videos folder")},
        {QStringLiteral("xdg-run/pipewire-0"), QStringLiteral("PipeWire socket")},
    };

    QString label = names.value(path, path);
    if (denied)
        return QStringLiteral("No access: %1").arg(label);
    if (readOnly)
        return QStringLiteral("Read-only: %1").arg(label);
    return QStringLiteral("Read/write: %1").arg(label);
}

void parseContext(GKeyFile *keyFile, AppDetailData &data)
{
    static const QHash<QString, QString> shared = {
        {QStringLiteral("network"), QStringLiteral("Network access")},
        {QStringLiteral("ipc"), QStringLiteral("Inter-process communication")},
    };
    static const QHash<QString, QString> sockets = {
        {QStringLiteral("x11"), QStringLiteral("X11 windowing")},
        {QStringLiteral("fallback-x11"), QStringLiteral("X11 fallback")},
        {QStringLiteral("wayland"), QStringLiteral("Wayland windowing")},
        {QStringLiteral("pulseaudio"), QStringLiteral("Audio (PulseAudio)")},
        {QStringLiteral("cups"), QStringLiteral("Printing (CUPS)")},
        {QStringLiteral("pcsc"), QStringLiteral("Smart cards")},
        {QStringLiteral("gpg-agent"), QStringLiteral("GnuPG agent")},
        {QStringLiteral("ssh-auth"), QStringLiteral("SSH agent")},
        {QStringLiteral("system-bus"), QStringLiteral("System D-Bus")},
        {QStringLiteral("session-bus"), QStringLiteral("Session D-Bus")},
        {QStringLiteral("xtest"), QStringLiteral("X11 input injection")},
    };
    static const QHash<QString, QString> devices = {
        {QStringLiteral("dri"), QStringLiteral("GPU access")},
        {QStringLiteral("kvm"), QStringLiteral("Virtualization (KVM)")},
        {QStringLiteral("shm"), QStringLiteral("Shared memory")},
        {QStringLiteral("all"), QStringLiteral("All devices")},
        {QStringLiteral("usb"), QStringLiteral("USB devices")},
    };

    if (gchar *value = g_key_file_get_string(keyFile, "Context", "shared", nullptr)) {
        for (const QString &token : splitSemicolons(QString::fromUtf8(value)))
            addUnique(data.permissions, shared.value(token, token));
        g_free(value);
    }
    if (gchar *value = g_key_file_get_string(keyFile, "Context", "sockets", nullptr)) {
        for (const QString &token : splitSemicolons(QString::fromUtf8(value)))
            addUnique(data.permissions, sockets.value(token, token));
        g_free(value);
    }
    if (gchar *value = g_key_file_get_string(keyFile, "Context", "devices", nullptr)) {
        for (const QString &token : splitSemicolons(QString::fromUtf8(value)))
            addUnique(data.permissions, devices.value(token, token));
        g_free(value);
    }
    if (gchar *value = g_key_file_get_string(keyFile, "Context", "filesystems", nullptr)) {
        for (const QString &token : splitSemicolons(QString::fromUtf8(value)))
            addUnique(data.permissions, describeFilesystem(token));
        g_free(value);
    }
    if (gchar *value = g_key_file_get_string(keyFile, "Context", "persistent", nullptr)) {
        for (const QString &token : splitSemicolons(QString::fromUtf8(value)))
            addUnique(data.permissions, QStringLiteral("Persistent storage: %1").arg(token));
        g_free(value);
    }
}

bool parseMetadataBytes(GBytes *bytes, AppDetailData &data)
{
    if (!bytes)
        return false;
    g_autoptr(GKeyFile) keyFile = g_key_file_new();
    if (!g_key_file_load_from_bytes(keyFile, bytes, G_KEY_FILE_NONE, nullptr))
        return false;

    parseContext(keyFile, data);

    if (gchar *runtime = g_key_file_get_string(keyFile, "Application", "runtime", nullptr)) {
        data.runtime = QString::fromUtf8(runtime);
        g_free(runtime);
    }
    return true;
}

void enrichFromInstalledRef(FlatpakInstalledRef *ref, FlatpakInstallation *installation, AppDetailData &data)
{
    data.installed = true;
    data.found = true;

    const quint64 size = flatpak_installed_ref_get_installed_size(ref);
    if (size)
        data.installedSize = humanSize(size);

    if (data.version.isEmpty()) {
        if (const char *v = flatpak_installed_ref_get_appdata_version(ref))
            data.version = QString::fromUtf8(v);
    }
    if (data.name.isEmpty()) {
        if (const char *v = flatpak_installed_ref_get_appdata_name(ref))
            data.name = QString::fromUtf8(v);
    }
    if (data.summary.isEmpty()) {
        if (const char *v = flatpak_installed_ref_get_appdata_summary(ref))
            data.summary = QString::fromUtf8(v);
    }
    if (data.license.isEmpty()) {
        if (const char *v = flatpak_installed_ref_get_appdata_license(ref))
            data.license = QString::fromUtf8(v);
    }

    GError *error = nullptr;
    g_autoptr(GBytes) metadata = flatpak_installed_ref_load_metadata(ref, nullptr, &error);
    if (!metadata)
        g_clear_error(&error);
    else
        parseMetadataBytes(metadata, data);

    const QString appId = QString::fromUtf8(flatpak_ref_get_name(FLATPAK_REF(ref)));
    g_autoptr(GPtrArray) updates = flatpak_installation_list_installed_refs_for_update(installation, nullptr, nullptr);
    if (updates) {
        for (guint i = 0; i < updates->len; ++i) {
            auto *candidate = FLATPAK_REF(g_ptr_array_index(updates, i));
            if (appId == QString::fromUtf8(flatpak_ref_get_name(candidate))) {
                data.updateAvailable = true;
                break;
            }
        }
    }
}

void enrichFromRemoteRef(FlatpakInstallation *installation, const QString &remoteName, FlatpakRef *ref,
                         AppDetailData &data)
{
    data.found = true;

    guint64 downloadSize = 0;
    guint64 installedSize = 0;
    if (flatpak_installation_fetch_remote_size_sync(installation, remoteName.toUtf8().constData(), ref, &downloadSize,
                                                    &installedSize, nullptr, nullptr)) {
        if (downloadSize)
            data.downloadSize = humanSize(downloadSize);
        if (installedSize)
            data.installedSize = humanSize(installedSize);
    }

    GError *error = nullptr;
    g_autoptr(GBytes) metadata =
        flatpak_installation_fetch_remote_metadata_sync(installation, remoteName.toUtf8().constData(), ref, nullptr,
                                                        &error);
    if (!metadata)
        g_clear_error(&error);
    else
        parseMetadataBytes(metadata, data);
}

void enrichFromFlatpak(FlatpakManager *manager, const QString &appId, AppDetailData &data)
{
    QList<FlatpakInstallation *> installations;
    if (auto *system = manager->systemInstallation())
        installations << system;
    if (auto *user = manager->userInstallation())
        installations << user;

    // Installed apps: the deployed ref gives us sizes and the sandbox metadata.
    for (FlatpakInstallation *installation : installations) {
        g_autoptr(GPtrArray) refs = flatpak_installation_list_installed_refs(installation, nullptr, nullptr);
        if (!refs)
            continue;
        for (guint i = 0; i < refs->len; ++i) {
            auto *ref = FLATPAK_INSTALLED_REF(g_ptr_array_index(refs, i));
            if (flatpak_ref_get_kind(FLATPAK_REF(ref)) != FLATPAK_REF_KIND_APP)
                continue;
            if (appId == QString::fromUtf8(flatpak_ref_get_name(FLATPAK_REF(ref)))) {
                enrichFromInstalledRef(ref, installation, data);
                return;
            }
        }
    }

    // Not installed: fetch sizes and metadata straight from the remote.
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
            for (guint j = 0; j < refs->len; ++j) {
                auto *ref = FLATPAK_REF(g_ptr_array_index(refs, j));
                if (flatpak_ref_get_kind(ref) != FLATPAK_REF_KIND_APP)
                    continue;
                if (appId == QString::fromUtf8(flatpak_ref_get_name(ref))) {
                    enrichFromRemoteRef(installation, remoteName, ref, data);
                    return;
                }
            }
        }
    }
}

} // namespace
#endif

namespace {

// Mirrors what `flatpak uninstall --delete-data` removes: the per-app XDG base
// directory Flatpak creates for every app, plus the app's own persistent "data"
// directory inside each installation. Best-effort, since the system-wide data
// directory is owned by root and may not be removable from inside the sandbox.
void removeAppData(const QString &appId)
{
    const QString home = QDir::homePath();
    QDir(home + QStringLiteral("/.var/app/") + appId).removeRecursively();

    const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!dataHome.isEmpty())
        QDir(dataHome + QStringLiteral("/flatpak/app/") + appId + QStringLiteral("/data")).removeRecursively();
    QDir(QStringLiteral("/var/lib/flatpak/app/") + appId + QStringLiteral("/data")).removeRecursively();
}

// Runs the AppStream pass for the Explore page. Kept as a free function so it is
// only compiled when AppStream is available.
ExploreResult loadExploreData(FlatpakManager *manager)
{
#ifdef HAVE_APPSTREAM
    return appStreamExplore(manager);
#else
    Q_UNUSED(manager)
    return ExploreResult();
#endif
}

// The set of application ids currently installed in either installation.
QSet<QString> installedAppIds(FlatpakManager *manager)
{
    QSet<QString> ids;
#ifdef HAVE_FLATPAK
    QList<FlatpakInstallation *> installations;
    if (auto *system = manager->systemInstallation())
        installations << system;
    if (auto *user = manager->userInstallation())
        installations << user;

    for (FlatpakInstallation *installation : installations) {
        g_autoptr(GPtrArray) refs = flatpak_installation_list_installed_refs(installation, nullptr, nullptr);
        if (!refs)
            continue;
        for (guint i = 0; i < refs->len; ++i) {
            auto *ref = FLATPAK_REF(g_ptr_array_index(refs, i));
            if (flatpak_ref_get_kind(ref) != FLATPAK_REF_KIND_APP)
                continue;
            ids.insert(QString::fromUtf8(flatpak_ref_get_name(ref)));
        }
    }
#else
    Q_UNUSED(manager)
#endif
    return ids;
}

bool looksLikeAppId(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_]+(\\.[A-Za-z0-9_]+)+$"));
    return re.match(name).hasMatch();
}

// Extension refs (locales, debug info, …) live in the same directories but
// belong to an installed app, so they are not leftovers.
bool isExtensionRef(const QString &name)
{
    static const QStringList suffixes = {
        QStringLiteral(".Locale"), QStringLiteral(".Debug"),   QStringLiteral(".Sources"),
        QStringLiteral(".Docs"),   QStringLiteral(".BaseApp"), QStringLiteral(".Var"),
        QStringLiteral(".Devel"),
    };
    for (const QString &suffix : suffixes) {
        if (name.endsWith(suffix))
            return true;
    }
    return false;
}

quint64 directorySize(const QString &path)
{
    quint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += static_cast<quint64>(it.fileInfo().size());
    }
    return total;
}

// Finds data directories that belong to applications which are no longer
// installed (the "leftover files" shown on the cleanup page).
QList<LeftoverData> scanLeftovers(FlatpakManager *manager)
{
    const QSet<QString> installed = installedAppIds(manager);

    struct Candidate {
        QString dir;
        bool isUser;
    };
    QList<Candidate> candidates;
    candidates.append({QDir::homePath() + QStringLiteral("/.var/app"), true});
    const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!dataHome.isEmpty())
        candidates.append({dataHome + QStringLiteral("/flatpak/app"), true});
    candidates.append({QStringLiteral("/var/lib/flatpak/app"), false});

    QList<LeftoverData> leftovers;
    QSet<QString> seenPaths;

    for (const Candidate &candidate : candidates) {
        QDir dir(candidate.dir);
        if (!dir.exists())
            continue;

        const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &name : entries) {
            if (isExtensionRef(name) || !looksLikeAppId(name))
                continue;
            if (installed.contains(name))
                continue;

            const QString path = dir.absoluteFilePath(name);
            if (seenPaths.contains(path))
                continue;
            seenPaths.insert(path);

            LeftoverData data;
            data.appId = name;
            data.path = path;
            data.isUser = candidate.isUser;
            data.size = directorySize(path);
            data.sizeText = humanSize(data.size);
            if (data.sizeText.isEmpty())
                data.sizeText = QStringLiteral("0 B");
            leftovers.append(data);
        }
    }

    std::sort(leftovers.begin(), leftovers.end(), [](const LeftoverData &a, const LeftoverData &b) {
        return a.size > b.size;
    });
    return leftovers;
}

AppDetailData loadDetails(FlatpakManager *manager, const QString &appId, quint64 generation)
{
    AppDetailData data;
#ifdef HAVE_APPSTREAM
    data = appStreamDetails(manager, appId);
#endif
    if (data.appId.isEmpty())
        data.appId = appId;
#ifdef HAVE_FLATPAK
    enrichFromFlatpak(manager, appId, data);
#endif
    if (data.name.isEmpty())
        data.name = appId;
    data.generation = generation;
    return data;
}

} // namespace

Backend::Backend(QObject *parent)
    : QObject(parent)
{
    m_flatpak = new FlatpakManager(this);
    m_search = new EntryModel(this);
    m_installed = new EntryModel(this);
    m_updates = new EntryModel(this);
    m_category = new EntryModel(this);
    m_details = new Details(this);
    m_remotes = new RemoteModel(this);
    m_leftovers = new LeftoverModel(this);
    m_flathub = new FlathubClient(this);

    m_watcher = new QFutureWatcher<QList<EntryData>>(this);
    connect(m_watcher, &QFutureWatcher<QList<EntryData>>::finished, this, [this]() {
        const quint64 generation = m_searchGeneration;
        const QList<EntryData> results = m_watcher->result();
        if (generation != m_searchGeneration)
            return; // A newer search superseded this one.
        for (const EntryData &entry : results)
            m_search->append(entry);
        m_searching = false;
        Q_EMIT searchingChanged();
    });

    m_detailsWatcher = new QFutureWatcher<AppDetailData>(this);
    connect(m_detailsWatcher, &QFutureWatcher<AppDetailData>::finished, this, [this]() {
        const AppDetailData data = m_detailsWatcher->result();
        if (data.generation != m_detailsGeneration)
            return; // A newer request superseded this one.
        m_details->apply(data);
    });

    // The Explore page is assembled from two asynchronous sources: the local
    // AppStream pool (categories + offline fallback) and the Flathub API
    // (curated rows). explorePartDone() rebuilds the page once both are in.
    m_exploreWatcher = new QFutureWatcher<ExploreResult>(this);
    connect(m_exploreWatcher, &QFutureWatcher<ExploreResult>::finished, this, [this]() {
        const ExploreResult result = m_exploreWatcher->result();
        m_exploreCategories = result.categories;
        m_recentNew = result.newest;
        m_recentUpdated = result.updated;
        explorePartDone();
    });

    connect(m_flathub, &FlathubClient::allFinished, this, [this]() { explorePartDone(); });

    m_categoryWatcher = new QFutureWatcher<QList<EntryData>>(this);
    connect(m_categoryWatcher, &QFutureWatcher<QList<EntryData>>::finished, this, [this]() {
        const quint64 generation = m_categoryGeneration;
        const QList<EntryData> results = m_categoryWatcher->result();
        if (generation != m_categoryGeneration)
            return; // A newer category superseded this one.
        for (const EntryData &entry : results)
            m_category->append(entry);
        m_categoryLoading = false;
        Q_EMIT categoryChanged();
    });

    m_leftoverWatcher = new QFutureWatcher<QList<LeftoverData>>(this);
    connect(m_leftoverWatcher, &QFutureWatcher<QList<LeftoverData>>::finished, this, [this]() {
        m_leftovers->setLeftovers(m_leftoverWatcher->result());
        m_scanningLeftovers = false;
        Q_EMIT leftoversChanged();
    });

    // Background update checks: hourly while the app is running, plus one check
    // shortly after launch so the user gets told about pending updates.
    m_updateTimer = new QTimer(this);
    m_updateTimer->setInterval(60 * 60 * 1000);
    connect(m_updateTimer, &QTimer::timeout, this, &Backend::checkForUpdates);
    m_updateTimer->start();
    QTimer::singleShot(60 * 1000, this, &Backend::checkForUpdates);
}

#ifdef HAVE_FLATPAK
namespace {

QList<EntryData> refSearch(FlatpakManager *manager, const QString &query)
{
    QList<EntryData> results;
    QSet<QString> seen;

    QList<FlatpakInstallation *> installations;
    if (auto *system = manager->systemInstallation())
        installations << system;
    if (auto *user = manager->userInstallation())
        installations << user;

    for (FlatpakInstallation *installation : installations) {
        g_autoptr(GPtrArray) remotes = flatpak_installation_list_remotes(installation, nullptr, nullptr);
        if (!remotes)
            continue;

        for (guint i = 0; i < remotes->len; ++i) {
            auto *remote = FLATPAK_REMOTE(g_ptr_array_index(remotes, i));
            const QString remoteName = QString::fromUtf8(flatpak_remote_get_name(remote));

            GError *error = nullptr;
            g_autoptr(GPtrArray) refs =
                flatpak_installation_list_remote_refs_sync(installation, remoteName.toUtf8().constData(), nullptr, &error);
            if (!refs) {
                g_clear_error(&error);
                continue;
            }

            for (guint j = 0; j < refs->len; ++j) {
                auto *ref = FLATPAK_REF(g_ptr_array_index(refs, j));
                if (flatpak_ref_get_kind(ref) != FLATPAK_REF_KIND_APP)
                    continue;
                const QString name = QString::fromUtf8(flatpak_ref_get_name(ref));
                if (!name.contains(query, Qt::CaseInsensitive) || seen.contains(name))
                    continue;
                seen.insert(name);
                results.append(entryFromRef(ref, QStringLiteral("From %1").arg(remoteName), false));
            }
        }
    }
    return results;
}

} // namespace
#endif

void Backend::search(const QString &query)
{
    m_search->clear();
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        m_searching = false;
        Q_EMIT searchingChanged();
        return;
    }

    ++m_searchGeneration;
    m_searching = true;
    Q_EMIT searchingChanged();

#ifdef HAVE_APPSTREAM
    m_watcher->setFuture(QtConcurrent::run([this, trimmed]() { return appStreamSearch(m_flatpak, trimmed); }));
#elif defined(HAVE_FLATPAK)
    m_watcher->setFuture(QtConcurrent::run([this, trimmed]() { return refSearch(m_flatpak, trimmed); }));
#else
    m_searching = false;
    Q_EMIT searchingChanged();
#endif
}

void Backend::refreshInstalled()
{
    m_installed->clear();
    m_updates->clear();

#ifdef HAVE_FLATPAK
    QSet<QString> seen;
    QSet<QString> updateIds;

    QList<FlatpakInstallation *> installations;
    if (auto *system = m_flatpak->systemInstallation())
        installations << system;
    if (auto *user = m_flatpak->userInstallation())
        installations << user;

    for (FlatpakInstallation *installation : installations) {
        g_autoptr(GPtrArray) installed = flatpak_installation_list_installed_refs(installation, nullptr, nullptr);
        if (installed) {
            for (guint i = 0; i < installed->len; ++i) {
                auto *ref = FLATPAK_REF(g_ptr_array_index(installed, i));
                if (flatpak_ref_get_kind(ref) != FLATPAK_REF_KIND_APP)
                    continue;
                const QString name = QString::fromUtf8(flatpak_ref_get_name(ref));
                if (seen.contains(name))
                    continue;
                seen.insert(name);
                EntryData entry = entryFromRef(ref, tr("Installed"), true);
                entry.name = installedDisplayName(ref);
                m_installed->append(entry);
            }
        }

        g_autoptr(GPtrArray) forUpdate =
            flatpak_installation_list_installed_refs_for_update(installation, nullptr, nullptr);
        if (forUpdate) {
            for (guint i = 0; i < forUpdate->len; ++i) {
                auto *ref = FLATPAK_REF(g_ptr_array_index(forUpdate, i));
                if (flatpak_ref_get_kind(ref) != FLATPAK_REF_KIND_APP)
                    continue;
                const QString name = QString::fromUtf8(flatpak_ref_get_name(ref));
                if (updateIds.contains(name))
                    continue;
                updateIds.insert(name);
                EntryData entry = entryFromRef(ref, tr("Update available"), true);
                entry.name = installedDisplayName(ref);
                m_updates->append(entry);
            }
        }
    }
#endif
}

void Backend::loadExplore()
{
    if (m_exploring)
        return;

    m_exploring = true;
    m_exploreCurated.clear();
    m_exploreSections.clear();
    m_exploreCategories.clear();
    m_recentNew.clear();
    m_recentUpdated.clear();
    for (EntryModel *model : m_exploreModels)
        model->deleteLater();
    m_exploreModels.clear();
    Q_EMIT exploreChanged();

    // Two asynchronous sources: the AppStream worker and the Flathub API.
    m_explorePending = 2;
    m_exploreWatcher->setFuture(QtConcurrent::run([this]() { return loadExploreData(m_flatpak); }));
    m_flathub->fetchExplore();
}

void Backend::explorePartDone()
{
    if (m_explorePending > 0)
        --m_explorePending;
    if (m_explorePending > 0)
        return;
    buildExplore();
}

void Backend::buildExplore()
{
    for (EntryModel *model : m_exploreModels)
        model->deleteLater();
    m_exploreModels.clear();
    m_exploreCurated.clear();
    m_exploreSections.clear();

    // Curated rows, in the requested order. Each falls back to the AppStream
    // release-date list when the Flathub API row is unavailable (offline).
    struct RowDef {
        const char *key;
        QString title;
        QString icon;
    };
    const RowDef rows[] = {
        {FlathubClient::TrendingKey, tr("Trending"), QStringLiteral("office-chart-line")},
        {FlathubClient::PopularKey, tr("Most Popular"), QStringLiteral("starred")},
        {FlathubClient::NewKey, tr("New"), QStringLiteral("list-add")},
        {FlathubClient::UpdatedKey, tr("Recently Updated"), QStringLiteral("system-software-update")},
    };

    for (const RowDef &row : rows) {
        const QString key = QString::fromLatin1(row.key);
        QList<EntryData> entries = m_flathub->collection(key);
        if (entries.isEmpty()) {
            if (key == QLatin1String(FlathubClient::NewKey))
                entries = m_recentNew;
            else if (key == QLatin1String(FlathubClient::UpdatedKey))
                entries = m_recentUpdated;
        }
        if (entries.isEmpty())
            continue;

        auto *model = new EntryModel(this);
        for (const EntryData &entry : entries)
            model->append(entry);
        m_exploreModels.append(model);

        QVariantMap map;
        map.insert(QStringLiteral("title"), row.title);
        map.insert(QStringLiteral("icon"), row.icon);
        map.insert(QStringLiteral("category"), QString());
        map.insert(QStringLiteral("model"), QVariant::fromValue(static_cast<QObject *>(model)));
        m_exploreCurated.append(map);
    }

    for (const ExploreSectionData &section : m_exploreCategories) {
        auto *model = new EntryModel(this);
        for (const EntryData &entry : section.entries)
            model->append(entry);
        m_exploreModels.append(model);

        QVariantMap map;
        map.insert(QStringLiteral("title"), section.title);
        map.insert(QStringLiteral("icon"), section.icon);
        map.insert(QStringLiteral("category"), section.category);
        map.insert(QStringLiteral("model"), QVariant::fromValue(static_cast<QObject *>(model)));
        m_exploreSections.append(map);
    }

    m_exploring = false;
    Q_EMIT exploreChanged();
}

void Backend::loadCategory(const QString &category)
{
    m_category->clear();
    m_categoryLoading = true;
    const quint64 generation = ++m_categoryGeneration;
    Q_EMIT categoryChanged();

#ifdef HAVE_APPSTREAM
    m_categoryWatcher->setFuture(
        QtConcurrent::run([this, category, generation]() { return appStreamCategory(m_flatpak, category); }));
#else
    Q_UNUSED(category)
    Q_UNUSED(generation)
    m_categoryLoading = false;
    Q_EMIT categoryChanged();
#endif
}

void Backend::openApp(const QString &appId)
{
    const quint64 generation = ++m_detailsGeneration;
    m_details->reset(appId);
    m_detailsWatcher->setFuture(QtConcurrent::run([this, appId, generation]() {
        return loadDetails(m_flatpak, appId, generation);
    }));
}

void Backend::installApp(const QString &appId)
{
    startTransaction(Transaction::Install, appId);
}

void Backend::updateApp(const QString &appId)
{
    startTransaction(Transaction::Update, appId);
}

void Backend::removeApp(const QString &appId)
{
    startTransaction(Transaction::Remove, appId);
}

void Backend::launchApp(const QString &appId)
{
    const QStringList args{QStringLiteral("run"), appId};

    // Inside a Flatpak sandbox there is no nested Flatpak session: starting a
    // second sandbox from within one has to go through the host's Flatpak via
    // the org.freedesktop.Flatpak portal ("flatpak-spawn --host"). The bundled
    // flatpak would otherwise try (and fail) to run the app in the sandbox.
    if (QFile::exists(QStringLiteral("/.flatpak-info"))
        && QProcess::startDetached(QStringLiteral("flatpak-spawn"),
                                   QStringList{QStringLiteral("--host"), QStringLiteral("flatpak")} + args))
        return;

    if (QProcess::startDetached(QStringLiteral("flatpak"), args))
        return;
    qWarning() << "Failed to launch" << appId;
}

void Backend::dismissTransaction()
{
    if (!m_transaction || m_transaction->running())
        return;
    m_transaction->deleteLater();
    m_transaction = nullptr;
    Q_EMIT currentTransactionChanged();
}

void Backend::startTransaction(int kind, const QString &appId)
{
    if (m_transaction && m_transaction->running())
        return; // A transaction is already in flight.

    if (m_transaction) {
        m_transaction->deleteLater();
        m_transaction = nullptr;
    }

    auto *transaction = new Transaction(m_flatpak, static_cast<Transaction::Kind>(kind), appId, this);
    m_transaction = transaction;
    Q_EMIT currentTransactionChanged();

    connect(transaction, &Transaction::completed, this, &Backend::refreshInstalled);
    connect(transaction, &Transaction::completed, this, [this, appId, transaction]() {
        // A removal should also drop the app's leftover data.
        if (transaction->operation() == QStringLiteral("remove"))
            removeAppData(appId);
        // Keep an open detail page in sync after install/update/remove.
        if (m_details && m_details->appId() == appId)
            openApp(appId);
    });
    connect(transaction, &Transaction::finishedChanged, this, [this, transaction]() {
        if (m_transaction == transaction)
            Q_EMIT currentTransactionChanged();
    });

    transaction->start();
}

void Backend::refreshRemotes()
{
    QList<RemoteData> remotes;
#ifdef HAVE_FLATPAK
    const struct {
        FlatpakInstallation *installation;
        bool isUser;
    } installations[] = {
        {m_flatpak->systemInstallation(), false},
        {m_flatpak->userInstallation(), true},
    };

    for (const auto &entry : installations) {
        if (!entry.installation)
            continue;

        g_autoptr(GPtrArray) list = flatpak_installation_list_remotes(entry.installation, nullptr, nullptr);
        if (!list)
            continue;

        for (guint i = 0; i < list->len; ++i) {
            auto *remote = FLATPAK_REMOTE(g_ptr_array_index(list, i));

            RemoteData data;
            data.name = QString::fromUtf8(flatpak_remote_get_name(remote));
            if (g_autofree char *title = flatpak_remote_get_title(remote))
                data.title = QString::fromUtf8(title);
            if (g_autofree char *url = flatpak_remote_get_url(remote))
                data.url = QString::fromUtf8(url);
            data.disabled = flatpak_remote_get_disabled(remote);
            data.isUser = entry.isUser;
            remotes.append(data);
        }
    }
#endif
    m_remotes->setRemotes(remotes);
}

void Backend::addRemote(const QString &name, const QString &url, bool user)
{
#ifdef HAVE_FLATPAK
    FlatpakInstallation *installation = user ? m_flatpak->userInstallation() : m_flatpak->systemInstallation();
    if (!installation) {
        qWarning() << "No" << (user ? "user" : "system") << "installation available to add a remote";
        return;
    }

    g_autoptr(FlatpakRemote) remote = flatpak_remote_new(name.toUtf8().constData());
    flatpak_remote_set_url(remote, url.toUtf8().constData());

    GError *error = nullptr;
    if (!flatpak_installation_add_remote(installation, remote, TRUE, nullptr, &error)) {
        qWarning() << "Could not add remote" << name << ":" << (error ? error->message : "unknown error");
        g_clear_error(&error);
        return;
    }

    refreshRemotes();

    // Pull the new remote's AppStream metadata so its apps appear in Explore and
    // search. Runs off the main thread because it may hit the network.
    QtConcurrent::run([installation, name]() {
        GError *error = nullptr;
        gboolean out_changed = FALSE;
        flatpak_installation_update_appstream_sync(installation, name.toUtf8().constData(), nullptr, &out_changed, nullptr, &error);
        if (error) {
            qWarning() << "Could not update AppStream for" << name << ":" << error->message;
            g_clear_error(&error);
        }
    });
#else
    Q_UNUSED(name)
    Q_UNUSED(url)
    Q_UNUSED(user)
#endif
}

void Backend::removeRemote(const QString &name, bool user)
{
#ifdef HAVE_FLATPAK
    FlatpakInstallation *installation = user ? m_flatpak->userInstallation() : m_flatpak->systemInstallation();
    if (!installation)
        return;

    GError *error = nullptr;
    if (!flatpak_installation_remove_remote(installation, name.toUtf8().constData(), nullptr, &error)) {
        qWarning() << "Could not remove remote" << name << ":" << (error ? error->message : "unknown error");
        g_clear_error(&error);
        return;
    }
    refreshRemotes();
#else
    Q_UNUSED(name)
    Q_UNUSED(user)
#endif
}

void Backend::setRemoteEnabled(const QString &name, bool user, bool enabled)
{
#ifdef HAVE_FLATPAK
    FlatpakInstallation *installation = user ? m_flatpak->userInstallation() : m_flatpak->systemInstallation();
    if (!installation)
        return;

    GError *error = nullptr;
    g_autoptr(FlatpakRemote) remote =
        flatpak_installation_get_remote_by_name(installation, name.toUtf8().constData(), nullptr, &error);
    if (!remote) {
        qWarning() << "Could not load remote" << name << ":" << (error ? error->message : "unknown error");
        g_clear_error(&error);
        return;
    }

    flatpak_remote_set_disabled(remote, !enabled);
    if (!flatpak_installation_modify_remote(installation, remote, nullptr, &error)) {
        qWarning() << "Could not modify remote" << name << ":" << (error ? error->message : "unknown error");
        g_clear_error(&error);
        return;
    }
    refreshRemotes();
#else
    Q_UNUSED(name)
    Q_UNUSED(user)
    Q_UNUSED(enabled)
#endif
}

void Backend::refreshLeftovers()
{
    if (m_scanningLeftovers)
        return;

    m_scanningLeftovers = true;
    Q_EMIT leftoversChanged();
    m_leftoverWatcher->setFuture(QtConcurrent::run([this]() { return scanLeftovers(m_flatpak); }));
}

void Backend::removeLeftover(const QString &path)
{
    if (path.isEmpty())
        return;
    QDir(path).removeRecursively();
    refreshLeftovers();
}

void Backend::checkForUpdates()
{
    if (m_transaction && m_transaction->running())
        return; // Don't disturb an in-flight transaction.

    refreshInstalled();

    const int count = m_updates->rowCount();
    if (count > 0 && count != m_lastNotifiedUpdates) {
        m_lastNotifiedUpdates = count;
        notifyUpdates(count);
    } else if (count == 0) {
        m_lastNotifiedUpdates = 0;
    }
}

void Backend::notifyUpdates(int count)
{
#ifdef HAVE_KNOTIFICATIONS
    KNotification::event(KNotification::Notification,
                         tr("Updates available"),
                         tr("%n application(s) can be updated.", "", count),
                         QStringLiteral("com.zachvlat.qutestore"),
                         KNotification::CloseOnTimeout);
#else
    Q_UNUSED(count)
#endif
}
