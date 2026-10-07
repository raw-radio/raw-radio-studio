#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# generate-icons.sh — regenerate the raw-radio-studio raster icon assets from the
# full-bleed master SVG (assets/brand/app-icon.svg).
#
# The generated binaries are committed so CI produces reproducible bundles
# without a converter installed; this script only refreshes them when the source
# SVG changes.
#
# Outputs:
#   assets/brand/AppIcon.icns                        macOS app icon
#   packaging/linux/raw-radio-studio.png             512x512 (AppImage)
#   packaging/linux/icons/hicolor/<s>x<s>/apps/*.png hicolor set for the .deb
#
# Requirements:
#   * ImageMagick 7 (`magick`). Without it we exit with a clear message — the
#     committed assets remain the source of truth.
#   * macOS `.icns` additionally needs `iconutil` (macOS only). On Linux the
#     script skips the .icns and still refreshes the Linux PNGs.
#
# macOS mask: `iconutil` does NOT add the macOS squircle, so the master is
# pre-masked to Apple's Big Sur icon grid — an 824x824 rounded square centred in
# the 1024 canvas (100 px margin), corner radius 185. The designer's exact mask
# recipe was not present in the repository when these assets were generated, so
# the standard Apple grid is used; adjust the MASK_* constants if a recipe lands.
#
# Usage:
#   assets/brand/generate-icons.sh
# -----------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SVG="${SCRIPT_DIR}/app-icon.svg"

if ! command -v magick >/dev/null 2>&1; then
    echo "error: ImageMagick 7 ('magick') not found." >&2
    echo "       The committed icon assets are already usable; install ImageMagick" >&2
    echo "       only if you need to regenerate them from the SVG." >&2
    exit 1
fi

if [[ ! -f "${SVG}" ]]; then
    echo "error: master SVG not found: ${SVG}" >&2
    exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

# Full-bleed master (8-bit sRGB with alpha). Linux ships this unmasked; the OS
# desktop applies whatever rounding it wants.
magick -background none "${SVG}" -resize 1024x1024 \
    -depth 8 -define png:color-type=6 "${WORK}/master.png"

# --- macOS -------------------------------------------------------------------
if command -v iconutil >/dev/null 2>&1; then
    MASK_MARGIN=100
    MASK_RADIUS=185
    BODY_END=$((1024 - MASK_MARGIN - 1))

    magick -size 1024x1024 xc:none -fill white \
        -draw "roundrectangle ${MASK_MARGIN},${MASK_MARGIN} ${BODY_END},${BODY_END} ${MASK_RADIUS},${MASK_RADIUS}" \
        -depth 8 -define png:color-type=6 "${WORK}/mask.png"

    magick "${WORK}/master.png" "${WORK}/mask.png" \
        -alpha off -compose CopyOpacity -composite \
        -depth 8 -define png:color-type=6 "${WORK}/masked.png"

    ICONSET="${WORK}/AppIcon.iconset"
    mkdir -p "${ICONSET}"

    while read -r name size; do
        magick "${WORK}/masked.png" -filter Lanczos -resize "${size}x${size}" \
            -depth 8 -define png:color-type=6 "${ICONSET}/${name}"
    done <<'EOF'
icon_16x16.png 16
icon_16x16@2x.png 32
icon_32x32.png 32
icon_32x32@2x.png 64
icon_128x128.png 128
icon_128x128@2x.png 256
icon_256x256.png 256
icon_256x256@2x.png 512
icon_512x512.png 512
icon_512x512@2x.png 1024
EOF

    iconutil -c icns "${ICONSET}" -o "${SCRIPT_DIR}/AppIcon.icns"
    echo "wrote ${SCRIPT_DIR}/AppIcon.icns"
else
    echo "warning: iconutil not found (macOS only) — skipping AppIcon.icns" >&2
fi

# --- Linux -------------------------------------------------------------------
for size in 16 24 32 48 64 128 256 512; do
    icon_dir="${REPO_ROOT}/packaging/linux/icons/hicolor/${size}x${size}/apps"
    mkdir -p "${icon_dir}"
    magick "${WORK}/master.png" -filter Lanczos -resize "${size}x${size}" \
        -depth 8 -define png:color-type=6 "${icon_dir}/raw-radio-studio.png"
done

# The AppImage recipe uses a single 512x512 PNG at the top level.
cp "${REPO_ROOT}/packaging/linux/icons/hicolor/512x512/apps/raw-radio-studio.png" \
    "${REPO_ROOT}/packaging/linux/raw-radio-studio.png"

echo "wrote Linux hicolor set (16..512) + packaging/linux/raw-radio-studio.png"
