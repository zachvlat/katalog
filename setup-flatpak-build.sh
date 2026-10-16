#!/bin/sh
# Install everything needed to build com.zachvlat.qutestore.flatpak, then build it.
#
#   ./setup-flatpak-build.sh               install dependencies and build the bundle
#   ./setup-flatpak-build.sh --deps-only   only install dependencies
#   ./setup-flatpak-build.sh --run         any other option is passed on to build-flatpak.sh
#
# Plain POSIX sh on purpose: on minimal systems (e.g. Alpine/WSL) bash, which
# build-flatpak.sh needs, is one of the things this script installs.

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MANIFEST="${SCRIPT_DIR}/com.zachvlat.qutestore.json"
FLATHUB_URL="https://dl.flathub.org/repo/flathub.flatpakrepo"

DEPS_ONLY=false
for arg in "$@"; do
  case "$arg" in
    --deps-only) DEPS_ONLY=true ;;
    -h|--help)
      sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'
      exit 0 ;;
  esac
done

say() { printf '\n==> %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

# --- Sanity: CRLF checkouts ------------------------------------------------
# A Windows checkout with core.autocrlf=true turns the patches and scripts into
# CRLF files, which then fail inside flatpak-builder. .gitattributes prevents it
# for fresh checkouts; catch older ones here.
cr="$(printf '\r')"
if grep -l "$cr" "$0" "$SCRIPT_DIR/build-flatpak.sh" "$SCRIPT_DIR"/build-aux/flatpak/* >/dev/null 2>&1; then
  die "CRLF line endings found in the build files. Fix the checkout with:
    git config core.autocrlf input
    git rm --cached -r -q . && git reset --hard"
fi

# --- Privileges ------------------------------------------------------------
# Under sudo, "--user" means root's own Flatpak installation instead of the
# real user's. Package installation calls sudo itself when needed.
if [ "$(id -u)" -eq 0 ] && [ -n "${SUDO_USER:-}" ]; then
  die "do not run this with sudo; run it as your own user (it calls sudo itself for system packages)"
fi
if [ "$(id -u)" -eq 0 ]; then
  SUDO=""
elif have sudo; then
  SUDO="sudo"
elif have doas; then
  SUDO="doas"
else
  SUDO=""
  echo "warning: not root and no sudo/doas; system packages cannot be installed" >&2
fi

# --- Distribution packages -------------------------------------------------
# Distribution package name for a tool.
package_for() {
  case "$1" in
    xz) if have apt-get; then echo xz-utils; else echo xz; fi ;;
    *) echo "$1" ;;
  esac
}

# Installs the given packages with the system package manager. Only missing
# packages are ever passed in, so tools that are already there (possibly not
# from a package, e.g. a flatpak shipped with an image-based OS) are left alone.
install_packages() {
  if have apt-get; then
    $SUDO apt-get update
    $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y "$@"
  elif have dnf; then
    $SUDO dnf install -y "$@"
  elif have pacman; then
    # No -y: syncing the database without upgrading (-Sy) is a partial
    # upgrade, which Arch does not support.
    $SUDO pacman -S --needed --noconfirm "$@"
  elif have zypper; then
    $SUDO zypper --non-interactive install "$@"
  elif have apk; then
    $SUDO apk add "$@"
  elif have xbps-install; then
    $SUDO xbps-install -y "$@"
  else
    echo "warning: unknown package manager; install these yourself: $*" >&2
    return 1
  fi
}

# Missing tools among the arguments ("tar" counts as missing unless it is GNU
# tar: BusyBox tar cannot unpack everything flatpak-builder needs).
missing_tools() {
  for tool in "$@"; do
    if [ "$tool" = tar ]; then
      tar --version 2>/dev/null | grep -q 'GNU tar' || printf '%s ' tar
    elif ! have "$tool"; then
      printf '%s ' "$tool"
    fi
  done
}

install_tools() {
  pkgs=""
  for tool in "$@"; do
    pkgs="$pkgs $(package_for "$tool")"
  done
  # shellcheck disable=SC2086
  install_packages $pkgs ca-certificates
}

say "Checking required tools"

# flatpak and bash are always needed.
missing="$(missing_tools flatpak bash)"
if [ -n "$missing" ]; then
  echo "installing: $missing"
  # shellcheck disable=SC2086
  install_tools $missing || true
fi
have flatpak || die "flatpak is missing and could not be installed; install it and re-run"
have bash || die "bash is missing and could not be installed; install it and re-run"

# A host flatpak-builder also needs xz, patch and GNU tar on the host: it
# unpacks and patches the sources itself. The org.flatpak.Builder app brings
# all of them in its own runtime, so it is the fallback whenever the host
# tools are not there and cannot be installed.
builder_app_installed() {
  flatpak info --user org.flatpak.Builder >/dev/null 2>&1 || flatpak info org.flatpak.Builder >/dev/null 2>&1
}

USE_BUILDER_APP=false
missing="$(missing_tools flatpak-builder xz patch tar)"
if [ -n "$missing" ]; then
  if builder_app_installed; then
    USE_BUILDER_APP=true
  else
    echo "installing: $missing"
    # shellcheck disable=SC2086
    if ! install_tools $missing; then
      echo "note: could not install the host build tools; using the org.flatpak.Builder app instead" >&2
    fi
    [ -n "$(missing_tools flatpak-builder xz patch tar)" ] && USE_BUILDER_APP=true
  fi
fi
if [ "$USE_BUILDER_APP" = true ]; then
  echo "building with the org.flatpak.Builder app"
  # The Builder app looks the SDK up through the desktop session, which uses
  # the default ~/.local/share/flatpak. A shell that points XDG_DATA_HOME
  # elsewhere (e.g. a KDE development environment) would make the runtime
  # and SDK below go into an installation the Builder never looks at.
  if [ -n "${XDG_DATA_HOME:-}" ] && [ "$XDG_DATA_HOME" != "$HOME/.local/share" ]; then
    echo "note: ignoring XDG_DATA_HOME=$XDG_DATA_HOME; using $HOME/.local/share/flatpak"
    unset XDG_DATA_HOME
  fi
else
  echo "building with the host flatpak-builder"
fi

# --- Flathub, runtime and SDK ----------------------------------------------
# Everything goes into the --user installation: no polkit/system helper is
# needed, which matters in containers and WSL where those are not running.
say "Adding the Flathub remote (--user)"
flatpak remote-add --user --if-not-exists flathub "$FLATHUB_URL"

json_value() {
  sed -n "s/.*\"$1\": \"\([^\"]*\)\".*/\1/p" "$MANIFEST" | head -1
}
RUNTIME="$(json_value runtime)"
SDK="$(json_value sdk)"
VERSION="$(json_value runtime-version)"
[ -n "$RUNTIME" ] && [ -n "$SDK" ] && [ -n "$VERSION" ] || die "could not read runtime/sdk from $MANIFEST"

say "Installing $RUNTIME//$VERSION and $SDK//$VERSION (about 1-2 GB the first time)"
flatpak install --user -y --noninteractive flathub "$RUNTIME//$VERSION" "$SDK//$VERSION"

BUILD_ARGS=""
if [ "$USE_BUILDER_APP" = true ]; then
  if ! builder_app_installed; then
    say "Installing org.flatpak.Builder from Flathub"
    flatpak install --user -y --noninteractive flathub org.flatpak.Builder
  fi
  BUILD_ARGS="--use-builder-app"
fi

# --- Build -----------------------------------------------------------------
if [ "$DEPS_ONLY" = true ]; then
  say "Dependencies installed. Build with: ./build-flatpak.sh $BUILD_ARGS"
  exit 0
fi

# Drop our own option before handing the rest to build-flatpak.sh.
for arg in "$@"; do
  shift
  [ "$arg" = "--deps-only" ] || set -- "$@" "$arg"
done

say "Building the bundle"
# shellcheck disable=SC2086
exec bash "$SCRIPT_DIR/build-flatpak.sh" $BUILD_ARGS "$@"
