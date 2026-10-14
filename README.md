# Qute Store

A work-in-progress **native KDE/Qt (Kirigami) port of a Flatpak store**, borrowing its
design and per-feature behavior from [Bazaar](https://github.com/kolunmi/bazaar).

## Layout

- `src/` – C++ backend: Flatpak management, models, and the QML bridge.
- `qml/` – Kirigami/QML UI.
- `data/` – Desktop entry, AppStream metadata, icons.
- `com.zachvlat.qutestore.json` – Flatpak manifest targeting `org.kde.Platform` / `org.kde.Sdk`.

## Mapping to Bazaar

| Bazaar (reference) | This project |
| --- | --- |
| `src/bz-flatpak-instance.{c,h}` | `src/flatpakinstance.{h,cpp}` (`FlatpakManager`) |
| `src/bz-flatpak-entry.{c,h}` | `src/entrymodel.{h,cpp}` (`EntryModel`, `EntryData`) |
| `src/bz-backend.{c,h}` | `src/backend.{h,cpp}` bridging to QML models |
| transaction handling | `src/transaction.{h,cpp}` (`Transaction`, wraps `FlatpakTransaction`) |
| `build-aux/flatpak/...Bazaar.json` | `com.zachvlat.qutestore.json` |

## Building locally

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH=/usr \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`libflatpak` (`HAVE_FLATPAK`) and `libappstream` (`HAVE_APPSTREAM`) are detected
optionally at configure time; the app still builds and runs without them.

## Building the `.flatpak` bundle

```sh
./build-flatpak.sh              # build com.zachvlat.qutestore.flatpak
./build-flatpak.sh --install    # ... and install it (--user)
./build-flatpak.sh --run        # ... install it and launch it
./build-flatpak.sh --incremental   # rebuild only what changed (keeps the build dir)
./build-flatpak.sh --clean      # drop the previous build dir/repo first
```

The result is `com.zachvlat.qutestore.flatpak` in the project root:

```sh
flatpak install --user ./com.zachvlat.qutestore.flatpak
flatpak run com.zachvlat.qutestore
```

Prerequisites:

- `flatpak`
- `org.kde.Platform//6.11` and `org.kde.Sdk//6.11`
  (`flatpak install flathub org.kde.Platform//6.11 org.kde.Sdk//6.11`)
- `flatpak-builder` (distribution package) **or** the `org.flatpak.Builder` app
  (`flatpak install --user flathub org.flatpak.Builder`)

The first build compiles the bundled dependency stack, so it takes a while; later
builds are fast.

### What the bundle contains

`libflatpak`, `libostree` and `bubblewrap` are **not** part of `org.kde.Platform`, so
`com.zachvlat.qutestore.json` builds what is missing before building the app itself:

| module | why |
| --- | --- |
| `bubblewrap` 0.12.0 | sandboxes launched apps; built first because `flatpak`'s configure runs `bwrap --version` |
| `libfuse` 2.9.9 (+ patches, `fusermount` wrapper) | `ostree`/`flatpak` revokefs staging |
| `ostree` v2026.4 | content-addressed store used by `libflatpak` |
| `xdg-dbus-proxy` 0.1.8 | proxied bus for launched apps |
| `polkit` 127 (`-Dlibs-only=true`) | `flatpak` system-helper client |
| `flatpak` 1.18.2 (`-Dmalcontent=disabled`, `-Ddconf=disabled`, `-Dsystem_helper=enabled`) | the API the app links against |
| `qutestore` (this project) | the application |

`appstream` is deliberately **not** bundled: the KDE SDK has the 1.0.6 development
files and the runtime ships `libappstream.so.5`, so the app links against the runtime
copy and the build is much shorter.

### Notes

- If `org.kde.Sdk` is installed with `flatpak install --user`, the *sandboxed*
  `org.flatpak.Builder` cannot see it (its sandbox resolves the user installation to
  its own private data directory and exits with `Unable to find sdk org.kde.Sdk`).
  `build-flatpak.sh` handles this by running the Builder's `flatpak-builder` binary
  directly instead of inside its sandbox. Installing the SDK system-wide
  (`flatpak install --system flathub org.kde.Sdk//6.11`) also makes the plain
  `flatpak run --command=flatpak-builder org.flatpak.Builder …` workflow work.
- Shells that override `HOME`/`XDG_DATA_HOME` (build sandboxes, some IDEs) hide the
  user installation from flatpak tooling; `unset XDG_DATA_HOME` before building.

## Status

Currently implemented:

- Runnable Kirigami application (`qutestore`) with Explore/Search/Installed/Updates navigation
- `FlatpakManager` opening system and user `FlatpakInstallation`s (optional, guarded by `HAVE_FLATPAK`)
- `EntryModel` list model plus `Backend` bridge exposed to QML as `backend`
- Installed/Updates lists from `flatpak_installation_list_installed_refs[_for_update]`
- **AppStream-backed search** (guarded by `HAVE_APPSTREAM`), run off the UI thread with
  `QtConcurrent`: loads each remote's `appstream.xml.gz` from
  `flatpak_remote_get_appstream_dir()` into an `AsPool` and uses `as_pool_search()`,
  mirroring Bazaar's approach. Falls back to name-matching remote refs when AppStream
  is unavailable.
- **Install / update / remove transactions** via `FlatpakTransaction`, run on a worker
  thread with progress and status reported back to QML. The footer shows a progress bar,
  the current operation, and any error; `Transaction` resolves the installed ref and/or
  the owning remote automatically. Launching installed apps is available too.
- Flatpak manifest targeting `org.kde.Platform` / `org.kde.Sdk` 6.11

### Development in a container / distrobox

If `/var/lib/flatpak` is not mounted (e.g. inside a distrobox), point the AppStream
loader at the host bundle explicitly:

```sh
QUTESTORE_APPSTREAM_DIRS=/run/host/var/lib/flatpak/appstream/flathub/x86_64/active ./build/qutestore
```

Note that libflatpak still looks for `/var/lib/flatpak`, so the Installed/Updates lists
and transactions stay empty in that environment even though search works.

Still to port from Bazaar: curated YAML page, the Explore page, full app details
(screenshots/permissions), hooks and blocklists.
