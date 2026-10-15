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

cr="$(printf '\r')"
if grep -l "$cr" "$0" "$SCRIPT_DIR/build-flatpak.sh" "$SCRIPT_DIR"/build-aux/flatpak/* >/dev/null 2>&1; then
  die "CRLF line endings found in the build files. Fix the checkout with:
    git config core.autocrlf input
    git rm --cached -r -q . && git reset --hard"
fi

if [ "$(id -u)" -eq 0 ]; then
  SUDO=""
elif command -v sudo >/dev/null 2>&1; then
  SUDO="sudo"
elif command -v doas >/dev/null 2>&1; then
  SUDO="doas"
else
  SUDO=""
  echo "warning: not root and no sudo/doas; system packages cannot be installed" >&2
fi

install_packages() {
  if command -v apt-get >/dev/null 2>&1; then
    $SUDO apt-get update
    $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y \
      flatpak flatpak-builder bash ca-certificates tar xz-utils patch
  elif command -v dnf >/dev/null 2>&1; then
    $SUDO dnf install -y flatpak flatpak-builder bash ca-certificates tar xz patch
  elif command -v pacman >/dev/null 2>&1; then
    $SUDO pacman -Sy --needed --noconfirm flatpak flatpak-builder bash ca-certificates tar xz patch
  elif command -v zypper >/dev/null 2>&1; then
    $SUDO zypper --non-interactive install flatpak flatpak-builder bash ca-certificates tar xz patch
  elif command -v apk >/dev/null 2>&1; then
    if ! grep -q '^[^#].*/community' /etc/apk/repositories 2>/dev/null; then
      die "enable the Alpine 'community' repository in /etc/apk/repositories first"
    fi
    $SUDO apk add flatpak flatpak-builder bash ca-certificates tar xz patch
  elif command -v xbps-install >/dev/null 2>&1; then
    $SUDO xbps-install -Sy flatpak flatpak-builder bash ca-certificates tar xz patch
  else
    echo "warning: unknown package manager; install flatpak, flatpak-builder, bash, tar, xz and patch yourself" >&2
  fi
}

say "Installing distribution packages"
need_pkgs=false
for cmd in flatpak flatpak-builder bash xz patch; do
  command -v "$cmd" >/dev/null 2>&1 || need_pkgs=true
done

tar --version 2>/dev/null | grep -q 'GNU tar' || need_pkgs=true
if [ "$need_pkgs" = true ]; then
  install_packages || echo "warning: package installation failed, continuing with what is available" >&2
else
  echo "all required packages are already installed"
fi

for cmd in flatpak bash xz patch; do
  command -v "$cmd" >/dev/null 2>&1 || die "$cmd is still missing; install it and re-run"
done

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

if ! command -v flatpak-builder >/dev/null 2>&1; then
  say "No flatpak-builder package; installing org.flatpak.Builder from Flathub"
  flatpak install --user -y --noninteractive flathub org.flatpak.Builder
fi

if [ "$DEPS_ONLY" = true ]; then
  say "Dependencies installed. Build with: ./build-flatpak.sh"
  exit 0
fi

for arg in "$@"; do
  shift
  [ "$arg" = "--deps-only" ] || set -- "$@" "$arg"
done

say "Building the bundle"
exec bash "$SCRIPT_DIR/build-flatpak.sh" "$@"
