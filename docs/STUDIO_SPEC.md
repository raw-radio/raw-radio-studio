# raw-radio-studio — Technical & Product Specification

> **Status:** Living document — versioned in git, changes via Pull Request only. This is the frozen
> baseline of settled product decisions; the ADRs in [§12](#12-decisions-baseline-adr-table) are the
> authoritative record of *what* was decided and *why*. Anything not marked as a hard requirement is a
> target and may move.
>
> **Document version:** 1.5.0
> **Last updated:** 2026 (Epic 3 delivered: plugin hosting, routing, software cue mixes, stems, region export, presets; Epic 2 cue-mix gap closed)
> **Owner:** raw-radio project owner
> **Repository:** https://github.com/raw-radio/raw-radio-studio
> **Local project dir (to be bootstrapped in Epic 0):** `/Users/mac/projects/raw_radio/raw-radio-studio`
> **License:** AGPLv3 (top-level) — see [§7](#7-licensing--legal)

---

## Table of Contents

1. [Overview / Vision](#1-overview--vision)
2. [Target Users & Scenarios](#2-target-users--scenarios)
3. [Functional Requirements](#3-functional-requirements)
4. [Non-Functional Requirements](#4-non-functional-requirements)
5. [Architecture](#5-architecture)
6. [UI / UX](#6-ui--ux)
7. [Licensing & Legal](#7-licensing--legal)
8. [Build / CI / Release](#8-build--ci--release)
9. [Roadmap (Epics 0–7)](#9-roadmap-epics-07)
10. [Risks & Open Questions](#10-risks--open-questions)
11. [Glossary](#11-glossary)
12. [Decisions Baseline (ADR-style table)](#12-decisions-baseline-adr-table)
13. [Changelog](#13-changelog)

---

## 1. Overview / Vision

### 1.1 One-line summary

`raw-radio-studio` is a **free, open-source (AGPLv3) digital audio workstation (DAW)** for a small
recording studio that records friends' music. It is built on the **Tracktion Engine** + **JUCE**
stack in **C++20**, targets **macOS 12+** and **Ubuntu 24.04+**, and is operated by a
**sound engineer** during in-person sessions.

### 1.2 The problem

A small, non-commercial studio records music by friends and acquaintances across all genres
(universal, not genre-specific). Today the studio has no purpose-built recording application that:

- runs natively on both the engineer's macOS machine and a Linux workstation,
- works with **whatever class-compliant audio interface is on hand** (from a stereo USB
  mixer-quality device up to a multichannel interface),
- stays fully open-source and auditable, with no per-seat or commercial license encumbrance,
- is optimized for the **tracking** workflow (arm → record → overdub → rough mix → export) rather
  than for electronic-music production.

The engineer needs a dependable capture tool, not a beat-making environment.

### 1.3 Goals

- **G1 — Reliable capture.** Record audio takes without dropouts on the blessed hardware, with
  crash-safe results (every recorded take is persisted and recoverable).
- **G2 — Universal I/O.** Work with any channel count from 1..N channels, device-agnostic, on both
  macOS (CoreAudio) and Linux.
- **G3 — Tracking-first UX.** A clear, engineer-oriented workflow: arm tracks, set monitoring/cue
  mixes, hit record, comp, export. No production-first friction.
- **G4 — Open and honest.** Entirely AGPLv3, public Corresponding Source per released tag, DCO
  contributions, no closed-source builds of this project.
- **G5 — Free dependencies only.** Engine, UI toolkit, and time-stretch/pitch components must be
  free/libre libraries compatible with AGPLv3 (see [§7](#7-licensing--legal)).
- **G6 — Predictable performance.** Up to **16 tracks**, ~**32 plugins**, monitoring latency
  **< 10 ms** (targets; see [§4.1](#41-performance)).
- **G7 — Standard, portable project format.** Use the Tracktion native project format plus autosave
  and crash recovery.
- **G8 — Reproducible builds.** Pin exact dependency commits (JUCE, Tracktion Engine) and build the
  same way locally and in CI.

### 1.4 Non-goals (explicit)

- **NG1 — No integration with `raw-radio.ru`.** This is a **standalone product**. There is no API,
  sync, upload, or account integration with raw-radio.ru. The engineer uploads finished recordings
  to raw-radio.ru **manually** and out-of-band; that is not a feature of this application.
- **NG2 — No remote / networked sessions in v1.** Sessions are **local only** — the artist is
  physically present. No remote collaboration, no cloud project storage, no live networked sessions.
- **NG3 — No closed-source builds.** This project is not offered in a proprietary/closed build.
  Downstream users may not use the free-tier Tracktion Engine or JUCE in closed-source paid builds;
  see [§7.5](#75-commercial--closed-source-build-constraints).
- **NG4 — No CLAP plugin support.** CLAP is explicitly out of scope. (VST3, LV2 opt-in, and AU on
  macOS are in scope; VST2 is not shipped — see decision D24.)
- **NG5 — No commercial time-stretch/pitch libraries.** No Elastique (commercial), no RubberBand
  (GPLv2+ — avoided for AGPL compatibility). Free libraries only (Signalsmith Stretch / SoundTouch).
- **NG6 — No MIDI / virtual instruments in the v1 audio core.** MIDI and virtual instruments are
  deferred to a separate epic *after* the audio path is solid (Epic 5).
- **NG7 — No music-production feature creep.** Beat sequencing/pattern workflows are not a
  first-class goal; tracking is.
- **NG8 — No mobile (Android/iOS) target.** Desktop only (macOS + Linux).

---

## 2. Target Users & Scenarios

### 2.1 Primary user — the sound engineer

- **Who:** The operator of the small studio. Technically competent, comfortable with audio
  interfaces, gain staging, cue mixes, and DAW conventions (Reaper / Ardour / Waveform).
- **Needs:** Fast session setup, dependable recording, clear metering, flexible input selection,
  easy comping, and dead-simple export.
- **Environment:** macOS 12+ primary; Ubuntu 24.04+ secondary. Varies by day.

### 2.2 Secondary user — the artist / musician

- **Who:** A friend recording their music in the studio, across any genre.
- **Needs:** To hear themselves while performing (monitoring/cue mix), to record multiple takes,
  and to leave with recorded material.
- **Note:** The artist does not operate the software; the engineer does. The artist's experience is
  mediated through monitoring quality and session flow.

### 2.3 Representative scenarios

- **S1 — Single stereo take (MVP / Epic 1).** Engineer connects the **Fifine Ampli1** USB audio
  interface — the studio's **Epic 1 test device** ([§2.5](#25-hardware-notes)): a condenser mic on its
  XLR input (with 48 V phantom) while the backing "minus" plays from the DAW out to main/headphones —
  arms one stereo track, sets monitoring, records a voice take, and exports a WAV. This is the walking
  skeleton. The **Maonocaster E2 Gen2** remains an alternative stereo-mixdown device; a true
  multichannel interface (≥4-in, 24-bit) is still to be purchased (decision D21).
- **S2 — Multitrack tracking (Epic 2).** With a 4-in or 8-in class-compliant interface (e.g.
  Behringer UMC404HD, Focusrite Scarlett 4i4 4th Gen, MOTU M4, NI Komplete Audio 6 MK2, Audient
  EVO 8), the engineer maps separate inputs to separate tracks and records a live ensemble.
- **S3 — Overdub with click and cue mix.** Engineer plays back existing tracks and records an
  overdub; the artist hears a software cue mix (since vendor mixers are unavailable on Linux).
- **S4 — Mix, process, and export (Epic 3).** Engineer applies plugins (VST3/LV2/AU), mixes, and
  exports WAV / stems.
- **S5 — Comp and edit (Epic 4).** Engineer comps multiple takes, trims clips, applies fades, and
  uses time-stretch/pitch on free libraries.
- **S6 — MIDI & virtual instruments (Epic 5, later).** Engineer records MIDI and runs virtual
  instruments.

### 2.4 Studio workflow (end-to-end)

```
Setup        →  Connect interface → select device/channels → set buffer/latency
Pre-roll     →  Create tracks → assign inputs → set gains → arm tracks
Monitor      →  Configure monitoring + cue mix (per-performer) → check < 10 ms
Record       →  Roll → capture takes (crash-safe) → multiple passes
Review       →  Comp/edit/clip → fades → time-stretch if needed
Process      →  Insert plugins (VST3/LV2/AU) → mix → automation
Deliver      →  Export WAV/stems → engineer uploads manually to raw-radio.ru
```

> The final "upload" arrow is **manual and out of scope** for this software (NG1).

### 2.5 Hardware notes

The studio owns two class-compliant USB audio devices:

- **Fifine Ampli1 (= Fifine SC1) — Epic 1 test device.** A **2-in/2-out** USB audio interface:
  **USB Audio Class 1**, **class-compliant (driverless on macOS + Linux)**, **16-bit / 48 kHz**.
  Inputs: one **XLR/TRS combo** (mic/line, **48 V phantom**) + one **1/4" instrument** input; a
  **Mic/Inst** toggle splits them into **2 discrete USB channels** (mic mode: mono→stereo; inst mode:
  ch1 = mic, ch2 = instrument). Outputs: main out + headphone, with **hardware direct monitor**. It
  has **no loopback and no ASIO**. It is **not** a true multichannel (≥4-in, 24-bit) interface.
- **Maonocaster E2 Gen2.** A stereo-mixdown device (2-in/2-out, 48 kHz/16-bit, class-compliant);
  stays a stereo-mixdown option — **not** the Epic 1 test device.

**Caveat — 16-bit/48 kHz source.** The Ampli1 captures at **16-bit / 48 kHz**, so it validates the
capture→monitor→export **pipeline** but **not 24-bit capture**. WAV **export** remains **24-bit** at
the session sample rate (FR-EXP-1): the engine converts the 16-bit source into its float32 internal
format and renders 24-bit files. The Ampli1 also **cannot provide ≥4 simultaneous tracks** — the
Epic 2 multitrack acceptance still requires a true multichannel interface (D21).

**Epic 1 test workflow (Ampli1).** Play the backing track ("minus") from the DAW out to the interface's
**main out / headphones**, and record a **voice take** from a **condenser mic on the XLR input** with
**48 V phantom** engaged. This exercises the full walking-skeleton loop (arm → monitor → record →
export) on real hardware without a true multichannel interface.

---

## 3. Functional Requirements

Requirements are tagged:
**[v1]** = in the audio-core MVP path (Epics 1–4, audio-focused), **[later]** = Epic 5+ or post-v1,
**[hard]** = hard requirement, **[target]** = goal/aspirational with acceptable slack.

### 3.1 Recording

| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-REC-1 | Record audio to disk, one or more tracks, in real time. | [hard] | v1 (Epic 1: 1 stereo track) |
| FR-REC-2 | Arm/disarm recording per track; global transport record. | [hard] | v1 |
| FR-REC-3 | Input assignment per track: select device, channel(s), mono/stereo. | [hard] | v1 (stereo), Epic 2 (N ch) |
| FR-REC-4 | Per-track input gain / trim and record-enable metering. | [hard] | v1 |
| FR-REC-5 | Take management: multiple takes per track; non-destructive. | [hard] | v1 |
| FR-REC-6 | Overdub playback + record against existing material. | [hard] | v1 |
| FR-REC-7 | Punch-in/out and loop-recording of takes. | [target] | Epic 2/4 |
| FR-REC-8 | Crash-safe capture: each recorded take survives an app crash and is recoverable. | [hard] | v1 (Epic 1 basics, Epic 7 hardening) |
| FR-REC-9 | Sample rate + bit depth selection for the session (24-bit; 44.1/48 default, up to 96 kHz). | [hard] | v1 |
| FR-REC-10 | Count-in / metronome for overdubs. | [target] | Epic 2 |

### 3.2 Monitoring

| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-MON-1 | Direct/software monitoring of armed tracks while recording. | [hard] | v1 |
| FR-MON-2 | Monitoring latency < 10 ms (see [§4.1](#41-performance)). | [target] | v1 / Epic 2 |
| FR-MON-3 | Per-performer **cue mixes**, implemented **in software** (vendor mixers unavailable on Linux). | [hard] | Epic 2 → **✅ Epic 3** |
| FR-MON-4 | Monitor mute/solo, monitor level, and per-channel routing. | [hard] | v1 (basic), Epic 2 → **✅ Epic 3** |
| FR-MON-5 | Clear, actionable error when the audio device is unavailable/busy (ALSA `EBUSY`; see [§5.3](#53-audio-io-abstraction), [§10](#10-risks--open-questions)). | [hard] | v1 |

> **FR-MON-3 / FR-MON-4 — delivered in Epic 3.** Per-performer **software cue mixes** are
> implemented as an aux-send → aux-return bus → **independent hardware output pair** path (each
> source track carries a post-fader aux send with independent enable + level; the control-room/main
> mix stays independent). Monitor level and per-channel routing are provided. This **closes the Epic 2
> hard gap** recorded in [`docs/EPIC2_GAPS.md`](EPIC2_GAPS.md). The cue-mix UI is **basic** (a single
> send control per cue, no send matrix) and physical multi-output separation still needs on-hardware
> verification — both tracked in [`docs/EPIC3_GAPS.md`](EPIC3_GAPS.md).

### 3.3 Mixing, Routing & Plugins

| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-MIX-1 | Multitrack mixer: fader, pan, mute per track **and master**; solo per track (master solo **N/A**). | [hard] | Epic 2/3 |
| FR-MIX-2 | Flexible routing: track → bus/group → master; sends (at least one aux/cue send). | [hard] | **✅ Epic 3** |
| FR-MIX-3 | Metring per track and master (peak/RMS), clip indication. | [hard] | Epic 2/3 |
| FR-MIX-4 | Plugin hosting: **VST3** (default), **LV2 opt-in**, **AU on macOS only**. | [hard] | **✅ Epic 3** |
| FR-MIX-5 | VST2 support. | **EXCLUDED** (decision D24 — no VST2 hosting in releases) | — |
| FR-MIX-6 | Plugin state save/restore with the project; plugin preset management. | [hard] | **✅ Epic 3** |
| FR-MIX-7 | **Out-of-process plugin scanning** (crash isolation during scan). | [hard] | **✅ Epic 3**, hardened Epic 7 |
| FR-MIX-8 | CLAP support. | **EXCLUDED** (NG4) | — |
| FR-MIX-9 | Automation of mixer/plugin parameters. | [target] | Epic 4 |

> **FR-MIX-1 note — master solo is intentionally N/A.** Solo isolates a channel against its sibling
> tracks; the **master bus has no sibling to isolate against**, so a master solo control would be a
> no-op. Master **fader, pan, and mute** are provided, and **solo is per track**. This is the
> delivered behaviour, recorded as a deliberate qualification of FR-MIX-1 (see
> [`docs/EPIC2_GAPS.md`](EPIC2_GAPS.md)).

> **FR-MIX-2 / FR-MIX-4 / FR-MIX-6 / FR-MIX-7 — delivered in Epic 3.** Flexible routing is
> implemented (per-track output assignment, submix folder tracks, and aux sends — FR-MIX-2). Plugin
> hosting covers **VST3 (default), LV2 (opt-in), and AU (macOS only)** with no VST2 (FR-MIX-4);
> plugin state round-trips with the project and **named user presets** are supported (FR-MIX-6);
> plugin **scanning is out-of-process** (FR-MIX-7). The task shorthand **“FR-PLG-1/2/3”** corresponds
> to these FR-MIX IDs. Preset management is **basic** and crash-during-scan has not been exercised
> with a real crashing plugin — both tracked in [`docs/EPIC3_GAPS.md`](EPIC3_GAPS.md).

### 3.4 Editing

| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-ED-1 | Arrangement view: tracks as lanes, clips on a timeline. | [hard] | Epic 1 (minimal) → Epic 4 |
| FR-ED-2 | Clip operations: move, trim, split, duplicate, delete, loop. | [hard] | Epic 4 |
| FR-ED-3 | Fades: fade-in/out, crossfades. | [hard] | Epic 4 |
| FR-ED-4 | Comping: assemble a master take from multiple takes. | [hard] | Epic 4 |
| FR-ED-5 | Time-stretch and pitch-shift using **free libraries only** (Signalsmith Stretch / SoundTouch). | [hard] | Epic 4 |
| FR-ED-6 | Undo/redo across edit operations. | [hard] | Epic 4 (basic earlier) |
| FR-ED-7 | Snap/grid, markers, tempo map. | [target] | Epic 4 |

### 3.5 MIDI & Virtual Instruments (later — Epic 5)

| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-MIDI-1 | MIDI input capture (hardware controllers). | [target] | Epic 5 |
| FR-MIDI-2 | MIDI note editing (piano roll). | [target] | Epic 5 |
| FR-MIDI-3 | Virtual instrument hosting (VST3/LV2/AU instruments). | [target] | Epic 5 |
| FR-MIDI-4 | MIDI routing (in → track → instrument → mixer). | [target] | Epic 5 |

> MIDI/virtual instruments are **explicitly separate** from the v1 audio core (NG6) and must not be
> pulled into Epics 1–4.

### 3.6 Export

| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-EXP-1 | Export session to **WAV** (24-bit; session sample rate). | [hard] | v1 (Epic 1) |
| FR-EXP-2 | Export range: full session and/or selected region. | [hard] | Epic 2/3 → **✅ Epic 3** |
| FR-EXP-3 | Export stems (per-track/bus WAV). | [target] | Epic 3/4 → **✅ Epic 3** |
| FR-EXP-4 | Offline (faster-than-realtime) render where feasible. | [target] | Epic 3 |
| FR-EXP-5 | Any upload/publish to raw-radio.ru. | **EXCLUDED** (NG1) | — |

> **FR-EXP-2 / FR-EXP-3 — delivered in Epic 3.** **Region export** renders a selected timeline range
> (the full-session default is unchanged); **stems export** renders one 24-bit WAV per clip-bearing
> track plus a master mix, offline via `RenderSpecification` + `RenderQueue`. Both are recorded in the
> Epic 3 commit history. Region selection is **transient UI state** (not persisted with the project) —
> tracked in [`docs/EPIC3_GAPS.md`](EPIC3_GAPS.md).

### 3.7 Project / Session

| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-PRJ-1 | Use the **Tracktion native project format**. | [hard] | Epic 1 |
| FR-PRJ-2 | **Autosave** (periodic, non-destructive) with configurable interval. | [hard] | Epic 1 (basic), Epic 7 |
| FR-PRJ-3 | **Crash recovery**: on restart, detect an unclean shutdown and offer recovery. | [hard] | Epic 7 |
| FR-PRJ-4 | Save-as / project templates. | [target] | Epic 3 |
| FR-PRJ-5 | No cloud/remote project storage. | **EXCLUDED** (NG2) | — |

### 3.8 What v1 is — and is not

**MVP / Epic 1 (walking skeleton) — the narrowest end-to-end slice:**
record **1 stereo track** (from the **Fifine Ampli1** test device; [§2.5](#25-hardware-notes)) +
**monitoring** + **export WAV**. **No plugins. No mixing beyond the bare minimum.** The goal is a
working capture→monitor→export loop.

**v1 audio core (through Epic 4):** multitrack + device-agnostic I/O, mixer + routing + plugin
hosting, and editing/comping/fades/time-stretch.

**Later (Epic 5+):** MIDI + virtual instruments, auto-update/packaging polish, hardening beyond the
MVP bar.

---

## 4. Non-Functional Requirements

### 4.1 Performance

| ID | Requirement | Type |
|----|-------------|------|
| NFR-P-1 | Support up to **16 tracks** in a session. | [target] (hard top-end for v1 design) |
| NFR-P-2 | Run up to **~32 plugins** simultaneously within the performance target hardware. | [target] |
| NFR-P-3 | Monitoring (round-trip) latency **< 10 ms** on blessed hardware at 48 kHz with a small, stable buffer. | [target] |
| NFR-P-4 | No audio dropouts (xruns) during recording under the specified load on blessed hardware. | [hard] |
| NFR-P-5 | Deterministic real-time audio thread — no allocations/locks on the audio callback path. | [hard] (engineering rule) |
| NFR-P-6 | UI remains responsive during recording; UI work never blocks the audio thread. | [hard] |

> **Target vs hard:** NFR-P-1/P-2/P-3 are *targets* over agreed thresholds. NFR-P-4/P-5/P-6 are
> engineering hard requirements and are testable. The reference machines are **resolved** (decision
> D21): an Apple Silicon Mac with 16 GB RAM and an Ubuntu x86_64 machine with 16 GB RAM, using a 4-in
> class-compliant USB interface ([§10.9](#109-decisions--open-questions)).

### 4.2 Audio format

| ID | Requirement |
|----|-------------|
| NFR-A-1 | Record/export at **24-bit**. |
| NFR-A-2 | Sample rate: **44.1 kHz or 48 kHz default; up to 96 kHz** supported. |
| NFR-A-3 | Internal processing in **float32**, regardless of file bit depth. |
| NFR-A-4 | Default session: **48 kHz / 24-bit** (matches Maonocaster and typical interfaces). |

### 4.3 Platform support

| ID | Requirement |
|----|-------------|
| NFR-OS-1 | **macOS 12+** (Monterey and newer). Audio backend: **CoreAudio**. |
| NFR-OS-2 | **Ubuntu 24.04+** (and compatible). Audio backend: **ALSA `hw` directly** (see [§5.3](#53-audio-io-abstraction)). |
| NFR-OS-3 | Windows: **not a v1 target**; possible support **after v1** (low-priority future consideration, not permanently excluded; see [§10.9](#109-decisions--open-questions)). |

### 4.4 Reliability

| ID | Requirement |
|----|-------------|
| NFR-REL-1 | Crash-safe capture: recorded audio already written to disk remains usable after a crash. |
| NFR-REL-2 | Crash recovery on restart (see FR-PRJ-3). |
| NFR-REL-3 | Plugin crashes during **scan** must not crash the host (out-of-process scan). |
| NFR-REL-4 | Plugin crashes at **runtime** should not take down the whole app if avoidable; hosting runs **in-process** (no sandboxing), so this is best-effort (see [§10.6](#106-risk--plugin-scan--runtime-crashes), decision D23). |

### 4.5 Device-agnostic I/O

| ID | Requirement |
|----|-------------|
| NFR-IO-1 | Operate with **1..N input channels** and any supported output count; never assume a fixed channel count. |
| NFR-IO-2 | Enumerate devices and channels; let the engineer map any input to any track. |
| NFR-IO-3 | Handle hot-plug / device disappearance gracefully; surface clear errors (see FR-MON-5). |
| NFR-IO-4 | **Linux: ALSA `hw` directly, no fallback** — if the device is busy (e.g. held by PipeWire), surface a clear error; do not silently fall back to another backend. |
| NFR-IO-5 | Do not assume vendor control panels/mixers exist (they often do not on Linux); cue mixes are **software**. |

### 4.6 Maintainability & reproducibility

| ID | Requirement |
|----|-------------|
| NFR-M-1 | C++20, CMake (**≥ 3.22**). |
| NFR-M-2 | Dependencies pinned to **exact commits** (JUCE, Tracktion Engine) as git submodules. |
| NFR-M-3 | Single-command local build and identical CI build. |
| NFR-M-4 | Build artifacts reproducible enough to satisfy AGPL Corresponding Source obligations. |

### 4.7 Licensing compliance

| ID | Requirement |
|----|-------------|
| NFR-L-1 | Top-level license is **AGPLv3**. |
| NFR-L-2 | Every release tag ships Corresponding Source (the tag itself) + license texts + NOTICE. |
| NFR-L-3 | Contributions require **DCO sign-off** (no CLA). |
| NFR-L-4 | Third-party inventory maintained; no GPLv2+-only or commercial-only components pulled in (RubberBand, Elastique, etc. excluded). |
| NFR-L-5 | Ship the **host only** — do not bundle proprietary or GPL plugins in the installer. |

---

## 5. Architecture

### 5.1 Stack summary

| Layer | Choice | Notes |
|-------|--------|-------|
| Language | **C++20** | Required by Tracktion Engine. |
| Build system | **CMake ≥ 3.22** | Single top-level build; submodule deps. |
| Engine | **Tracktion Engine** | Multitrack audio, clips, comping, MIDI, automation, render/export, plugin hosting. **No UI.** |
| Framework / UI toolkit | **JUCE 9** | `juce_gui_basics` components; audio device I/O; plugin hosting. |
| UI | **JUCE GUI** (from scratch) | Tracktion provides no UI — this is the main project cost. |
| Plugin formats | VST3, LV2 (opt-in), AU (macOS) | No CLAP (NG4); no VST2 (D24). |
| Time-stretch/pitch | Signalsmith Stretch / SoundTouch | Free libraries only (NG5). |
| Project format | Tracktion native | + autosave + crash recovery. |

### 5.2 High-level data flow

```
                         ┌────────────────────────────────────────────┐
                         │                 JUCE GUI                    │
                         │  (arrangement / mixer / transport / dialogs)│
                         └───────────────┬────────────────────────────┘
                                         │ commands (message thread)
                                         ▼
              ┌──────────────────────────────────────────────────────┐
              │                  Edit / Engine model                   │
              │         (Tracktion Engine: Edit, Tracks, Clips)        │
              └───────────────┬───────────────────────┬──────────────┘
                              │                        │
                    audio graph changes         plugin graph
                              │                        │
                              ▼                        ▼
              ┌────────────────────────┐   ┌──────────────────────────┐
              │   Audio Graph (RT)      │   │  Plugin Host              │
              │  tracks→buses→master    │   │    VST3 / LV2 / AU        │
              └───────────┬─────────────┘   └──────────────────────────┘
                          │
                          ▼
              ┌────────────────────────┐
              │  DeviceManager / I/O    │
              │  CoreAudio | ALSA hw    │
              └───────────┬─────────────┘
                          │
                          ▼
                 ┌──────────────────┐
                 │  Audio Hardware   │
                 └──────────────────┘
```

### 5.3 Audio I/O abstraction

- **macOS:** JUCE **CoreAudio** backend. JUCE 9 rewrote macOS CoreAudio to use Apple's aggregate
  device API (multi-device), which is relevant for multi-interface setups.
- **Linux:** **ALSA `hw` directly**. There is **no native PipeWire backend in JUCE** (JUCE 9 offers
  ALSA + JACK only). This is a settled decision (NG/decision 7): open the ALSA `hw` device directly
  with **no fallback**. Consequence: if PipeWire (or another client) already holds the device, the
  open fails with `EBUSY`, and the app must **surface a clear error** (FR-MON-5) rather than falling
  back. The owner accepts this tradeoff. JACK is not the default path.
- **I/O model:** enumerate devices → user selects device + input/output channel ranges → engine binds
  any 1..N channels. No assumption of stereo.
- **Latency:** engineer selects buffer size; the app displays estimated round-trip latency. Target
  < 10 ms monitoring on blessed hardware (NFR-P-3).

> **Design note:** Because the whole I/O layer is channel-count-agnostic, the Maonocaster's stereo
> mixdown is simply the 2-channel case, and a 4/8-in interface is the N-channel case — no separate
> code path.

### 5.4 Audio graph

- Built and owned by Tracktion Engine's `Edit` / track hierarchy.
- **Node types (conceptual):** input node → track (with plugin chain) → bus/group → master →
  output.
- **Threading:** control changes from the message thread are applied to the graph without blocking
  the audio thread (RT-safe handoff). No allocations/locks on the audio callback (NFR-P-5).
- **Monitoring path:** armed input tracks are summed into software cue/monitor mixes (Epic 2 basic;
  per-performer software cue mixes via aux-send → aux-return → output-to-device in Epic 3); latency
  budget is tracked end-to-end.

### 5.5 Plugin host

- Hosting is provided through JUCE's plugin facilities, surfaced by Tracktion Engine.
- **Formats:** VST3 (default), LV2 (opt-in — requires `JUCE_PLUGINHOST_LV2=1`), AU (macOS only).
  **No CLAP.** **No VST2** — VST2 hosting is not shipped in releases (decision D24).
- **Scanning:** must be **out-of-process** to survive crashes
  (`PluginScanHelpers::PluginScanChildProcess`). A crashed scanner must not crash the host.
- **Hosting:** runs **in-process** — no out-of-process sandboxing of hosted plugins (decision D23).
  A plugin that crashes at runtime may therefore affect the host; this is accepted.
- **State:** plugin states serialized into the Tracktion project.
- **Legal:** hosting via public APIs (VST3/LV2/AU) is not a derivative work (see [§7.4](#74-plugin-hosting-position)).
  Ship the **host only**; do not bundle plugins.

### 5.6 Project format & persistence

- **Tracktion native format** is the canonical project representation.
- **Autosave:** periodic, non-destructive snapshots (configurable interval) in addition to manual
  saves.
- **Crash recovery:** on launch, detect unclean shutdown (lock/sentinel file) and offer to recover
  the most recent autosave (Epic 7; basic safeguards from Epic 1).
- **Audio files:** takes written incrementally (crash-safe); project references them
  non-destructively.

### 5.7 Module layout (proposed)

> This is a proposed structure for Epic 0; it is a *plan*, refined as implementation proceeds.

```
raw-radio-studio/
├── CMakeLists.txt                 # top-level build
├── cmake/                         # toolchain, options, helper modules
├── modules/                       # in-repo C++ modules (JUCE-style)
│   ├── studio_core/               # app model, session, project persistence
│   ├── studio_audio/              # device I/O, graph setup, monitoring, cue mixes
│   ├── studio_recording/          # arm/record, take mgmt, crash-safe writers
│   ├── studio_mixer/              # mixer model, routing, meters
│   ├── studio_plugins/            # plugin host, scanning (out-of-process)
│   ├── studio_editing/            # clips, comping, fades, time-stretch
│   └── studio_ui/                 # JUCE GUI: arrangement, mixer, transport
├── app/                           # application target(s), main entry point
├── third_party/
│   ├── tracktion_engine/          # git submodule (pinned commit)
│   └── JUCE/                      # git submodule (pinned commit)
├── docs/                          # this spec + other docs
├── .github/workflows/             # CI
├── LICENSES/                      # AGPLv3 + dependency license texts
├── NOTICE                         # third-party inventory + attribution
├── DCO                            # Developer Certificate of Origin text
└── CONTRIBUTING.md                # DCO sign-off process
```

### 5.8 Pinned dependency strategy

Resolved in Epic 0. Both dependencies are git submodules pinned to **exact commits**; no floating
branch is used in CI or release builds.

- **JUCE**: git submodule → upstream `juce-framework/JUCE`, pinned to the **release tag `9.0.3`**
  (commit `be29c81492b6151c8ea8d14c840e1311963b3a83`) — **not** the `develop` branch.
- **Tracktion Engine**: git submodule → upstream `Tracktion/tracktion_engine`, pinned to an exact
  `develop` commit `2d2d452fd0336805cadb78268436e83b4a9f68e6` (`VERSION.md` = `3.5.0`; latest tagged
  GitHub release is still `v3.2.0`). Integrated as JUCE modules compiled against *our* pinned JUCE.
- **Non-recursive submodules:** submodules are initialised **non-recursively**
  (`git submodule update --init`, not `--recursive`). Tracktion Engine's nested `modules/juce`
  submodule is intentionally **not** initialised: its URL is an SSH URL (`git@github.com:…`),
  unusable on CI runners, and its pin is fixed by Tracktion's tree. We pin JUCE independently
  (see the studio repo's `DEPENDENCIES.md`).
- **Compatibility:** JUCE 9.0.3 + Tracktion Engine 3.5.0 verified compatible in Epic 0
  ([§10.3](#103-risk--open-question--juce-9--tracktion-engine-compatibility)); documented fallback
  is JUCE **8.0.13** (upstream's own nested pin).
- Time-stretch libraries pinned similarly when adopted: **Signalsmith Stretch**
  is a git submodule pinned to tag `1.1.0` (commit `44c8f865`); see
  [`DEPENDENCIES.md`](../DEPENDENCIES.md) / OQ-2.

---

## 6. UI / UX

### 6.1 Paradigm — tracking-first

The UI follows a **Reaper/Ardour-style tracking DAW** paradigm: a single arrangement timeline is the
center of gravity, with a mixer alongside and a transport always accessible. The engineer's loop
(arm → monitor → record → review → export) is the primary flow. Everything is built from **JUCE GUI**
components; Tracktion Engine supplies no UI.

### 6.2 Screen inventory

| Screen / area | Purpose | Phase |
|---------------|---------|-------|
| **Transport bar** | Play/stop/record, position, tempo, metronome, latency/buffer indicator. | Epic 1 |
| **Arrangement view** | Tracks as lanes; clips on a timeline; playhead; arm/solo/mute per track. | Epic 1 (minimal) → Epic 4 |
| **Input / device panel** | Select audio device, buffer size, sample rate, channel mapping per track. | Epic 1 |
| **Monitor / cue panel** | Configure monitoring and per-performer cue mixes (software). | Epic 1 basic → Epic 3 (basic UI; send matrix pending) |
| **Mixer view** | Faders, pans, mutes, solos, sends, meters, plugin slots. | Epic 2/3 |
| **Plugin browser** | Scan/browse/insert VST3/LV2/AU; presets. | Epic 3 |
| **Editor (clip/comp)** | Trim/split/fade/comp/time-stretch. | Epic 4 |
| **MIDI editor (piano roll)** | MIDI note editing. | Epic 5 |
| **Export dialog** | WAV / stems, range, format options. | Epic 1 (WAV) → Epic 3 (stems) |
| **Settings** | Autosave interval, paths, plugin folders, recovery behavior. | Epic 1 |
| **Recovery dialog** | Offered on unclean-shutdown detection. | Epic 7 |
| **About / licenses** | Version, AGPLv3 notice, third-party NOTICE. | Epic 1 |

### 6.3 Core workflow (arm / record / overdub / monitor / export)

```
1. Select device   → Device panel: pick CoreAudio/ALSA device, buffer, sample rate.
2. Create/map      → Add tracks; map inputs (1..N ch) → tracks; set mono/stereo.
3. Set gain        → Adjust input trim; watch input meters.
4. Arm + monitor   → Arm tracks; configure monitor/cue mix; verify latency.
5. Roll            → Transport: record. Loop/punch as needed.
6. Overdub         → Play back; arm new tracks; record new takes.
7. Review/comp     → Takes list; comp a master take; trim/fade.
8. Process/mix     → Insert plugins; balance; automation.
9. Export          → WAV (and stems later).
10. (manual)       → Engineer uploads to raw-radio.ru outside the app.
```

### 6.4 MVP UI scope (Epic 1) vs later

- **Epic 1 (MVP):** device selection, one stereo track, arm/record/stop, basic monitoring,
  transport, input level meter, WAV export. **No** mixer beyond a master level, **no** plugins,
  **no** editing beyond clip playback.
- **Later:** mixer, plugins, editing/comping, MIDI, auto-update UX, recovery UI.

### 6.5 UX principles

- **Clear state:** armed tracks, recording, monitoring, and device status must be unmistakable.
- **Fail loudly, not silently:** device-busy (ALSA `EBUSY`) and scan failures must produce explicit,
  actionable messages (FR-MON-5, NFR-REL-3).
- **Engineer-first density:** information-rich, low-click, keyboard-friendly; not a consumer
  beatmaker UI.
- **Non-destructive by default:** takes, comps, and edits never destroy source audio.
- **Accessibility:** keyboard navigation for core transport/record actions (target).

---

## 7. Licensing & Legal

> **Important:** The factual statements below reflect prior legal research. A qualified lawyer must
> sign off on the linking model and third-party inventory **before public v1** (see
> [§7.6](#76-lawyer-sign-off-gate)).

### 7.1 Top-level license — AGPLv3

- The project is licensed **AGPLv3** at the top level.
- **Why AGPLv3 and not GPLv3:** The engine (Tracktion Engine, GPLv3-or-later) and the framework
  (JUCE, AGPLv3) are combined. GPLv3 + AGPLv3 combine validly **via GPLv3 §13** (which permits
  linking with AGPLv3-covered works and requires the combined work to offer Corresponding Source
  under AGPLv3 terms). The top-level license **must therefore be AGPLv3, not GPLv3**.

### 7.2 Corresponding Source obligations

- For **each released tag**, the Corresponding Source must be available from the **public GitHub
  repository** (the tag *is* the release). This includes:
  - license texts,
  - the **NOTICE** file and third-party inventory,
  - **pinned submodule commits** (JUCE, Tracktion Engine, and any other deps),
  - **build scripts** sufficient to reproduce the build.
- CI/release process must guarantee the tag points at the exact source that produced the binaries.

### 7.3 Contributions — DCO, no CLA

- Contributions are accepted via **Developer Certificate of Origin sign-off** (`git commit -s`,
  `Signed-off-by:` trailer).
- **No CLA** is required. This keeps the project easy to contribute to and consistent with the
  free-software posture.
- `CONTRIBUTING.md` + `DCO` file must document the process.

### 7.4 Plugin hosting position

- Hosting third-party plugins through their **public APIs** (VST3/LV2/AU) is **not** a derivative
  work of the plugins and does not impose their licenses on this host. This follows the reasoned
  precedent of the **Ardour Plugin Clarification**.
- **Bundling** proprietary or GPL plugins inside the installer is **risky** (distribution/linking
  implications). Therefore: **ship the host only** (NFR-L-5). Do not bundle plugin binaries.

### 7.5 Commercial / closed-source build constraints

- The **free tier** of Tracktion Engine and JUCE **cannot be used** in closed-source paid builds.
- Accordingly, this project is **not** offered as a closed-source build (NG3). Any downstream party
  wishing to ship a closed-source product must obtain **commercial licenses** from the respective
  vendors (Tracktion, JUCE) and cannot use this project's free-tier combination as-is.
- AGPLv3's network-copyleft (§13) means anyone operating a modified version as a network service
  must offer Corresponding Source.

### 7.6 Lawyer sign-off gate

- A **qualified lawyer must review and sign off** on:
  - the Tracktion Engine + JUCE **linking model** under GPLv3 §13 → AGPLv3,
  - the **third-party inventory** (including the time-stretch library choice and any future deps),
  - the plugin-hosting position and installer contents.
- **Gate:** no **public v1** release until this sign-off is recorded. This should be captured as a
  checked item in the release checklist.

### 7.7 Third-party inventory (initial)

| Component | License | Role | Notes |
|-----------|---------|------|-------|
| Tracktion Engine | GPLv3-or-later / commercial dual | Core DAW engine | Free tier only valid for GPLv3(+) projects. |
| JUCE | AGPLv3 / commercial dual | Framework, GUI, device I/O, plugin hosting | Combined via GPLv3 §13. |
| Signalsmith Stretch | MIT (verified — `LICENSES/SignalsmithStretch.txt`) | Time-stretch/pitch (Epic 4) | **ADOPTED** — pinned tag `1.1.0` (self-contained; bundles its MIT `dsp/`). AGPLv3-compatible. |
| SoundTouch | LGPL-2.1 | Time-stretch alternative | **Not adopted** — LGPL relink obligation avoided; Signalsmith (MIT) selected. |
| **RubberBand** | GPLv2+ | — | **EXCLUDED** (AGPL compatibility concern). |
| **Elastique** | Commercial | — | **EXCLUDED** (NG5). |

> Inventory is a living list; every dependency addition requires a license review and a NOTICE update.

---

## 8. Build / CI / Release

### 8.1 Build system

- **CMake ≥ 3.22**, top-level `CMakeLists.txt`.
- **C++20**.
- Dependencies via **git submodules pinned to exact commits** (JUCE release tag `9.0.3`, Tracktion
  Engine exact `develop` commit `2d2d452f`), initialised **non-recursively** — Tracktion's nested
  `modules/juce` SSH submodule is not used ([§5.8](#58-pinned-dependency-strategy)).
- Single-command local build (e.g. `cmake -B build && cmake --build build`) that mirrors CI.
- Options include enabling LV2 hosting (`JUCE_PLUGINHOST_LV2=1`) as opt-in.

### 8.2 CI — GitHub Actions

**Matrix: macOS + Ubuntu.**

| Job | Runner | What it does |
|-----|--------|--------------|
| `build-macos` | `macos-*` | Configure + build (C++20, pinned deps); run tests; produce `.dmg`/app bundle. |
| `build-linux` | `ubuntu-24.04` | Install ALSA/JUCE build deps; configure + build; run tests; produce `AppImage` + `deb`. |

CI must:
- initialize submodules at the **pinned** commits,
- cache build outputs where safe,
- fail on license/NOTICE drift if we add a check (recommended).

### 8.3 Release artifacts

| Platform | Artifact | Status | Notes |
|----------|----------|--------|-------|
| macOS | **unsigned `.dmg` + install instructions** | v1 | Notarization **deferred** ([§8.5](#85-code-signing--notarization)). |
| Linux | **`.deb`** | v1 | Ubuntu 24.04+. Shipped as a standalone package; **apt repo deferred** (D22). |
| Linux | **`AppImage`** | v1 | Primary portable artifact. |

Published to **GitHub Releases** on the project repo. A release tag = the Corresponding Source release
([§7.2](#72-corresponding-source-obligations)). A hosted **apt repository is deferred** (revisit on
demand), and **Flatpak is not adopted** for v1 (decision D22).

### 8.4 Auto-update (Epic 6)

| Platform | Mechanism | Notes |
|----------|-----------|-------|
| macOS | **Sparkle 2** | MIT. Uses EdDSA signatures + Apple code signing; supports delta updates. EdDSA key can ship before notarization. |
| Linux | **AppImageUpdate** (zsync, `gh-releases-zsync`) | Updates AppImage from GitHub Releases. |
| Linux (alt) | Flatpak / deb + apt repo | **Deferred** (D22) — not adopted for v1; revisit on demand. |

Auto-update is **Epic 6**, not v1.

### 8.5 Code signing / notarization

- **Current decision:** macOS builds are **unsigned `.dmg`** with clear **install instructions**
  (Gatekeeper bypass guidance). **Notarization is deferred.**
- Distributing signed/notarized macOS apps outside the App Store requires an **Apple Developer
  account ($99/yr)**, a **Developer ID**, **Hardened Runtime**, and **notarization** — deferred.
- **Note for later:** Hardened Runtime **inherits entitlements** for hosted plugins; this will need
  attention when signing is enabled.
- Linux: no code signing; AppImage/deb integrity via release hashes (+ EdDSA-style signature for
  Sparkle only applies to macOS).

### 8.6 Reproducibility & AGPL

- Pinned submodules + CMake + committed build scripts ensure the Corresponding Source per tag is
  buildable.
- Release checklist must verify: tag → submodule SHAs → NOTICE + license texts present → artifacts
  attached.

---

## 9. Roadmap (Epics 0–7)

The epic order below is **agreed and frozen**. Dependencies are explicit.

### Epic 0 — Bootstrap

**Goal:** A buildable, licensed, CI-driven skeleton repo.

**Deliverables**
- Repo `raw-radio-studio` scaffolded at `/Users/mac/projects/raw_radio/raw-radio-studio`.
- Top-level CMake (≥ 3.22) + C++20; app target that opens an empty JUCE window.
- Pinned git submodules (non-recursive): JUCE release tag `9.0.3` + Tracktion Engine exact `develop`
  commit `2d2d452f`.
- **Verify JUCE 9 / Tracktion Engine compatibility** by building — **done** (resolved
  [OQ-3](#103-risk--open-question--juce-9--tracktion-engine-compatibility)).
- License: AGPLv3 text, `NOTICE`, `DCO`, `CONTRIBUTING.md` (sign-off process).
- GitHub Actions matrix: macOS + Ubuntu build; produce **unsigned `.dmg`**, **`AppImage`**, **`deb`**.

**Acceptance criteria**
- [ ] Fresh clone → `cmake` configure + build succeeds on macOS 12+ and Ubuntu 24.04 CI runners.
- [ ] CI produces `.dmg` (macOS), `AppImage` + `deb` (Linux) as artifacts.
- [ ] `LICENSE` = AGPLv3; `NOTICE`, `DCO`, `CONTRIBUTING.md` present.
- [ ] Submodules pinned to exact SHAs; SHAs recorded in repo.
- [ ] Documented JUCE9/Tracktion compatibility result.

**Depends on:** nothing.

### Epic 1 — Walking Skeleton (MVP)

**Goal:** Record 1 stereo track (from the **Fifine Ampli1** test device;
[§2.5](#25-hardware-notes)) + monitoring + export WAV. **No plugins, no mixing.**

**Deliverables**
- Audio device selection (CoreAudio / ALSA `hw`), buffer + sample rate (48 kHz / 24-bit).
- One stereo track; arm/record/stop; take written to disk (crash-safe).
- Monitoring of the armed track.
- Minimal transport + input meter.
- Export session to WAV.
- Tracktion native project save/open (basic autosave).

**Acceptance criteria**
- [ ] On the **Fifine Ampli1** test device ([§2.5](#25-hardware-notes)), record a stereo take and hear it during monitoring.
- [ ] Take survives an app crash and is present after restart.
- [ ] Export WAV is 24-bit at session sample rate and plays back identically.
- [ ] Project reopens with the take intact.
- [ ] Clear error is shown if the device is missing/busy (no silent fallback).

**Depends on:** Epic 0.

### Epic 2 — Multitrack + Device-Agnostic I/O

**Status: ✅ delivered (the one hard gap — software cue mixes — was closed in Epic 3).** Every
deliverable below is implemented. The previously deferred **software cue mixes** (FR-MON-3/FR-MON-4)
landed on the Epic 3 routing foundation and are now **CLOSED in Epic 3**; the history and rationale
remain in [`docs/EPIC2_GAPS.md`](EPIC2_GAPS.md). Residual Epic 2 *targets* (punch-in/out, RMS
metering, on-hardware acceptance, latency/16-track measurement) are tracked there and in
[`docs/EPIC3_GAPS.md`](EPIC3_GAPS.md).

**Goal:** 1..N channels; multiple tracks; software cue mixes.

**Deliverables**
- ✅ **Input mapping** for arbitrary channel counts (mono/stereo/N) — **FR-REC-3** (device-agnostic
  per-track input mapping; mixer-strip UI + persistence).
- ✅ **Multitrack recording + overdub** — **FR-REC-1/7 (partial)**: multitrack + overdub landed;
  **punch-in/out and loop-recording remain open** (FR-REC-7 [target], see gaps doc).
- ✅ **Basic mixer** (fader/pan/mute per track **and master**; **solo per track** — master solo N/A,
  per the FR-MIX-1 note in [§3.3](#33-mixing-routing--plugins)), metering (peak + clip) —
  **FR-MIX-1/3** (**RMS metering** still open).
- ✅ **Software cue mixes** per performer (vendor mixers unavailable on Linux) — **FR-MON-3 / FR-MON-4
  [hard]** — **delivered in Epic 3** (routing: AuxSend → AuxReturn → independent output-to-device;
  see [§3.2](#32-monitoring) and [`docs/EPIC2_GAPS.md`](EPIC2_GAPS.md)).
- ✅ **Count-in/metronome** (target) — **FR-REC-10** (measured; excluded from WAV export).

**Acceptance criteria** — ✅ **the hard criteria are met**; target criteria pending measurement, see
[`docs/EPIC2_GAPS.md`](EPIC2_GAPS.md).
- [x] Record on a 4-in interface with 4 separate tracks mapped to 4 inputs. *(verified against a
      hosted device; on-hardware run pending a true multichannel interface — D21)*
- [x] Two independent cue mixes are audible and distinct. *(**delivered in Epic 3** — two sources
      sent to two cue returns at different levels; per-device output measured as distinct and
      independent. On-hardware multi-output separation still pending — see
      [`docs/EPIC3_GAPS.md`](EPIC3_GAPS.md).)*
- [ ] 16-track playback target not exceeded; no xruns under defined load (target). *(needs the
      reference machines/interface — Epic 7)*
- [ ] Monitoring latency < 10 ms on blessed hardware (target). *(needs the reference machines —
      Epic 7)*

**Depends on:** Epic 1.

### Epic 3 — Mixer + Routing + Plugin Hosting

**Status: ✅ delivered.** Plugin hosting, flexible routing, software cue mixes (closing the Epic 2
hard gap), stems/region export, and plugin preset management have landed. The remaining open items
(cue-mix UI depth, on-hardware multi-output verification, real crashing-plugin scan test, basic
preset management, transient region selection) are tracked in
[`docs/EPIC3_GAPS.md`](EPIC3_GAPS.md).

**Goal:** Buses/groups/sends + VST3/LV2/AU hosting + stems.

**Deliverables**
- ✅ **Routing:** track → bus/group → master; per-track output assignment, submix folder tracks, and
  aux sends (at least one aux/cue send) — **FR-MIX-2**.
- ✅ **Plugin hosting:** VST3 (default), LV2 (opt-in via `JUCE_PLUGINHOST_LV2=1`), AU (macOS).
  **No VST2** (decision D24); no CLAP (NG4) — **FR-MIX-4**.
- ✅ **Plugin state save/restore; named user preset management** — **FR-MIX-6**.
- ✅ **Out-of-process plugin scan** (`PluginScanHelpers::PluginScanChildProcess`); hosting in-process
  (D23) — **FR-MIX-7**.
- ✅ **Software cue mixes** (FR-MON-3/FR-MON-4) — aux-return bus per cue, routed to an independent
  hardware output pair when the interface exposes one; per-source post-fader sends. Closes the Epic 2
  hard gap.
- ✅ **Export stems** (one 24-bit WAV per clip-bearing track + master) — **FR-EXP-3** — and **region
  export** — **FR-EXP-2** — via offline render.

**Acceptance criteria** — ✅ met, with the noted exceptions in
[`docs/EPIC3_GAPS.md`](EPIC3_GAPS.md).
- [x] Insert a VST3 plugin, process audio, save project, reopen → plugin state restored. *(verified
      headlessly with a deterministic stand-in plugin through a fresh save/reopen; no VST3 required
      on the machine)*
- [x] LV2 hosting works when `JUCE_PLUGINHOST_LV2=1` build is used. *(opt-in flag verified; LV2
      runtime hosting is exercised when a system LV2 host is available)*
- [x] AU hosting works on macOS only; absence on Linux is graceful. *(macOS scan found AU plugins with
      out-of-process scanning enabled; other platforms report the format absent)*
- [ ] A deliberately crashing plugin during scan does **not** crash the host. *(out-of-process scan is
      implemented and verified to isolate a probe; **not yet exercised with a real crashing plugin** —
      see [`docs/EPIC3_GAPS.md`](EPIC3_GAPS.md))*
- [x] Export stems produce per-track/bus WAV files. *(measured: 2 stems + master, 24-bit / 48 kHz,
      per-stem levels correct and master = sum)*
- [x] Region export renders exactly the selected range. *(measured: output length and in-region level
      match the selection while the full-session default still renders the whole file)*

**Depends on:** Epic 2.

### Epic 4 — Editing

**Goal:** Clips, comping, fades, time-stretch/pitch.

**Deliverables**
- [x] Arrangement editing: move/trim/split/duplicate/loop/delete clips. *(interactive
      arrangement: clips on track lanes; drag to move, drag edges to trim, top-corner fade
      handles; split-at-playhead, duplicate, loop, delete, mute.)*
- [x] Fades and crossfades. *(per-clip fade-in/out + equal-power crossfade between adjacent
      clips; measured: junction step drops from 0.496 to 0.029 — the source's own slope.)*
- [x] Comping from multiple takes. *(non-destructive master take on a new track from stacked
      takes; measured 220|330|440 Hz per region. Engine `CompManager` take lanes are a
      documented follow-up — see [`EPIC4_GAPS.md`](EPIC4_GAPS.md).)*
- [x] Time-stretch/pitch via **free libraries only** — **Signalsmith Stretch (MIT)** selected
      (OQ-2 resolved), pinned as a git submodule at tag `1.1.0`. *(stretch-to-target-duration
      plus independent pitch shift; measured exact target lengths.)*
- [x] Undo/redo across edit operations. *(every edit is one `UndoManager` transaction.)*
- [~] Snap/grid/markers/tempo (target); automation (target). **Deferred** — see
      [`EPIC4_GAPS.md`](EPIC4_GAPS.md).

**Acceptance criteria**
- [x] Comp a master take from 3+ takes non-destructively. *(measured: 3 takes unchanged on
      disk, comp renders 220|330|440 Hz by region.)*
- [x] Apply a crossfade between adjacent clips without artifacts. *(measured: no junction
      discontinuity in the offline render.)*
- [x] Time-stretch a clip to a target duration using the chosen free library. *(measured: a
      1.000 s clip renders to exactly 0.500 s; DSP unit test hits the target length and
      preserves pitch.)*
- [x] Undo/redo restores edit state consistently. *(measured: trim/split/duplicate/delete and
      clip time-stretch all restore their previous state.)*

**Depends on:** Epic 3 (mixer/plugin context helpful; core edit can proceed in parallel).

### Epic 5 — MIDI + Virtual Instruments

**Goal:** MIDI recording/editing and instrument hosting. **Separate epic after audio.**

**Deliverables**
- MIDI input capture; MIDI routing.
- Piano-roll MIDI editor.
- Virtual instrument hosting (VST3/LV2/AU instruments).

**Acceptance criteria**
- [ ] Record MIDI from a hardware controller to a track.
- [ ] Edit notes in a piano roll; play back via a VST3 instrument.
- [ ] MIDI + audio coexist in one session and export correctly.

**Depends on:** Epic 3 (plugin host), Epic 4 (editor patterns).

### Epic 6 — Auto-Update + Packaging Polish

**Goal:** Sparkle (macOS) / AppImageUpdate (Linux); distribution polish.

**Deliverables**
- **Sparkle 2** integration (macOS): appcast, EdDSA signatures, delta updates (code signing deferred).
- **AppImageUpdate** (zsync, `gh-releases-zsync`) integration (Linux).
- Packaging polish: **AppImage + deb**, install instructions. **apt repo and Flatpak deferred**
  (D22; revisit on demand).

**Acceptance criteria**
- [ ] macOS app checks for and installs an update from a hosted appcast.
- [ ] AppImage self-updates from GitHub Releases via AppImageUpdate.
- [ ] Release artifacts attach cleanly to GitHub Releases with checksums.

**Depends on:** Epics 0–4 (stable artifacts).

### Epic 7 — Hardening

**Goal:** Reliability and performance at the agreed envelope.

**Deliverables**
- Crash recovery UX (detect unclean shutdown, offer autosave recovery).
- Performance validation: **16 tracks**, **~32 plugins**, monitoring **< 10 ms** on blessed hardware;
  eliminate xruns.
- Out-of-process plugin **scan** hardening (hosting runs in-process — no sandboxing; decision D23).
- QA pass: device hot-plug, error surfacing, long-session soak.

**Acceptance criteria**
- [ ] 16-track session with ~32 plugins records/plays without xruns on the reference machines
      (Apple Silicon Mac 16 GB RAM / Ubuntu x86_64 16 GB RAM, 4-in class-compliant USB interface;
      decision D21).
- [ ] Monitoring latency < 10 ms verified with measurement on the reference machines.
- [ ] Crash during recording → restart → recovery offered → captured audio usable.
- [ ] Plugin scanner crash is fully isolated; host unaffected (scan only; hosting is in-process).

**Depends on:** Epics 0–6.

### Epic dependency graph

```
Epic 0 ──► Epic 1 ──► Epic 2 ──► Epic 3 ──► Epic 4 ──► Epic 5
                                     │           │
                                     └───────────┴──► Epic 6 ──► Epic 7
```

- Epic 5 (MIDI) depends on the plugin host (Epic 3) and editor patterns (Epic 4).
- Epic 6 depends on stable artifacts (through Epic 4).
- Epic 7 depends on everything; it is the final hardening gate.

---

## 10. Risks & Open Questions

### 10.1 Risk — ALSA `hw` without fallback (`EBUSY`)

- **Risk:** On Linux with PipeWire/PulseAudio holding the device, opening ALSA `hw` fails with
  `EBUSY`. There is no fallback by design, so the app is unusable until the device is freed.
- **Impact:** Engineer must stop/route PipeWire or use a direct ALSA path; source of user friction.
- **Mitigation:** surface a **clear, actionable error** (FR-MON-5); document the setup; consider a
  helper doc (not a fallback backend).
- **Residual:** accepted by the owner.

### 10.2 Risk — Tracktion Engine provides NO UI (main project cost)

- **Risk:** Every arrangement, piano-roll, mixer, and automation UI element must be written from
  scratch on JUCE. This is the **largest development-cost item**.
- **Impact:** Timeline/scope risk across Epics 1–5.
- **Mitigation:** incremental UI; reuse JUCE components; prioritize the tracking loop first.

### 10.3 Risk / Open Question — JUCE 9 ↔ Tracktion Engine compatibility

- **Status:** **RESOLVED (Epic 0).** JUCE **9.0.3** (pinned release tag) builds, links, and runs with
  Tracktion Engine **3.5.0** (pinned `develop` commit `2d2d452f`). Verified by a full configure +
  build + link + run on:
  - **macOS arm64** (AppleClang, local) — ✅
  - **Ubuntu 24.04** (GCC, container) — ✅

  The engine is genuinely linked (public symbol `tracktion::engine::Engine::getVersion()` present;
  `raw-radio-studio --version` prints `JUCE 9.0.3, Tracktion Engine 3.5.0`). Exact pinned SHAs and
  evidence are recorded in the studio repo's `DEPENDENCIES.md` and `NOTICE`.
- **Residual risk:** Tracktion's own nested submodule still pins JUCE **8.0.13**, so JUCE 9 is ahead
  of the combination upstream tests; upstream may not officially claim JUCE 9 support yet.
- **Documented fallback:** if a JUCE 9 / Tracktion regression appears, pin JUCE **8.0.13**
  (`37c894f83d379179b2070d437ccd0f1cd9af9576`) — the revision Tracktion ships.

### 10.4 Risk — unsigned macOS builds

- **Risk:** Unsigned, un-notarized `.dmg` triggers Gatekeeper warnings and requires manual bypass.
- **Impact:** Friction for users; perceived trustworthiness.
- **Mitigation:** clear install instructions for now; defer notarization (Apple Developer $99/yr +
  Developer ID + Hardened Runtime + notarization). Track entitlements-inheritance for plugins when
  enabled.

### 10.5 Risk — AGPL obligations / dependency licensing

- **Risk:** Missing Corresponding Source, NOTICE drift, or an incompatible dependency breaks
  license compliance.
- **Mitigation:** per-tag release checklist; pinned submodules; third-party inventory in NOTICE;
  license review gate for every new dep; **lawyer sign-off before public v1** ([§7.6](#76-lawyer-sign-off-gate)).

### 10.6 Risk — plugin scan / runtime crashes

- **Risk:** Malformed plugins can crash during scan or at runtime.
- **Mitigation:** **out-of-process scanning** (Epic 3, hardened Epic 7). Hosted plugins run
  **in-process** — no sandboxing (decision D23) — so runtime plugin crashes are a **residual,
  accepted** risk; the host should degrade gracefully where feasible (NFR-REL-4) but is not
  guaranteed to survive.

### 10.7 Risk — Tracktion API churn

- **Risk:** Upstream `develop` APIs change; pulling updates can break the build.
- **Mitigation:** **pin exact commits**; update deliberately and re-run CI; never track `develop`
  floating in release builds.

### 10.8 Risk — time-stretch library licensing

- **Risk:** The "free" choice must be confirmed AGPL-compatible; exact license text of Signalsmith
  Stretch needs verification.
- **Resolution (OQ-2 closed):** Signalsmith Stretch is **MIT** (library and its bundled `dsp/`
  copy); SoundTouch is LGPL-2.1. MIT was selected — permissive, no copyleft or relink obligation.
  Pinned as a git submodule at tag `1.1.0` (`44c8f865`); that revision is self-contained (later
  revisions pull `signalsmith-linear` via FetchContent, which this project avoids). The decision
  remains behind an abstraction (`rrs::TimeStretch`) so it is still reversible.

### 10.9 Decisions & open questions

#### 10.9.1 Resolved decisions (owner)

The following previously-open questions are now **settled by the owner**. They are recorded here and
mirrored as ADRs in [§12](#12-decisions-baseline-adr-table).

| OQ | Question | Resolution | ADR |
|----|----------|------------|-----|
| OQ-1 | "Blessed hardware" reference machine | **RESOLVED.** Reference machines: **Apple Silicon Mac, 16 GB RAM** and **Ubuntu x86_64, 16 GB RAM**, both using a **4-in class-compliant USB interface**. Defines the 16 tracks / ~32 plugins / <10 ms acceptance targets. | D21 |
| OQ-4 | Linux packaging channels | **RESOLVED.** Ship **AppImage + deb**. Hosted **apt repo and Flatpak are deferred** (revisit on demand). | D22 |
| OQ-5 | Windows support | **DECIDED.** **Not a v1 target**; possible support **after v1** (low-priority future consideration, not permanently excluded). | D25 |
| OQ-6 | Plugin isolation scope | **RESOLVED.** **Out-of-process plugin scanning only**; plugin **hosting runs in-process** (no sandboxing). Runtime plugin crashes are a residual, accepted risk. | D23 |
| OQ-8 | VST2 support | **RESOLVED.** **Do NOT ship VST2 support** — no VST2 hosting in releases. | D24 |

#### 10.9.2 Open questions (technical — to be resolved by agents)

These are tracked as open and are expected to be resolved by engineering agents during the
indicated epics (OQ-3 was resolved during Epic 0).

- **OQ-2 — Time-stretch library (Epic 4):** **RESOLVED.** **Signalsmith Stretch (MIT)** selected
  over SoundTouch (LGPL-2.1); pinned as a git submodule at tag **1.1.0**
  (`44c8f865`). MIT is AGPLv3-compatible with no relink obligation. See
  [`DEPENDENCIES.md`](../DEPENDENCIES.md#oq-2--time-stretch-library-licence-verdict) and §10.8.
- **OQ-3 — JUCE 9 / Tracktion compatibility (Epic 0):** **RESOLVED.** JUCE **9.0.3** + Tracktion
  Engine **3.5.0** verified compatible by building on macOS arm64 + Ubuntu 24.04; documented
  fallback JUCE **8.0.13**
  ([§10.3](#103-risk--open-question--juce-9--tracktion-engine-compatibility)).
- **OQ-9 — Project file portability (Epic 1/2):** does the Tracktion native format round-trip
  cleanly across macOS/Linux (paths, case sensitivity)?

#### 10.9.3 Deferred follow-ups (not blocking v1)

- **OQ-7 — Code-signing / notarization timeline:** **remains deferred/open.** Revisit **after MVP /
  user growth**. Apple Developer enrollment ($99/yr), Developer ID, Hardened Runtime, and
  notarization are documented follow-ups ([§8.5](#85-code-signing--notarization)).

### 10.10 Open items (consolidated)

See [§10.9](#109-decisions--open-questions): resolved decisions in §10.9.1, open technical items in
§10.9.2, and deferred follow-ups in §10.9.3.

---

## 11. Glossary

| Term | Meaning |
|------|---------|
| **AGPLv3** | GNU Affero General Public License v3 — strong copyleft with network-use source obligation. |
| **ALSA** | Advanced Linux Sound Architecture; here used via `hw` devices directly. |
| **AppImage / AppImageUpdate** | Portable Linux app format / its zsync-based updater. |
| **AU** | Audio Units — Apple's plugin format, macOS only. |
| **Cue mix** | A separate monitor mix sent to a performer (software-implemented here). |
| **CLAP** | CLever Audio Plugin — an open plugin standard. **Not supported** by this project. |
| **CoreAudio** | Apple's audio subsystem (macOS backend). |
| **Corresponding Source** | AGPL term for the source needed to build/install the released binary. |
| **DAW** | Digital Audio Workstation. |
| **DCO** | Developer Certificate of Origin — sign-off process used instead of a CLA. |
| **EdDSA** | Edwards-curve signature scheme used by Sparkle for update signing. |
| **`EBUSY`** | POSIX "device or resource busy" error; expected when ALSA `hw` is held by PipeWire. |
| **float32** | 32-bit floating-point internal audio sample format. |
| **Hardened Runtime** | macOS security mode required for notarization. |
| **HLS / HLS** | (Context: streaming; not a core DAW feature.) |
| **JUCE** | C++ framework for audio apps/plugins (GUI, device I/O, plugin hosting). |
| **LV2** | LADSPA version 2 — open Linux plugin format (opt-in here). |
| **Maonocaster E2 Gen2** | Maono USB audio device; exposes **only a stereo mixdown** (2-in/2-out, 48 kHz/16-bit, class-compliant). |
| **Fifine Ampli1** | Fifine SC1 USB audio interface — the **Epic 1 test device**: 2-in/2-out, UAC1 class-compliant, 16-bit/48 kHz; 1 mic (XLR/TRS, 48 V) + 1 instrument input, direct monitor; no loopback/ASIO; not ≥4-in/24-bit. |
| **Monitoring latency** | Round-trip time from input to monitored output. |
| **MVP** | Minimum Viable Product — here, Epic 1 walking skeleton. |
| **Pinned commit** | An exact git SHA recorded for deterministic builds. |
| **PipeWire** | Linux multimedia server; may hold the device, causing ALSA `EBUSY`. |
| **Plugin scan** | Discovering/validating plugins; done out-of-process here. |
| **Sparkle 2** | macOS auto-update framework (MIT), EdDSA-signed updates. |
| **Stems** | Per-track/bus rendered WAV files. |
| **Take** | A single recorded pass on a track. |
| **Time-stretch / pitch-shift** | Changing a clip's duration/pitch independently (free libs only here). |
| **Tracktion Engine** | Open/C++ audio engine powering the DAW; provides no UI. |
| **VST2 / VST3** | Steinberg plugin formats (VST3 default/supported; VST2 legacy — **not supported here**, D24). |
| **Walking skeleton** | Thinnest end-to-end implementation that exercises the full pipeline. |
| **xrun** | Audio buffer over/underrun causing a dropout. |

---

## 12. Decisions Baseline (ADR-style table)

These decisions are **settled by the owner** and are the frozen baseline. Each row states the
decision and the rationale.

| # | Decision | Value | Rationale |
|---|----------|-------|-----------|
| D1 | License | **AGPLv3** top-level | Tracktion (GPLv3-or-later) + JUCE (AGPLv3) combine via GPLv3 §13; top level must be AGPLv3. |
| D2 | Contributions | **DCO sign-off, no CLA** | Low-friction, free-software-aligned contributions. |
| D3 | Product class | **Full DAW, tracking-first** | Engineer needs capture-and-mix, not a beatmaker. |
| D4 | Engine/stack | **Tracktion Engine + JUCE, C++20, CMake** | Mature multitrack engine + proven audio framework; no UI provided. |
| D5 | UI toolkit | **JUCE GUI** | Same framework; no separate UI toolkit. |
| D6 | Platforms | **macOS 12+, Ubuntu 24.04+** | Covers the engineer's two environments. |
| D7 | Distribution | **GitHub Releases: unsigned .dmg + instructions; deb + AppImage** | Ship now; notarization deferred (cost). |
| D8 | Auto-update | **Sparkle (macOS) / AppImageUpdate (Linux)** — Epic 6 | Standard free updaters per platform. |
| D9 | Audio I/O | **Device-agnostic; CoreAudio / ALSA `hw` no-fallback** | Universal channel counts; JUCE lacks PipeWire. Owner accepts EBUSY. |
| D10 | Audio format | **24-bit; 44.1/48 default, up to 96 kHz; float32 internal** | Standard studio quality with headroom. |
| D11 | Plugin hosting | **VST3 + LV2 (opt-in) + AU (macOS); NO CLAP; NO VST2** | Covers Linux/macOS ecosystems; CLAP and VST2 out of scope (D24). |
| D12 | Time-stretch/pitch | **Free libs only (Signalsmith Stretch / SoundTouch)** | Avoid commercial (Elastique) and GPLv2+ (RubberBand) AGPL issues. |
| D13 | Project format | **Tracktion native + autosave/crash recovery** | Engine-native round-trip; reliability. |
| D14 | Performance | **16 tracks, ~32 plugins, < 10 ms monitoring** | Realistic small-studio envelope. |
| D15 | Sessions | **Local only** | Artist physically present; no remote in v1. |
| D16 | MIDI/VI | **Separate epic after audio (Epic 5)** | Keep v1 audio core focused. |
| D17 | MVP (Epic 1) | **1 stereo track + monitoring + WAV export** | Walking skeleton; no plugins/mixing. |
| D18 | Epic order | **0→7 as listed** | Bootstrap → capture → I/O → mixing → editing → MIDI → packaging → hardening. |
| D19 | No integration | **No raw-radio.ru integration** | Standalone product; manual upload. |
| D20 | Ship host only | **No bundled plugins** | Avoid distribution/linking legal risk. |
| D21 | Reference hardware | **Apple Silicon Mac 16 GB RAM + Ubuntu x86_64 16 GB RAM, 4-in class-compliant USB interface** | Resolves OQ-1; concrete acceptance target for Epic 7. |
| D22 | Linux packaging | **AppImage + deb only; apt repo + Flatpak deferred** | Resolves OQ-4; keep distribution surface small for v1. |
| D23 | Plugin isolation | **Out-of-process scanning only; hosting in-process** | Resolves OQ-6; scanning crash isolation without sandboxing cost. |
| D24 | VST2 | **Not shipped** | Resolves OQ-8; deprecated/legacy format with licensing history. |
| D25 | Windows | **Not a v1 target; possible after v1** | Resolves OQ-5; keep v1 focused on macOS + Ubuntu. |

### 12.1 Settled vs derived

- **Settled (owner):** D1–D25 above, the epic order (D18), the MVP (D17), and the non-goals (NG1–NG8).
  D21–D25 resolve OQ-1/4/5/6/8 (see [§10.9.1](#1091-resolved-decisions-owner)).
- **Derived (engineering):** module layout ([§5.7](#57-module-layout-proposed)), proposed UI screens
  ([§6.2](#62-screen-inventory)), and suggested CI jobs ([§8.2](#82-ci--github-actions)) — these are
  implementation proposals, not owner decisions, and may be revised by PR.
- **Open:** technical items in [§10.9.2](#1092-open-questions-technical--to-be-resolved-by-agents) and
  deferred follow-ups in [§10.9.3](#1093-deferred-follow-ups-not-blocking-v1).

---

## 13. Changelog

This section is maintained by hand and updated on every merged PR that changes decisions or
requirements. Format follows [Keep a Changelog](https://keepachangelog.com/) loosely.

### [Unreleased]

- (nothing yet)

### [1.5.0] — 2026 — Epic 4 implemented: arrangement editing, fades, comping, undo/redo, time-stretch

- **Epic 4 (§9) marked as delivered** (snap/grid/markers/tempo and automation deferred as
  `[target]`; see [`docs/EPIC4_GAPS.md`](EPIC4_GAPS.md)):
  - arrangement editing — move / trim / split / duplicate / loop / delete / mute clips on the
    timeline, with an interactive arrangement view (clip rectangles, drag to move, edge drags to
    trim, top-corner fade handles) — FR-ED-1/FR-ED-2;
  - fades and equal-power crossfades between adjacent clips — FR-ED-3;
  - non-destructive comping from multiple stacked takes onto a new track — FR-ED-4;
  - undo/redo across edit operations (one `UndoManager` transaction per edit) — FR-ED-6;
  - time-stretch / pitch-shift — FR-ED-5.
- **Resolved OQ-2** (time-stretch library): **Signalsmith Stretch (MIT)** selected over SoundTouch
  (LGPL-2.1), pinned as a git submodule at tag **1.1.0** (self-contained; later revisions use
  FetchContent for `signalsmith-linear`, avoided here). Updated §7.7, §10.8, §10.9.2, and the
  dependency inventory in `NOTICE` / `DEPENDENCIES.md` / `LICENSES/`.
- Added [`docs/EPIC4_GAPS.md`](EPIC4_GAPS.md) for the still-open Epic 4 items (snap/grid/markers/
  tempo, automation, engine-`CompManager` take lanes, limited time-stretch UI, fade-curve choice).
- Bumped document version to **1.5.0**.

### [1.4.0] — 2026 — Epic 3 implemented: plugin hosting, routing, software cue mixes, stems, region export, presets

- **Epic 3 (§9) marked as delivered.** Recorded as implemented: plugin hosting (VST3 default, LV2
  opt-in, AU on macOS only — **FR-MIX-4**; the task shorthand “FR-PLG-1/2/3” maps to these FR-MIX
  IDs), out-of-process plugin scanning (FR-MIX-7), flexible routing with per-track output assignment,
  submix folders, and aux sends (FR-MIX-2), and plugin preset management (FR-MIX-6).
- **Software cue mixes delivered (FR-MON-3/FR-MON-4 [hard])**, closing the Epic 2 hard gap: aux-return
  bus per cue routed to an independent hardware output pair, with per-source post-fader sends. The
  Epic 2 status note and acceptance criteria were updated accordingly.
- **Export:** per-track + master **stems** (FR-EXP-3) and **region export** (FR-EXP-2) recorded as
  delivered (offline render).
- **Added [`docs/EPIC3_GAPS.md`](EPIC3_GAPS.md)** for the still-open Epic 3 items (basic cue-mix UI,
  on-hardware multi-output verification, real crashing-plugin scan test, basic preset management,
  transient region selection); marked the software cue-mix gap **CLOSED in Epic 3** in
  [`docs/EPIC2_GAPS.md`](EPIC2_GAPS.md).
- **Licensing:** recorded the bundled **VST3 SDK as MIT** (Steinberg; in JUCE's pinned tree) and the
  bundled **LV2 SDK as permissive (ISC)** — added `LICENSES/VST3-SDK.txt` and
  `LICENSES/LV2-SDK.txt`; corrected `NOTICE` / `DEPENDENCIES.md`.
- Bumped document version to **1.4.0**.

### [1.3.0] — 2026 — Epic 2 progress recorded; FR-MIX-1 master-solo qualified

- **Epic 2 (§9) marked as substantially delivered but not complete.** Marked as implemented the
  device-agnostic per-track input mapping (FR-REC-3), multitrack + overdub (FR-REC-1/7-partial),
  the basic mixer (FR-MIX-1/3), and count-in/metronome (FR-REC-10); annotated acceptance criteria.
- **Software cue mixes (FR-MON-3/FR-MON-4 [hard]) recorded as an explicit open gap**, deferred to
  the Epic 3 routing foundation (FR-MIX-2 sends); Epic 2 is **not** marked fully complete. Points to
  [`docs/EPIC2_GAPS.md`](EPIC2_GAPS.md).
- **Amended FR-MIX-1 (§3.3):** fader/pan/mute per track **and master**; **solo per track** —
  **master solo is N/A** (a master bus has no sibling to isolate against). Added a rationale note.
- Bumped document version to **1.3.0**.

### [1.2.1] — 2026 — Clarified hardware: Fifine Ampli1 as Epic 1 test device

- **Recorded the Fifine Ampli1** (= Fifine SC1) as the **Epic 1 test device** (2-in/2-out, UAC1
  class-compliant, 16-bit/48 kHz, 1 mic + 1 instrument input, hardware direct monitor, no
  loopback/ASIO). Added §2.5 "Hardware notes"; updated §2.3 S1, §3.8, and the Epic 1 goal/acceptance;
  added a glossary entry.
- **Caveat:** the Ampli1 is a **16-bit/48 kHz source** — it validates the pipeline but **not 24-bit
  capture** (WAV **export** stays 24-bit, engine-converted) and cannot provide **≥4 simultaneous
  tracks** (Epic 2 still needs a true multichannel interface, D21).
- Documented the **Epic 1 test workflow**: DAW "minus" → main out/headphones; **condenser mic → XLR +
  48 V phantom** → recorded voice take.

### [1.2.0] — 2026 — Epic 0 executed: engine wired, OQ-3 resolved

- **Resolved OQ-3** (JUCE ↔ Tracktion compatibility): JUCE **9.0.3** + Tracktion Engine **3.5.0**
  verified compatible by building on macOS arm64 + Ubuntu 24.04; documented fallback JUCE
  **8.0.13**. Updated §10.3 status and the OQ-3 entry in §10.9.2.
- Recorded Epic 0 dependency pins: JUCE pinned to release tag **9.0.3** (not the `develop` branch),
  Tracktion Engine pinned to exact `develop` commit **2d2d452f**; submodules initialised
  **non-recursively** (Tracktion's nested `modules/juce` SSH submodule is unused). Updated §5.8,
  §8.1, and the Epic 0 deliverables.

### [1.1.0] — 2026 — Resolved OQ-1/4/5/6/8 per owner

- **Resolved OQ-1** (reference hardware → D21): Apple Silicon Mac 16 GB RAM + Ubuntu x86_64 16 GB
  RAM, 4-in class-compliant USB interface. Updated NFR-P note, Epic 7 acceptance criteria, §10.9.1.
- **Resolved OQ-4** (Linux packaging → D22): ship AppImage + deb; apt repo + Flatpak deferred.
  Updated §8.3, §8.4, Epic 6 deliverables.
- **Decided OQ-5** (Windows → D25): not a v1 target; possible after v1. Updated NFR-OS-3.
- **Resolved OQ-6** (plugin isolation → D23): out-of-process scanning only; hosting in-process.
  Updated NFR-REL-4, §5.5, §10.6, Epic 7 deliverables/acceptance.
- **Resolved OQ-8** (VST2 → D24): do not ship VST2 support. Updated NG4, FR-MIX-5, §5.1, §5.2,
  §5.5, §12 D11.
- **OQ-7** (code-signing/notarization) remains deferred — revisit after MVP/user growth.
- **OQ-2/3/9** remain open technical items for agents in Epic 4/0/1 respectively.
- Restructured §10.9 into §10.9.1 resolved / §10.9.2 open / §10.9.3 deferred; added D21–D25 to the
  decisions baseline; bumped document version to 1.1.0.

### [1.0.0] — 2026 — Initial specification

- Initial authoring of the `raw-radio-studio` technical/product specification.
- Documented settled baseline decisions (D1–D20), non-goals (NG1–NG8), architecture, licensing,
  build/CI/release plan, Epics 0–7 with acceptance criteria, risks, and open questions.
- Status: **living document**; changes via PR.

---

*End of specification.*
