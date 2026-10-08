#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# make-dmg.sh — build an UNSIGNED macOS .dmg from a built raw-radio-studio.app
#
# Epic 0. No code-signing / notarization (deferred, STUDIO_SPEC §8.5).
# Uses the built-in `hdiutil` (always present on macOS runners) instead of
# `create-dmg`, which would require a Homebrew install and is therefore more
# fragile in CI. Tradeoff: no fancy background/icon-arrangement layout.
#
# Usage:
#   scripts/make-dmg.sh <path/to/raw-radio-studio.app> <version> <output-dir>
#
# Example:
#   packaging/macos/make-dmg.sh \
#       build/raw_radio_studio_artefacts/Release/raw-radio-studio.app 0.1.1 dist
#
# Output:
#   <output-dir>/raw-radio-studio-<version>-macos.dmg
# -----------------------------------------------------------------------------
set -euo pipefail

APP_PATH="${1:?usage: make-dmg.sh <app-bundle> <version> <output-dir>}"
VERSION="${2:?usage: make-dmg.sh <app-bundle> <version> <output-dir>}"
OUTDIR="${3:?usage: make-dmg.sh <app-bundle> <version> <output-dir>}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VOLNAME="raw-radio-studio"
DMG_BASENAME="raw-radio-studio-${VERSION}-macos"
DMG_PATH="${OUTDIR}/${DMG_BASENAME}.dmg"

if [[ ! -d "${APP_PATH}" ]]; then
    echo "error: app bundle not found: ${APP_PATH}" >&2
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
