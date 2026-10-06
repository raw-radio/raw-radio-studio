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

> **Placeholder** — the C++ style guide will be finalized when the engine
> integration lands (Epic 0, C++ specialist). Until then:

- C++20. Prefer RAII, `const` correctness, and the standard library.
- Match the style of the surrounding file; keep diffs minimal.
- No allocations, locks, or I/O on the real-time audio callback.
- No floating dependency branches — dependencies are pinned to exact commits.
- The exact formatter/linter config (e.g. `.clang-format`) will be added and
  enforced in CI.

## Build prerequisites

- **CMake ≥ 3.22**
- A **C++20** compiler: Apple Clang / Clang / GCC
- Git (for future pinned submodules)

Configure and build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/raw_radio_studio
```

> Engine dependencies (JUCE, Tracktion Engine) are **not** part of this
> skeleton — they are added as pinned git submodules by the C++ specialist
> (Epic 0). Do not add floating branches.

## Licensing of contributions

- Contributions are accepted under the project's top-level **AGPLv3** license.
- Do not add dependencies that are commercial-only or GPLv2+-only (e.g.
  RubberBand, Elastique). Every new dependency needs a license review and a
  `NOTICE` update.
- Do not bundle plugins or other binaries you do not have the right to ship.
