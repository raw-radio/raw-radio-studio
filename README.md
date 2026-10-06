# raw-radio-studio

A **free, open-source (AGPLv3) digital audio workstation (DAW)** for a small
studio that records friends' music. Built on the **Tracktion Engine** + **JUCE**
stack in **C++20**, targeting **macOS 12+** and **Ubuntu 24.04+**, and operated
by a sound engineer during in-person sessions.

It is **tracking-first**: the engineer's workflow — arm → record → overdub →
rough mix → export — is the product. It is not a beat-making environment.

> **Status: pre-alpha — Epic 0 (bootstrap).**
> This repository is a buildable, licensed, CI-driven skeleton. The audio engine
> (JUCE / Tracktion Engine) is **not wired yet**; that integration is deferred to
> the C++ specialist. The current `raw_radio_studio` target is a placeholder that
> prints a version banner. Epic 1 (the walking-skeleton MVP) is pending the
> engine integration.

## Scope (target)

- Native, standalone desktop DAW. No cloud, no accounts, no server.
- Engine: **Tracktion Engine**; framework/UI: **JUCE**; language: **C++20**;
  build: **CMake ≥ 3.22**.
- Device-agnostic audio I/O: **CoreAudio** (macOS), **ALSA `hw` directly**
  (Linux, no fallback).
- Plugin hosting: **VST3** (default), **LV2** (opt-in), **AU** (macOS only).
  No CLAP.
- Free/libre dependencies only — no commercial or GPLv2+-only components.

## Specification

The canonical specification is **`STUDIO_SPEC.md`** — product vision,
requirements, architecture, licensing, build/CI/release plan, and the Epic 0–7
roadmap. It currently lives in the `raw_radio` monorepo and will be vendored into
this repository's `docs/`. See [`docs/README.md`](docs/README.md) for the
pointer.

## Build

Prerequisites:

- **CMake ≥ 3.22**
- A **C++20** compiler (Apple Clang / Clang / GCC)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/raw_radio_studio
```

Expected output:

```
raw-radio-studio <version>
AGPLv3 — Epic 0 bootstrap skeleton (engine not wired yet)
https://github.com/raw-radio/raw-radio-studio
```

> The build currently produces the Epic 0 placeholder only. JUCE + Tracktion
> Engine integration (pinned commit SHAs) is a TODO for the C++ specialist; see
> the comments in [`CMakeLists.txt`](CMakeLists.txt).

## Contributing

Contributions use the **Developer Certificate of Origin (DCO)**: sign off your
commits (`git commit -s`). No CLA. See [`CONTRIBUTING.md`](CONTRIBUTING.md) and
the [`DCO`](DCO) text.

## License

Licensed under the **GNU Affero General Public License, version 3 (AGPLv3)** —
see [`LICENSE`](LICENSE).

This project combines Tracktion Engine (GPLv3-or-later, free tier) and JUCE
(AGPLv3, free tier). The combined work is distributed under **AGPLv3** via GPLv3
§13 / AGPLv3 §13. Third-party licensing composition and the (placeholder)
inventory are documented in [`NOTICE`](NOTICE).
