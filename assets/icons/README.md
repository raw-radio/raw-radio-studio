# Vendored icons — Material Icons (Outlined)

The SVGs in this directory are vendored from the **Material Icons / Material
Symbols (Outlined)** set — the same Material family the RAW Radio ecosystem uses
(e.g. the Material `mic`). They replace the previous Lucide set so the studio UI
shares one icon language with the rest of RAW Radio.

Licence: **Apache-2.0**. See
[`../../LICENSES/Material-Icons.txt`](../../LICENSES/Material-Icons.txt) and the
third-party inventory in [`../../NOTICE`](../../NOTICE).

## Upstream / pinning

Upstream: <https://github.com/google/material-design-icons> (Apache-2.0).
Vendored via the optimised npm package **`@material-design-icons/svg`** at
version **`0.14.15`**, directory `outlined/`. The package is a build-time
optimisation mirror of the Google repo (SVGO pass); the icon artwork and licence
are Google's.

## Vendored set (Epic 1)

`note_add`, `folder_open`, `save`, `save_as`, `upload_file`, `download`,
`close`, `settings`, `info`, `radio_button_checked`, `headphones`,
`fiber_manual_record`, `play_arrow`, `pause`, `stop`, `skip_previous`, `timer`,
`add`, `delete`, `auto_fix_high`, `refresh`.

Semantic mapping used by the UI (`src/ui/MainComponent.cpp`,
`src/ui/DevicePanel.cpp`):

| Action | Material name |
|--------|---------------|
| New | `note_add` |
| Open | `folder_open` |
| Save | `save` |
| Save As | `save_as` |
| Import | `upload_file` |
| Export | `download` |
| Close | `close` |
| Settings | `settings` |
| About | `info` |
| Arm | `radio_button_checked` |
| Monitor | `headphones` |
| Record | `fiber_manual_record` |
| Play / Pause | `play_arrow` / `pause` |
| Stop | `stop` |
| Go to start | `skip_previous` |
| Metronome / Click | `timer` |
| Add track | `add` |
| Remove track | `delete` |
| Normalize | `auto_fix_high` |
| Rescan devices | `refresh` |

## Local modification

JUCE does not resolve the SVG `currentColor` keyword, and Material ships its
`<path>` elements with **no** fill (rendered black by default), so every path is
rewritten at vendor time to carry a literal `fill="#FFFFFF"`. The icons are
tinted at runtime by `src/ui/IconCache` via `Drawable::replaceColour`
(`#FFFFFF` → requested tint).

`IconCache::resourceNameFor` must retain underscores in the lookup key because
JUCE's `makeBinaryDataIdentifierName()` keeps them: `play_arrow.svg` becomes the
symbol `play_arrow_svg` (whereas a dash is dropped, `file-plus` → `fileplus`).

## Refreshing

```bash
# from the repository root, for each icon <name> (Outlined, 24x24):
curl -sSL "https://registry.npmjs.org/@material-design-icons/svg/-/svg-0.14.15.tgz" -o /tmp/mdi.tgz
tar -xzf /tmp/mdi.tgz -C /tmp
for name in note_add folder_open save save_as upload_file download close \
            settings info radio_button_checked headphones fiber_manual_record \
            play_arrow pause stop skip_previous timer add delete auto_fix_high refresh; do
  python3 - "$name" <<'PY'
import sys
s = open(f"/tmp/package/outlined/{sys.argv[1]}.svg").read().strip()
s = s.replace("<path ", '<path fill="#FFFFFF" ')
open(f"assets/icons/{sys.argv[1]}.svg", "w").write(s + "\n")
PY
done
```

Keep the pinned package version in sync with the entries in
`DEPENDENCIES.md` / `NOTICE`, and re-review the licence if the upstream project
changes.
