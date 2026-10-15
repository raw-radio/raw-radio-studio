# Changelog

All notable changes to **raw-radio-studio** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Pre-`1.0` versions are published as GitHub **pre-releases** with an `-alpha`
suffix (`v0.1.x-alpha`). Entries below for `v0.1.0-alpha` … `v0.1.2-alpha` are
reconstructed from the corresponding GitHub Releases; the product roadmap and
the "what is delivered / still open" status live in
[`docs/STUDIO_SPEC.md`](docs/STUDIO_SPEC.md) and the `docs/EPIC*_GAPS.md` files.

## [0.1.3-alpha] - 2026-10-15

Fourth alpha — makes the first take crash-safe, seeds the `CHANGELOG.md`, and adds
an ASan test gate plus automated tag releases.

### Added

- **ASan gate** (`-DRAW_RADIO_STUDIO_ENABLE_ASAN=ON`): the app and both test
  binaries build with `-fsanitize=address` (the pinned JUCE/Tracktion submodules
  stay uninstrumented) and CI runs the whole suite through `ctest` with a single,
  narrow LSan suppression for an upstream one-shot fontconfig allocation; any
  real leak still fails the gate.
- **Release automation** in CI (`.github/workflows/ci.yml`): pushing a `v*` tag
  now builds, checksums (sha256 `checksums.txt`) and publishes the release
  artifacts to a GitHub Release (`-alpha`/`-beta`/`-rc` tags become pre-releases).
  Previously releases were created by hand.
- **AGPLv3 compliance set is embedded in every artifact** — `LICENSE`, `NOTICE`
  and the verbatim third-party texts under `LICENSES/` now ship in the `.dmg`
  (volume root), the `.deb` and the AppImage
  (`usr/share/doc/raw-radio-studio/`). `NOTICE` referenced `LICENSES/*` that
  were not actually shipped before.
- **Linux install notes** (`packaging/linux/INSTALL.txt`), covering the `.deb`
  and the AppImage, the FUSE 2 caveat on Ubuntu 24.04 (`libfuse2t64`) and the
  no-FUSE `APPIMAGE_EXTRACT_AND_RUN=1` bypass.

### Changed

- The macOS artifact filename now carries its architecture
  (`raw-radio-studio-<version>-macos-arm64.dmg`) instead of implying a
  universal build; only the built architecture is labelled.
- `CONTRIBUTING.md` build/test sections updated to match reality (submodules,
  build paths); the CI ASan job runs the whole suite via `ctest` with no extra
  env.

### Fixed

- **The first take is no longer lost on a crash.** A crash before Tracktion's
  first ~6 s flush left a 0-sample WAV header over real payload; the header is
  now repaired/finalised on write, shrinking the loss window from ~6 s to ~1 s.
- **Recovery keeps data**: 0-sample / broken WAVs are repaired before reading
  instead of being silently dropped, and every skip is recorded (name + reason).
- Recovered takes are routed back to their original track (by the
  `_<track>_Take_` name infix) instead of piling onto track 0 by mtime.
- A clean exit no longer leaves a parasitic "unsaved version found" recovery
  prompt.
- Quitting (Cmd+Q / window close) now runs the shared Save/Discard/Cancel guard
  instead of discarding unsaved work.

### Notes

- Remaining Epic 6 work (Sparkle 2 appcast for macOS, AppImageUpdate/zsync for
  Linux, macOS notarization) is tracked in
  [`docs/EPIC6_GAPS.md`](docs/EPIC6_GAPS.md).

## [0.1.2-alpha] - 2026-10-09

Third alpha — adds **Epic 3** (plugins, routing, cue mixes, stems) and **Epic 4**
(editing), plus a large batch of fixes and UI work.

### Added

- **Plugins**: VST3 (default) + LV2 (opt-in) + AU (macOS); out-of-process
  scanning, plugin browser, editor window, and persistence in the project. The
  insert list is effects-only (instruments/panners are rejected gracefully
  instead of silencing the session).
- **Plugin presets**: save/load a plugin's state to named user presets.
- **Routing**: aux send/return, submix folder tracks, per-track output
  assignment.
- **Software cue mixes**: independent per-performer monitor mixes.
- **Exports**: per-track + master stems (24-bit) and region export (Shift-drag
  on the ruler).
- **Editing**: move/trim/split/duplicate/loop/delete clips; fade-in/out and
  crossfades; time-stretch (MIT Signalsmith Stretch); undo/redo; clip snapping
  (Alt to bypass); resize/fade cursors; synced playhead.
- **UI**: RAW Radio theme, Phosphor icons, icon-only buttons, new vinyl app
  icon, uniform clip height, remembered last save/export directory.

### Fixed

- Export no longer interrupts playback/monitoring (playback context restored).
- Metronome/count-in are per-session and honoured at record start.
- Meter scale is −60…0 dBFS; faders reshaped; Stop keeps the playhead position.

## [0.1.1-alpha] - 2026-10-08

Second alpha — adds **Epic 3** (plugins, routing, cue mixes, stems) plus fixes
and UI work since `v0.1.0-alpha`.

### Added

- **Plugin hosting**: VST3 (default, LV2 opt-in, AU on macOS), out-of-process
  plugin scanning, plugin browser (scan/search), insert on a track, plugin
  editor window, and persistence in the project.
- **Plugin presets**: save/load a plugin's state to named user presets.
- **Routing**: aux send/return busses, submix folder tracks, per-track output
  assignment.
- **Software cue mixes** (independent per-performer monitor mixes) — closes the
  Epic 2 hard gap.
- **Stems export**: per-track stems + master (24-bit).
- **Region export**: Shift-drag on the timeline to select a range and export
  just that region.
- **Transport**: Stop keeps position; dedicated "go to start" button.
- **Recording**: per-track input TRIM + take normalize; the record track's
  previous take is muted during a new take.
- **UI**: RAW Radio theme (brand accent), Phosphor icons, icon-only buttons with
  tooltips, new app icon (vinyl record).

### Changed

- **Mixer**: per-track/master fader/pan/mute; faders reshaped; export now
  matches playback (master chain included).
- **Meters**: smoothed ballistics (no flicker), −60…0 dBFS scale.

### Fixed

- Count-in / metronome are per-session and honoured at record start.

## [0.1.0-alpha] - 2026-10-07

First alpha — **Epics 0–2** delivered. Open-source (AGPLv3), tracking-first DAW
for macOS 12+ and Ubuntu 24.04+, built on Tracktion Engine + JUCE (C++20).

### Added

- **Recording**: record 1..N tracks with device-agnostic per-track input mapping
  (mono/stereo/N, any channel count); multitrack recording + overdub; per-track
  record-arm.
- **Mixer**: per-track fader/pan/mute/solo plus master fader/pan/mute; per-track
  and master metering (peak/clip).
- **Count-in / metronome** (kept out of exports).
- **Import** backing tracks (WAV/AIFF/FLAC/MP3/OGG) — playback only.
- **Monitoring** (mono inputs centered) and 24-bit WAV export at the session
  rate.
- **Projects**: native save/open + autosave + crash recovery; Close Project with
  an unsaved-changes guard.
- **Devices**: device selection with independent Input/Output (works with
  input-only mics).
- **Build/CI** (Epic 0): C++20 + pinned JUCE/Tracktion submodules; macOS +
  Ubuntu CI producing unsigned `.dmg` / `.deb` / `AppImage`.

[Unreleased]: https://github.com/raw-radio/raw-radio-studio/compare/v0.1.2-alpha...HEAD
[0.1.2-alpha]: https://github.com/raw-radio/raw-radio-studio/releases/tag/v0.1.2-alpha
[0.1.1-alpha]: https://github.com/raw-radio/raw-radio-studio/releases/tag/v0.1.1-alpha
[0.1.0-alpha]: https://github.com/raw-radio/raw-radio-studio/releases/tag/v0.1.0-alpha
