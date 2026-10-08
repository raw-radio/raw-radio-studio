# Epic 4 — open gaps (explicit)

> Status: **Epic 4 is delivered, but not fully complete.** This file records the
> Epic 4 deliverables that are deliberately deferred or only partially met, so
> the epic is not silently marked done. It is not a substitute for the epic
> acceptance criteria in [`STUDIO_SPEC.md`](STUDIO_SPEC.md) §9.

## Delivered in Epic 4

- Arrangement editing — **move / trim-start / trim-end / split / duplicate /
  loop / delete / mute** on the timeline, with an interactive arrangement view
  (clips drawn on track lanes; drag the body to move, the edges to trim, the top
  corners to drag fade handles) — FR-ED-1/FR-ED-2.
- Fades — per-clip fade-in/fade-out plus an **equal-power crossfade** between
  adjacent clips (the left clip is extended over the right and complementary
  convex fades are applied) — FR-ED-3.
- Comping — a **non-destructive master take** assembled on a new track from N
  stacked takes, as trimmed copies that reference the original files — FR-ED-4.
- Undo/redo — every edit operation is one `UndoManager` transaction; wired to
  the Undo/Redo buttons and Cmd/Ctrl+Z / Cmd/Ctrl+Shift+Z — FR-ED-6.
- Time-stretch / pitch-shift — **Signalsmith Stretch (MIT)**, pinned as a git
  submodule (tag `1.1.0`); stretch a clip's visible region to a target duration,
  with an optional independent pitch shift — FR-ED-5 (see OQ-2 below).

## Resolved — OQ-2 time-stretch library

**Signalsmith Stretch** is **MIT** (its bundled `dsp/` copy is also MIT) and is
therefore AGPLv3-compatible with no relink obligation; **SoundTouch (LGPL-2.1)**
was not needed. Pinned at tag `1.1.0` — the last self-contained revision (later
releases pull `signalsmith-linear` via FetchContent, which this project's
offline pinning avoids). Full rationale: [`../DEPENDENCIES.md`](../DEPENDENCIES.md).

## Open — snap / grid / markers / tempo map (FR-ED-7, target)

**What is missing.** There is no snapping to a grid, no user markers, and no
tempo map / time-signature editor. Edits land wherever the mouse drops them.

**Why it is deferred.** FR-ED-7 is a **[target]**, not a hard requirement; the
hard edit operations (FR-ED-1..6) are the load-bearing part and were delivered
first. A tempo map in particular needs a dedicated UX and touches the engine's
`TempoSequence`, which deserves its own pass.

**Planned approach.** Add a bar/beat ruler (the engine owns the tempo sequence),
a snap toggle (off / grid / clip edges), and a marker lane with add/rename/seek.
The existing region selection (FR-EXP-2, currently transient — see
[`EPIC3_GAPS.md`](EPIC3_GAPS.md)) should become a persistable named region at the
same time.

## Open — automation of mixer/plugin parameters (FR-MIX-9, target)

**What is missing.** No automation curves or recording of parameter movement.

**Why it is deferred.** **[target]** priority, and it depends on the tempo/timeline
work above for a usable time base.

**Planned approach.** Tracktion's `AutomationCurveList` already exists per
`Clip`/`Track`; expose an automation lane per track and per plugin parameter,
with read/write/latch modes.

## Open — comping uses an app-level clip assembly, not the engine CompManager

**What is missing.** Comping is implemented as a **non-destructive clip
assembly** on a new track (trimmed copies of the chosen takes), which satisfies
"assemble a master take from 3+ takes non-destructively" and is measurable. It
does **not** use Tracktion's `CompManager`/take lanes (which require a
file/folder project layout to render a comp take) and there is no per-region
"audition and pick" UI beyond the API.

**Planned approach.** When a folder-based project layout is adopted, wire the
engine `CompManager` (`addNewComp` + `addSection`) and expose take lanes and
region auditioning in the arrangement.

## Open — time-stretch UI is limited to the region-selection length

**What is missing.** The Stretch action stretches the selected clip to the
current region-selection length. There is no numeric target-duration field, no
pitch-shift control in the UI, and no on-clip stretch marker. The engine-side
API (`Session::stretchClipToDuration`) already takes an arbitrary target and
semitones.

**Planned approach.** A small dialog (target seconds + semitones) and, later, a
draggable on-clip stretch handle.

## Open — fade handles are drag-only (no numeric entry / curve choice)

**What is missing.** Fade lengths are set by dragging the top corners; the fade
curve is fixed (linear for plain fades, convex/equal-power for crossfades). There
is no numeric fade entry or curve selector, and no explicit "crossfade" overlay
on the two overlapping clips.

**Planned approach.** Show the overlap region and a curve dropdown; keep the
drag handles.

## Related

- [`STUDIO_SPEC.md`](STUDIO_SPEC.md) §3 (FR-ED-1..7, FR-MIX-9) and §9 (Epic 4
  acceptance criteria).
- [`../DEPENDENCIES.md`](../DEPENDENCIES.md) — OQ-2 licence verdict and the
  Signalsmith Stretch pin.
