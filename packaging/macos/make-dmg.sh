#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# make-dmg.sh — build an UNSIGNED macOS .dmg from a built raw-radio-studio.app
#
# Epic 0. No code-signing / notarization (deferred, STUDIO_SPEC §8.5).
# Uses the built-in `hdiutil` (always present on macOS runners) instead of
# `create-dmg`, which would require a Homebrew install and is therefore more
# fragile in CI. Tradeoff: no fancy background/icon-arrangement layout.
#
# The image ships the full AGPLv3 compliance set next to the app: LICENSE,
# NOTICE and the verbatim third-party texts under LICENSES/. The arch is part of
# the filename (only the arch actually built — no bogus "universal").
#
# Usage:
#   scripts/make-dmg.sh <path/to/raw-radio-studio.app> <version> <output-dir>
#
# Example:
#   packaging/macos/make-dmg.sh \
#       build/raw_radio_studio_artefacts/Release/raw-radio-studio.app 0.1.3 dist
#
# Output:
#   <output-dir>/raw-radio-studio-<version>-macos-<arch>.dmg   (arch: arm64 / x86_64)
# -----------------------------------------------------------------------------
set -euo pipefail

APP_PATH="${1:?usage: make-dmg.sh <app-bundle> <version> <output-dir>}"
VERSION="${2:?usage: make-dmg.sh <app-bundle> <version> <output-dir>}"
OUTDIR="${3:?usage: make-dmg.sh <app-bundle> <version> <output-dir>}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# The .dmg is single-arch (the app is built for one architecture). Tag the
# filename accordingly so an arm64-only build is no longer masquerading as a
# universal one (Intel/universal packaging is an open Epic 6 follow-up).
case "$(uname -m)" in
    arm64|aarch64) DMG_ARCH="arm64" ;;
    x86_64)        DMG_ARCH="x86_64" ;;
    *)             DMG_ARCH="$(uname -m)" ;;
esac

VOLNAME="raw-radio-studio"
DMG_BASENAME="raw-radio-studio-${VERSION}-macos-${DMG_ARCH}"
DMG_PATH="${OUTDIR}/${DMG_BASENAME}.dmg"

if [[ ! -d "${APP_PATH}" ]]; then
    echo "error: app bundle not found: ${APP_PATH}" >&2
    exit 1
fi

# AGPLv3 compliance: the license texts must travel with the binary we ship
# (STUDIO_SPEC §7.2 "Corresponding Source" / §8.6 release checklist).
for f in LICENSE NOTICE; do
    if [[ ! -f "${REPO_ROOT}/${f}" ]]; then
        echo "error: required license file missing: ${REPO_ROOT}/${f}" >&2
        exit 1
    fi
done
if [[ ! -d "${REPO_ROOT}/LICENSES" ]]; then
    echo "error: required license directory missing: ${REPO_ROOT}/LICENSES" >&2
    exit 1
fi

mkdir -p "${OUTDIR}"

STAGE="$(mktemp -d)"
cleanup() { rm -rf "${STAGE}"; }
trap cleanup EXIT

# Staging folder = exact contents of the mounted volume.
cp -R "${APP_PATH}" "${STAGE}/raw-radio-studio.app"

# Install notes describing the Gatekeeper bypass.
cp "${SCRIPT_DIR}/INSTALL.txt" "${STAGE}/INSTALL.txt"

# License set, at the volume root next to INSTALL.txt (visible without digging
# into the .app bundle).
cp "${REPO_ROOT}/LICENSE" "${STAGE}/LICENSE"
cp "${REPO_ROOT}/NOTICE" "${STAGE}/NOTICE"
mkdir -p "${STAGE}/LICENSES"
cp -R "${REPO_ROOT}/LICENSES/." "${STAGE}/LICENSES/"

# Drag-to-Applications shortcut.
ln -s /Applications "${STAGE}/Applications"

# Deterministic name in the volume; compression UDZO = zlib (read-only).
rm -f "${DMG_PATH}"
hdiutil create \
    -volname "${VOLNAME}" \
    -srcfolder "${STAGE}" \
    -ov \
    -format UDZO \
    "${DMG_PATH}"

hdiutil verify "${DMG_PATH}"

echo "created: ${DMG_PATH}"
ls -lh "${DMG_PATH}"
