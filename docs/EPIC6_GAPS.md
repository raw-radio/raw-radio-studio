# Epic 6 — open gaps (explicit)

> Status: **Epic 6 is NOT delivered.** Auto-update (Sparkle 2 / AppImageUpdate)
> is unimplemented; only the *packaging-polish* half has been advanced (release
> automation, license-complete artifacts, install notes). This file records what
> is done and what is deliberately still open, so the epic is not silently
> marked complete. It is not a substitute for the epic acceptance criteria in
> [`STUDIO_SPEC.md`](STUDIO_SPEC.md) §9.

## Scope (from the spec)

[`STUDIO_SPEC.md`](STUDIO_SPEC.md) §8.4 + §9 «Epic 6» define three deliverables
and three acceptance criteria:

- Sparkle 2 (macOS): appcast, EdDSA signatures, delta updates.
- AppImageUpdate (zsync, `gh-releases-zsync`) for the Linux AppImage.
- Packaging polish: AppImage + deb, install instructions.

Acceptance criteria (all still **open** except the third, now met):

- [ ] macOS app checks for and installs an update from a hosted appcast.
- [ ] AppImage self-updates from GitHub Releases via AppImageUpdate.
- [x] Release artifacts attach cleanly to GitHub Releases with checksums.

## Delivered (partial — release automation & packaging polish)

Committed by the distribution/packaging task (files under `.github/workflows/`,
`packaging/`, `CHANGELOG.md`, `docs/EPIC6_GAPS.md`):

- **Tag-triggered release automation** (`.github/workflows/ci.yml`): pushing a
  `v*` tag runs a `release` job that downloads the build artifacts, writes a
  sha256 `checksums.txt`, and creates/updates the GitHub Release
  (`gh release create`; `--clobber` on re-run, so it is idempotent). Tags
  containing `-alpha`/`-beta`/`-rc` are published as **pre-releases**. The job
  uses only the workflow-scoped `GITHUB_TOKEN` (`permissions: contents: write`);
  no PAT/secret is hard-coded. This satisfies acceptance criterion 3 and removes
  the previous manual release step.
- **The version-on-tag branch is now reachable**: `Determine version` already
  read `GITHUB_REF_NAME#v`, but with no tag trigger that path never ran; it does
  now.
- **License-complete artifacts** (AGPLv3 compliance, spec §7.2/§8.6): `LICENSE`,
  `NOTICE` and the verbatim third-party texts under `LICENSES/` are now embedded
  in the `.dmg` (volume root, next to `INSTALL.txt`), the `.deb`
  (`/usr/share/doc/raw-radio-studio/`) and the AppImage
  (`usr/share/doc/raw-radio-studio/`). Before this, `NOTICE` referenced
  `LICENSES/*` files that were not shipped at all.
- **Linux install notes** (`packaging/linux/INSTALL.txt`, shipped inside the
  `.deb` and AppImage): `.deb` install/uninstall, AppImage launch, the FUSE 2
  caveat on Ubuntu 24.04 (`libfuse2t64`) and the no-FUSE bypass
  (`APPIMAGE_EXTRACT_AND_RUN=1`).
- **Honest macOS artifact name**: the `.dmg` is now
  `raw-radio-studio-<version>-macos-arm64.dmg` (was `-macos`), because CI builds
  only arm64 — the old name implied a universal build.
- **`CHANGELOG.md`** (Keep a Changelog) seeded from the real `v0.1.0-alpha` …
  `v0.1.2-alpha` GitHub Releases.

## Open — auto-update (the core of Epic 6)

### macOS — Sparkle 2

**What is missing.** No Sparkle 2 integration: no `SPUStandardUpdaterController`,
no EdDSA update-signing key, no appcast XML, no delta updates.

**Why.** Sparkle's security model assumes a **code-signed** app (EdDSA
signatures are verified against the bundle); signing/notarization is itself
deferred (see below). The EdDSA key *can* ship before notarization
([`STUDIO_SPEC.md`](STUDIO_SPEC.md) §8.4), but the integration + appcast host is
still unimplemented.

### Linux — AppImageUpdate (zsync)

**What is missing.** No `gh-releases-zsync` update information embedded in the
AppImage and no `.zsync` metadata asset published with the release. The
`make-appimage.sh` production path does not pass `--updateinformation`.

**Planned approach.** Embed `gh-releases-zsync|<owner>|<repo>|latest|*x86_64.AppImage.zsync`
at build time and attach the generated `.zsync` next to the AppImage in the
release (the `release` job's asset glob would need to include `*.zsync`).

## Open — packaging polish

- **Intel / universal macOS build.** CI builds macOS on `macos-latest` (arm64)
  only, so Intel Macs are unserved. Considered and **deferred**: GitHub-hosted
  x86_64 macOS runners (`macos-13`) are being retired, and a second macOS build
  duplicates the heavy JUCE/Tracktion compile. Options when revisited: a
  `macos-13` matrix leg, or producing a universal (arm64 + x86_64) binary.
- **AppImage runs without FUSE only via an env var.** A type-2 AppImage mounts
  itself with FUSE. On Ubuntu 24.04 (no `libfuse2t64` by default) it must be
  launched with `APPIMAGE_EXTRACT_AND_RUN=1` (documented). A nicer default
  (a shipped wrapper/`.desktop` that sets the variable) is a possible follow-up;
  the artifact itself cannot self-mount without FUSE.
- **apt repository / Flatpak** are **deferred** by decision **D22** (revisit on
  demand); **Windows** is out of scope for v1.

## External blocker — macOS notarization / code signing

**What is missing.** The `.dmg` is **unsigned and un-notarized**; Gatekeeper
blocks the first launch (the shipped `INSTALL.txt` documents the right-click →
Open / `xattr -dr com.apple.quarantine` workaround).

**Why it is an external blocker.** Distributing a signed/notarized macOS app
outside the App Store requires an **Apple Developer account ($99/yr)**, a
**Developer ID** certificate, **Hardened Runtime**, and a notarytool submission
([`STUDIO_SPEC.md`](STUDIO_SPEC.md) §8.5, open question **OQ-7**). None of that
can be done in-repo without the paid account/credentials. Note for later:
Hardened Runtime **inherits entitlements** for hosted plugins, which will need
attention when signing is enabled.

## Related

- [`STUDIO_SPEC.md`](STUDIO_SPEC.md) §8.3 (release artifacts), §8.4 (auto-update),
  §8.5 (signing/notarization), §8.6 (reproducibility & AGPL), §9 Epic 6.
- [`../.github/workflows/ci.yml`](../.github/workflows/ci.yml) — build + the
  `release` job.
- [`../CHANGELOG.md`](../CHANGELOG.md) — released changes, forward-looking.
- [`../packaging/linux/INSTALL.txt`](../packaging/linux/INSTALL.txt) — Linux
  install + FUSE guidance.
- [`../README.md`](../README.md) — build/run commands and the delivered-epic
  overview.
