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
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
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

// AppStream component ids and Flatpak ref names do not always agree: some
// upstream metainfo files declare a `.desktop`-suffixed id (e.g.
// "org.kde.dolphin.desktop") while the Flatpak ref is spelled without it
// ("org.kde.dolphin"). Normalise the id so both spellings can be compared.
QString flatpakIdForComponentId(const QString &appId)
{
    constexpr qsizetype desktopSuffixLength = 8; // strlen(".desktop")
    return appId.endsWith(QLatin1String(".desktop")) ? appId.chopped(desktopSuffixLength) : appId;
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

// Resolve the icon an installed app is deployed with. Every app ships its icon
// inside its deployment directory, so this is the most reliable icon source and
// it also works for remotes that carry no AppStream data at all.
QString deploymentIconUrl(FlatpakInstalledRef *ref, const QString &appId)
{
    const char *deployDir = flatpak_installed_ref_get_deploy_dir(ref);
    if (!deployDir)
        return QString();

    const QString deploy = QString::fromUtf8(deployDir);
    const QString iconsRoot = deploy + QStringLiteral("/files/share/icons");

    // The icon is usually named after the app id, but the exported desktop file
    // is authoritative when it declares a different name.
    QStringList names{appId};
    QFile desktopFile(deploy + QStringLiteral("/files/share/applications/") + appId + QStringLiteral(".desktop"));
    if (desktopFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        static const QRegularExpression iconEntry(QStringLiteral("^Icon=(\\S+)\\s*$"),
                                                  QRegularExpression::MultilineOption);
        const auto match = iconEntry.match(QString::fromUtf8(desktopFile.readAll()));
        if (match.hasMatch()) {
            const QString iconName = match.captured(1);
            if (!iconName.isEmpty() && !names.contains(iconName))
                names << iconName;
        }
    }

    // Prefer an icon from an apps/ directory, then a scalable vector icon, then
    // the largest available raster size.
    QString bestPath;
    int bestScore = -1;
    QDirIterator it(iconsRoot, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        if (!names.contains(info.completeBaseName()))
            continue;

        int score = 0;
        if (QDir(info.absolutePath()).dirName() == QLatin1String("apps"))
            score += 1000000;

        const QString themeDir = info.absolutePath().section(QLatin1Char('/'), -2, -2); // e.g. "128x128"
        if (themeDir == QLatin1String("scalable")) {
            score += 100000;
        } else {
            bool ok = false;
            const int pixels = themeDir.section(QLatin1Char('x'), 0, 0).toInt(&ok);
            if (ok)
                score += pixels;
        }

        if (score > bestScore) {
            bestScore = score;
            bestPath = info.absoluteFilePath();
            if (score >= 1100000) // An apps/ scalable icon is the best possible.
                break;
        }
    }

    return bestPath.isEmpty() ? QString() : QUrl::fromLocalFile(bestPath).toString();
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

// as_pool_get_components_by_id() only matches the id exactly, but some upstream
// metainfo files declare a `.desktop`-suffixed id while the Flatpak ref (and
// therefore the id we are asked for) omits it. Try both spellings. Returns an
// owned box, or nullptr when nothing matches; the caller frees it.
AsComponentBox *findComponent(AsPool *pool, const QString &appId)
{
    AsComponentBox *box = as_pool_get_components_by_id(pool, appId.toUtf8().constData());
    if (box && as_component_box_len(box) > 0)
        return box;
    if (box)
        g_object_unref(box);

    const QByteArray suffixed = appId.toUtf8() + ".desktop";
    box = as_pool_get_components_by_id(pool, suffixed.constData());
    if (box && as_component_box_len(box) > 0)
        return box;
    if (box)
        g_object_unref(box);

    return nullptr;
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

    AsComponentBox *box = findComponent(pool, appId);
    if (box) {
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

// The "KDE apps" pseudo-category. AppStream has no such category, so collect
// the desktop apps that belong to KDE: by the org.kde.* id namespace, or by
// KDE being the declared developer (catches apps published outside org.kde.*).
QList<EntryData> appStreamKdeApps(FlatpakManager *manager)
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

    AsComponentBox *all = as_pool_get_components(pool);
    if (all) {
        QSet<QString> seen;
        const guint count = as_component_box_len(all);
        for (guint i = 0; i < count; ++i) {
            AsComponent *component = as_component_box_index(all, i);
            if (as_component_get_kind(component) != AS_COMPONENT_KIND_DESKTOP_APP)
                continue;
            const gchar *id = as_component_get_id(component);
            if (!id)
                continue;
            const QString appId = QString::fromUtf8(id);
            if (seen.contains(appId))
                continue;

            bool isKde = appId.startsWith(QLatin1String("org.kde."));
            if (!isKde) {
                if (AsDeveloper *developer = as_component_get_developer(component)) {
                    if (const gchar *name = as_developer_get_name(developer))
                        isKde = QString::fromUtf8(name).compare(QLatin1String("KDE"), Qt::CaseInsensitive) == 0;
                }
            }
            if (!isKde)
                continue;

            seen.insert(appId);
            results.append(entryFromComponent(component));
        }
        g_object_unref(all);
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

    // AppStream has no component for apps from remotes without metainfo data;
    // fall back to the icon the app is deployed with.
    if (data.icon.isEmpty())
        data.icon = deploymentIconUrl(ref, appId);

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
    // The app id may come from AppStream and carry a `.desktop` suffix that
    // Flatpak refs never have; compare against the ref spelling.
    const QString refId = flatpakIdForComponentId(appId);

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
            const auto kind = flatpak_ref_get_kind(FLATPAK_REF(ref));
            if (kind != FLATPAK_REF_KIND_APP && kind != FLATPAK_REF_KIND_RUNTIME)
                continue;
            if (refId == QString::fromUtf8(flatpak_ref_get_name(FLATPAK_REF(ref)))) {
                // Hand the real ref name to the UI so install/launch/remove all
                // operate on a ref that exists.
                data.appId = refId;
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
                const auto kind = flatpak_ref_get_kind(ref);
                if (kind != FLATPAK_REF_KIND_APP && kind != FLATPAK_REF_KIND_RUNTIME)
                    continue;
                if (refId == QString::fromUtf8(flatpak_ref_get_name(ref))) {
                    data.appId = refId;
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

// The per-app directory Flatpak keeps the user's data, config and cache in.
// This is the only place app data lives: the app directories inside an
// installation (~/.local/share/flatpak/app, /var/lib/flatpak/app) hold the
// deployed app itself and are managed by libflatpak, never by us.
QString appDataDir(const QString &appId)
{
    return QDir::homePath() + QStringLiteral("/.var/app/") + appId;
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

// Collects the application ids installed in either installation. Returns false
// when the list cannot be trusted (no Flatpak support, no installation, or a
// listing failed): callers that delete data must then do nothing, because an
// empty or partial list would make installed apps look uninstalled.
bool installedAppIds(FlatpakManager *manager, QSet<QString> *ids)
{
#ifdef HAVE_FLATPAK
    QList<FlatpakInstallation *> installations;
    if (auto *system = manager->systemInstallation())
        installations << system;
    if (auto *user = manager->userInstallation())
        installations << user;
    if (installations.isEmpty())
        return false;

    for (FlatpakInstallation *installation : installations) {
        GError *error = nullptr;
        g_autoptr(GPtrArray) refs = flatpak_installation_list_installed_refs(installation, nullptr, &error);
        if (!refs) {
            qWarning() << "Could not list installed refs:" << (error ? error->message : "unknown error");
            g_clear_error(&error);
            return false;
        }
        for (guint i = 0; i < refs->len; ++i) {
            auto *ref = FLATPAK_REF(g_ptr_array_index(refs, i));
            if (flatpak_ref_get_kind(ref) != FLATPAK_REF_KIND_APP)
                continue;
            ids->insert(QString::fromUtf8(flatpak_ref_get_name(ref)));
        }
    }
    return true;
#else
    Q_UNUSED(manager)
    Q_UNUSED(ids)
    return false;
#endif
}

// Deletes an app's data directory, but only once the app is installed in
// neither installation (it may still be installed for the other scope).
void removeAppDataIfUninstalled(FlatpakManager *manager, const QString &appId)
{
    const QString refId = flatpakIdForComponentId(appId);
    QSet<QString> installed;
    if (!installedAppIds(manager, &installed) || installed.contains(refId) || installed.contains(appId))
        return;
    QDir(appDataDir(refId)).removeRecursively();
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

// Finds data directories in ~/.var/app that belong to applications which are
// no longer installed (the "leftover files" shown on the cleanup page).
LeftoverScan scanLeftovers(FlatpakManager *manager)
{
    LeftoverScan scan;

    QSet<QString> installed;
    if (!installedAppIds(manager, &installed)) {
        scan.error = QObject::tr("The list of installed applications could not be read, "
                                 "so leftover files cannot be told apart from the data of installed apps.");
        return scan;
    }

    QDir dir(QDir::homePath() + QStringLiteral("/.var/app"));
    if (!dir.exists())
        return scan;

    const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : entries) {
        if (isExtensionRef(name) || !looksLikeAppId(name))
            continue;
        if (installed.contains(name) || name == QLatin1String("io.github.zachvlat.qutestore"))
            continue;

        LeftoverData data;
        data.appId = name;
        data.path = dir.absoluteFilePath(name);
        data.isUser = true;
        data.size = directorySize(data.path);
        data.sizeText = humanSize(data.size);
        if (data.sizeText.isEmpty())
            data.sizeText = QStringLiteral("0 B");
        scan.items.append(data);
    }

    std::sort(scan.items.begin(), scan.items.end(), [](const LeftoverData &a, const LeftoverData &b) {
        return a.size > b.size;
    });
    return scan;
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
    m_installedRuntimes = new EntryModel(this);
    m_updates = new EntryModel(this);
    m_updatesApps = new EntryModel(this);
    m_updatesRuntimes = new EntryModel(this);
    m_category = new EntryModel(this);
    m_details = new Details(this);
    m_remotes = new RemoteModel(this);
    m_leftovers = new LeftoverModel(this);
    m_flathub = new FlathubClient(this);
    m_nam = new QNetworkAccessManager(this);

    m_watcher = new QFutureWatcher<QList<EntryData>>(this);
    connect(m_watcher, &QFutureWatcher<QList<EntryData>>::finished, this, [this]() {
        const quint64 generation = m_searchGeneration;
        const QList<EntryData> results = m_watcher->result();
        if (generation != m_searchGeneration)
            return; // A newer search superseded this one.
        const QSet<QString> verified = m_flathub->verifiedAppIds();
        for (const EntryData &entry : results) {
            EntryData row = entry;
            row.verified = verified.contains(row.appId);
            m_search->append(row);
        }
        m_searching = false;
        Q_EMIT searchingChanged();
    });

    m_detailsWatcher = new QFutureWatcher<AppDetailData>(this);
    connect(m_detailsWatcher, &QFutureWatcher<AppDetailData>::finished, this, [this]() {
        AppDetailData data = m_detailsWatcher->result();
        if (data.generation != m_detailsGeneration)
            return; // A newer request superseded this one.
        const QSet<QString> verified = m_flathub->verifiedAppIds();
        data.verified = verified.contains(data.appId)
            || verified.contains(flatpakIdForComponentId(data.appId));
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
    connect(m_flathub, &FlathubClient::verifiedAppsReady, this, &Backend::applyVerifiedApps);

    // The verification list applies to every view (not just Explore), so fetch
    // it once at startup and cache it in FlathubClient.
    m_flathub->fetchVerifiedApps();

    m_categoryWatcher = new QFutureWatcher<QList<EntryData>>(this);
    connect(m_categoryWatcher, &QFutureWatcher<QList<EntryData>>::finished, this, [this]() {
        const quint64 generation = m_categoryGeneration;
        const QList<EntryData> results = m_categoryWatcher->result();
        if (generation != m_categoryGeneration)
            return; // A newer category superseded this one.
        const QSet<QString> verified = m_flathub->verifiedAppIds();
        for (const EntryData &entry : results) {
            EntryData row = entry;
            row.verified = verified.contains(row.appId);
            m_category->append(row);
        }
        m_categoryLoading = false;
        Q_EMIT categoryChanged();
    });

    m_remoteWatcher = new QFutureWatcher<RemoteOpResult>(this);
    connect(m_remoteWatcher, &QFutureWatcher<RemoteOpResult>::finished, this,
            [this]() { finishRemoteOperation(m_remoteWatcher->result()); });

    m_installedWatcher = new QFutureWatcher<InstalledLists>(this);
    connect(m_installedWatcher, &QFutureWatcher<InstalledLists>::finished, this,
            [this]() { applyInstalledLists(m_installedWatcher->result()); });

    m_leftoverWatcher = new QFutureWatcher<LeftoverScan>(this);
    connect(m_leftoverWatcher, &QFutureWatcher<LeftoverScan>::finished, this, [this]() {
        const LeftoverScan scan = m_leftoverWatcher->result();
        m_leftovers->setLeftovers(scan.items);
        m_leftoverError = scan.error;
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

namespace {

// Lists installed apps and pending updates. Runs on a worker thread:
// list_installed_refs_for_update() contacts every remote, so it can take
// seconds (or hang until a network timeout).
InstalledLists loadInstalledLists(FlatpakManager *manager)
{
    InstalledLists lists;
#ifdef HAVE_FLATPAK
    QSet<QString> seen;
    QSet<QString> updateIds;

    QList<FlatpakInstallation *> installations;
    if (auto *system = manager->systemInstallation())
        installations << system;
    if (auto *user = manager->userInstallation())
        installations << user;

    for (FlatpakInstallation *installation : installations) {
        g_autoptr(GPtrArray) installed = flatpak_installation_list_installed_refs(installation, nullptr, nullptr);
        if (installed) {
            for (guint i = 0; i < installed->len; ++i) {
                auto *ref = FLATPAK_REF(g_ptr_array_index(installed, i));
                const auto kind = flatpak_ref_get_kind(ref);
                if (kind != FLATPAK_REF_KIND_APP && kind != FLATPAK_REF_KIND_RUNTIME)
                    continue;
                const QString name = QString::fromUtf8(flatpak_ref_get_name(ref));
                if (seen.contains(name))
                    continue;
                seen.insert(name);
                EntryData entry = entryFromRef(ref, QCoreApplication::translate("Backend", "Installed"), true);
                entry.name = installedDisplayName(ref);
                if (FLATPAK_IS_INSTALLED_REF(ref))
                    entry.icon = deploymentIconUrl(FLATPAK_INSTALLED_REF(ref), name);
                entry.kind = (kind == FLATPAK_REF_KIND_RUNTIME) ? 1 : 0;
                lists.installed.append(entry);
            }
        }

        g_autoptr(GPtrArray) forUpdate =
            flatpak_installation_list_installed_refs_for_update(installation, nullptr, nullptr);
        if (forUpdate) {
            for (guint i = 0; i < forUpdate->len; ++i) {
                auto *ref = FLATPAK_REF(g_ptr_array_index(forUpdate, i));
                const auto kind = flatpak_ref_get_kind(ref);
                if (kind != FLATPAK_REF_KIND_APP && kind != FLATPAK_REF_KIND_RUNTIME)
                    continue;
                const QString name = QString::fromUtf8(flatpak_ref_get_name(ref));
                if (updateIds.contains(name))
                    continue;
                updateIds.insert(name);
                EntryData entry = entryFromRef(ref, QCoreApplication::translate("Backend", "Update available"), true);
                entry.name = installedDisplayName(ref);
                if (FLATPAK_IS_INSTALLED_REF(ref))
                    entry.icon = deploymentIconUrl(FLATPAK_INSTALLED_REF(ref), name);
                entry.kind = (kind == FLATPAK_REF_KIND_RUNTIME) ? 1 : 0;
                lists.updates.append(entry);
            }
        }
    }
#else
    Q_UNUSED(manager)
#endif
    return lists;
}

} // namespace

void Backend::refreshInstalled()
{
    // One listing at a time; a request that arrives meanwhile (e.g. a
    // transaction finishing during the hourly check) runs right after it.
    if (m_loadingInstalled) {
        m_installedRefreshQueued = true;
        return;
    }

    m_loadingInstalled = true;
    Q_EMIT installedChanged();
    m_installedWatcher->setFuture(QtConcurrent::run([this]() { return loadInstalledLists(m_flatpak); }));
}

void Backend::applyInstalledLists(const InstalledLists &lists)
{
    // Replace the rows in one step so an open Installed/Updates page never
    // flashes empty.
    const QSet<QString> verified = m_flathub->verifiedAppIds();
    QList<EntryData> installed = lists.installed;
    for (EntryData &entry : installed)
        entry.verified = verified.contains(entry.appId);
    QList<EntryData> updates = lists.updates;
    for (EntryData &entry : updates)
        entry.verified = verified.contains(entry.appId);

    QList<EntryData> installedApps, installedRuntimes;
    for (const auto &e : installed) {
        if (e.kind == 1) installedRuntimes.append(e);
        else installedApps.append(e);
    }
    QList<EntryData> updatesApps, updatesRuntimes;
    for (const auto &e : updates) {
        if (e.kind == 1) updatesRuntimes.append(e);
        else updatesApps.append(e);
    }

    m_installed->setEntries(installed);
    m_installedRuntimes->setEntries(installedRuntimes);
    m_updates->setEntries(updates);
    m_updatesApps->setEntries(updatesApps);
    m_updatesRuntimes->setEntries(updatesRuntimes);
    m_loadingInstalled = false;
    Q_EMIT installedChanged();

    if (m_notifyAfterRefresh) {
        m_notifyAfterRefresh = false;
        const int count = lists.updates.size();
        if (count > 0 && count != m_lastNotifiedUpdates) {
            m_lastNotifiedUpdates = count;
            notifyUpdates(count);
        } else if (count == 0) {
            m_lastNotifiedUpdates = 0;
        }
    }

    if (m_installedRefreshQueued) {
        m_installedRefreshQueued = false;
        refreshInstalled();
    }
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

    const QSet<QString> verified = m_flathub->verifiedAppIds();

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
        for (const EntryData &entry : entries) {
            EntryData marked = entry;
            marked.verified = marked.verified || verified.contains(marked.appId);
            model->append(marked);
        }
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
        for (const EntryData &entry : section.entries) {
            EntryData marked = entry;
            marked.verified = marked.verified || verified.contains(marked.appId);
            model->append(marked);
        }
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

void Backend::applyVerifiedApps()
{
    const QSet<QString> &ids = m_flathub->verifiedAppIds();
    m_search->setVerifiedIds(ids);
    m_installed->setVerifiedIds(ids);
    m_updates->setVerifiedIds(ids);
    m_category->setVerifiedIds(ids);
    for (EntryModel *model : m_exploreModels)
        model->setVerifiedIds(ids);

    if (m_details && !m_details->appId().isEmpty()) {
        const QString appId = m_details->appId();
        m_details->setVerified(ids.contains(appId) || ids.contains(flatpakIdForComponentId(appId)));
    }
}

void Backend::loadCategory(const QString &category)
{
    m_category->clear();
    m_categoryLoading = true;
    const quint64 generation = ++m_categoryGeneration;
    Q_EMIT categoryChanged();

#ifdef HAVE_APPSTREAM
    if (category == QLatin1String("kde")) {
        m_categoryWatcher->setFuture(QtConcurrent::run([this]() { return appStreamKdeApps(m_flatpak); }));
    } else {
        m_categoryWatcher->setFuture(
            QtConcurrent::run([this, category, generation]() { return appStreamCategory(m_flatpak, category); }));
    }
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
    m_updateQueue.clear();
    startTransaction(Transaction::Install, appId);
}

void Backend::updateApp(const QString &appId)
{
    qWarning() << "Backend::updateApp called for" << appId;
    m_updateQueue.clear();
    startTransaction(Transaction::Update, appId);
}

void Backend::updateAll()
{
    if (m_transaction && m_transaction->running())
        return; // An operation is already in flight.

    QStringList pending;
    const QList<EntryData> &rows = m_updates->entries();
    for (const EntryData &entry : rows)
        pending << entry.appId;
    if (pending.isEmpty())
        return;

    m_updateQueue = pending;
    startNextQueuedUpdate();
}

void Backend::removeApp(const QString &appId, bool deleteData)
{
    m_updateQueue.clear();
    startTransaction(Transaction::Remove, appId, deleteData);
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

void Backend::startTransaction(int kind, const QString &appId, bool deleteData)
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

    connect(transaction, &Transaction::completed, this, [this]() { qWarning() << "Transaction completed, refreshing"; refreshInstalled(); });
    connect(transaction, &Transaction::completed, this, [this, appId, deleteData]() {
        // The user's data is only deleted when they explicitly asked for it in
        // the removal dialog, like `flatpak uninstall --delete-data`.
        if (deleteData)
            removeAppDataIfUninstalled(m_flatpak, appId);
        // Keep an open detail page in sync after install/update/remove.
        if (m_details && m_details->appId() == appId)
            openApp(appId);
    });
    connect(transaction, &Transaction::finishedChanged, this, [this, transaction]() {
        if (m_transaction == transaction)
            Q_EMIT currentTransactionChanged();
    });

    // "Update all" runs the queued updates one after the next. The chain stops
    // at the first failure so the error stays on screen instead of being
    // quietly followed by more operations.
    connect(transaction, &Transaction::finishedChanged, this, [this, transaction]() {
        if (m_updateQueue.isEmpty())
            return;
        if (transaction->success())
            startNextQueuedUpdate();
        else
            m_updateQueue.clear();
    });

    transaction->start();
}

void Backend::startNextQueuedUpdate()
{
    if (m_updateQueue.isEmpty() || (m_transaction && m_transaction->running()))
        return;
    startTransaction(Transaction::Update, m_updateQueue.takeFirst());
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


// Remote (repository) operations. Each one runs on a worker thread: changing a
// system-wide remote goes through the Flatpak system helper and can wait on a
// polkit password prompt, and adding a remote downloads its AppStream bundle.
// Neither may block the UI.

#ifdef HAVE_FLATPAK
namespace {

QString errorText(GError *error, const char *fallback)
{
    return QString::fromUtf8(error && error->message ? error->message : fallback);
}

// Adds a configured remote and pulls its AppStream metadata so its apps show up
// in Explore and search. A failed metadata download does not undo the add: the
// next refresh retries it.
RemoteOpResult addConfiguredRemote(FlatpakInstallation *installation, FlatpakRemote *remote, const QString &name)
{
    RemoteOpResult result;
    GError *error = nullptr;
    if (!flatpak_installation_add_remote(installation, remote, TRUE, nullptr, &error)) {
        result.message = QCoreApplication::translate("Backend", "Could not add “%1”: %2")
                             .arg(name, errorText(error, "unknown error"));
        g_clear_error(&error);
        return result;
    }

    gboolean changed = FALSE;
    if (!flatpak_installation_update_appstream_sync(installation, name.toUtf8().constData(), nullptr, &changed,
                                                    nullptr, &error)) {
        qWarning() << "Could not update AppStream for" << name << ":" << errorText(error, "unknown error");
        g_clear_error(&error);
    }

    result.succeeded = true;
    result.reloadExplore = true;
    result.message = QCoreApplication::translate("Backend", "Added “%1”.").arg(name);
    return result;
}

// A bare repository URL: only works for repositories that are not signed.
RemoteOpResult addRemoteFromUrl(FlatpakInstallation *installation, const QString &name, const QString &url)
{
    g_autoptr(FlatpakRemote) remote = flatpak_remote_new(name.toUtf8().constData());
    flatpak_remote_set_url(remote, url.toUtf8().constData());
    return addConfiguredRemote(installation, remote, name);
}

// A downloaded .flatpakrepo file, which carries the remote's GPG key.
RemoteOpResult addRemoteFromFile(FlatpakInstallation *installation, const QString &name, const QByteArray &file)
{
    g_autoptr(GBytes) bytes = g_bytes_new(file.constData(), file.size());
    GError *error = nullptr;
    g_autoptr(FlatpakRemote) remote = flatpak_remote_new_from_file(name.toUtf8().constData(), bytes, &error);
    if (!remote) {
        RemoteOpResult result;
        result.message = QCoreApplication::translate("Backend", "Not a valid repository file: %1")
                             .arg(errorText(error, "unknown error"));
        g_clear_error(&error);
        return result;
    }
    return addConfiguredRemote(installation, remote, name);
}

RemoteOpResult removeRemoteSync(FlatpakInstallation *installation, const QString &name)
{
    RemoteOpResult result;
    GError *error = nullptr;
    if (!flatpak_installation_remove_remote(installation, name.toUtf8().constData(), nullptr, &error)) {
        result.message = QCoreApplication::translate("Backend", "Could not remove “%1”: %2")
                             .arg(name, errorText(error, "unknown error"));
        g_clear_error(&error);
        return result;
    }
    result.succeeded = true;
    result.reloadExplore = true;
    result.message = QCoreApplication::translate("Backend", "Removed “%1”.").arg(name);
    return result;
}

RemoteOpResult setRemoteEnabledSync(FlatpakInstallation *installation, const QString &name, bool enabled)
{
    RemoteOpResult result;
    GError *error = nullptr;
    g_autoptr(FlatpakRemote) remote =
        flatpak_installation_get_remote_by_name(installation, name.toUtf8().constData(), nullptr, &error);
    if (remote) {
        flatpak_remote_set_disabled(remote, !enabled);
        if (flatpak_installation_modify_remote(installation, remote, nullptr, &error)) {
            result.succeeded = true;
            result.reloadExplore = true;
            result.message = enabled ? QCoreApplication::translate("Backend", "Enabled “%1”.").arg(name)
                                     : QCoreApplication::translate("Backend", "Disabled “%1”.").arg(name);
            return result;
        }
    }
    result.message = QCoreApplication::translate("Backend", "Could not change “%1”: %2")
                         .arg(name, errorText(error, "unknown error"));
    g_clear_error(&error);
    return result;
}

} // namespace
#endif

bool Backend::beginRemoteOperation()
{
    if (m_remoteBusy)
        return false;
    m_remoteBusy = true;
    m_remoteMessage.clear();
    Q_EMIT remoteMessageChanged();
    return true;
}

void Backend::runRemoteOperation(bool user, std::function<RemoteOpResult(FlatpakInstallation *)> operation)
{
#ifdef HAVE_FLATPAK
    FlatpakInstallation *installation = user ? m_flatpak->userInstallation() : m_flatpak->systemInstallation();
    if (!installation) {
        RemoteOpResult result;
        result.message = user ? tr("No user installation is available.") : tr("No system installation is available.");
        finishRemoteOperation(result);
        return;
    }
    m_remoteWatcher->setFuture(
        QtConcurrent::run([installation, operation = std::move(operation)]() { return operation(installation); }));
#else
    Q_UNUSED(user)
    Q_UNUSED(operation)
    RemoteOpResult result;
    result.message = tr("Flatpak support is not available in this build.");
    finishRemoteOperation(result);
#endif
}

void Backend::finishRemoteOperation(const RemoteOpResult &result)
{
    m_remoteBusy = false;
    m_remoteMessage = result.message;
    m_remoteSucceeded = result.succeeded;
    Q_EMIT remoteMessageChanged();
    refreshRemotes();
    // The set of remotes decides which apps Explore can show.
    if (result.reloadExplore)
        loadExplore();
}

void Backend::addRemote(const QString &name, const QString &url, bool user)
{
    if (name.isEmpty() || url.isEmpty() || !beginRemoteOperation())
        return;

    // A .flatpakrepo file describes the remote completely, including the GPG
    // key its content is signed with. Without that key a signed remote such as
    // Flathub can be added but nothing can be installed from it.
    if (url.endsWith(QLatin1String(".flatpakrepo"), Qt::CaseInsensitive)) {
        QNetworkRequest request{QUrl(url)};
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(15000);
        QNetworkReply *reply = m_nam->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, name, user]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                RemoteOpResult result;
                result.message = tr("Could not download the repository file: %1").arg(reply->errorString());
                finishRemoteOperation(result);
                return;
            }
            const QByteArray file = reply->readAll();
#ifdef HAVE_FLATPAK
            runRemoteOperation(user, [name, file](FlatpakInstallation *installation) {
                return addRemoteFromFile(installation, name, file);
            });
#else
            Q_UNUSED(file)
            runRemoteOperation(user, {});
#endif
        });
        return;
    }

#ifdef HAVE_FLATPAK
    runRemoteOperation(user, [name, url](FlatpakInstallation *installation) {
        return addRemoteFromUrl(installation, name, url);
    });
#else
    runRemoteOperation(user, {});
#endif
}

void Backend::removeRemote(const QString &name, bool user)
{
    if (!beginRemoteOperation())
        return;
#ifdef HAVE_FLATPAK
    runRemoteOperation(user, [name](FlatpakInstallation *installation) { return removeRemoteSync(installation, name); });
#else
    Q_UNUSED(name)
    runRemoteOperation(user, {});
#endif
}

void Backend::setRemoteEnabled(const QString &name, bool user, bool enabled)
{
    if (!beginRemoteOperation())
        return;
#ifdef HAVE_FLATPAK
    runRemoteOperation(user, [name, enabled](FlatpakInstallation *installation) {
        return setRemoteEnabledSync(installation, name, enabled);
    });
#else
    Q_UNUSED(name)
    Q_UNUSED(enabled)
    runRemoteOperation(user, {});
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
    // Only paths the last scan reported can be deleted, never an arbitrary one.
    const QString appId = m_leftovers->appIdForPath(path);
    if (appId.isEmpty() || m_scanningLeftovers)
        return;

    m_scanningLeftovers = true;
    Q_EMIT leftoversChanged();
    m_leftoverWatcher->setFuture(QtConcurrent::run([this, appId]() {
        // Re-checks that the app is still not installed, then deletes off the
        // UI thread (data directories can be gigabytes) and rescans.
        removeAppDataIfUninstalled(m_flatpak, appId);
        return scanLeftovers(m_flatpak);
    }));
}

void Backend::checkForUpdates()
{
    if (m_transaction && m_transaction->running())
        return; // Don't disturb an in-flight transaction.

    // The notification is sent from applyInstalledLists() once the
    // asynchronous listing is in.
    m_notifyAfterRefresh = true;
    refreshInstalled();
}

void Backend::notifyUpdates(int count)
{
#ifdef HAVE_KNOTIFICATIONS
    KNotification::event(KNotification::Notification,
                         tr("Updates available"),
                         tr("%n application(s) can be updated.", "", count),
                         QStringLiteral("io.github.zachvlat.qutestore"),
                         KNotification::CloseOnTimeout);
#else
    Q_UNUSED(count)
#endif
}
