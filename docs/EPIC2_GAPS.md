# Epic 2 — open gaps (explicit)

> Status: **Epic 2's one hard gap — software cue mixes (FR-MON-3/FR-MON-4) —
> was CLOSED in Epic 3.** This file is kept as the historical record of what was
> deferred and why, and of the Epic 2 *targets* that remain open. It is not a
> substitute for the epic acceptance criteria in
> [`STUDIO_SPEC.md`](STUDIO_SPEC.md) §9.
>
> See also [`EPIC3_GAPS.md`](EPIC3_GAPS.md) for the Epic 3 items still open.

## Delivered in Epic 2

- Device-agnostic per-track input mapping (device channel(s)/mono/stereo/N) —
  FR-REC-3 — with mixer-strip UI, persistence, and a headless persistence test.
- Multitrack recording + overdub (1..N tracks) — FR-REC-1/2/6 — verified by the
  measured 4-in / 4-track integration test (hosted device).
- Basic mixer: fader/pan/mute/solo per track **and** master fader/pan/mute,
  peak + clip metering — FR-MIX-1/3 — measured in the integration tests.
- Count-in / metronome — FR-REC-10 (target) — measured, and excluded from WAV
  export.
- UI can arm every track (mixer strips + clickable lanes) and map every track's
  input, so the "record 4 tracks from 4 inputs" acceptance is reachable from the
  app.

## CLOSED in Epic 3 — software cue mixes (FR-MON-3 / FR-MON-4, **hard**)

> **Resolution (Epic 3).** Software cue mixes are **implemented**: the Epic 3
> routing foundation (FR-MIX-2) landed, and `Session::createCueMix` builds an
> aux-return bus per cue, routed to its **own hardware output pair** when the
> interface exposes one, with per-source **post-fader aux sends** (independent
> enable + level); the control-room/master mix stays independent. Verified
> headlessly — two sources sent to two cues at different levels produce
> **distinct, independent** per-device outputs (measured ~0.5 and ~0.125), and
> each cue was cross-checked with an independent `RenderSpecification` render of
> its return bus. The **Epic 2 acceptance criterion "two independent cue mixes
> are audible and distinct" is therefore met.** Remaining caveats (basic cue-mix
> UI, on-hardware multi-output verification) are tracked in
> [`EPIC3_GAPS.md`](EPIC3_GAPS.md). The text below is the original deferral
> record, kept for history.

**What was missing.** The spec requires **per-performer software cue mixes**
(vendor mixers are unavailable on Linux): each performer hears a distinct mix of
the armed/live tracks plus the backing material. At the time, only a single
shared monitor path existed (`InputDevice::MonitorMode` onto the main output);
there was no send/return matrix and no independent output-to-device routing per
cue.

**Why it was deferred.** It is a routing/architecture item (it needs the
bus/send/return foundation that Epic 3 introduces, FR-MIX-2: "sends — at least
one aux/cue send"). Bolting an ad-hoc cue path onto the track → master graph now
would be thrown away when the routing layer lands.

**Planned approach (Epic 3, with the routing foundation) — as executed.**

1. Add one or more **AuxSend** points per source track (post-fader by default),
   each with an independent send level and enable.
2. Add a matching **AuxReturn** bus per cue; the return carries the summed sends
   for one performer/cue and any cue-specific processing.
3. Route each **AuxReturn to a distinct hardware output pair** ("output to
   device") so two performers on a multi-out interface get genuinely distinct
   mixes. With a stereo-only interface the cue return falls back to the main
   output and cues are not physically separable — this must surface in the UI.
4. Take care that per-cue **monitor latency** stays within the < 10 ms target
   (FR-MON-2/NFR-P-3); measure with a loopback, not by ear.

**How it will be verified.** Tracktion Engine exposes the render graph through a
`RenderSpecification`; a cue mix is correct when the graph, rendered per output
device, contains the expected send→return→device path and the measured
level/latency on each cue output matches the configured send levels. The
verification will be an offline/integration test (per-device RenderSpecification
render, comparing cue outputs to the expected sums), because no two independent
performers can be present in CI/hardware self-tests.

## Other Epic 2 items still open

- **Punch-in/out and loop-recording** (FR-REC-7, target) — not implemented.
- **RMS metering** (FR-MIX-3 asks peak/RMS) — only peak + clip are shown; RMS is
  a display item, deferred.
- **Hardware acceptance on a true 4-in interface** (D21) — the 4-in multitrack
  test runs against a hosted device; the on-hardware run is pending a
  multichannel interface. `raw-radio-studio --selftest-multitrack <s> <out>`
  exists for that run.
- **16-track / no-xrun / < 10 ms latency** (targets) — need the reference
  machines and interface for measurement (Epic 7).
- **Master solo** — intentionally omitted: the master bus has no sibling to
  isolate, so a master solo control would be a no-op. Master fader/pan/mute are
  provided (FR-MIX-1).
