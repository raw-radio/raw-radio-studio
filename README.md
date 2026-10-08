# raw-radio-studio

A free, open-source (**AGPLv3**) **tracking-first DAW** for macOS and Linux —
built for a small studio that records friends' music.

`raw-radio-studio` pairs the **Tracktion Engine** with **JUCE** in **C++20**
(CMake ≥ 3.22). It is operated by a sound engineer during in-person sessions,
so its center of gravity is the tracking loop — **arm → monitor → record →
overdub → rough mix → export** — not beat-making. It is part of the
[RAW Radio](https://github.com/raw-radio/raw-radio) ecosystem but is a
**standalone, local-only** application: no accounts, no cloud, no integration
with raw-radio.ru (finished recordings are uploaded by hand, outside the app).

> **Status: pre-alpha.** Under active early development. Expect rough edges,
> shifting UI, and breaking changes until v1.

## Implemented so far

- **Epic 0 — Bootstrap:** licensed, CI-driven repository scaffold; top-level
  CMake + C++20; pinned engine wiring; macOS + Ubuntu CI producing unsigned
  `.dmg`, `.deb`, and `AppImage` artifacts.
- **Epic 1 — Walking skeleton (MVP):** the full capture → monitor → export loop
  on one stereo track, plus project persistence.

Roadmap details, acceptance criteria, and the frozen epic order (Epics 2–7) live
in the [specification](docs/STUDIO_SPEC.md#9-roadmap-epics-07).

## Features

### Current (Epic 1)

- **One stereo track** with arm / record / stop.
- **Crash-safe capture:** each take is written to disk as it records.
- **Software monitoring** of the armed track, with an input level meter.
- **Minimal transport.**
- **Audio device selection:** CoreAudio on macOS, ALSA `hw` directly on Linux
  (no silent fallback), plus buffer size and sample rate.
- **Export to 24-bit WAV** at the session sample rate.
- **Tracktion-native project save/open**, periodic non-destructive **autosave**,
  and **crash recovery** of autosaved sessions and recorded takes.
- **Minimal audio import** for backing tracks.
- **Explicit device errors** — a missing or busy device is surfaced clearly,
  never silently worked around. Headless `--version` for smoke checks.

### Planned (Epics 2–7 — see the [roadmap](docs/STUDIO_SPEC.md#9-roadmap-epics-07))

- Multitrack recording and device-agnostic I/O up to 16 tracks; software cue
  mixes (Epic 2).
- Mixer, routing (buses/groups/sends), and plugin hosting — **VST3** (default),
  **LV2** (opt-in), **AU** (macOS only); no VST2/CLAP (Epic 3).
- Editing: clips, comping, fades, time-stretch/pitch via free libraries (Epic 4).
- MIDI and virtual instruments (Epic 5).
- Auto-update (Sparkle / AppImageUpdate) and packaging polish (Epic 6).
- Hardening: recovery UX, performance validation, soak testing (Epic 7).

There are **no plugins and no mixing** today, and MIDI is not yet supported.

## Requirements

- **macOS 12+** (CoreAudio) or **Ubuntu 24.04+** / compatible Linux (ALSA `hw`).
- A **C++20** compiler (Apple Clang / Clang / GCC) and **CMake ≥ 3.22**.
- Engine dependencies (pinned git submodules):
  **JUCE 9.0.3** and **Tracktion Engine 3.5.0** — exact commits and the
  verified compatibility verdict are in [`DEPENDENCIES.md`](DEPENDENCIES.md).

## Build

Clone the pinned dependencies (non-recursive on purpose — see
[`DEPENDENCIES.md`](DEPENDENCIES.md)), then configure and build:

```bash
git submodule update --init
cmake -B build
cmake --build build
```

**Linux build dependencies** (Ubuntu 24.04), as used in CI:

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    build-essential libasound2-dev libfreetype6-dev libfontconfig1-dev \
    libgl1-mesa-dev libx11-dev libx11-xcb-dev libxext-dev libxinerama-dev \
    libxrandr-dev libxcursor-dev libxcomposite-dev libxi-dev libxfixes-dev \
    libwebkit2gtk-4.1-dev dpkg-dev fakeroot
```

Run the unit and headless integration tests:

```bash
ctest --test-dir build --output-on-failure
```

## Run

```bash
# macOS (app bundle)
build/raw_radio_studio_artefacts/Release/raw-radio-studio.app/Contents/MacOS/raw-radio-studio

# Linux (executable)
build/raw_radio_studio_artefacts/Release/raw-radio-studio
```

Headless smoke check (no window is created):

```bash
raw-radio-studio --version
# raw-radio-studio 0.1.1  (JUCE 9.0.3, Tracktion Engine 3.5.0)
```

Launched without arguments, the app opens the Epic 1 session window: select the
audio device, arm the stereo track, record, monitor, and export a 24-bit WAV.
Sessions are saved as `.tracktionedit`; autosaved versions (`.tmp_<name>`) and
recorded takes under `Recordings/` are offered for recovery if the previous run
ended unexpectedly.

## Documentation

- **[`docs/STUDIO_SPEC.md`](docs/STUDIO_SPEC.md)** — the canonical
  specification: vision, requirements, architecture, licensing, build/CI/release
  plan, and the Epic 0–7 roadmap.
- [`DEPENDENCIES.md`](DEPENDENCIES.md) — pinned submodules and the JUCE ↔
  Tracktion compatibility verdict.
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — how to contribute.
- [`docs/README.md`](docs/README.md) — documentation index.

## Contributing

Contributions use the **Developer Certificate of Origin (DCO)** — sign off your
commits with `git commit -s`. No CLA. See [`CONTRIBUTING.md`](CONTRIBUTING.md)
and the [`DCO`](DCO) text.

## License

Licensed under the **GNU Affero General Public License, version 3 (AGPLv3)** —
see [`LICENSE`](LICENSE).

This project combines Tracktion Engine (GPLv3-or-later, free tier) and JUCE
(AGPLv3, free tier); the combined work is distributed under **AGPLv3** via GPLv3
§13 / AGPLv3 §13. Third-party licensing composition and the pinned inventory are
documented in [`NOTICE`](NOTICE).

## Links

- **Repository:** <https://github.com/raw-radio/raw-radio-studio>
- **Specification:** [`docs/STUDIO_SPEC.md`](docs/STUDIO_SPEC.md)
- **Issues:** <https://github.com/raw-radio/raw-radio-studio/issues>
