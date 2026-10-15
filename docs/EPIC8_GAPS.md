# Epic 8 — open gaps (explicit)

> Status: **Epic 8 in progress — phases 1a and 1b delivered.** Phase 1a (device panel → left
> collapsible sidebar; manual mixer collapse; lane-height floor 56 px + vertical scroll + 56/72/96
> preset) and phase 1b (in-lane arm/mute/solo + input selector + mini level indicator, hotkeys) are
> implemented on `main`; phases **2 / 3 / 4 / 5 are not started**. This file records the
> owner-agreed, phase-ordered plan and what is still open in each phase, so the epic is not silently
> marked done as it is implemented. It is not a substitute for the epic definition in
> [`STUDIO_SPEC.md`](STUDIO_SPEC.md) §9 (Epic 8 — Arrangement UX).
>
> The detailed interaction design is being authored separately in **`docs/EPIC8_UX_SPEC.md`**
> (a companion plan — it may not exist yet when this file was written). Where this file says
> "see the UX spec", it points at that companion document.

## Scope (from the spec)

[`STUDIO_SPEC.md`](STUDIO_SPEC.md) §9 «Epic 8 — Arrangement UX» defines **five phases with a
mandatory order** (1a → 1b → 2 → 3 → 4 → 5):

- **1a** — device panel → left collapsible sidebar (accordion Device / Channels / Status;
  resizable width; remembered collapse state); manual mixer-collapse button (auto-hide kept as a
  fallback); minimum lane height 56 px + vertical scroll + global lane-height preset
  (56 / 72 / 96).
- **1b** — in-lane controls: arm / mute / solo + input selector + mini level indicator; hotkeys;
  click-to-arm retained.
- **2** — waveforms: live take from the engine's `RecordingThumbnailManager`, clips via
  `te::SmartThumbnail`; drawn instead of the solid accent fill.
- **3** — status-bar diagnostics: latency + xrun + CPU%; input meters show only active channels
  in L/R pairs.
- **4** — timeline: pixels-per-second zoom (4–400 px/s), Ctrl+wheel + buttons; page auto-scroll
  only while playing; seconds-snap (off by default; 0.1 / 0.25 / 1 s).
- **5** — recording: loop-record first (transport loop in/out + `record()`), then punch-in/out
  (`edit->recordingPunchInOut = true`); pre-roll = existing count-in.

Related requirements: **FR-ED-1** (arrangement view), **FR-ED-7** (snap/grid `[target]`, deferred
in Epic 4), **FR-REC-7** (punch-in/out + loop-recording `[target]`, deferred in Epic 2/4),
**FR-REC-4** (per-track input meter), **FR-MIX-3** (per-track metering), **FR-MON-2 / NFR-P-3**
(latency read-out), **NFR-P-4** (xrun), **NFR-P-6** (UI responsive under load).

## Delivery / verification rules (epic decisions, owner-agreed)

- **No per-phase tags.** After **each phase**: push to `main`. The release (**`v0.1.4-alpha`**) is
  cut **at the end** of the epic; that release also **verifies, live, that
  `release.needs: [build, asan]`** holds ([`EPIC7_GAPS.md`](EPIC7_GAPS.md) — the ASan gate blocks
  publication).
- **Build + `ctest` on the remote Linux machine** (the fast cycle; GUI is run on its `X0`
  display). On **each phase**, a **CI `.dmg` artifact** is produced for the owner's **manual**
  check on macOS.
- **New unit tests only for pure math** — scroll offset, zoom mapping, lane geometry — no
  UI-automation tests.
- **Do not build on macOS** (the standing project prohibition; full JUCE/Tracktion builds paralyse
  the working Mac). The integration suite has a **historical flake** (root-caused and fixed once,
  see [`EPIC7_GAPS.md`](EPIC7_GAPS.md) §«Integration-test flake»); treat occasional red as
  pre-existing flake, not an Epic 8 regression, and re-check before blaming a phase.

## Design decisions (owner-agreed — R-A…R-F)

These close the questions the designer raised (see [`EPIC8_UX_SPEC.md`](EPIC8_UX_SPEC.md) §10
«Открытые вопросы»). They are **decisions, not options**. Where the UX spec still shows its
pre-decision draft (e.g. a compact preset of **40** px, or the height switcher living only in the
sidebar «Channels» section), **these decisions supersede it** — the UX spec is the design draft,
this file is the record of what was agreed.

- **R-A — Lane height.** The height presets are **56 / 72 / 96 px**, and the floor is **56**
  («compact» = 56, **not** 40). The «min 56 vs preset 40» conflict is resolved: values below 56
  are never used; when the window is too short, **vertical scrolling** kicks in instead of further
  compression.
- **R-B — Where the height switcher lives.** In the **right edge of the edit-row** (next to
  Undo/Redo/Split/…), as a **compact icon-dropdown**.
- **R-C — Loop region.** Defined by **dragging on the timeline ruler** (the top strip, 28 px),
  **not** on clips — the export region stays **`Shift`+drag on the lanes**, so the gestures do not
  collide.
- **R-D — Right `InputMeter` column (180 px)** stays **as is**; it shows **only active channels, in
  L/R pairs**.
- **R-E — CPU / xrun source.** Atomic counters in `AudioEngine` (RT-safe: `std::atomic<uint32_t>`;
  xrun from `juce::AudioIODevice::getXRunCount()` (wrapped), CPU from
  `te::DeviceManager::getCpuUsage()`), read on the **message thread**, with the status line updated
  at **30 Hz**.
- **R-F — Multichannel metering.** **Yes** — **only active channels, in L/R pairs**.

## Phase 1a — device panel → left collapsible sidebar; lane height + scroll

**Delivered** (Epic 8 phase 1a — `src/ui/SidebarPanel.{h,cpp}`, `src/ui/SidebarLayout.h`,
`src/ui/TrackLaneLayout.h`, `src/ui/MixerPanel.{h,cpp}`, `src/ui/MainComponent.{h,cpp}`,
`src/ui/DevicePanelLayout.h`).

**What was done.** The device panel moved from its dedicated top region into a **left collapsible
sidebar column** holding **accordion sections: Device / Channels / Status**; the column width is
resizable (default 320 / min 240 / max 420, rail 44) and its **collapse state + width + open sections
are remembered** across runs (`ui.sidebar*` in the existing `juce::PropertiesFile`). A **manual
mixer collapse** was added (header 22 px; the existing **auto-hide-the-mixer-by-window-height**
behaviour is retained as the fallback/priority). The device panel now reclaims the empty
`errorLabel` space when there is no error (320 → 224 px). Track lanes have a **minimum height of
56 px** with **vertical scrolling** (offset + one `ScrollBar` + `mouseWheelMove`; no `Viewport`) and
a global **lane-height preset** (compact **56** / normal **72** / large **96**, switcher at the right
edge of the edit row per **R-B**).

**Open / deferred (1a):**

- **Sidebar content scroll** (UX spec §3.3, wheel over the sidebar) is **not** implemented: the
  three sections fit inside the minimum 820 px window, so it was not needed for 1a.
- **Transport-row relayout / wrapping** is **not** done (deliberately — a separate task for **1b/4**).
  To avoid clipping the fixed ~700 px transport row at the 1100 px minimum window, the sidebar width
  is now capped by `sidebar::effectiveMaxWidth(windowWidth)` (= 360 px at 1100, instead of 420).
- The **device-panel compact path** (224 px with no error) is covered by a unit test only — the
  capture environment always surfaces an ALSA "no hardware device" error, so the 320 px path is what
  is exercised live.

**Integration points (as implemented).**

- Layout is no longer a single linear `MainComponent::resized()`: it is title (full width) →
  status + action rows (full width, bottom-anchored) → `mainRow` = sidebar + right work column.
- Device panel geometry/height: `src/ui/DevicePanelLayout.h` — `devicePanelHeight = 320` with an
  error, `devicePanelHeightCompact = 224` without (`devicePanelHeightFor`); the error label only gets
  space while an error exists.
- Mixer height: `MixerPanel::preferredHeight = 220`, `headerHeight = 22` (manual collapse).
- Lane geometry: `src/ui/TrackLaneLayout.h` — `uniformLaneHeight` floors at `minHeight = 56`,
  `computeLaneRects(area, numTracks, scrollOffsetY, preset)`, plus the pure scroll helpers
  (`maxScrollOffset`, `clampScrollOffset`, `visibleLaneRange`, `laneIndexAtY`).
- Still **no `Viewport`** in `src/ui/`: scrolling is a `scrollOffsetY` baked into the lane rects.

**Estimate:** **M** (as planned).

**UX spec:** `docs/EPIC8_UX_SPEC.md` (sidebar + presets design).

## Phase 1b — in-lane track controls

**Delivered** (Epic 8 phase 1b — `src/ui/TrackLaneLayout.h` control geometry,
`src/ui/MainComponent.{h,cpp}` paint/hit-test/paint-controls, `src/studio/Session.{h,cpp}`
`peekTrackMeter`).

**What was done.** A **control band ("gutter")** — the left `gutterWidth` (112) px of every lane —
holds **arm / mute / solo / input selector / mini level indicator**. The band is a *column* (every
lane shares the same x/width), **not** a per-lane offset inside the timeline: the x<->time map runs
over `track_lane::timelineAreaFor(trackLaneArea)` (the lane area with the gutter carved off the
left); its `hInset` (== `track_lane::laneInset` == `hMargin`) puts `0 s` exactly on
`track_lane::laneContentX(area)` (the gutter's right edge), and the ruler is trimmed the same way
so ticks and clips stay aligned. The band scrolls with the lanes (same `laneScrollOffsetY`, baked
into `trackLaneRects`) and is clipped to `trackLaneArea`. arm/mute/solo write the existing `Session`
state (single source of truth — the mixer reads the same state and both repaint via the Session
change message); the input selector reuses `Session::getTrackInputMapping` / `setTrackInputMapping`
with the same popup as `MixerPanel`.

**Metering (pump ownership, 1b follow-up).** The engine's level meters are **read-and-clear**, so a
single owner pumps them: **`MainComponent::pumpMeters()`** runs on the always-on ~30 Hz tick and calls
`Session::readTrackMeter(i)` / `readMasterMeter()`, publishing per-track + master readings. The mixer
strips and the lane mini-meters only **peek** them (`Session::peekTrackMeter` / `peekMasterMeter`).
This makes metering independent of the mixer's state (expanded / manually collapsed / auto-hidden /
`ui.mixerCollapsed=true` at startup) — collapsing the mixer no longer freezes the meters.

**Hotkeys:** **Space** (play/stop), **R** (toggle record), **A** (arm/disarm the track under the
cursor). They only act when `MainComponent` itself owns the keyboard focus (never when a
`juce::Button` is focused — the button already reacts to Space itself) and never while the piano-roll
overlay is open (so Space/R cannot start the transport/record mid-MIDI-edit). Auto-repeat is guarded
(a held key is ignored until released).

**MIDI / playback-only tracks (deliberate):** a MIDI/instrument track and an imported playback-only
track show **no input chip and no mini level meter** — they have no audio hardware input (MIDI tracks
still **arm** via their MIDI input; playback-only tracks show a disabled arm glyph). Their slot is
reserved so every lane's clip zone starts at the same x.

**Open / deferred (1b):**

- **Track selection concept** — the lane is still not a first-class "selected track"; `A` arms the
  track **under the cursor** (deterministic without it). Introducing a selected-track model is a
  later decision.
- **Right-click context menu** on the band (spec §4.7: Remove track / input mapping) is **not**
  implemented.
- The **96 px large preset** shows the full control set but does not yet use the reserved space for
  future `fx`/`send` buttons (that is a later epilogue, not 1b).

**Estimate:** **S/M** (as planned).

**UX spec:** `docs/EPIC8_UX_SPEC.md` (lane control strip).

## Phase 2 — waveforms

**Not started.**

**What to do.** Draw the **live take** from
`Engine::getRecordingThumbnailManager()->getThumbnailFor(file)` (the **engine writes peaks on the
audio thread** — RT-safe) and **ordinary clips** via `te::SmartThumbnail`. Draw the waveform
**instead of** the current solid `brand::accent` fill: dim the background, draw the waveform in
the brand colour.

**Integration points.**

- Today the "orange block" is a solid **fill** of `brand::accent`:
  `src/ui/MainComponent.cpp:1739-1740`; colour defined in `src/ui/BrandColours.h:31`.
- Arrangement painting is concentrated in one `MainComponent::paint()`
  (`src/ui/MainComponent.cpp:1638-1805`) — the waveform draw replaces the fill there.
- Thumbnails must be created/refreshed off the audio thread; only *reading* engine-written
  thumbnail data is RT-safe.

**Estimate:** **S/M.**

**UX spec:** `docs/EPIC8_UX_SPEC.md` (waveform styling).

## Phase 3 — runtime diagnostics (latency / xrun / CPU%) + active-channel meters

**Not started.**

**What to do.** Surface **latency (round-trip) + xrun + CPU%** in the status bar. Make input
meters show **only active channels, in L/R pairs** (today: hard-wired 2).

**Integration points.**

- **xrun:** `juce::AudioIODevice::getXRunCount()` exists in the pinned JUCE 9.0.3
  (`third_party/JUCE/modules/juce_audio_devices/audio_io/juce_AudioIODevice.h:356`); CoreAudio
  counts overloads — **we do not wrap it yet**.
- **CPU:** `te::DeviceManager::getCpuUsage()`
  (`third_party/tracktion_engine/modules/tracktion_engine/playback/tracktion_DeviceManager.h:64`).
- **Latency:** already computed — `AudioEngine::getEstimatedRoundTripLatencyMs()`
  (`src/studio/AudioEngine.h:135`, `:440`), currently displayed in `src/ui/DevicePanel.cpp:184`.
- **Metering:** the engine exposes per-track **and** per-channel levels
  (`LevelMeasurer::Client::getAndClearAudioLevel(ch)`); our hard-wired **2** is spread across
  `src/studio/Session.h:30-31` (`peak[2]` / `rms[2]`), `src/studio/Session.cpp:166,181,4643,4671,4704,4721`,
  `src/ui/InputMeter.h:49` (`std::array<ChannelState, 2>`), `src/ui/MixerPanel.cpp:169`.

**Estimate:** **S/M.**

**UX spec:** `docs/EPIC8_UX_SPEC.md` (status-bar read-outs).

## Phase 4 — timeline zoom + auto-scroll + snap

**Not started.**

**What to do.** **Pixels-per-second zoom (4–400 px/s)** via **Ctrl+wheel** and buttons;
**page auto-scroll only while playing**; **snap by seconds, off by default**, steps **0.1 / 0.25 /
1 s**.

**Integration points.**

- There is **no zoom and no scroll** anywhere in `src/ui/` today. `Timeline::xForSeconds` /
  `secondsForX` are **normalised to the whole timeline length** (`src/ui/Timeline.h:49,52`) — a
  zoom level requires introducing an explicit px/s scale. The audit counted **~19 call sites** to
  revisit (**7** in `src/ui/Timeline.cpp`, **12** in `src/ui/MainComponent.cpp`).
- Auto-scroll interacts with the linear `resized()` (`MainComponent.cpp:1859-1989`) and the
  painted (non-component) lane area.
- Snap applies to the same hit-test/edit paths as Phase 1b (clip drag/trim/split in
  `MainComponent`).

**Estimate:** **S/M.**

**UX spec:** `docs/EPIC8_UX_SPEC.md` (zoom/snap affordances).

## Phase 5 — loop-record, then punch-in/out

**Not started.**

**What to do.** Implement **loop-record first** (transport **loop in/out** + `record()`), **then**
**punch-in/out** (reuse the **same loop range** + `edit->recordingPunchInOut = true`). **Pre-roll =
the existing count-in** (FR-REC-10).

**Integration points.**

- **Punch is a flag, not a range API.** Tracktion Engine has **no `Edit::setPunchIn/Out`** — only
  `Edit::recordingPunchInOut` (`third_party/tracktion_engine/modules/tracktion_engine/model/edit/tracktion_Edit.h:885`).
  The in/out positions come from the **loop** range:
  `TransportControl::setLoopIn` / `setLoopOut` / `setLoopRange`
  (`third_party/tracktion_engine/modules/tracktion_engine/playback/tracktion_TransportControl.h:186-201`).
- Count-in / metronome already exist — `src/studio/Session.cpp:4116-4175` (the FR-REC-10 surface),
  re-asserted on the engine before rolling at `src/studio/Session.cpp:2284-2290`.
- No "loop-record" helper or UI exists yet; `Session` drives the transport today.

**Estimate:** **M** (loop-record) and **M–L** (punch-in/out).

**UX spec:** `docs/EPIC8_UX_SPEC.md` (transport loop/punch UI).

## Known implementation notes (audit — verified `file:line`)

These are the checked facts the phases are built on; they are **not** new decisions.

1. **Arrangement painting is centralised.** All arrangement drawing is in one
   `MainComponent::paint()` (`src/ui/MainComponent.cpp:1638-1805`); lane geometry is in
   `src/ui/TrackLaneLayout.h` (`uniformLaneHeight` `:47-54`, `computeLaneRects` `:58-75`;
   constants `preferredHeight = 72` `:36`, `vMargin = 6` `:42`, `hMargin = 8` `:39`).
2. **Clips are not components.** The "orange block" is a **fill** of `brand::accent`
   (`src/ui/MainComponent.cpp:1739-1740`; `src/ui/BrandColours.h:31`).
3. **No `Viewport` / scroll / zoom exists in `src/ui/`.** `Timeline::xForSeconds` /
   `secondsForX` are normalised to the full length (`src/ui/Timeline.{h,cpp}`); a zoom level
   touches **~19 call sites** (7 in `Timeline.cpp`, 12 in `MainComponent.cpp`).
4. **Hit-testing is spread** across `clipRectFor` / `hitTestClip` / `zoneForPoint` / `mouseDown` /
   `mouseDrag` / `mouseUp` (`src/ui/MainComponent.cpp:2004,2019,2031,2243,2320,2440`).
5. **Layout is linear.** `MainComponent::resized()` (`src/ui/MainComponent.cpp:1859-1989`); device
   panel **320 px** (`src/ui/DevicePanelLayout.h:20`) with **~96 px reserved for an empty
   `errorLabel`** (`src/ui/DevicePanelLayout.h:80-85`); mixer **220 px** (`src/ui/MixerPanel.h:28`).
6. **Diagnostics sources exist but are unwrapped.** `juce::AudioIODevice::getXRunCount()` is
   present in the pinned JUCE 9.0.3 (`juce_AudioIODevice.h:356`); CPU via
   `te::DeviceManager::getCpuUsage()` (`tracktion_DeviceManager.h:64`); latency already computed
   (`AudioEngine::getEstimatedRoundTripLatencyMs`, shown in `src/ui/DevicePanel.cpp:184`).
7. **Metering is 2-channel-hard-wired on our side.** The engine offers per-track and per-channel
   levels (`LevelMeasurer::Client::getAndClearAudioLevel(ch)`); our "2" is in
   `src/studio/Session.h:30-31`, `src/studio/Session.cpp:166,181,4643,4671,4704,4721`,
   `src/ui/InputMeter.h:49`, `src/ui/MixerPanel.cpp:169`.
8. **Punch is a flag.** TE has **no `Edit::setPunchIn/Out`** — only `Edit::recordingPunchInOut`
   (`model/edit/tracktion_Edit.h:885`) plus loop in/out (`TransportControl.h:186-201`).
9. **Baseline present:** metronome / count-in already exists (`Session.cpp:4116-4175`,
   `:2284-2290`); **no** zoom logic exists; there is **no `MenuBarModel`** — hotkeys go through
   `MainComponent::keyPressed` (`src/ui/MainComponent.cpp:2445-2475`).

## Open questions / verify live

These are unanswered until the relevant phase runs on hardware/GUI; they must not be assumed.

- **Does xrun actually grow on CoreAudio?** We do not wrap `getXRunCount()` yet; confirm the
  counter is meaningful (non-zero/rising under load) on the owner's Mac before showing it.
- **Does `audioDeviceError` reach us?** Verify an ALSA/CoreAudio failure actually surfaces through
  the current error path (FR-MON-5) rather than being swallowed, before wiring the status bar.
- **How many channels does a per-track `LevelMeter` expose?** Confirm the engine's per-track
  `LevelMeasurer` channel count vs our hard-wired 2, so Phase 3 meters read the right channels.
- **Input-meter gate.** Confirm when the input meter should be considered "active" (armed-only vs
  any signal) and that inactive channels are hidden without flicker.
- **Loop-record semantics in TE 3.5.0.** Determine exactly what the engine does with `record()` on
  a looped transport (each pass = a new take? overlaid?) before choosing the take model.
- **Punch behaviour.** With only `recordingPunchInOut` + loop markers, confirm the recorded region,
  the behaviour at loop boundaries, and whether pre-roll/count-in interacts correctly.
- **Snap threshold at zoom.** Decide at which px/s a 0.1 s snap step stops being usable (and
  whether the step list adapts to zoom) — drive it from the UX spec.

## Related

- [`STUDIO_SPEC.md`](STUDIO_SPEC.md) §9 (Epic 8 — Arrangement UX) and the related FRs in §3
  (FR-ED-1/FR-ED-7, FR-REC-4/FR-REC-7, FR-MIX-3) and NFRs in §4 (FR-MON-2/NFR-P-3, NFR-P-4,
  NFR-P-6).
- `docs/EPIC8_UX_SPEC.md` — companion interaction-design plan (may not exist yet).
- [`EPIC7_GAPS.md`](EPIC7_GAPS.md) — the ASan-before-release gate, the integration-suite flake, and
  the remote-Linux build/verification setup this epic reuses.
- [`EPIC4_GAPS.md`](EPIC4_GAPS.md) — snap/grid `[target]` deferred from Epic 4 (Phase 4 here).
- [`EPIC2_GAPS.md`](EPIC2_GAPS.md) — punch-in/out / loop-record `[target]` deferred from Epic 2
  (Phase 5 here).
