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

## Input routing & recording semantics — agreement & task graph

> Status: **owner-agreed fix package, not started.** Frozen after the live retest of phases 1a / 1b / 2
> (input mapping, track removal, waveform). It **extends R-A…R-F** and is the basis for the
> routing-and-recording-semantics work: changing the input invariant is the **largest intrusion into
> the audio core** of this whole effort, so it is done first, deliberately, and behind unit tests.
> The graph is ordered head-first: **routing + deletion blockers → waveform view → LV2 → live wave
> growth.** Every live-audio check uses the ALSA loopback runbook in
> [`DEV_AUDIO_LINUX.md`](DEV_AUDIO_LINUX.md); all builds/tests run on the remote Linux machine
> (`~/projects/raw-radio/`, screenshot display `Xvfb :99`) per [`EPIC7_GAPS.md`](EPIC7_GAPS.md).

### What is wrong (verified `file:line`)

1. **Cross-track target clobbering.** `configureTracks()` calls
   `setTarget(track->itemID, /*move=*/true, …)` (`src/studio/Session.cpp:1038`); with `move=true` the
   engine **removes every other target** on that instance
   (`third_party/tracktion_engine/modules/tracktion_engine/playback/devices/tracktion_InputDevice.cpp:108-112`),
   so a wave input backing several tracks keeps only the last-assigned one.
2. **Silent device fallback.** `resolveInputDeviceFor` (`src/studio/Session.cpp:528`) clamps to a
   device index when no device carries the requested channel — a channel that is not on the map is
   still assigned. Callers assume non-`nullptr` (`:758`, `:1280`, `:590`, `:4804`).
3. **Unbounded track creation.** `addAudioTrack` (`src/studio/Session.cpp:1059`) never refuses when
   the device has fewer channels than the tracks requested.
4. **Channel menu lists positions, not the device.** Both menus enumerate `0..getNumInputChannels()-1`
   with no occupancy marking (`src/ui/MainComponent.cpp:3134`, `src/ui/MixerPanel.cpp:651`).
5. **Grouping is incidental.** The one-device-per-channel split is a side effect of
   `maybeSplitInputGrouping` (`src/studio/Session.cpp:909`), driven by a "duplicate device" heuristic,
   not by the track layouts.
6. **No per-track removal UI.** `Session::removeAudioTrack` (`src/studio/Session.cpp:1126`) removes
   **any** track, but the UI offers only the action-row button (selected/last) and has no right-click
   menu (`MainComponent.cpp:358`, `:2642`).
7. **Device rebuild re-applies only the channel config.** `timerCallback`
   (`src/studio/Session.cpp:4793-4824`) re-applies the `ChannelConfiguration` but not the
   grouping/targets after a device-config change.
8. **The take clip appears only at stop.** The engine inserts the clip on `stopRecording`
   (`third_party/tracktion_engine/.../playback/devices/tracktion_WaveInputDevice.cpp:886-903`), so
   `getClips()` returns nothing while the take rolls — the wave cannot grow.

### Agreed decisions (I1…I12)

- **I1 — One track = one `WaveInputDevice`/channel or one stereo pair.** A device backs a track 1:1;
  targets are never clobbered. The grouping is **derived from the track layouts**, not incidental.
- **I2 — No silent fallback.** A channel the device does not expose is not assigned (no clamped
  substitute device).
- **I3 — Do not create tracks beyond the device's channels.** Refuse with a clear message.
- **I4 — The Channel menu shows the device's real channels and marks/disables occupied ones.**
- **I5 — `Mono` / `Stereo` are working layouts; defaults are 2 channels → stereo, ≥4 channels → mono
  per channel.** A `Mono` track writes a **1-channel file**. The default rule lives in **one place** —
  `InputMapping::defaultLayoutForDevice` (canonical owner; `resolveInputLayout` and
  `InputGrouping::planLayoutFor` both delegate to it). The **3-channel** case resolves to **Mono**
  (conservative completion between 2 → stereo and ≥4 → mono).
- **I6 — `mono→stereo` over a neighbour owned by another track is refused with an explanation.**
- **I7 — Track removal is available for any track via a right-click menu (gutter/lane) and a hotkey**,
  not only the last/selected one.
- **I8 — After a device rebuild, mappings and targets are re-applied** (not only the channel config).
- **I9 — Waveform normalisation by the clip's own peak + a linear/dB scale toggle** (pure, testable
  math in `src/ui/ClipWaveform.h`).
- **I10 — Stereo = two strips, mono = one; overlaid into a single strip when the lane is too short.**
- **I11 — A live take grows during recording** via a **synthetic live clip** built by `Session`
  (`start` = punch-in/record start, `end` = transport position, `sourceFile` = the active recording
  file), drawn through the existing `drawClip(..., live=true)` path with the engine's
  `RecordingThumbnailManager` thumbnail.
- **I12 — LV2 hosting is on by default** (`CMakeLists.txt:49-50`); the answer is not "opt-in".
  Test LV2 packages are installed on the build box and the scan is proven with a **plugin-browser
  screenshot**.

### Task graph

Legend: **Est** ∈ {S, M, L}; `depends on` lists hard prerequisites. `RT-*` = routing/removal,
`WV-*` = waveform view, `LV-*` = LV2, `LW-*` = live wave growth.

| ID | Task | Goal | Files (primary) | Est | depends on |
|----|------|------|-----------------|-----|------------|
| **RT-1** | Tracktion-free grouping + occupancy helpers | Pure helpers: given `{track, InputMapping}[]` + device channel count, derive (a) the desired wave-device grouping (1 device per channel or per stereo pair), (b) `channel → owning track` occupancy, (c) the per-device target list (no clobber). Unit-tested. | `src/studio/InputMapping.h` (or new `src/studio/InputGrouping.h`), `tests/test_studio.cpp` | **M** | — |
| **RT-2** | Drop the silent device fallback | `resolveInputDeviceFor` returns `nullptr` when no device carries the channel; audit + null-harden every call site. | `src/studio/Session.cpp`, `src/studio/Session.h` | **M** | RT-1 |
| **RT-3** | One device per channel/pair; no target clobbering | **Core change.** Replace incidental `maybeSplitInputGrouping` with layout-derived grouping; assign with `setTarget(..., /*move=*/false, index)` so a shared device keeps every track's target; **never regroup while recording**. | `src/studio/Session.cpp` (`configureTracks`, `maybeSplitInputGrouping`, `applyInputChannelConfiguration`), `src/studio/InputRouting.h` | **L** | RT-1, RT-2 |
| **RT-4** | Block tracks beyond the channel count | `addAudioTrack` refuses (clear message) when no free hardware channel remains. | `src/studio/Session.cpp`, `src/studio/Session.h` | **S** | RT-1 |
| **RT-5** | Mono/Stereo layouts + 1-channel mono takes | Working `Mono`/`Stereo`; defaults 2 ch → stereo, ≥4 ch → mono-per-channel; a `Mono` track writes a **1-channel** WAV. | `src/studio/InputMapping.h`, `src/studio/Session.cpp`, `src/studio/InputRouting.h`, tests | **M** | RT-3 |
| **RT-6** | Refuse mono→stereo over an occupied neighbour | Pure conflict check + refusal with an explanation (no silent overwrite); surface the message in the UI. | `src/studio/Session.cpp` (`setTrackInputMapping`), `InputMapping.h`/`InputGrouping.h`, `MainComponent.cpp`, `MixerPanel.cpp` | **S/M** | RT-1, RT-5 |
| **RT-7** | Re-apply mappings **and** targets after a device rebuild | `timerCallback` re-runs the full assignment (grouping + config + targets) when the device changed — change-gated, never mid-take. | `src/studio/Session.cpp` (`timerCallback`, `configureTracks`) | **M** | RT-3 |
| **RT-8** | Channel menu: real channels, occupied marked/disabled | Shared occupancy provider feeds the gutter popup and the mixer popup; occupied channels are disowned or labelled with the owning track. | `src/ui/MainComponent.cpp` (`showLaneInputMenu:3130`), `src/ui/MixerPanel.cpp` (`showInputMenu:650`), `MainComponent.h`, `MixerPanel.h` | **M** | RT-1 |
| **RT-9** | Right-click track menu + any-track removal | Right-click on gutter/lane opens `Remove track` + the input-mapping submenu; a hotkey mirrors it; keep the action-row button. | `src/ui/MainComponent.cpp` (`mouseDown:2642`, `keyPressed:2912`, new `showTrackContextMenu`), `MainComponent.h` | **M** | RT-8 |
| **WV-1** | Waveform scale math (normalise + linear/dB) | Pure `ClipWaveform.h`: scale bins by the clip's own peak; linear↔dB mapping; unit-tested. | `src/ui/ClipWaveform.h`, `tests/test_studio.cpp` | **M** | — |
| **WV-2** | Wire normalisation + linear/dB toggle | `drawClip`/`paint` use the helpers; a compact persistent toggle in the edit row. | `src/ui/MainComponent.cpp` (`drawClip:3455`, `paint`), `MainComponent.h` | **M** | WV-1 |
| **WV-3** | Stereo = 2 strips, mono = 1 | Two strips for stereo, one for mono; overlay into one strip when the lane is too short. | `src/ui/MainComponent.cpp` (`drawClip`), `ClipWaveform.h`, `TrackLaneLayout.h` | **M** | WV-1, WV-2 |
| **LV-1** | LV2 default-on | Flip `RAW_RADIO_STUDIO_ENABLE_LV2` to `ON`, keep the opt-out, document it. | `CMakeLists.txt:49-50`, `README.md` / `DEPENDENCIES.md` | **S** | — |
| **LV-2** | Prove the LV2 scan (evidence) | Install test LV2 packages on the build box; scan; attach a **plugin-browser screenshot** (`Xvfb :99`). | evidence (screenshot + note); `EPIC3_GAPS.md`/`DEV_AUDIO_LINUX.md` if recorded | **S** | LV-1 |
| **LW-1** | Synthetic live clip from `Session` | While recording, expose a `ClipInfo` (`start` = record/punch start, `end` = transport position, `sourceFile` = active file) so `getClips()` returns the growing take. | `src/studio/Session.cpp` (`getClips` / new `getLiveClip`), `Session.h`, tests | **S/M** | — |
| **LW-2** | Draw the growing live take | Render the synthetic clip via `drawClip(..., live=true)` with the live thumbnail; de-dupe against the engine's clip once it is inserted at stop. | `src/ui/MainComponent.cpp` (`paint`, `drawClip`), `MainComponent.h` | **S** | LW-1, WV-2 |

**Order note.** Head order is RT → WV → LV → LW. `LV-1` is dependency-free and may be done in
parallel from the start (it is sequenced after WV only to keep the commit train linear); `LV-2` needs
the remote box. `LW-1`'s pure span math can be written before WV-2 but `LW-2` needs WV-2's drawing.

**RT-4 model (owner-agreed).** Track creation is **never blocked**: input is an
attribute assigned later. A track with no free hardware channel is created in the valid
**`InputLayout::None`** state ("No input", name `Track N`) — not with an off-device channel. Two
states are distinct: **unassigned** (`None`; no error; assign later) vs **invalid** (a channel/pair
beyond the device, or a *different* span overlapping an owned one → `Session::getTrackInputProblem`
with a concrete reason). **Sharing an identical span** (same `firstChannel` + same `need`) is allowed:
several tracks on one channel/pair is valid (the device channel config is identical); the device is
allocated per **span**, so sharing tracks share one `WaveInputDevice`.

**Known limitations (documented, deferred).**

- **Shared span = one device gain.** The record trim (FR-REC-4) is a property of the
  `WaveInputDevice` (applied on the audio thread before monitoring/recording), so when several
  tracks share one span the trim of the *representative* track wins — a second sharing track's trim
  has no effect. Per-track gain would need a per-track gain stage before the input node (a semantic
  change), so it is knowingly deferred.
- **Channel-menu occupancy marking** — **delivered (RT-8)**: both popups (gutter + mixer) are built
  from one occupancy source (`Session::getInputChannelMenu` → `rrs::input_menu::buildChannelMenu`),
  listing every real device channel and marking it free / shared (identical span, RT-4) / used
  (another span, disabled, named owner) / off-device (hinted). The mixer chips are unchanged — they
  read the planner's per-track problem (`getTrackInputProblem`), which now shares the **one** span
  rule with the menu (`checkSpanOccupancy`), so a mark and a chip can never disagree. Occupancy follows
  the tracks' **saved (intended)** mappings, not only the applied layouts (documented + tested).
- **`Stereo` on a device too small for a pair** degrades to a 2-channel centred `L == R` config in
  `mappedChannelsFor`. This is **unreachable via `configureTracks`** (the planner flags the span as
  off-device and the track gets no input/config), but is documented so the fallback is not mistaken
  for the RT-5 mono path.
- **A failing input pass retries at the timer rate (RT-7, pre-existing, accepted).** When
  `configureTracks` cannot succeed — no device open, or every input track unassigned (`None`) — it
  returns false, so `inputsConfigured` stays false and the 10 Hz timer calls it again on every tick.
  Nothing is mutated, saved or restarted in that state (the work lives behind the success path, which
  the `getInputApplyCount()` diagnostic gates and a test pins), so the cost is a planner run on the
  message thread; left as-is rather than adding a back-off.

**Delivered (RT-6 / RT-8 / RT-9).**

- **RT-6 — refuse `mono→stereo` over an occupied neighbour.** `Session::canSetTrackInputMapping`
  rejects a **2-channel** span (a pair) whose channels are owned by a *different* span, with the
  owner named (the wording comes from `describeInputProblem`, so the refusal and the per-track
  problem read the same). `setTrackInputMapping` calls it **before** writing, so a refusal leaves the
  mapping untouched (no silent overwrite). Two deliberate non-refusals: an identical span is shared
  (RT-4), and a mono/multi span overlapping an owned channel stays writable so the planner reports it
  as a per-track problem (RT-2b pin-invalid — that behaviour is preserved).
- **RT-8 — the Channel menu.** One pure model (`rrs::channelMenuModel`) + one builder
  (`src/ui/InputMenu.h`) used by both popups; the mark is the same `checkSpanOccupancy` decision as
  the write guard, so the menu and the guard can never disagree. `Free` / `Shared "…"` (selectable) /
  `Used "…"` (disabled, owner named) / off-device (hint). A `None` track still ticks nothing.
  The **result decoding lives in `src/ui/InputMenuModel.h`** (`decodeResult` → `MenuSelection` →
  `applySelection`): one decoder, one mapping update for both popups, with `static_assert`s keeping
  the channel ids (100..199) and layout ids (200..203) disjoint — the B1 bug was a hand-rolled
  `id >= channelItemBase` test that read a Layout id as a channel (`firstChannel = 101..103`).
  The single span rule is `checkSpanOccupancy`: the planner (`planInputLayout`), the RT-6 guard and
  the menu all decide through it (the M3 invariant test pins planner ↔ occupancy agreement).
- **RT-9 — right-click track menu + any-track removal.** Right-click on a lane/gutter opens
  *Remove track (Cmd/Ctrl+Delete)* + *Assign input* + *Layout*; the same removal is on the hotkey
  **Cmd/Ctrl+Delete** (plain Delete stays "delete clip"; only Cmd+Z was taken on the command
  modifier, so there is no clash) and on the existing action-row button — all through one
  `MainComponent::deleteTrackAt`. Any track may be removed (middle, unarmed, …) except the last and
  the one whose take is rolling (`Session::removeAudioTrack` guards `isTrackRecording`).
- **RT-7 — full, change-gated re-apply after a device change.** The timer compares a two-part input
  **signature** — the wave-device count **and** the active input-channel count
  (`appliedWaveDeviceCount` / `appliedInputChannelCount` via the pure
  `rrs::inputSignatureChanged()`, `Session::timerCallback`). A real change drops `inputsConfigured`,
  so the next tick re-runs the whole pass (planner + channel configs + targets) in `configureTracks`,
  which records the new signature on success and bumps the `getInputApplyCount()` diagnostic. Both
  halves are needed and both are pinned by a unit test: a card can keep the same number of
  `WaveInputDevice`s while exposing a different number of active channels (2 → 8 → 2), which the
  device count alone would miss. When the signature is unchanged the gate stays shut: no planner run,
  no `setAllWaveInputsToNumChannels`, no `save`/`restartPlayback` (`getInputApplyCount()` is flat over
  ~15 steady ticks; one switch = one re-apply; several switches during one take collapse into one
  apply after the stop). While a take is rolling `configureTracks` returns early, so the change stays
  pending and lands right after the transport stops.
- **RT-7 guard family.** `setTrackInputMapping` no longer calls `restartPlayback()` while a take is
  rolling (the mapping is written and the apply is deferred like any other device change);
  `configureTracks` itself already refuses mid-take. **Known gap (documented, deferred):** other
  mutation paths (`addMidiTrack`, `convertTrackToMidi`, `importAudioFile`, `normaliseTake`, clip
  edits, plugin inserts) still call `restartPlayback()` unconditionally, so a take will not be
  protected if one of those is invoked mid-record — outside RT-7's scope, to be covered by the
  hardening pass (Epic 7).
- **Reproduce the live device-switch check.** The env-gated integration case `input routing: LIVE
  card switch FiFine <-> Loopback (RT-7, RRS_RT7_LIVE=1)` performs exactly the owner scenario on real
  hardware: it opens a narrow card (default `RRS_RT7_NARROW=fifine`, falling back to the built-in
  2-in codec), switches to a wide one (`RRS_RT7_WIDE=Loopback`), and prints the
  `track / layout / first / need / applied device / problem` table after each switch (run the
  integration test binary with `RRS_RT7_LIVE=1`). Measured: on the wide card the two off-device monos
  gained channels 2/3 while the stereo pair kept its pair config; back on the narrow card they became
  off-device again with a concrete reason and the unassigned track stayed unassigned. Known
  limitation (engine-level, not this code): opening a physically different card mid-take tears the
  audio device down, which aborts the take; the app's startup recovery still finds the orphaned file.

### Commit & review sequence

Each unit = **review (`code-reviewer`) → fixes → one commit** (conventional message, **DCO `-s`**,
direct push to `main`; leak-scan before commit — the repo is public). A commit lands only when
`ctest` is green (remote Linux); no per-phase tags (R-decision above).

1. `feat(input): Tracktion-free input grouping + occupancy helpers` — **RT-1** (+ tests). **Carries
   this document section** (the planner does not commit it; the first task does).
2. `fix(input): drop the silent device fallback in resolveInputDeviceFor` — RT-2.
3. `refactor(input): derive wave-device grouping from track layout` — RT-3a (grouping only).
4. `fix(input): never clobber another track's input target` — RT-3b (targets). RT-3 is split because
   it is the risky core; each half is independently revertible and green.
5. `feat(input): block input tracks beyond the device channels` — RT-4.
6. `feat(input): mono/stereo layouts and 1-channel mono takes` — RT-5.
7. `fix(input): refuse mono→stereo over an occupied neighbour` — RT-6.
8. `fix(input): re-apply mapping and targets after a device rebuild` — RT-7.
9. `feat(ui): channel menu shows real channels and marks occupied` — RT-8.
10. `feat(ui): right-click track menu and remove any track` — RT-9.
11. `feat(ui): clip-waveform normalisation and linear/dB scale` — WV-1 + WV-2 (split if the diff is
    large: math-only commit, then wiring).
12. `feat(ui): stereo/mono waveform strips` — WV-3.
13. `build(plugins): enable LV2 hosting by default` — LV-1 (+ the LV-2 evidence note/screenshot).
14. `feat(ui): the live take grows during recording` — LW-1 + LW-2.

**Review emphasis.** Commits 2–4 (RT-2/RT-3) get an **audio-core review**: the reviewer must check
every `resolveInputDeviceFor` caller for null and confirm `configureTracks` can never leave a track
without a target or regroup during a take. Commit 6 (RT-5, mono file format) gets a **data-format
review** (recovery/import path). The rest are ordinary UI reviews.

### Risks & rollback points

| Risk | Impact | Detection | Rollback |
|---|---|---|---|
| **Target/move semantics (RT-3)** | A track silently loses its input, or recording writes to the wrong/no track → "track without input", unusable takes | Unit: grouping yields the expected targets; live: arm 2 tracks, record, each file non-empty and on its own channel; the lane input chip shows a device for every armed track | `git revert` 4 (and 3 if grouping is implicated) |
| **`nullptr` fallback removal (RT-2)** | A caller that assumed a device now dereferences null → crash / no monitoring | Compile + unit + null-audit of the four call sites; live monitoring still works | Revert 2 (with 3) |
| **1-channel mono WAV (RT-5)** | Engine/recovery/import mishandle a mono take; old stereo mappings change meaning | Live mono record → file channel count == 1; recovery/import integration | Revert 6 (keep the mapping change, drop the file-format change) |
| **Regrouping mid-take (RT-3/RT-7)** | A device-list rebuild during recording stops/skips the take (the EPIC 7 flake's failure mode) | Guard: configure/timer never regroup while `isRecording()`; live: start a take, change nothing, confirm the file is complete | Revert 3/8 |
| **Timer re-apply thrash (RT-7)** | `restartPlayback()` every tick → dropouts | **Closed**: the gate compares the device-count + active-channel signature, so a steady state does no work at all (`getInputApplyCount()` is flat over ~15 ticks; one switch = one apply) — pinned by the `input signature` unit test and the no-storm integration test | Revert 8 |
| **Normalisation hides level (WV-1/2)** | Clipping or a loud-looking near-silent take; dB confusion | Pure-math tests (unity, −∞, peaks); a screenshot review by the owner | Revert 11 |
| **Live synthetic clip duplication (LW)** | Two clips drawn for one take at stop | De-dupe by `sourceFile`; live check: the take appears once when the transport stops | Revert 14 |
| **LV2 default-on (LV-1)** | Wider scan surface; `.dmg`/`.deb` format set changes; a broken bundle affects the scan | Live scan + browser screenshot; CI build (formats present in `getHostedFormatNames`) | Revert 13 |

### Verification plan

**Unit tests (add to `tests/test_studio.cpp`; names indicative).**

- Grouping/occupancy (RT-1): `"input grouping: one device per channel on a 4-in interface"`,
  `"input grouping: a stereo pair keeps two channels on one device"`,
  `"input grouping: default is stereo for 2 channels and mono-per-channel for >=4"`,
  `"input grouping: occupancy reports each channel's owning track"`.
- Fallback (RT-2): `"input mapping: no device is resolved when the channel is off the map"`.
- Layouts (RT-5): `"input mapping: a Mono track requests a one-channel configuration"`.
- Conflict (RT-6): `"input mapping: mono->stereo is refused over an occupied neighbour"`.
- Waveform (WV-1): `"clipwave: normalise scales bins by the clip peak"`,
  `"clipwave: the linear and dB mappings agree at unity"`,
  `"clipwave: stereo splits into two strips, mono one, and collapses below the height floor"`.
- Live clip (LW-1): `"session: the live clip spans record start to the transport position"`.

**Live checks (remote Linux, ALSA loopback — `scripts/dev/alsa-loopback.sh`).**

- `--tone 60` + record **one mono track** → non-empty **1-channel** file; **two tracks** → two files,
  each on its own channel, neither clobbered.
- **Default layout**: a 2-ch device → stereo; a ≥4-ch device → mono per channel.
- **Occupancy**: the Channel menu marks the channels already used.
- **RT-6**: attempt `mono→stereo` over an occupied neighbour → refusal message.
- **RT-7/I8**: change the input device (or replug) → mappings/targets re-applied, no "track without
  input".
- Whole suite green: `ctest` unit and integration counts grow, **ASan gate green**
  (`ctest --test-dir build-asan`), plus the pre-existing MIDI/count-in flake recognised as unrelated.

**Screenshots (`Xvfb :99`).**

- Plugin browser listing installed LV2 plugins (**LV-2 evidence**).
- Waveform states: idle / selected / loop / **live-growing**; **linear vs dB**.
- Channel menu with occupied channels disabled; right-click track menu (Remove track).

### Open questions (verify live — do not assume)

- **Real 2-channel interface grouping.** Does JUCE/Tracktion present one stereo `WaveInputDevice` or
  two mono devices for a physical 2-in interface? This decides the I5 default mapping.
- **`setTarget` for a shared device.** Whether `move=false` **plus** an explicit `index` is the
  correct call to keep several targets on one instance (and what `index` must be).
- **1-channel capture — RESOLVED (RT-5).** A `Mono` mapping now uses a distinct single-channel
  `ChannelConfiguration` (`mappedChannelsFor` returns one `centre` channel; Tracktion sizes the
  recorded file from the config's channel count), so a mono track writes a **true 1-channel WAV**.
  The mono→stereo centring on playback/monitor is done by the engine's channel conversion
  (`ChannelMap::conversion (1, 2)` = duplicate to L+R, used by `ChannelRemappingNode`), not by
  duplicating to L+R at the config; it is guarded deterministically by
  *"a mono source is upmixed centred (measured L == R) on a stereo track (RT-5)"* (a headless
  host-device render of the live monitor proved order-dependent/unreliable, so the guard exercises
  the conversion rule itself). Recovery/import accept 1-channel takes (covered by the mono
  `WavRepair` and orphan-import tests). The old "duplicate to L+R" `centredMono` survives only as
  the (planner-unreachable) fallback for a `Stereo` request on a device too small for a pair.
- **Loopback as a stand-in.** The `snd-aloop` card exposes two full-duplex PCM devices with eight
  subdevices each — confirm the "active input channels" count it presents, so the grouping tests use
  a valid configuration.
- **Back-compat migration.** Opening a pre-change session (single `Auto` track) after the I5 default
  change — does the stored mapping migrate cleanly, or does it need a migration step?
- **Layout-refusal surface (RT-6) — RESOLVED.** The refusal is shown on the shared **status line**
  (both popups) and the refused layout is greyed out in the Layout submenu with "— taken by another
  track" (probed via `Session::canSetTrackInputMapping`), so the user sees the reason before choosing.
- **LV2 packages.** Which LV2 test packages exist on Ubuntu 24.04, their names, and what the plugin
  browser actually lists (for the evidence screenshot).

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

**Delivered** (Epic 8 phase 2 — `src/ui/ClipWaveform.h` (binning math),
`src/ui/MainComponent.{h,cpp}` `drawClip`/thumbnail cache, `src/studio/Session.{h,cpp}`
`ClipInfo::sourceFile`, `src/ui/BrandColours.h` `waveformIdle`).

**What was done.** The solid `brand::accent` clip fill is replaced by a **dimmed background
(`brand::clip` / `clipSelected`) + waveform**: the live take from
`Engine::getRecordingThumbnailManager()->getThumbnailFor(file)` (the engine writes peaks on the
audio thread — RT-safe), ordinary clips from a cached `te::SmartThumbnail` per source file. Files
come from `Session::ClipInfo::sourceFile` (filled from `WaveAudioClip::getOriginalFile()`).
**Live detection is clip-level**: a clip is the take only when its `sourceFile` equals
`Session::getActiveRecordingFileForTrack(track)` (from `InputDeviceInstance::getRecordingFile`) — a
pre-existing clip on an armed track is never mistaken for the take. Thumbnails are created/cached
off the paint path (message thread, LRU-evicted, pruned on session change); `paint()` only reads
pre-computed peaks (no rebuild, no allocation proportional to pixels).
Bins are 2 px, the wave is `brand::waveformIdle` (accent @55%) / `accent` when selected or live /
`accent` @80% for loops, with a `textTertiary`@30% centre line; a ≤ −60 dBFS window draws only the
centre line.

**Open / deferred (2):**

- **Clip hover outline** (spec §5.4 hover = `borderHover` contour) is **not** drawn: the
  arrangement tracks hover only for the lane gutter controls, not for individual clips.
- **Fine (1 px) hi-res waveforms** — we bin at the spec's 2 px step; no sub-pixel/hi-res mode.
- Live/hardware waveform verification (a real take growing) needs an audio device — not possible
  in the headless ALSA-broken CI environment.

**Estimate:** **S/M** (as planned).

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
- §«Input routing & recording semantics — agreement & task graph» (this file) — the owner-agreed
  input-invariant / waveform / LV2 fix package and its task graph.
- [`DEV_AUDIO_LINUX.md`](DEV_AUDIO_LINUX.md) — the ALSA-loopback runbook used for the live
  routing/recording checks above.
- [`EPIC7_GAPS.md`](EPIC7_GAPS.md) — the ASan-before-release gate, the integration-suite flake, and
  the remote-Linux build/verification setup this epic reuses.
- [`EPIC4_GAPS.md`](EPIC4_GAPS.md) — snap/grid `[target]` deferred from Epic 4 (Phase 4 here).
- [`EPIC2_GAPS.md`](EPIC2_GAPS.md) — punch-in/out / loop-record `[target]` deferred from Epic 2
  (Phase 5 here).
