# Epic 7 — open gaps (explicit)

> Status: **Epic 7 is partially delivered.** This file records what the current
> reliability/data-safety pass closed, and what is still open — deliberately
> deferred or only partially met — so the epic is not silently marked done. It is
> not a substitute for the epic acceptance criteria in
> [`STUDIO_SPEC.md`](STUDIO_SPEC.md) §9 (Epic 7 — Hardening).
>
> This pass targeted the *software* reliability blockers found by a prior audit
> (crash-safe capture, recovery correctness, clean-exit and quit guards,
> real-time-safety verification). **Hardware acceptance (16 tracks / ~32 plugins /
> monitoring < 10 ms / soak / xrun / hot-plug) remains open** — it needs the
> reference machines, not CI.

## Delivered — data-safety & recovery hardening

### B3 — ~6 s capture-loss window at the start of a take (closed, FR-REC-8)

**Root cause (confirmed in code).** Tracktion records through a JUCE
`WavAudioFormatWriter` (`third_party/tracktion_engine/.../audio_files/tracktion_AudioFile.cpp:143`,
`numSamplesPerFlush = 48000 * 6`). The writer emits a *placeholder* RIFF/`data`
header when the file is created — with `lengthInSamples == 0` — and only rewrites
it on `flush()`, i.e. roughly every 6 s. The sample data itself is pushed to disk
in ~32 KB chunks by JUCE's `FileOutputStream` (≈ every 0.1 s,
`third_party/JUCE/.../juce_WavAudioFormat.cpp:1661` `write` vs `:1716` `writeHeader`).
So a crash inside the first window left a file with **a 0-sample header but real
payload on disk**, and `Session::importOrphanedRecordings` silently discarded it
(the old `Session.cpp:4405` `getLength() <= 0 → continue`).

**Fix (application-level, no engine fork, nothing on the audio thread).**

1. **Header-safe repair** — `src/studio/WavRepair.{h,cpp}`
   (`rrs::wavrepair::repairTruncatedHeader`). It walks the RIFF chunk list, finds
   the `data` chunk, and rewrites the RIFF + `data` size fields **from the file's
   real byte length** using the `fmt ` block-align to drop a single even-length
   pad byte. It writes only the two 32-bit size fields in place (a
   `juce::FileOutputStream` at a seek offset — JUCE 9 has no `RandomAccessFile`),
   never extends or truncates the file.
2. **Recovery uses it** — `src/studio/Session.cpp:4419`
   (`importOrphanedRecordings`): before the `te::AudioFile` read, the header is
   repaired; a take that then reads back is recovered instead of dropped.
3. **In-progress header finaliser** — `src/studio/Session.cpp:4535`
   (`finaliseInProgressRecordingHeaders`), driven from the 10 Hz timer
   (`Session.cpp:4794`, throttled to ~1 Hz). While a take rolls, the on-disk header
   is kept valid, so the loss window shrinks from ~6 s to ~1 s **even for readers
   outside the app**. Message-thread file I/O only; the audio callback is untouched.

The Epic 1 limitation comment at the old `Session.cpp:4407-4411` is removed; the
site now documents the closed behaviour (`Session.cpp:4459`).

### 0-sample / broken `.wav` are handled explicitly, not swallowed (FR-REC-8)

`importOrphanedRecordings` no longer `continue`s silently. Every skipped file is
recorded with a reason in `Session::RecordingRecoveryReport`
(`src/studio/Session.h:727`, built in `Session.cpp:4419`): `imported`,
`repaired`, `importedFiles`, `skippedFiles` ("name (reason)"). The startup
recovery path surfaces it — status bar + `[recovery] …` console line
(`src/ui/MainComponent.cpp:1545`). Test:
`tests/test_integration.cpp:4853` (imported == 1, repaired == 1, no skips).

### Parasitic recovery prompt after a clean exit (FR-PRJ-3) — closed

The old `Session::~Session` unconditionally wrote a `.tmp_` autosave version
(`saveTempVersion`), so a *clean* quit left a stale temp → the next launch
prompted "an unsaved version was found" for a session the user had deliberately
closed. The destructor now behaves like `Session::close()`: it **deletes** the
temp version and clears the sentinel (`src/studio/Session.cpp:224`). Only a real
crash — where the destructor never runs — leaves recovery artifacts. Test:
`tests/test_integration.cpp:4927`.

### Quit now honours unsaved changes (FR-PRJ-1) — closed

`RawRadioStudioApplication::systemRequestedQuit` used to call `quit()` with no
prompt, so Cmd+Q / the window close button discarded unsaved work. It now routes
through `MainWindow::requestQuit()` → `MainComponent::requestQuit()`
(`src/main.cpp:760`, `src/main.cpp:803`, `src/ui/MainComponent.cpp:888`), which
runs the same Save/Discard/Cancel guard as New/Open/Close. A re-entrancy flag
(`MainComponent.h:280` `quitConfirmPending`) prevents double prompts; the shared
guard gained an optional `onCancel` so a failed save or a Cancel releases the flag
(`MainComponent.cpp:833`).

### Recovered takes are routed to the right track (FR-REC-8) — improved

Previously every recovered take was dumped on track 0 by mtime. Recording file
names already carry the track name
(`StudioEngineBehaviour::getFileForNewAudioRecording` →
`<session>_<track>_Take_<n>.wav`), so `Session::trackForRecoveredTake`
(`src/studio/Session.cpp:4385`) matches the `_<track>_Take_` infix against the live
track names and lays each take on its original track, sequentially per track so
takes never overlap. Takes with no recognisable track name fall back to track 0
(the old behaviour). Deterministic ordering (mtime, then file name) is unchanged.
Test: `tests/test_integration.cpp:4968`.

## Delivered — RT-safety gate (NFR-P-5)

### Allocation probe (audio-callback path)

- **Test** — `tests/test_integration.cpp:5058` (`RT-safety: the input-callback
  level meter performs no allocations`). Global `operator new`/`delete` hooks
  (inert until armed; JUCE does not install its own with our build flags —
  `JUCE_ENABLE_ALLOCATION_HOOKS` defaults to 0) count allocations strictly inside
  `Session::acceptInputBuffer` over 100 000 blocks and assert zero. This is the
  strongest check available without hardware; it proves the one audio-callback
  function we own is allocation-free (`InputLevels::push` is pure atomics,
  `src/studio/Session.cpp:173`).
- **Probe completeness — fixed in this pass.** The probe replaced the *throwing*
  `operator new`/`delete` but left the **nothrow** forms at their default. That
  asymmetry is an AddressSanitizer `alloc-dealloc-mismatch (operator new vs
  free)`: libstdc++'s `std::stable_sort` allocates its `_Temporary_buffer` via
  `get_temporary_buffer` → `::operator new(size, std::nothrow)` (default → ASan
  "operator new") and frees it via `__return_temporary_buffer` →
  `::operator delete(ptr, size)` (ours → `std::free`). `stable_sort` is reached
  from `juce::ValueTree::sort` (`juce_ElementComparator.h:104`), which Tracktion
  calls in `TrackList::sortTracksByType`
  (`tracktion_TrackUtils.cpp:250`) during `Edit::loadTracks`
  (`tracktion_Edit.cpp:1017`). This was a **harness** bug, not an engine bug. The
  probe now routes **all non-aligned** allocation/deallocation operators through
  `std::malloc`/`std::free` (`tests/test_integration.cpp:92`), so ASan's
  `alloc_dealloc_mismatch` detector stays enabled and no ASan option is weakened.

### ASan gate

- **Build option** — `CMakeLists.txt` (`RAW_RADIO_STUDIO_ENABLE_ASAN`) +
  `raw_radio_studio_apply_sanitizers`, applied to the app and both test targets.
  Instruments **our** sources only (not the pinned JUCE/Tracktion submodules).
- **Test environment** — when the option is on, CMake sets
  `LSAN_OPTIONS=suppressions=<repo>/tests/lsan.supp` as the `ctest` environment
  for both test targets (`CMakeLists.txt:441`). No `ASAN_OPTIONS` are set: every
  ASan detector — including `alloc-dealloc-mismatch` and leak detection — runs at
  its default, so the gate still fails on any *real* leak or corruption in our
  code. The suppression only silences the single upstream one-shot below.
- **Reproduce with one command:**
  ```bash
  cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DRAW_RADIO_STUDIO_ENABLE_ASAN=ON \
    && cmake --build build-asan --parallel \
    && ctest --test-dir build-asan --output-on-failure
  ```
- **CI** — `.github/workflows/ci.yml` job `asan` runs exactly this sequence
  (through `ctest`, with no env overrides); see the CI section below for the
  evidence and the blocking-status decision.

### Sanitizer triage — what the gate reports and whose bug it is

| Finding | Owner | User risk | Reproducible without our code? | Action |
|---|---|---|---|---|
| `alloc-dealloc-mismatch (operator new vs free)` in `std::stable_sort` (integration) | **ours (test harness)** | none | no — only the probe's incomplete operator overrides trigger it | fixed (`tests/test_integration.cpp:92`) |
| LSan one-shot **320 B** in `FcInitLoadConfigAndFonts` → `juce::FTLibWrapper` → `rrs::brand::initialiseFonts()` (`src/ui/BrandFonts.cpp:59`), unit | **upstream** (fontconfig via JUCE) | none — constant, does not grow | yes — plain `juce::Typeface::createSystemTypefaceFor` triggers it | suppressed, narrowly, in `tests/lsan.supp` |
| UBSan: NaN → `long long` at `tracktion_graph/.../tracktion_Utility.h:27` | **upstream** (Tracktion) | none observed | yes | gate is ASan-only (see below) |

**Why the fontconfig leak is suppressed, not fixed.** JUCE creates one FreeType
library + one fontconfig config per process (the `FTTypefaceList` singleton,
`juce_Fonts_freetype.cpp:388` / `:75`) and holds them until shutdown; fontconfig
keeps process-global caches for its whole lifetime and never frees them (`FcFini`
is effectively a no-op). It is the classic fontconfig "allocate once, never free"
behaviour, independent of session length or buffer count — not a leak in
raw-radio-studio and not attributable to our code. Because the suppression
matches the *allocation path* (`tests/lsan.supp`), LSan otherwise stays fully
armed: running the ASan unit binary with `LSAN_OPTIONS=suppressions=/dev/null`
fails the 320 B leak, and with the repo suppression it passes — i.e. the
suppression hides exactly one upstream allocation, nothing else.

## Open — no reference hardware has been exercised (Epic 7 acceptance)

**What is missing.** None of the numeric Epic 7 acceptance criteria run on the
blessed machines: **16 tracks with ~32 plugins**, monitoring **< 10 ms**,
long-session **soak**, **xrun** elimination, and device **hot-plug**, on an
Apple Silicon Mac 16 GB / Ubuntu x86_64 16 GB with a 4-in class-compliant USB
interface (`STUDIO_SPEC.md` §9 Epic 7, decision D21).

**Why.** The dev machine is headless for audio (engine tests run with
auto-initialise disabled, e.g. `tests/test_integration.cpp:117` `HeadlessBehaviour`),
and no reference interface is attached. All timing/xrun numbers are meaningless
without a real clock.

**Manual/hardware step.** On the reference machines: build Release; open a 16-track
session, insert ~32 (VST3) plugins, arm/record and play while watching the
monitor latency read-out and the engine's xrun counter; run a multi-hour soak;
then hot-plug and unplug the USB interface mid-session. Compare against the
acceptance table in `STUDIO_SPEC.md` §9.

## Open — plugin **scan** hardening not re-verified (FR-MIX-7)

**What is missing.** The out-of-process scan crash-isolation acceptance
(`STUDIO_SPEC.md` §9 Epic 7; D23: out-of-process *scan*, in-process *hosting*)
was delivered in Epic 3 but not re-exercised here; a deliberately crashing
scanner child was not run as part of this pass.

**Manual step.** Point the scanner at a known-bad/crashing plugin bundle and
confirm the host survives and reports the scan failure; the app must never host
that plugin in-process as part of scanning.

## Open — UBSan gate over our sources (NFR-P-5 follow-up)

**What is missing.** The sanitizer option is **ASan only**. A combined
ASan+UBSan gate cannot pass because UBSan aborts on a pre-existing Tracktion
Engine violation reached long before our code:
`third_party/tracktion_engine/modules/tracktion_graph/tracktion_graph/tracktion_Utility.h:27`
("nan is outside the range of representable values of type 'long long'"), hit
during an offline-export integration test. This was confirmed locally with an
`-fsanitize=address,undefined -fno-sanitize-recover=all` build.

**Planned approach.** Run UBSan alone over raw-radio-studio's own translation
units (not the engine modules) with a small suppressions set, or wait for the
upstream engine fix. Until then ASan (which does pass) is the gate.

## Open — recovery UX is still minimal (FR-PRJ-3)

**What is missing.** Recovery is a single native `Restore`/`Discard` dialog and a
status line; there is no list of individual takes, no per-take preview, and no
"open the recovered project as a copy" flow. The quit prompt and the recovery
prompt are the only guards; there is no crash-report upload or session-health log.

**Planned approach.** A recovery panel that lists candidate takes (with repair
status from `RecordingRecoveryReport`) and lets the user audition/choose before
importing them onto their (now name-routed) tracks.

## CI — the ASan gate job (`.github/workflows/ci.yml`, job `asan`)

The RT-safety / memory-safety gate is continuous: the `asan` job (Linux-only,
`ubuntu-24.04`) configures with `RAW_RADIO_STUDIO_ENABLE_ASAN=ON` and runs the
**whole suite through `ctest`**:

```yaml
- run: |
    cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
          -DRAW_RADIO_STUDIO_ENABLE_ASAN=ON
    cmake --build build-asan --parallel 2 \
          --target raw_radio_studio_tests raw_radio_studio_integration_tests
- run: ctest --test-dir build-asan --output-on-failure
```

Key points:
- **No `ASAN_OPTIONS`/`LSAN_OPTIONS` in the workflow.** Both old crutches
  (`detect_leaks=0`, `alloc_dealloc_mismatch=0`) were removed: the mismatch is
  fixed at the source in the probe, and the one upstream fontconfig leak is
  silenced by `tests/lsan.supp`. Every ASan detector — leak detection included —
  runs at its default, so nothing is weakened.
- **`ctest`, not the raw binaries.** The suppression path is a CMake *test
  property* (`LSAN_OPTIONS=suppressions=<repo>/tests/lsan.supp`, wired only when
  the gate is on), so only `ctest` injects it; running the binaries by hand would
  need the env set manually and could silently diverge from CI.
- **Linux only** (GCC/Clang); the option is a no-op on MSVC and is guarded so a
  bare Release `ctest` is unaffected.
- **`git submodule update --init`** first (pinned JUCE/Tracktion submodules).
- ASan roughly doubles build/link time and the integration binary runs ~73 s, so
  the job carries a generous `timeout-minutes`.

### Evidence (reference host, `~/projects/raw-radio/build-asan`)

- 3× `ctest --test-dir build-asan --output-on-failure` → **100% tests passed,
  0 failed out of 2** each run (unit 29/29 cases; integration 69/69 cases).
- 6 further integration-only ASan runs → 69/69 each ⇒ **9/9 integration runs
  green**, no flake observed in this sample.
- **LSan is armed, not disabled.** The unit binary run with
  `LSAN_OPTIONS=suppressions=/dev/null` exits **1** with
  `SUMMARY: AddressSanitizer: 320 byte(s) leaked in 3 allocation(s).`
  (`FcInitLoadConfigAndFonts` → `juce::FTLibWrapper` →
  `rrs::brand::initialiseFonts`); with the repo suppression it exits **0** and
  prints `Suppressions used: 3 320 FcInitLoadConfigAndFonts`. The suppression
  hides exactly that one upstream allocation and nothing else.

### Blocking status — currently **non-required** (advisory)

The job runs on every PR/push but is intentionally **not** a required status
check yet: the *pre-existing, non-sanitizer* flake below is still open, and a
memory-safety gate should not gate merges while it can go red for an unrelated
reason. A blanket retry (`ctest --repeat until-pass`) is deliberately **not**
added — retrying an ASan gate can mask a genuinely nondeterministic memory bug
(e.g. a race), which is exactly what the gate exists to catch. Promote `asan` to
required (branch-protection) once the harness determinism fix lands.

**Known pre-existing flakiness (not a sanitizer issue).** Running the *whole*
integration suite, it intermittently fails one timing/order-sensitive case —
`Epic 5: MIDI input records into a clip on the armed track`
(`tests/test_integration.cpp:4446`, "MIDI clips: 0") or `count-in delays the
start of recording…` (`tests/test_integration.cpp:3236`). Evidence that this is
unrelated to the sanitizer work:
- it reproduces with the probe change reverted, i.e. it long predates the
  sanitizer/probe work;
- the MIDI case, run in isolation (`--test-case="Epic 5: MIDI input records…"`),
  passes repeatedly, i.e. it is an **inter-test isolation/ordering** problem in
  the shared `te::Engine`/`DeviceManager` harness, not an inherent race and not a
  sanitizer artefact;
- it is order/timing dependent, not simply load dependent (it also occurred on
  an idle host);
- it did **not** recur in the 9 ASan runs recorded above (consistent with a low,
  intermittent rate).

A follow-up should make the async MIDI device-list / recording setup
deterministic (pump until the recording target is actually connected, not just
until the device name is listed) and re-verify per-test isolation; then flip the
check to required.

## Related

- [`STUDIO_SPEC.md`](STUDIO_SPEC.md) §9 (Epic 7 — Hardening): FR-REC-8 (`:194`),
  FR-PRJ-2/3 (`:289`–`:290`), NFR-P-5 (`:319`), Epic 7 acceptance (`:956`).
- [`EPIC5_GAPS.md`](EPIC5_GAPS.md) — MIDI/instrument gaps.
- [`DEPENDENCIES.md`](../DEPENDENCIES.md) — pinned JUCE/Tracktion commits (the
  flush behaviour above is pinned to those revisions).
