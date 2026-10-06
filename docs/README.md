# Documentation

The canonical project specification is **`STUDIO_SPEC.md`** (document version
1.1.0). It defines the product vision, functional and non-functional
requirements, architecture, licensing, build/CI/release plan, and the Epic 0–7
roadmap with acceptance criteria.

**Where it lives today:** `docs/STUDIO_SPEC.md` in the `raw_radio` monorepo
(a sibling repository). It is a living document, versioned in git and changed via
PR only.

**Planned:** once Epic 0 lands, the spec will be vendored/moved into this
repository's `docs/` directory so that the Corresponding Source per released tag
is self-contained (see the spec's licensing/corresponding-source section).

Additional documentation — build runbook, JUCE 9 / Tracktion Engine compatibility
notes, CI/release notes — will be added as later epics land.

At bootstrap (Epic 0) this repository intentionally contains only the spec
pointer; no engine code or dependency inventory is vendored yet.
