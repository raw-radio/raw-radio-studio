# raw-radio-studio

A free, open-source (**AGPLv3**) **tracking-first DAW** for macOS and Linux —
built for a small studio that records friends' music.

`raw-radio-studio` pairs the **Tracktion Engine** with **JUCE** in **C++20**
(CMake ≥ 3.22). It is operated by a sound engineer during in-person sessions,
so its center of gravity is the tracking loop — **arm → monitor → record →
overdub → rough mix → export** — not beat-making. It is a **standalone,
local-only** application: no accounts, no cloud, no integration with any web
service (finished recordings are uploaded by hand, outside the app).

> **Status: pre-alpha (v0.1.3).** Epics 0–5 are **delivered**: capture/monitor/export,
> multitrack device-agnostic I/O with software cue mixes, a mixer with routing and
> **VST3 / LV2 (opt-in) / AU** plugin hosting, clip editing (fades, comping,
> time-stretch), and MIDI with a piano roll and virtual instruments. **Epics 6–7**
> (auto-update + packaging polish, and hardening/hardware acceptance) are **open**.
> The living per-epic status is in the `docs/EPIC*_GAPS.md` files (see the
> [docs index](docs/README.md)); the canonical roadmap is
> [`docs/STUDIO_SPEC.md §9`](docs/STUDIO_SPEC.md#9-roadmap-epics-07).

## Features

| Epic | Delivered | Status |
|------|-----------|--------|
| **0 — Bootstrap** | Licensed (AGPLv3) CI-driven repo; C++20 + pinned JUCE/Tracktion submodules; macOS + Ubuntu CI producing unsigned `.dmg` / `.deb` / `AppImage`. | ✅ |
| **1 — Walking skeleton** | Capture → software monitor → 24-bit WAV export on one stereo track; CoreAudio / ALSA `hw` device selection (buffer + sample rate); Tracktion-native save/open, autosave and crash recovery. | ✅ |
| **2 — Multitrack + device-agnostic I/O** | 1..N-channel input mapping and multitrack recording/overdub (up to 16 tracks); basic mixer (fader/pan/mute per track and master, solo per track) with peak/clip metering; count-in/metronome. Punch-in/out and loop-recording remain open. | ✅ (targets open) |
| **3 — Mixer + routing + plugin hosting** | Per-track output assignment, submix folder tracks and aux sends; **VST3** (default), **LV2** (opt-in), **AU** (macOS); out-of-process scan, in-process hosting; plugin state + named presets; software cue mixes; stems and region export. | ✅ (basic UI) |
| **4 — Editing** | Move/trim/split/duplicate/loop/delete/mute clips; per-clip fades and equal-power crossfades; non-destructive comping; undo/redo; time-stretch/pitch (Signalsmith Stretch, MIT). Snap/grid/markers/tempo and automation are deferred. | ✅ (targets open) |
| **5 — MIDI + virtual instruments** | MIDI input + routing, MIDI recording, piano-roll editing, and virtual-instrument hosting (instrument slot separate from the effect chain); MIDI and audio coexist and export together. | ✅ |
| **6 — Auto-update + packaging polish** | Sparkle (macOS) / AppImageUpdate (Linux); release automation. | ⏳ open |
| **7 — Hardening** | Crash-recovery UX, performance validation (16 tracks, ~32 plugins, < 10 ms), scanner-crash isolation, soak testing; on-hardware acceptance. | ⏳ open |

## Known limitations

- **One audio device at a time.** The engine opens a single input device and a
  single output device (no multi-interface aggregation); all I/O therefore shares
  one clock, with no cross-device drift compensation or resampling.
- **Metering is 2 channels** per track/master (L/R) — no per-channel meters beyond
  stereo.
- **No punch-in/out and no loop-recording** (FR-REC-7, a `[target]`).
- **Master and submix-folder (bus) outputs cannot be routed to a specific physical
  output pair** — only individual audio tracks and cue returns can be assigned to a
  hardware output pair.
- **Plugin hosting is in-process** (only *scanning* is out-of-process), so a
  crashing plugin can take down the host (decision D23).
- **CLAP is not supported** (NG4); **LV2 is opt-in** at build time
  (`-DRAW_RADIO_STUDIO_ENABLE_LV2=ON`); **VST2 is not shipped** (D24); **AU** is
  macOS-only.
- **Linux uses ALSA `hw` directly with no fallback** — if PipeWire/PulseAudio holds
  the device, the open fails with `EBUSY` and a clear error is surfaced (no silent
  fallback to another backend).
- **Real MIDI hardware and third-party VST3/LV2/AU instruments have not been
  exercised yet** — MIDI verification is synthetic (the built-in `4osc`/sampler
  stand-in instruments) and the piano roll is basic.

## Requirements

- **macOS 12+** (CoreAudio) or **Ubuntu 24.04+** / compatible Linux (ALSA `hw`).
- A **C++20** compiler (Apple Clang / Clang / GCC) and **CMake ≥ 3.22**.
- Engine dependencies (pinned git submodules):
  **JUCE 9.0.3**, **Tracktion Engine 3.5.0**, and **Signalsmith Stretch 1.1.0** —
  exact commits and the verified compatibility verdict are in
  [`DEPENDENCIES.md`](DEPENDENCIES.md).

## Build

Clone the pinned dependencies (non-recursive on purpose — see
[`DEPENDENCIES.md`](DEPENDENCIES.md)), then configure and build:

```bash
git submodule update --init
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

**Linux build dependencies** (Ubuntu 24.04), as used in CI
([`.github/workflows/ci.yml`](.github/workflows/ci.yml)):

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    build-essential libasound2-dev libfreetype6-dev libfontconfig1-dev \
    libgl1-mesa-dev libx11-dev libx11-xcb-dev libxext-dev libxinerama-dev \
    libxrandr-dev libxcursor-dev libxcomposite-dev libxi-dev libxfixes-dev \
    libwebkit2gtk-4.1-dev dpkg-dev fakeroot
```

## Run

```bash
# macOS (app bundle)
build/raw_radio_studio_artefacts/Release/raw-radio-studio.app/Contents/MacOS/raw-radio-studio

# Linux (executable)
build/raw_radio_studio_artefacts/Release/raw-radio-studio
```

Launched without arguments, the app opens the session window: select the audio
device, add/map tracks, arm, record, monitor, edit, mix and export. Sessions are
saved as `.tracktionedit`; autosaved versions (`.tmp_<name>`) and recorded takes
under `Recordings/` are offered for recovery if the previous run ended
unexpectedly.

### Headless / diagnostic flags

Handled before the single-instance gate, so they run without opening a window
(see [`src/main.cpp`](src/main.cpp)):

```bash
raw-radio-studio --version                          # banner: 0.1.3  (JUCE 9.0.3, Tracktion Engine 3.5.0)
raw-radio-studio --list-devices                     # audio backends + MIDI inputs/outputs
raw-radio-studio --selftest-record <s> <out.wav>    # capture the input to a 24-bit WAV [--device "<substr>"]
raw-radio-studio --selftest-multitrack <s> <out>    # per-input-channel WAVs + per-channel stats
raw-radio-studio --selftest-plugin-scan [--rescan]  # out-of-process scan, print hosted formats + count
raw-radio-studio --selftest-time-stretch <s>        # Signalsmith Stretch smoke path
raw-radio-studio --selftest-midi [<out.wav>]        # render a MIDI clip through the stand-in instrument
```

## Tests

Unit tests (`raw_radio_studio_tests`) and headless integration tests
(`raw_radio_studio_integration_tests`, linked against the real engine with no
audio device) run via CTest:

```bash
ctest --test-dir build -C Release --output-on-failure
```

Hardware-dependent behaviour (recording, monitoring, cue separation on a real
multi-out interface) is verified manually.

## Documentation

- **[`docs/STUDIO_SPEC.md`](docs/STUDIO_SPEC.md)** — the canonical specification:
  vision, requirements, architecture, licensing, build/CI/release plan, and the
  Epic 0–8 roadmap.
- **[`docs/USAGE.md`](docs/USAGE.md)** — task-oriented usage guide (plugins,
  presets, routing, cue mixes, editing, MIDI, stems/region export) using the
  actual UI names; **[`docs/USAGE_RU.md`](docs/USAGE_RU.md)** is the Russian
  translation (the English guide is authoritative if they diverge).
- **`docs/EPIC2_GAPS.md` … `docs/EPIC5_GAPS.md`** — the **living per-epic status**:
  what is delivered and exactly which items remain open.
- [`DEPENDENCIES.md`](DEPENDENCIES.md) — pinned submodules and the JUCE ↔
  Tracktion compatibility verdict.
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
documented in [`NOTICE`](NOTICE) and [`LICENSES/`](LICENSES/).

## Links

- **Repository:** <https://github.com/raw-radio/raw-radio-studio>
- **Specification:** [`docs/STUDIO_SPEC.md`](docs/STUDIO_SPEC.md)
- **Issues:** <https://github.com/raw-radio/raw-radio-studio/issues>
