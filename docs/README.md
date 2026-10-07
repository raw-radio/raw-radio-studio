# Documentation

This directory holds the project documentation for `raw-radio-studio`.

## Specification

The canonical specification is **`STUDIO_SPEC.md`** (document version 1.2.1). It
defines the product vision, functional and non-functional requirements,
architecture, licensing, build/CI/release plan, and the Epic 0–7 roadmap with
acceptance criteria.

It is a living document, versioned in git and changed via PR only. It lives in
this repository at [`STUDIO_SPEC.md`](STUDIO_SPEC.md) so that the Corresponding
Source for each released tag is self-contained (see the spec's
licensing/corresponding-source section).

## Other documentation

- [`../README.md`](../README.md) — project overview, build, run, and license.
- [`../DEPENDENCIES.md`](../DEPENDENCIES.md) — pinned submodules, exact SHAs, and
  the JUCE 9 ↔ Tracktion Engine compatibility verdict.
- [`../CONTRIBUTING.md`](../CONTRIBUTING.md) — DCO sign-off and PR flow.
- [`../NOTICE`](../NOTICE) — third-party inventory and licensing composition.
- [`EPIC2_GAPS.md`](EPIC2_GAPS.md) — Epic 2 deliverables that remain open
  (notably the deferred **software cue mixes**, FR-MON-3/4), so the epic is not
  silently marked complete.

Build runbook, compatibility notes, and CI/release notes will be expanded as
later epics land.
