# Licenses

Verbatim license texts for the pinned third-party dependencies live in this
directory:

- `JUCE.txt` — JUCE Framework (`third_party/JUCE`, pinned to tag **9.0.3**).
- `tracktion_engine.txt` — Tracktion Engine (`third_party/tracktion_engine`,
  pinned to `develop` commit **2d2d452f**).
- `Material-Icons.txt` — Material Icons / Material Symbols (Outlined) icon set
  (`assets/icons/`, vendored from the `@material-design-icons/svg` package,
  version **0.14.15**): **Apache-2.0**. Vendored and compiled into the binary via
  `juce_add_binary_data` (see `assets/icons/README.md`).
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
