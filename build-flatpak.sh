#!/usr/bin/env bash
# Build a Flatpak bundle for com.zachvlat.qutestore

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MANIFEST="${SCRIPT_DIR}/com.zachvlat.qutestore.json"
OUTPUT="${SCRIPT_DIR}/com.zachvlat.qutestore.flatpak"
REPO="${SCRIPT_DIR}/.flatpak-builder/repo"
BUILD_DIR="${SCRIPT_DIR}/.flatpak-builder/build"
STATE_DIR="${SCRIPT_DIR}/.flatpak-builder/state"
INSTALL=false RUN=false CLEAN=false INCREMENTAL=false
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
  --incremental    Keep the previous build dir (only rebuild what changed)
  --install        Install after building (--user)
  --run            Install and run
  --clean          Remove previous build dirs
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
    --incremental) INCREMENTAL=true; shift ;;
    --install) INSTALL=true; shift ;;
    --run) RUN=true; INSTALL=true; shift ;;
    --clean) CLEAN=true; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

if ! command -v flatpak >/dev/null 2>&1; then
  echo "flatpak not found" >&2; exit 1
fi

# The runtime and SDK the manifest builds against must be installed.
RUNTIME="$(sed -n 's/.*"runtime": "\([^"]*\)".*/\1/p' "$MANIFEST" | head -1)"
RUNTIME_VERSION="$(sed -n 's/.*"runtime-version": "\([^"]*\)".*/\1/p' "$MANIFEST" | head -1)"
SDK="$(sed -n 's/.*"sdk": "\([^"]*\)".*/\1/p' "$MANIFEST" | head -1)"
missing=()
flatpak info "$SDK//$RUNTIME_VERSION" >/dev/null 2>&1 || missing+=("$SDK//$RUNTIME_VERSION")
flatpak info "$RUNTIME//$RUNTIME_VERSION" >/dev/null 2>&1 || missing+=("$RUNTIME//$RUNTIME_VERSION")
if [[ ${#missing[@]} -gt 0 ]]; then
  echo "Missing Flatpak runtime/SDK: ${missing[*]}" >&2
  echo "Install it, e.g.: flatpak install --user -y flathub ${missing[*]}" >&2
  exit 1
fi

# A --user installation is resolved through XDG_DATA_HOME, so shells that override
# HOME or XDG_DATA_HOME (build sandboxes, some editors) can hide it from the
# bundled flatpak tooling.
if ! flatpak info --user "$SDK//$RUNTIME_VERSION" >/dev/null 2>&1 \
   && flatpak info "$SDK//$RUNTIME_VERSION" >/dev/null 2>&1; then
  echo "note: $SDK//$RUNTIME_VERSION is only visible outside the current XDG_DATA_HOME" >&2
  echo "      (${XDG_DATA_HOME:-unset}). If the build cannot find the SDK, re-run after" >&2
  echo "      'unset XDG_DATA_HOME'." >&2
fi

# The paths the flatpak CLI reports are absolute paths in the host namespace. In
# containerised views (where the host root is bind mounted at /run/host) they may
# only be reachable under that prefix.
resolve_host_path() {
  if [[ -e "$1" ]]; then
    printf '%s\n' "$1"
  elif [[ -e "/run/host$1" ]]; then
    printf '%s\n' "/run/host$1"
  else
    printf '%s\n' "$1"
  fi
}

FLATPAK_BUILDER=(flatpak-builder)
ROFILES=()
if ! command -v flatpak-builder >/dev/null 2>&1; then
  BUILDER_LOC="$(flatpak info --user --show-location org.flatpak.Builder 2>/dev/null || true)"
  if [[ -z "$BUILDER_LOC" ]]; then
    BUILDER_LOC="$(flatpak info --show-location org.flatpak.Builder 2>/dev/null || true)"
  fi
  [[ -n "$BUILDER_LOC" ]] && BUILDER_LOC="$(resolve_host_path "$BUILDER_LOC")"
  if [[ -z "$BUILDER_LOC" ]]; then
    echo "flatpak-builder is not available. Install one of:" >&2
    echo "  - the flatpak-builder package for your distribution, or" >&2
    echo "  - flatpak install --user -y flathub org.flatpak.Builder" >&2
    exit 1
  fi

  # Use the Builder app's flatpak-builder binary directly instead of running it
  # inside the Builder sandbox: sandboxed, it resolves the SDK in the app's own
  # private user installation and therefore cannot see an SDK that was installed
  # with "flatpak install --user".
  BUILD_APP="${BUILDER_LOC}/files"
  if [[ ! -x "${BUILD_APP}/bin/flatpak-builder" ]]; then
    echo "org.flatpak.Builder does not provide bin/flatpak-builder" >&2; exit 1
  fi
  BUILDER_RUNTIME="$(flatpak info --user --show-runtime org.flatpak.Builder 2>/dev/null || flatpak info --show-runtime org.flatpak.Builder 2>/dev/null || true)"
  BUILDER_RUNTIME_DIR=""
  if [[ -n "$BUILDER_RUNTIME" ]]; then
    _runtime_loc="$(flatpak info --show-location "$BUILDER_RUNTIME" 2>/dev/null || true)"
    [[ -n "$_runtime_loc" ]] && BUILDER_RUNTIME_DIR="$(resolve_host_path "$_runtime_loc")/files"
  fi

  _libs="${BUILD_APP}/lib"
  _bins="${BUILD_APP}/bin"
  if [[ -n "$BUILDER_RUNTIME_DIR" ]]; then
    _libs="${_libs}:${BUILDER_RUNTIME_DIR}/lib/x86_64-linux-gnu:${BUILDER_RUNTIME_DIR}/lib"
    # flatpak-builder shells out to a few tools itself (patch, git, ...). Inside its
    # sandbox those come from the runtime, so do the same when running unsandboxed.
    _bins="${_bins}:${BUILDER_RUNTIME_DIR}/bin"
    export XDG_DATA_DIRS="${BUILD_APP}/share:${BUILDER_RUNTIME_DIR}/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
  fi
  export PATH="${_bins}:${PATH}"
  export LD_LIBRARY_PATH="${_libs}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
  FLATPAK_BUILDER=("${BUILD_APP}/bin/flatpak-builder")
  # Outside the Builder's sandbox its bundled fusermount wrapper cannot call
  # flatpak-spawn, so rofiles-fuse cannot mount the build dir. The build works the
  # same without it.
  ROFILES=(--disable-rofiles-fuse)

  # The Builder's flatpak-builder is built against the freedesktop runtime, so point
  # its TLS stack at a CA bundle that exists here.
  for _ca in /etc/ssl/certs/ca-certificates.crt /etc/pki/tls/certs/ca-bundle.crt \
             /run/host/etc/ssl/certs/ca-certificates.crt; do
    if [[ -r "$_ca" ]]; then
      export SSL_CERT_FILE="${SSL_CERT_FILE:-$_ca}"
      export CURL_CA_BUNDLE="${CURL_CA_BUNDLE:-$_ca}"
      break
    fi
  done
fi


[[ -f "$MANIFEST" ]] || { echo "Manifest not found: $MANIFEST" >&2; exit 1; }

if [[ "$CLEAN" == true ]]; then
  rm -rf "$REPO" "$BUILD_DIR" "$STATE_DIR"
fi
mkdir -p "$(dirname "$REPO")" "$(dirname "$BUILD_DIR")" "$STATE_DIR"

FORCE_CLEAN=(--force-clean)
if [[ "$INCREMENTAL" == true ]]; then
  FORCE_CLEAN=()
fi

echo "Building $(basename "$MANIFEST") with ${FLATPAK_BUILDER[*]}"
echo "  repo:      $REPO"
echo "  build dir: $BUILD_DIR"
echo "  jobs:      $JOBS"
echo "(the first build compiles libflatpak, ostree and friends, so it takes a while)"
echo

"${FLATPAK_BUILDER[@]}" "${FORCE_CLEAN[@]}" "${ROFILES[@]}" --ccache --jobs="$JOBS" --user \
  --state-dir="$STATE_DIR" --repo="$REPO" "$BUILD_DIR" "$MANIFEST"

echo
echo "Exporting bundle..."
flatpak build-bundle --runtime-repo=https://flathub.org/repo/flathub.flatpakrepo "$REPO" "$OUTPUT" com.zachvlat.qutestore

echo "Bundle: $OUTPUT"

[[ "$INSTALL" == true ]] && flatpak install --user -y "$OUTPUT"
[[ "$RUN" == true ]] && flatpak run com.zachvlat.qutestore

