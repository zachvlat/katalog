#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MANIFEST="${SCRIPT_DIR}/io.github.zachvlat.qutestore.json"
OUTPUT="${SCRIPT_DIR}/io.github.zachvlat.qutestore.flatpak"
REPO="${SCRIPT_DIR}/.flatpak-builder/repo"
BUILD_DIR="${SCRIPT_DIR}/.flatpak-builder/build"
STATE_DIR="${SCRIPT_DIR}/.flatpak-builder/state"
INSTALL=false RUN=false CLEAN=false USE_BUILDER_APP=false
JOBS="$(nproc)"

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]
  --manifest FILE  Flatpak manifest
  --output FILE    Output .flatpak bundle
  --repo DIR       Repo dir
  --build-dir DIR  Build dir
  --state-dir DIR  flatpak-builder state dir
  --jobs N         Parallel jobs (default $(nproc))
  --incremental    Accepted for compatibility; rebuilds are always incremental
  --install        Install after building (--user)
  --run            Install and run
  --clean          Remove previous build dirs
  --use-builder-app  Build with the org.flatpak.Builder app even if a host
                     flatpak-builder is installed
  -h, --help       Show help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --manifest) MANIFEST="$2"; shift 2 ;;
    --output) OUTPUT="$2"; shift 2 ;;
    --repo) REPO="$2"; shift 2 ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --state-dir) STATE_DIR="$2"; shift 2 ;;
    --jobs) JOBS="$2"; shift 2 ;;
    --incremental) shift ;;
    --install) INSTALL=true; shift ;;
    --run) RUN=true; INSTALL=true; shift ;;
    --clean) CLEAN=true; shift ;;
    --use-builder-app) USE_BUILDER_APP=true; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

if ! command -v flatpak >/dev/null 2>&1; then
  echo "flatpak not found" >&2; exit 1
fi

if [[ $EUID -eq 0 && -n "${SUDO_USER:-}" ]]; then
  echo "Do not run this with sudo: build as your own user (it uses your --user Flatpak installation)." >&2
  exit 1
fi

RUNTIME="$(sed -n 's/.*"runtime": "\([^"]*\)".*/\1/p' "$MANIFEST" | head -1)"
RUNTIME_VERSION="$(sed -n 's/.*"runtime-version": "\([^"]*\)".*/\1/p' "$MANIFEST" | head -1)"
SDK="$(sed -n 's/.*"sdk": "\([^"]*\)".*/\1/p' "$MANIFEST" | head -1)"
BASE="$(sed -n 's/.*"base": "\([^"]*\)".*/\1/p' "$MANIFEST" | head -1)"
BASE_VERSION="$(sed -n 's/.*"base-version": "\([^"]*\)".*/\1/p' "$MANIFEST" | head -1)"
missing=()
flatpak info "$SDK//$RUNTIME_VERSION" >/dev/null 2>&1 || missing+=("$SDK//$RUNTIME_VERSION")
flatpak info "$RUNTIME//$RUNTIME_VERSION" >/dev/null 2>&1 || missing+=("$RUNTIME//$RUNTIME_VERSION")
if [[ -n "$BASE" ]]; then
  flatpak info "$BASE//$BASE_VERSION" >/dev/null 2>&1 || missing+=("$BASE//$BASE_VERSION")
fi
if [[ ${#missing[@]} -gt 0 ]]; then
  echo "Missing Flatpak runtime/SDK: ${missing[*]}" >&2
  echo "Install it, e.g.: flatpak install --user -y flathub ${missing[*]}" >&2
  exit 1
fi

if ! flatpak info --user "$SDK//$RUNTIME_VERSION" >/dev/null 2>&1 \
   && flatpak info "$SDK//$RUNTIME_VERSION" >/dev/null 2>&1; then
  echo "note: $SDK//$RUNTIME_VERSION is only visible outside the current XDG_DATA_HOME" >&2
  echo "      (${XDG_DATA_HOME:-unset}). If the build cannot find the SDK, re-run after" >&2
  echo "      'unset XDG_DATA_HOME'." >&2
fi

FLATPAK_BUILDER=(flatpak-builder)
EXTRA_ARGS=()
if [[ "$USE_BUILDER_APP" == true ]] || ! command -v flatpak-builder >/dev/null 2>&1; then
  if ! flatpak info org.flatpak.Builder >/dev/null 2>&1; then
    echo "flatpak-builder is not available. Install one of:" >&2
    echo "  - the flatpak-builder package for your distribution, or" >&2
    echo "  - flatpak install --user -y flathub org.flatpak.Builder" >&2
    exit 1
  fi
  if [[ -n "${XDG_DATA_HOME:-}" && "$XDG_DATA_HOME" != "$HOME/.local/share" ]]; then
    echo "note: ignoring XDG_DATA_HOME=$XDG_DATA_HOME; using $HOME/.local/share/flatpak" >&2
    unset XDG_DATA_HOME
  fi
  if ! flatpak info "$SDK//$RUNTIME_VERSION" >/dev/null 2>&1; then
    echo "$SDK//$RUNTIME_VERSION is not installed where org.flatpak.Builder looks for it." >&2
    echo "Install it with:" >&2
    echo "  env -u XDG_DATA_HOME flatpak install --user -y flathub $RUNTIME//$RUNTIME_VERSION $SDK//$RUNTIME_VERSION" >&2
    exit 1
  fi
  builder_home="$(flatpak run --command=sh org.flatpak.Builder -c 'printf %s "$HOME"' 2>/dev/null || true)"
  FLATPAK_BUILDER=(flatpak run
                   "--env=FLATPAK_USER_DIR=${builder_home:-$HOME}/.local/share/flatpak"
                   --command=flatpak-builder org.flatpak.Builder)
  EXTRA_ARGS=(--disable-rofiles-fuse)

  for _path in "$MANIFEST" "$(dirname "$REPO")" "$(dirname "$BUILD_DIR")"; do
    _check="$_path"
    [[ -e "$_check" ]] || _check="$(dirname "$_check")"
    if ! flatpak run --command=test org.flatpak.Builder -e "$_check" 2>/dev/null; then
      echo "The org.flatpak.Builder sandbox cannot access $_check." >&2
      echo "Move the project (and build directories) under your home directory, or" >&2
      echo "install your distribution's flatpak-builder package instead." >&2
      exit 1
    fi
  done
fi

[[ -f "$MANIFEST" ]] || { echo "Manifest not found: $MANIFEST" >&2; exit 1; }

if [[ "$CLEAN" == true ]]; then
  rm -rf "$REPO" "$BUILD_DIR" "$STATE_DIR"
fi
mkdir -p "$(dirname "$REPO")" "$(dirname "$BUILD_DIR")" "$STATE_DIR"

FORCE_CLEAN=(--force-clean)

echo "Building $(basename "$MANIFEST") with ${FLATPAK_BUILDER[*]}"
echo "  repo:      $REPO"
echo "  build dir: $BUILD_DIR"
echo "  jobs:      $JOBS"
echo "(the first build compiles libflatpak, ostree and friends, so it takes a while)"
echo

"${FLATPAK_BUILDER[@]}" "${FORCE_CLEAN[@]}" "${EXTRA_ARGS[@]}" --ccache --jobs="$JOBS" --user \
  --state-dir="$STATE_DIR" --repo="$REPO" "$BUILD_DIR" "$MANIFEST"

echo
echo "Exporting bundle..."
flatpak build-bundle --runtime-repo=https://flathub.org/repo/flathub.flatpakrepo "$REPO" "$OUTPUT" io.github.zachvlat.qutestore

echo "Bundle: $OUTPUT"

if [[ "$INSTALL" == true ]]; then
  flatpak install --user -y "$OUTPUT"
fi
if [[ "$RUN" == true ]]; then
  flatpak run io.github.zachvlat.qutestore
fi

