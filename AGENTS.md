# AGENTS.md — raw-radio-studio

Navigation map for agents/contributors. Project: a free, open-source (AGPLv3) tracking-first
DAW on JUCE + Tracktion Engine (C++20). **[`README.md`](README.md) is the source of truth for state.**

## Repo map

- `docs/STUDIO_SPEC.md` — canonical spec + decisions (D1–D25); roadmap (Epics 0–7) is §9.
- `docs/EPIC2..EPIC5_GAPS.md` — **living per-epic status** (what's delivered / still open).
- `docs/USAGE.md` / `USAGE_RU.md` — UI guide in actual UI names.
- `DEPENDENCIES.md` — pinned commits (JUCE, Tracktion Engine, Signalsmith Stretch).
- `src/studio/` engine/session, I/O, routing, cue mixes, plugin host, MIDI, export; `src/ui/` GUI; `src/main.cpp` entry point + headless flags.
- `tests/` — doctest unit + headless integration; `third_party/` pinned submodules.
- `packaging/`, `.github/workflows/ci.yml`, `assets/`, `LICENSES/`, `NOTICE`, `DCO`.

## Iron rules

1. **Epic status ← `docs/EPIC*_GAPS.md`** — never infer/invent "done" states.
2. **Don't change engine pins** (JUCE/Tracktion/Signalsmith) without reason; exact commits in `DEPENDENCIES.md`.
3. **RT-safety:** no alloc/lock/I-O/logging on the audio callback — hand off to the message thread.

## Commands

```bash
git submodule update --init
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build -C Release --output-on-failure
build/raw_radio_studio_artefacts/Release/raw-radio-studio --version
```

## Gotchas

- Linux ALSA `hw` is **exclusive**: PipeWire/PulseAudio holding it → `EBUSY`; **no fallback**.
- **One audio device at a time** (single input + single output; one clock).
- Plugins host **in-process** (only scan is out-of-process); a crash can take down the host.
- **VST2 / CLAP unsupported**; LV2 opt-in (`-DRAW_RADIO_STUDIO_ENABLE_LV2=ON`).

## Contributing & licence — [`CONTRIBUTING.md`](CONTRIBUTING.md): **DCO sign-off** (`git commit -s`), no CLA. **AGPLv3**: [`LICENSE`](LICENSE), [`NOTICE`](NOTICE), [`LICENSES/`](LICENSES/).
