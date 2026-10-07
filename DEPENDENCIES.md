# Dependencies

`raw-radio-studio` pins all third-party build dependencies to **exact git
commits** as git submodules (STUDIO_SPEC §5.8 / §8.1). Nothing tracks a floating
branch in CI or release builds.

> **License summary:** JUCE is AGPLv3 (free tier) and Tracktion Engine is
> GPLv3-or-later (free tier). The combined work is distributed under AGPLv3 via
> GPLv3 §13 / AGPLv3 §13. See [`NOTICE`](NOTICE) for the full inventory and the
> licensing composition.

## Pinned git submodules

| Component | Submodule path | Upstream repository | Pinned ref | Pinned commit SHA |
|-----------|----------------|---------------------|------------|-------------------|
| **JUCE** | `third_party/JUCE` | <https://github.com/juce-framework/JUCE.git> | tag `9.0.3` | `be29c81492b6151c8ea8d14c840e1311963b3a83` |
| **Tracktion Engine** | `third_party/tracktion_engine` | <https://github.com/Tracktion/tracktion_engine.git> | `develop` (`VERSION.md` = **3.5.0**) | `2d2d452fd0336805cadb78268436e83b4a9f68e6` |

Initialise them with:

```bash
git submodule update --init
```

`shallow = true` is set for both in `.gitmodules`, so the checkout is shallow at
the pinned commit.

## How JUCE is wired into Tracktion Engine

Tracktion Engine's top-level `CMakeLists.txt` only adds its own bundled JUCE when
the `juce::juce_core` target does **not** already exist:

```cmake
if(NOT TARGET juce::juce_core)
    add_subdirectory(modules/juce)   # or CPM with JUCE_CPM_DEVELOP
endif()
```

Our top-level `CMakeLists.txt` therefore registers the pinned
`third_party/JUCE` **first**, then adds `third_party/tracktion_engine`. Tracktion
compiles against *our* pinned JUCE revision, and we fully control the JUCE pin.

### Tracktion's nested `modules/juce` submodule is intentionally NOT initialised

Tracktion Engine itself carries a nested `modules/juce` submodule. Our build does
**not** use it, for two reasons:

1. We need to pin an exact JUCE revision independently of Tracktion's pointer;
   the nested submodule's SHA is fixed by Tracktion's tree and cannot be changed
   without forking Tracktion.
2. Upstream's nested submodule URL is an SSH URL (`git@github.com:...`), which is
   not usable on CI runners without keys.

Because of (1) and (2), submodule initialisation is deliberately **non-recursive**
(`submodules: true`, not `recursive`). The nested submodule is never needed.

For reference, at the pinned Tracktion commit the nested pointer is JUCE
**8.0.13** (`37c894f83d379179b2070d437ccd0f1cd9af9576`) — i.e. upstream's own
tested combination. Our pin (JUCE 9.0.3) is ahead of that; see the OQ-3 verdict
below.

## OQ-3 — JUCE ↔ Tracktion Engine compatibility verdict

**Status: RESOLVED — JUCE 9.0.3 builds and links with Tracktion Engine 3.5.0.**

The exact pinned pair above was verified by a full `configure` + `build` on:

| Platform | Toolchain | Result |
|----------|-----------|--------|
| macOS (local) | AppleClang 21.0.0 (arm64), CMake 4.4.3 | ✅ configure + build + link + run |
| Ubuntu 24.04 (container) | GCC 13.3.0 (arm64), CMake 3.28.3 | ✅ configure + build + link + run |

Evidence:

- the Tracktion Engine translation units compile against JUCE 9.0.3
  (`juce_audio_processors_headless`, `juce_dsp`, `juce_gui_basics`, …),
- the public symbol `tracktion::engine::Engine::getVersion()` is present in the
  linked binary (the app references it, so the engine is genuinely linked),
- `raw_radio-studio --version` runs and prints
  `raw-radio-studio 0.0.0  (JUCE 9.0.3, Tracktion Engine 3.5.0)`.

**Caveat / risk:** Tracktion's own submodule pins JUCE **8.0.13**, so JUCE 9 is
ahead of the combination upstream tests. It builds cleanly here, but upstream may
not officially claim JUCE 9 support yet.

**Fallback:** if a future JUCE 9 / Tracktion regression appears, pin JUCE
**8.0.13** (`37c894f83d379179b2070d437ccd0f1cd9af9576`) — the revision Tracktion
ships — and record the deviation here.

**Upgrade path:** when upstream updates its `modules/juce` submodule to JUCE 9,
re-verify, update both pins deliberately, and re-run CI. Never auto-bump.

## Build

Prerequisites: CMake ≥ 3.22 and a C++20 compiler. See
[`README.md`](README.md) for platform packages.

```bash
git submodule update --init
cmake -B build
cmake --build build
```

Run (macOS app bundle / Linux executable):

```bash
# macOS
build/raw_radio_studio_artefacts/Release/raw-radio-studio.app/Contents/MacOS/raw-radio-studio
# Linux
build/raw_radio_studio_artefacts/Release/raw-radio-studio
```

Headless smoke check (no window created):

```bash
raw-radio-studio --version
```

## Tests

Epic 1 adds unit tests for pure/testable logic (device-error classification,
ALSA `hw` name policy, 24-bit WAV round-trip). The test framework is
**doctest 2.4.11**, vendored (pinned by content, no configure-time download) as
`third_party/doctest/doctest.h` — see [`NOTICE`](NOTICE).

```bash
ctest --test-dir build --output-on-failure
```

The test target (`raw_radio_studio_tests`) links only `juce_core` +
`juce_audio_formats`; hardware-dependent behaviour (recording, monitoring) is
verified manually.

## Vendored UI assets

The brand theme ships a small set of vendored, licence-reviewed UI assets that
are compiled into the application binary (no runtime or build-time download):

| Component | Location | Pinned revision | License (SPDX) | Role |
|-----------|----------|-----------------|----------------|------|
| **Lucide** | `assets/icons/*.svg` | tag `1.21.0` | ISC (MIT for Feather-derived icons) | UI icon set |

* Lucide (<https://github.com/lucide-icons/lucide>) is pinned to the tag
  matching the RAW Radio admin/app `lucide-react` family. The SVGs are normalised
  at vendor time (`currentColor` → `#FFFFFF`, since JUCE does not resolve SVG
  `currentColor`) and tinted at runtime by `src/ui/IconCache`.
* Full licence text: [`LICENSES/Lucide.txt`](LICENSES/Lucide.txt); provenance and
  refresh instructions: [`assets/icons/README.md`](assets/icons/README.md).

