# Vendored icons — Lucide

The SVGs in this directory are vendored from **Lucide**
(<https://github.com/lucide-icons/lucide>), pinned to tag **`1.21.0`** — the
same icon family/version the RAW Radio admin and app use (`lucide-react`).

Licence: **ISC** (with an MIT section for the Feather-derived icons). See
[`../../LICENSES/Lucide.txt`](../../LICENSES/Lucide.txt) and the third-party
inventory in [`../../NOTICE`](../../NOTICE).

## Vendored set (Epic 1)

`file-plus`, `folder-open`, `save`, `save-all`, `file-input`, `file-output`,
`x`, `settings`, `info`, `circle-dot`, `headphones`, `circle`, `play`, `pause`,
`square`, plus `skip-back`, `skip-forward`, `clock`, `volume-x`, `refresh-cw`,
`check`, `activity`, `triangle-alert`, `radio` for later screens.

## Local modification

JUCE does not resolve the SVG `currentColor` keyword, so every `currentColor`
occurrence is replaced with the literal `#FFFFFF` at vendor time. The icons are
tinted at runtime by `src/ui/IconCache` via `Drawable::replaceColour`.

## Refreshing

```bash
# from the repository root, for each icon <name>:
curl -sSL "https://raw.githubusercontent.com/lucide-icons/lucide/1.21.0/icons/<name>.svg" \
  -o "assets/icons/<name>.svg"
perl -pi -e 's/currentColor/#FFFFFF/g' assets/icons/*.svg
```

Keep the pinned tag in sync with the admin/app `lucide-react` version.
