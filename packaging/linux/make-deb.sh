#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# make-deb.sh — build a Debian package for raw-radio-studio
#
# Epic 0. Produces a curated, UNSIGNED .deb for Ubuntu 24.04+.
#
# Why not `cpack -G DEB`?
#   JUCE and Tracktion Engine register their own CMake `install()` rules
#   (JUCE config files + the `juceaide` tool; Tracktion module headers). CPack's
#   monolithic install runs *all* of them, so a CPack-generated .deb is polluted
#   with ~20 MB of build tooling and headers. CPack has no reliable way to
#   exclude third-party install rules. We therefore stage exactly the runtime
#   files and build the archive with `dpkg-deb` — the same underlying tool CPack
#   uses — which is fully deterministic.
#
# Runtime dependencies are still derived automatically with `dpkg-shlibdeps`
# (run against the linked ELF binary), so we do not hard-code distro-specific
# package names (e.g. libasound2 vs libasound2t64). If dpkg-shlibdeps is
# unavailable/fails, we fall back to a static Ubuntu 24.04 dependency list.
#
# Usage:
#   packaging/linux/make-deb.sh <binary|build-root> <version> <output-dir>
#
# The first argument is the built executable. A CMake build root
# (e.g. `build`, as produced by `cmake -B build`) is also accepted and resolved
# to the standard artefact path (see packaging/linux/lib.sh).
#
# Output:
#   <output-dir>/raw-radio-studio-<version>-<arch>.deb   (arch: amd64 / arm64)
# -----------------------------------------------------------------------------
set -euo pipefail

BIN_ARG="${1:?usage: make-deb.sh <binary|build-root> <version> <output-dir>}"
VERSION="${2:?usage: make-deb.sh <binary|build-root> <version> <output-dir>}"
OUTDIR="${3:?usage: make-deb.sh <binary|build-root> <version> <output-dir>}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=packaging/linux/lib.sh
source "${SCRIPT_DIR}/lib.sh"

BIN="$(rrs_resolve_binary "${BIN_ARG}")" || exit 1
BIN="$(cd "$(dirname "${BIN}")" && pwd)/$(basename "${BIN}")"
mkdir -p "${OUTDIR}"
OUTDIR="$(cd "${OUTDIR}" && pwd)"

if [[ ! -x "${BIN}" ]]; then
    echo "error: executable not found: ${BIN}" >&2
    exit 1
fi

# Debian architecture name (x86_64 -> amd64, aarch64 -> arm64).
case "$(uname -m)" in
    x86_64)        DEB_ARCH="amd64" ;;
    aarch64|arm64) DEB_ARCH="arm64" ;;
    *)             DEB_ARCH="$(dpkg --print-architecture)" ;;
esac

WORK="$(mktemp -d)"
cleanup() { rm -rf "${WORK}"; }
trap cleanup EXIT

PKG="${WORK}/raw-radio-studio"
mkdir -p \
    "${PKG}/DEBIAN" \
    "${PKG}/usr/bin" \
    "${PKG}/usr/share/applications" \
    "${PKG}/usr/share/icons/hicolor/256x256/apps" \
    "${PKG}/usr/share/doc/raw-radio-studio"

install -m 0755 "${BIN}" "${PKG}/usr/bin/raw-radio-studio"
install -m 0644 "${SCRIPT_DIR}/raw-radio-studio.desktop" \
    "${PKG}/usr/share/applications/raw-radio-studio.desktop"
install -m 0644 "${SCRIPT_DIR}/raw-radio-studio.png" \
    "${PKG}/usr/share/icons/hicolor/256x256/apps/raw-radio-studio.png"

# Ship the license texts (AGPLv3 + NOTICE) in the package.
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
[[ -f "${REPO_ROOT}/LICENSE" ]] && cp "${REPO_ROOT}/LICENSE" \
    "${PKG}/usr/share/doc/raw-radio-studio/copyright"
[[ -f "${REPO_ROOT}/NOTICE" ]] && cp "${REPO_ROOT}/NOTICE" \
    "${PKG}/usr/share/doc/raw-radio-studio/NOTICE"

INSTALLED_KB="$(du -sk "${PKG}/usr" | awk '{print $1}')"

# --- Runtime dependencies (auto) --------------------------------------------
# A minimal debian/control is required for dpkg-shlibdeps to resolve the package
# that owns the executable.
mkdir -p "${PKG}/debian"
cat > "${PKG}/debian/control" <<EOF
Source: raw-radio-studio
Section: sound
Priority: optional
Maintainer: raw-radio <raw-radio@users.noreply.github.com>
Standards-Version: 4.6.2

Package: raw-radio-studio
Architecture: ${DEB_ARCH}
Depends: \${shlibs:Depends}
Description: Free, open-source (AGPLv3) tracking-first DAW
 Built on JUCE and Tracktion Engine.
EOF

FALLBACK_DEPS="libasound2t64, libatomic1, libfontconfig1, libfreetype6, libc6, libgcc-s1, libstdc++6"

DEPS=""
if command -v dpkg-shlibdeps >/dev/null 2>&1; then
    DEPS="$(cd "${PKG}" && dpkg-shlibdeps -O --ignore-missing-info \
            -e usr/bin/raw-radio-studio 2>/dev/null \
            | sed -n 's/^shlibs:Depends=//p' || true)"
fi
if [[ -z "${DEPS}" ]]; then
    echo "warning: dpkg-shlibdeps unavailable/failed; using static dependency list" >&2
    DEPS="${FALLBACK_DEPS}"
fi
rm -rf "${PKG}/debian"

cat > "${PKG}/DEBIAN/control" <<EOF
Package: raw-radio-studio
Version: ${VERSION}
Section: sound
Priority: optional
Architecture: ${DEB_ARCH}
Depends: ${DEPS}
Installed-Size: ${INSTALLED_KB}
Maintainer: raw-radio <raw-radio@users.noreply.github.com>
Homepage: https://github.com/raw-radio/raw-radio-studio
Description: Free, open-source (AGPLv3) tracking-first DAW
 raw-radio-studio is a tracking-first digital audio workstation built on
 JUCE and Tracktion Engine.
EOF

TARGET="${OUTDIR}/raw-radio-studio-${VERSION}-${DEB_ARCH}.deb"
rm -f "${TARGET}"

if command -v fakeroot >/dev/null 2>&1; then
    fakeroot dpkg-deb --build --root-owner-group "${PKG}" "${TARGET}"
else
    dpkg-deb --build --root-owner-group "${PKG}" "${TARGET}"
fi

echo "created: ${TARGET}"
dpkg-deb --info "${TARGET}"
ls -lh "${TARGET}"
