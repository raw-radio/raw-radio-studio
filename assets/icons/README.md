# Vendored icons — Phosphor Icons (regular)

The SVGs in this directory are vendored from **Phosphor Icons** — the `regular`
weight (24x24 grid, single consistent fill). Phosphor replaced the previous
Material / Lucide sets: the owner asked for a more serious, neutral icon
language, and the regular weight reads that way (a single stroke/fill weight, no
forked stylistic variants).

Licence: **MIT**. See [`../../LICENSES/Phosphor.txt`](../../LICENSES/Phosphor.txt)
and the third-party inventory in [`../../NOTICE`](../../NOTICE).

## Upstream / pinning

Upstream: <https://github.com/phosphor-icons/core> (MIT).
Vendored from the `regular/` asset directory at tag **`v2.0.8`**, commit
`d42782b2abe747d904b971ccab48b182a1455f86`. The SVGs are committed here (not
fetched at build time) and compiled into the binary via `juce_add_binary_data`.

## Vendored set

`file-plus`, `folder-open`, `floppy-disk`, `floppy-disk-back`, `upload-simple`,
`download-simple`, `x`, `gear-six`, `info`, `record`, `headphones`, `play`,
`pause`, `stop`, `skip-back`, `metronome`, `timer`, `plus`, `trash`, `waveform`,
`arrows-clockwise`.

Semantic mapping used by the UI (`src/ui/MainComponent.cpp`,
`src/ui/DevicePanel.cpp`):

| Action | Phosphor name |
|--------|---------------|
| New | `file-plus` |
| Open | `folder-open` |
| Save | `floppy-disk` |
| Save As | `floppy-disk-back` |
| Import | `upload-simple` |
| Export | `download-simple` |
| Close | `x` |
| Settings | `gear-six` |
| About | `info` |
| Arm | `record` |
| Monitor | `headphones` |
| Record | `record` |
| Play / Pause | `play` / `pause` |
| Stop | `stop` |
| Go to start | `skip-back` |
| Metronome / Click | `metronome` |
| Count-in | `timer` (reserved for the count-in affordance; the transport combo is text-only today) |
| Add track | `plus` |
| Remove track | `trash` |
| Normalize | `waveform` |
| Rescan devices | `arrows-clockwise` |

## Local modification

Phosphor ships each icon as `<svg … fill="currentColor"><path …/></svg>`. JUCE
does not resolve the `currentColor` keyword, and a fill-less `<path>` renders
black, so at vendor time the root `fill` is rewritten to `#FFFFFF` **and** every
fill-capable element — `<path>`, `<circle>`, `<rect>`, `<ellipse>`, `<polygon>`,
`<polyline>` and `<g>` — is given a literal `fill="#FFFFFF"`. The icons are
tinted at runtime by `src/ui/IconCache` via `Drawable::replaceColour`
(`#FFFFFF` → requested tint); a single untinted element would survive as a dark
"hole" on the dark theme. `width`/`height` are set to `24` (Phosphor core ships
`viewBox="0 0 256 256"` only).

`IconCache::resourceNameFor` must **drop dashes** from the lookup key because
JUCE's `makeBinaryDataIdentifierName()` replaces spaces and dots with `_` and
then retains only `[A-Za-z0-9_]`: `file-plus.svg` becomes the symbol
`fileplus_svg`. Returning `file-plus_svg` (dash kept) would silently yield a
blank button.

## Refreshing

```bash
# from the repository root, for each icon <name> (regular weight):
PHOSPHOR_SHA=d42782b2abe747d904b971ccab48b182a1455f86
for name in file-plus folder-open floppy-disk floppy-disk-back upload-simple \
            download-simple x gear-six info record headphones play pause stop \
            skip-back metronome timer plus trash waveform arrows-clockwise; do
  curl -sSL "https://raw.githubusercontent.com/phosphor-icons/core/$PHOSPHOR_SHA/assets/regular/$name.svg" \
    -o "/tmp/$name.svg"
  python3 - "$name" <<'PY'
import re, sys
name = sys.argv[1]
s = open(f"/tmp/{name}.svg").read().strip()

# JUCE does not resolve currentColor; tint via Drawable::replaceColour(white -> tint).
s = s.replace("currentColor", "#FFFFFF")

# Phosphor core ships viewBox only; add an explicit intrinsic size.
if "width=" not in s.split(">", 1)[0]:
    s = s.replace("<svg ", '<svg width="24" height="24" ', 1)

# Rewrite `fill` on *every* fill-capable element, not just <path>.
def add_fill(match):
    tag, attrs = match.group(1), match.group(2)
    if "fill=" in attrs:
        return match.group(0)
    return f'<{tag} fill="#FFFFFF"{attrs}>'

s = re.sub(r"<(path|circle|rect|ellipse|polygon|polyline|g)\b([^>]*)>", add_fill, s)
open(f"assets/icons/{name}.svg", "w").write(s + "\n")
PY
done
```

Keep the pinned tag/SHA in sync with the entries in `DEPENDENCIES.md` /
`NOTICE`, and re-review the licence if the upstream project changes.
