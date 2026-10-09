# Epic 5 — open gaps (explicit)

> Status: **Epic 5 is delivered, but not fully complete.** This file records the
> Epic 5 deliverables that are deliberately deferred or only partially met, so
> the epic is not silently marked done. It is not a substitute for the epic
> acceptance criteria in [`STUDIO_SPEC.md`](STUDIO_SPEC.md) §9.

## Delivered in Epic 5

- **Virtual instrument hosting (FR-MIDI-3)** — a dedicated *instrument slot* on a
  MIDI track, separate from the audio-effect insert chain.
  `Session::insertInstrumentPlugin` (external VST3/LV2/AU) and
  `Session::insertBuiltInInstrument` (Tracktion's built-in `4osc`/`sampler`).
  The effect insert still rejects instruments and the instrument slot rejects
  effects (mutually exclusive; see `studio/PluginSelection.h`). The plugin
  browser gained an **Effects / Instruments** kind selector.
- **MIDI input + routing (FR-MIDI-1/4)** — enumerate MIDI input devices,
  select/enable one (persisted on the Edit state), route it to a MIDI track and
  arm the track. `Session::getMidiInputDeviceNames`,
  `setMidiInputDeviceEnabled`, `setSelectedMidiInput`, `armMidiTrack`.
- **MIDI recording (FR-MIDI-1)** — arm a MIDI track + Record; the engine captures
  the incoming messages into a MIDI clip on the track. Arming, `isAnyTrackArmed`
  and record-pass clip muting cover MIDI tracks as well as wave tracks.
- **MIDI clips + piano-roll editing (FR-MIDI-2)** — `Session::createMidiClip` and
  `addMidiNote` / `moveMidiNote` / `resizeMidiNote` / `removeMidiNote` /
  `setMidiClipLength`, each one undoable transaction. The `PianoRollPanel`
  overlay edits one clip's notes on a beat×pitch grid (click to add, drag body to
  move, drag right edge to resize, Delete/Backspace or arrow keys).
- **MIDI + audio coexist / export (FR-EXP-1)** — MIDI tracks and audio tracks
  live in one Edit and render through the normal offline export.
- **`--selftest-midi`** — headless diagnostic that injects notes into a clip,
  renders through the built-in stand-in instrument and reports peak/RMS; fails on
  silence.

## Closed — QA hardening (commit `e183bfb`)

These issues found while exercising the default browser / routing paths are
**fixed** and no longer open:

- **Input + MIDI track exclusivity** — a track is either a hardware *input* track
  or a *MIDI* track, never both. Inserting an instrument on an input track now
  clears its input-track marker and hardware mapping and stops the wave input
  from targeting it (`Session::detachTrackFromHardwareInput`), removing the
  confusing dual state.
- **MIDI device listing** — `--list-devices` now enumerates MIDI devices,
  printing a **MIDI inputs** / **MIDI outputs** section alongside the audio
  backends (from the same `DeviceManager` the "MIDI in" selector uses).
- **Arm / route robustness** — **Arm** falls back to the first armable track
  when a playback-only backing track is selected (`MainComponent::armTargetTrack`);
  MIDI routing rebuilds the playback graph and flushes pending device updates
  before enumerating input instances (retried once), and switching the selected
  MIDI input drops the previous device's targets and record-enables.
- **Note clamp** — piano-roll add/move/resize now clamp notes to the clip bounds,
  so a note past the clip end can no longer be created (it would never sound).
- **Second-instrument rejection** — a track hosts at most one instrument; a second
  insert is rejected with a clear error instead of silently stacking, and the
  instrument is always inserted at the head of the chain.

## Open — no real MIDI controller has been exercised

**What is missing.** All MIDI capture verification is **synthetic**: the tests
use the hosted audio device's MIDI input and inject `MidiMessage`s block by
block (`EnginePlayer::process(input, midi)`), and a virtual MIDI device is
created to settle the device list. No physical controller has been plugged in.

**Why.** The development machine has no MIDI hardware. The code path is
identical — `MidiInputDevice::handleIncomingMidiMessage` dispatches to the same
`MidiInputDeviceInstance` whether the message came from a physical port or the
hosted/virtual input.

**Manual step.** Connect a class-compliant USB MIDI controller, run
`--list-devices` — it now prints a **MIDI inputs** / **MIDI outputs** section
(in addition to the audio backends) and should list the controller; the
equivalent in-app view is the **MIDI in** combo. Then arm a MIDI track, select
the controller, Record, play notes → a MIDI clip appears and the piano roll
shows the notes.

## Open — no real VST3/LV2/AU instrument was exercised

**What is missing.** Playback/render of a MIDI track is verified through
Tracktion's **built-in stand-in instruments** (`4osc`, and the sampler). An
external VST3/LV2/AU instrument has not been instantiated end-to-end.

**Why.** No third-party instrument is installed on the CI/dev machine, and
shipping one is excluded (NFR-L-5). The instrument slot uses the same
`ExternalPlugin` path as effects, which *is* exercised for hosting, state
save/restore and out-of-process scan (Epic 3).

**Manual step.** Insert a VST3 synth via Plugins → Instruments, draw notes in the
piano roll, press Play → it sounds; save/reopen the project → the instrument and
its state are restored.

## Open — MIDI editor is basic

**What is missing.** The piano roll has no velocity lane, no quantise, no
multi-select / marquee, no copy/paste, no scroll/zoom (the four-octave range is
fixed: lowest note 36 → C1, highest note 83 → B4), and no snap toggle (it snaps
to a 1/16 grid). Overdub/loop MIDI recording is available through the engine
transport only — there is no dedicated overdub toggle.

**Planned approach.** A velocity strip, a snap/quantise control, marquee select,
and vertical scroll/zoom. MIDI clip editing on the arrangement (move/trim/split
already work via the generic clip path) can be surfaced with a MIDI-specific
colour.

## Open — MIDI device list is populated asynchronously

**What is missing.** `DeviceManager` rebuilds the MIDI device list on a timer, so
a just-created device may not appear in `getMidiInputDeviceNames()` within the
same message-loop turn. The tests pump the message loop (and create a virtual
device) until the list settles; the UI's 10 Hz refresh picks it up shortly after.

**Impact.** Cosmetic: the "MIDI in" combo can lag a device hot-plug by up to a
few hundred ms. No audio-thread involvement.

## Related

- [`STUDIO_SPEC.md`](STUDIO_SPEC.md) §3.5 (FR-MIDI-1..4) and §9 (Epic 5
  acceptance criteria).
- [`EPIC3_GAPS.md`](EPIC3_GAPS.md) — plugin hosting / instrument-slot substrate.
- [`EPIC4_GAPS.md`](EPIC4_GAPS.md) — arrangement editor patterns reused by the
  piano roll.
