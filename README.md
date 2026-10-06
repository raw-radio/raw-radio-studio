# raw-radio-studio

A **free, open-source (AGPLv3) digital audio workstation (DAW)** for a small
studio that records friends' music. Built on the **Tracktion Engine** + **JUCE**
stack in **C++20**, targeting **macOS 12+** and **Ubuntu 24.04+**, and operated
by a sound engineer during in-person sessions.

It is **tracking-first**: the engineer's workflow — arm → record → overdub →
rough mix → export — is the product. It is not a beat-making environment.

> **Status: pre-alpha — Epic 0 (bootstrap).**
> The repository now builds a real JUCE application linked against Tracktion
> Engine: it opens an empty window and prints a version banner. There is no audio
> graph, device I/O, or transport yet — those arrive in Epic 1 (the
> walking-skeleton MVP). The JUCE ↔ Tracktion compatibility question (OQ-3) is
> resolved: **JUCE 9.0.3 builds and links with Tracktion Engine 3.5.0** on macOS
> and Ubuntu (see [`DEPENDENCIES.md`](DEPENDENCIES.md)).

## Scope (target)

- Native, standalone desktop DAW. No cloud, no accounts, no server.
- Engine: **Tracktion Engine**; framework/UI: **JUCE**; language: **C++20**;
  build: **CMake ≥ 3.22**.
- Device-agnostic audio I/O: **CoreAudio** (macOS), **ALSA `hw` directly**
  (Linux, no fallback).
- Plugin hosting: **VST3** (default), **LV2** (opt-in), **AU** (macOS only).
  No CLAP, no VST2.
- Free/libre dependencies only — no commercial or GPLv2+-only components.

## Specification

The canonical specification is **`STUDIO_SPEC.md`** — product vision,
requirements, architecture, licensing, build/CI/release plan, and the Epic 0–7
roadmap. It currently lives in the `raw_radio` monorepo and will be vendored into
this repository's `docs/`. See [`docs/README.md`](docs/README.md) for the
pointer.

## Dependencies

JUCE and Tracktion Engine are pinned **git submodules** (exact commits under
`third_party/`). Exact SHAs, the JUCE↔Tracktion compatibility verdict, and the
pin/upgrade strategy are documented in [`DEPENDENCIES.md`](DEPENDENCIES.md).

## Build

Prerequisites:

- **CMake ≥ 3.22**
- A **C++20** compiler (Apple Clang / Clang / GCC)
- **Linux only:** JUCE build dependencies, e.g. (Ubuntu 24.04):

  ```bash
  sudo apt-get install -y \
      build-essential libasound2-dev libfreetype6-dev libfontconfig1-dev \
      libgl1-mesa-dev libx11-dev libx11-xcb-dev libxext-dev libxinerama-dev \
      libxrandr-dev libxcursor-dev libxcomposite-dev libxi-dev libxfixes-dev \
      libwebkit2gtk-4.1-dev dpkg-dev fakeroot
  ```

Clone the pinned dependencies, then build:

```bash
git submodule update --init
cmake -B build
cmake --build build
```

Run:

```bash
# macOS (app bundle)
build/raw_radio_studio_artefacts/Release/raw-radio-studio.app/Contents/MacOS/raw-radio-studio
# Linux (executable)
build/raw_radio_studio_artefacts/Release/raw-radio-studio
```

Headless version check (no window is created):

```bash
raw-radio-studio --version
# raw-radio-studio 0.0.0  (JUCE 9.0.3, Tracktion Engine 3.5.0)
```

When launched without arguments, the app opens an empty JUCE window.

> **Note on submodules:** initialisation is non-recursive on purpose. Tracktion
> Engine's nested `modules/juce` submodule is not used (we pin JUCE ourselves).
> See [`DEPENDENCIES.md`](DEPENDENCIES.md).

## Contributing

Contributions use the **Developer Certificate of Origin (DCO)**: sign off your
commits (`git commit -s`). No CLA. See [`CONTRIBUTING.md`](CONTRIBUTING.md) and
the [`DCO`](DCO) text.

## License

Licensed under the **GNU Affero General Public License, version 3 (AGPLv3)** —
see [`LICENSE`](LICENSE).

This project combines Tracktion Engine (GPLv3-or-later, free tier) and JUCE
(AGPLv3, free tier). The combined work is distributed under **AGPLv3** via GPLv3
§13 / AGPLv3 §13. Third-party licensing composition and the pinned inventory are
documented in [`NOTICE`](NOTICE).
