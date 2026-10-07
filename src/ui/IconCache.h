// raw-radio-studio — Lucide icon cache (UI only).
//
// Parses the vendored Lucide SVGs (compiled in via `RawRadioStudioAssets`),
// tints them with `Drawable::replaceColour` and rasterises each (name, tint,
// size) once. `paint()` only ever blits a cached image — no SVG parsing and no
// allocation on a hot repaint path, and nothing here ever touches the audio
// thread.

#pragma once

#include <JuceHeader.h>

#include <string>
#include <unordered_map>

namespace rrs
{
    class IconCache final
    {
    public:
        static IconCache& getInstance();

        /** Returns the tinted icon image at `sizePx` logical pixels, or a null
            image if the icon name is unknown. Cached by (name, tint, size). */
        juce::Image getIconImage (const juce::String& name, juce::Colour tint, int sizePx);

        /** Draws an icon centred within `area` (square, `area` height used). */
        void drawIcon (juce::Graphics&, const juce::String& name, juce::Colour tint,
                       juce::Rectangle<int> area);

    private:
        IconCache() = default;

        static juce::String resourceNameFor (const juce::String& iconName);

        std::unordered_map<std::string, juce::Image> imageCache;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconCache)
    };
}
