#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# make-appimage.sh — build an UNSIGNED x86_64 AppImage for raw-radio-studio
#
# Epic 0. Uses `linuxdeploy`, which deploys the executable together with its
# non-core shared-library dependencies, generates AppRun, and produces the
# AppImage in one step (appimagetool support is bundled in linuxdeploy, so no
# second download is needed).
#
# linuxdeploy is pinned to an exact release for reproducibility. It is itself an
# AppImage; `APPIMAGE_EXTRACT_AND_RUN=1` lets it run on runners without FUSE
# (ubuntu-24.04 ships no libfuse2 by default).
#
# Usage:
#   packaging/linux/make-appimage.sh <path/to/raw-radio-studio> <version> <output-dir>
#
# Output:
#   <output-dir>/raw-radio-studio-<version>-x86_64.AppImage
#
# Env overrides:
#   LINUXDEPLOY_VERSION  (default: 1-alpha-20251107-1)
#   LINUXDEPLOY_URL      (default: derived from the pinned version)
# -----------------------------------------------------------------------------
set -euo pipefail

BIN="${1:?usage: make-appimage.sh <binary> <version> <output-dir>}"
VERSION="${2:?usage: make-appimage.sh <binary> <version> <output-dir>}"
OUTDIR="${3:?usage: make-appimage.sh <binary> <version> <output-dir>}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$(cd "$(dirname "${BIN}")" && pwd)/$(basename "${BIN}")"

if [[ ! -x "${BIN}" ]]; then
    echo "error: executable not found: ${BIN}" >&2
    exit 1
fi

ARCH="$(uname -m)"
case "${ARCH}" in
    x86_64)          LD_ASSET="linuxdeploy-x86_64.AppImage" ;;
    aarch64|arm64)   LD_ASSET="linuxdeploy-aarch64.AppImage" ;;
    *) echo "error: unsupported architecture: ${ARCH}" >&2; exit 1 ;;
esac

LINUXDEPLOY_VERSION="${LINUXDEPLOY_VERSION:-1-alpha-20251107-1}"
LINUXDEPLOY_URL="${LINUXDEPLOY_URL:-https://github.com/linuxdeploy/linuxdeploy/releases/download/${LINUXDEPLOY_VERSION}/${LD_ASSET}}"

WORK="$(mktemp -d)"
cleanup() { rm -rf "${WORK}"; }
trap cleanup EXIT

TOOLS="${WORK}/tools"
APPDIR="${WORK}/raw-radio-studio.AppDir"
mkdir -p "${TOOLS}" "${APPDIR}" "${OUTDIR}"
OUTDIR="$(cd "${OUTDIR}" && pwd)"

LINUXDEPLOY="${TOOLS}/linuxdeploy"
curl -fsSL -o "${LINUXDEPLOY}" "${LINUXDEPLOY_URL}"
chmod +x "${LINUXDEPLOY}"

# Run the linuxdeploy AppImage without FUSE.
export APPIMAGE_EXTRACT_AND_RUN=1

# Populate the AppDir and produce the AppImage. linuxdeploy writes the output
# into the current working directory, so run from OUTDIR and use absolute paths.
rm -f "${OUTDIR}"/*.AppImage
(
    cd "${OUTDIR}"
    ARCH="${ARCH}" "${LINUXDEPLOY}" \
        --appdir "${APPDIR}" \
        --executable "${BIN}" \
        --desktop-file "${SCRIPT_DIR}/raw-radio-studio.desktop" \
        --icon-file "${SCRIPT_DIR}/raw-radio-studio.png" \
        --output appimage
)

PRODUCED="$(find "${OUTDIR}" -maxdepth 1 -name '*.AppImage' -print -quit)"
if [[ -z "${PRODUCED}" ]]; then
    echo "error: linuxdeploy did not produce an AppImage" >&2
    exit 1
fi

TARGET="${OUTDIR}/raw-radio-studio-${VERSION}-${ARCH}.AppImage"
if [[ "${PRODUCED}" != "${TARGET}" ]]; then
    mv -f "${PRODUCED}" "${TARGET}"
fi
chmod +x "${TARGET}"

# Smoke check: the AppImage runtime executes usr/bin/raw-radio-studio; the app
# supports a headless --version path.
"${TARGET}" --version || echo "warning: AppImage --version smoke check failed (continuing)"

echo "created: ${TARGET}"
ls -lh "${TARGET}"
