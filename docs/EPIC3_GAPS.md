# Epic 3 — open gaps (explicit)

> Status: **Epic 3 is delivered, but not fully complete.** This file records the
> Epic 3 deliverables that are deliberately deferred or only partially met, so
> the epic is not silently marked done. It is not a substitute for the epic
> acceptance criteria in [`STUDIO_SPEC.md`](STUDIO_SPEC.md) §9.
>
> The Epic 2 hard gap (software cue mixes, FR-MON-3/FR-MON-4) was **closed** by
> Epic 3; its history lives in [`EPIC2_GAPS.md`](EPIC2_GAPS.md).

## Delivered in Epic 3

- Plugin hosting — VST3 (default), LV2 (opt-in via `JUCE_PLUGINHOST_LV2=1`), AU
  (macOS only); no VST2 (D24), no CLAP (NG4) — FR-MIX-4.
- Out-of-process plugin scanning (`PluginScanHelpers::PluginScanChildProcess`);
  hosting stays in-process (D23) — FR-MIX-7.
- Plugin state save/restore with the project **and** named user preset
  management — FR-MIX-6.
- Flexible routing: per-track output assignment, submix folder tracks, and aux
  sends — FR-MIX-2.
- Software cue mixes — aux-return bus per cue routed to an independent hardware
  output pair, per-source post-fader sends — FR-MON-3/FR-MON-4 (closes the Epic 2
  hard gap).
- Export stems (one 24-bit WAV per clip-bearing track + master) — FR-EXP-3 — and
  region export — FR-EXP-2.

## Open — cue-mix UI is basic

**What is missing.** The cue-mix routing is functional, but the UI exposes only a
**single send control per cue** (enable + level). There is no **send matrix**
(sources × cues), no per-cue naming/colouring workflow, and no at-a-glance view
of which cue is routed to which hardware output pair.

**Why it is deferred.** The routing model and the headless verification are the
load-bearing parts (they satisfy FR-MON-3/FR-MON-4); the matrix UI is polish and
is best designed alongside the Epic 4 mixer/editor work rather than bolted on.

**Planned approach.** A grid/matrix view in the routing panel: rows = source
tracks, columns = cue returns, cells = send level; plus an explicit cue →
output-pair assignment column with a warning when the interface cannot physically
separate cues.

## Open — physical multi-output verification pending hardware

**What is missing.** Cue separation is verified **headlessly** (a 6-out hosted
device split into a main + two cue devices; per-device output measured as
distinct and independent). The **on-hardware** run — two performers hearing two
genuinely distinct cue mixes on a real multi-out interface — has **not** been
done, because the studio does not yet own a true multichannel (≥4-out) interface
(decision D21).

**Planned approach.** Run the existing headless cue test against the reference
interface when it arrives; confirm each cue appears on its configured physical
output pair and that cue monitor latency stays within the < 10 ms target
(FR-MON-2 / NFR-P-3), measured with a loopback (not by ear).

## Open — plugin crash-during-scan not tested with a real crashing plugin

**What is missing.** Out-of-process scanning is implemented and verified to
isolate a probe in a child process, but the Epic 3 acceptance criterion “a
deliberately crashing plugin during scan does **not** crash the host” has **not**
been exercised with a **real** plugin that crashes the scanner.

**Why it is deferred.** The CI/self-test environment has no reliable crashing
plugin binary; the deterministic stand-in plugins used for the hosting tests do
not crash.

**Planned approach.** Add a scan test with a small known-crashing VST3 (or a
purpose-built crashing probe) once one is available in the test environment;
assert the host survives and reports the plugin as failed. This overlaps with the
Epic 7 scanner-hardening pass.

## Open — FR-MIX-6 preset management is basic

**What is missing.** Named **user** presets are supported (save/load/delete of
the raw plugin-state blob under the app's preset folder), but there is **no**
factory-preset enumeration from plugins, no preset categories/tags, no
rename/overwrite-confirmation flow, and no per-preset metadata.

**Planned approach.** Extend `PluginPresets` to enumerate program names exposed
by hosted plugins (`AudioProcessor::getNumPrograms` / `getProgramName`) and
present them alongside user presets; add rename and category support.

## Open — region selection is transient (not persisted)

**What is missing.** The region export selection (FR-EXP-2) is **transient UI
state** — `Session` holds set/clear/has/get for it, but it is **not serialised**
with the project. Reopening a project loses the selected range.

**Planned approach.** Persist the selection in the Edit's ValueTree (or a
session setting) so it round-trips with the project, and add explicit
named-region/marker support when Epic 4 introduces markers (FR-ED-7).

## Related

- [`STUDIO_SPEC.md`](STUDIO_SPEC.md) §3 (FR-MON-3/4, FR-MIX-2/4/6/7, FR-EXP-2/3)
  and §9 (Epic 2/3 acceptance criteria).
- [`EPIC2_GAPS.md`](EPIC2_GAPS.md) — Epic 2 deferrals (software cue mixes now
  CLOSED in Epic 3) and the remaining Epic 2 targets.
