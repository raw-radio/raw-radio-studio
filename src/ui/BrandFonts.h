// raw-radio-studio — brand typography (Inter + JetBrains Mono).
//
// The vendored TTFs are compiled into the binary via `RawRadioStudioAssets` and
// registered with JUCE through `Typeface::createSystemTypefaceFor`. Fonts are
// built from the registered Typeface::Ptr rather than by family name: the static
// Inter Medium/SemiBold files report the family "Inter Medium"/"Inter SemiBold"
// on some platforms, so name+style lookup is not portable.
//
// UI-only: registration happens on the message thread at startup; nothing here
// is ever called from the audio callback.

#pragma once

#include <JuceHeader.h>

namespace rrs::brand
{
    enum class FontFamily { Inter, JetBrainsMono };
    enum class FontWeight { Regular, Medium, SemiBold, Bold };

    /** Registers the vendored typefaces with JUCE. Idempotent. */
    void initialiseFonts();

    /** Returns a registered typeface (registers lazily on first use). */
    juce::Typeface::Ptr getTypeface (FontFamily, FontWeight);

    /** FontOptions bound to the registered typeface at `height`. */
    juce::FontOptions fontOptions (FontFamily, FontWeight, float height);

    /** Builds a Font for a family/weight/height. */
    juce::Font font (FontFamily, FontWeight, float height);

    // --- Role helpers used across the UI --------------------------------
    inline juce::Font inter      (float height, FontWeight weight = FontWeight::Regular) { return font (FontFamily::Inter, weight, height); }
    inline juce::Font mono       (float height, FontWeight weight = FontWeight::Regular) { return font (FontFamily::JetBrainsMono, weight, height); }
    inline juce::Font uiRegular  (float height) { return inter (height, FontWeight::Regular); }
    inline juce::Font uiMedium   (float height) { return inter (height, FontWeight::Medium); }
    inline juce::Font uiSemiBold (float height) { return inter (height, FontWeight::SemiBold); }
    inline juce::Font uiBold     (float height) { return inter (height, FontWeight::Bold); }
    /** Monospace, for timecodes and dB readouts. */
    inline juce::Font monoRegular(float height) { return mono (height, FontWeight::Regular); }
    inline juce::Font monoMedium (float height) { return mono (height, FontWeight::Medium); }
}
