# Contributing to raw-radio-studio

Thanks for your interest in `raw-radio-studio`. This is a free, open-source
(AGPLv3) tracking-first DAW built on JUCE + Tracktion Engine in C++20.

## Developer Certificate of Origin (DCO) — required

This project uses the **Developer Certificate of Origin** and does **not**
require a CLA. Every commit must be signed off:

```bash
git commit -s -m "your message"
```

That appends a trailer:

```
Signed-off-by: Your Name <you@example.com>
```

- The name and email must match your git identity (`git config user.name`,
  `git config user.email`).
- Sign-off means you certify the contribution under the terms in the top-level
  [`DCO`](DCO) file (Developer Certificate of Origin, version 1.1).
- Commits without a valid `Signed-off-by:` trailer will not be merged.

If you already made commits without sign-off, amend or rebase them:

```bash
git commit --amend -s --no-edit        # last commit
git rebase --signoff HEAD~N            # last N commits
```

## Pull request flow

1. Fork the repository and create a topic branch off `main`.
2. Keep changes focused; one logical change per PR.
3. Ensure the build is green locally (see "Build prerequisites" below).
4. Ensure every commit is signed off (`Signed-off-by:`).
5. Open a PR against `main` describing **what** and **why**.
6. CI (macOS + Ubuntu) must pass. A maintainer reviews and merges.
7. Rebase on `main` if needed; do not merge `main` into your branch.

> Specification changes are made by PR only. The spec is a living document; see
> [`docs/README.md`](docs/README.md).

## Code style

- C++20. Prefer RAII, `const` correctness, and the standard library.
- Match the style of the surrounding file; keep diffs minimal.
- **RT-safety:** no allocations, locks, or I/O on the real-time audio callback —
  hand off to the message thread.
- No floating dependency branches — dependencies are pinned to exact commits
  (`DEPENDENCIES.md`).
- A dedicated formatter/linter config (e.g. `.clang-format`) is not committed
  yet; if you add one, wire it into CI in the same PR.

## Build prerequisites

- **CMake ≥ 3.22**
- A **C++20** compiler: Apple Clang / Clang / GCC
- Git, with the pinned submodules initialised (below)

Dependencies (JUCE, Tracktion Engine, Signalsmith Stretch) are pinned to exact
commits via git submodules (`DEPENDENCIES.md`). Initialise them
**non-recursively** — Tracktion's nested `modules/juce` submodule uses an SSH URL
and is deliberately not used:

```bash
git submodule update --init
```

Configure, build and run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
# built binary (macOS/Linux):
build/raw_radio_studio_artefacts/Release/raw-radio-studio --version
```

Run the tests:

```bash
ctest --test-dir build -C Release --output-on-failure
```

> Never float the dependency pins — the exact commits live in
> [`DEPENDENCIES.md`](DEPENDENCIES.md). The [`README.md`](README.md) is the
> source of truth for build/run commands and the delivered-epic overview.

## Licensing of contributions

- Contributions are accepted under the project's top-level **AGPLv3** license.
- Do not add dependencies that are commercial-only or GPLv2+-only (e.g.
  RubberBand, Elastique). Every new dependency needs a license review and a
  `NOTICE` update.
- Do not bundle plugins or other binaries you do not have the right to ship.
