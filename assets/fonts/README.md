# Vendored fonts — Inter + JetBrains Mono

The TTFs in this directory are vendored from the upstream projects pinned
below. They are compiled into the binary via `juce_add_binary_data` and
registered at startup by `src/ui/BrandFonts` with
`Typeface::createSystemTypefaceFor`. No font is loaded from disk at runtime and
no download happens at build time.

| File | Upstream | Pinned revision | License (SPDX) |
|------|----------|-----------------|----------------|
| `Inter-Regular.ttf`, `Inter-Medium.ttf`, `Inter-SemiBold.ttf`, `Inter-Bold.ttf` | [rsms/inter](https://github.com/rsms/inter) | tag `v4.1`, `extras/ttf/` | OFL-1.1 |
| `JetBrainsMono-Regular.ttf`, `JetBrainsMono-Medium.ttf` | [JetBrains/JetBrainsMono](https://github.com/JetBrains/JetBrainsMono) | tag `v2.304`, `fonts/ttf/` | OFL-1.1 |

Full licence texts: [`../../LICENSES/Inter-OFL.txt`](../../LICENSES/Inter-OFL.txt),
[`../../LICENSES/JetBrainsMono-OFL.txt`](../../LICENSES/JetBrainsMono-OFL.txt).
See the third-party inventory in [`../../NOTICE`](../../NOTICE).

Weights map to the brand typography as: Regular 400, Medium 500, SemiBold 600,
Bold 700 (Inter); Regular 400, Medium 500 (JetBrains Mono). JetBrains Mono is
used for timecodes and dB readouts; Inter for everything else.

## Refreshing

```bash
# Inter (v4.1): the static TTFs live under extras/ttf/ in the release zip.
curl -sSL -o /tmp/Inter-4.1.zip \
  "https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip"
unzip -j /tmp/Inter-4.1.zip \
  "extras/ttf/Inter-Regular.ttf" "extras/ttf/Inter-Medium.ttf" \
  "extras/ttf/Inter-SemiBold.ttf" "extras/ttf/Inter-Bold.ttf" \
  -d assets/fonts

# JetBrains Mono (v2.304).
curl -sSL -o /tmp/JetBrainsMono-2.304.zip \
  "https://github.com/JetBrains/JetBrainsMono/releases/download/v2.304/JetBrainsMono-2.304.zip"
unzip -j /tmp/JetBrainsMono-2.304.zip \
  "fonts/ttf/JetBrainsMono-Regular.ttf" "fonts/ttf/JetBrainsMono-Medium.ttf" \
  -d assets/fonts
```

> Note: the static `Inter-Medium.ttf` / `Inter-SemiBold.ttf` files report the
> family name `Inter Medium` / `Inter SemiBold` (not `Inter` + a style). That is
> why `BrandFonts` builds fonts from the registered `Typeface::Ptr` rather than
> resolving them by family name.
