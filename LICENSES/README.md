# Licenses

Verbatim license texts for the pinned third-party dependencies live in this
directory:

- `JUCE.txt` — JUCE Framework (`third_party/JUCE`, pinned to tag **9.0.3**).
- `tracktion_engine.txt` — Tracktion Engine (`third_party/tracktion_engine`,
  pinned to `develop` commit **2d2d452f**).
- `SignalsmithStretch.txt` — Signalsmith Stretch
  (`third_party/signalsmith-stretch`, pinned to tag **1.1.0**): **MIT**
  (Copyright (c) 2022 Geraint Luff / Signalsmith Audio Ltd.). Header-only; used
  for FR-ED-5 time-stretch/pitch-shift.
- `SignalsmithStretch-dsp.txt` — the copy of the Signalsmith DSP library bundled
  inside Signalsmith Stretch's `dsp/` folder at tag **1.1.0**: **MIT**
  (Copyright (c) 2021 Geraint Luff / Signalsmith Audio Ltd.).
- `VST3-SDK.txt` — VST3 SDK, bundled inside JUCE's pinned tree
  (`juce_audio_processors_headless/format_types/VST3_SDK/`): **MIT**
  (Copyright Steinberg Media Technologies GmbH). Compiled in with
  `JUCE_PLUGINHOST_VST3=1`; no separate Steinberg SDK is downloaded.
- `LV2-SDK.txt` — LV2 SDK (lv2, lilv, serd, sord, sratom), bundled inside JUCE's
  pinned tree (`juce_audio_processors_headless/format_types/LV2_SDK/`):
  permissive **ISC**-style. Compiled in only with the opt-in
  `-DRAW_RADIO_STUDIO_ENABLE_LV2=ON` build.
- `Phosphor.txt` — Phosphor Icons (regular weight) icon set
  (`assets/icons/`, vendored from `phosphor-icons/core` at tag **v2.0.8**):
  **MIT**. Vendored and compiled into the binary via `juce_add_binary_data`
  (see `assets/icons/README.md`).
- `Inter-OFL.txt` — Inter typeface (`assets/fonts/Inter-*.ttf`, pinned to tag
  **v4.1**): SIL Open Font License 1.1.
- `JetBrainsMono-OFL.txt` — JetBrains Mono typeface
  (`assets/fonts/JetBrainsMono-*.ttf`, pinned to tag **v2.304**): SIL Open Font
  License 1.1.

## Licensing composition

- The project itself (`raw-radio-studio`) is licensed under the **GNU AGPLv3**
  — see the top-level [`../LICENSE`](../LICENSE).
- **JUCE** is used under its free **AGPLv3** tier (dual-licensed with a
  commercial option we do not use).
- **Tracktion Engine** is used under its free **GPLv3-or-later** tier
  (dual-licensed with a commercial option we do not use).

These texts are collected here for the AGPL Corresponding Source obligation.
The full composition, rationale (GPLv3 §13 / AGPLv3 §13), and the pinned-source
inventory are maintained in [`../NOTICE`](../NOTICE).

Every dependency addition requires a license review and an update to `NOTICE`.
