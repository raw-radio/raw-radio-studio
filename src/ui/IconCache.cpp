// raw-radio-studio — Phosphor icon cache (see IconCache.h).

#include "IconCache.h"

#include <RawRadioStudioAssets.h>

#include <string>

namespace rrs
{
    IconCache& IconCache::getInstance()
    {
        // Intentionally leaked: the cache holds JUCE images/drawables that must
        // not be destroyed after JUCE's global state has shut down (static
        // destruction order is otherwise undefined and aborts at exit).
        static IconCache* cache = new IconCache();
        return *cache;
    }

    juce::String IconCache::resourceNameFor (const juce::String& iconName)
    {
        // Must match JUCE's `makeBinaryDataIdentifierName()` mangling exactly:
        // JUCE replaces spaces and '.' with '_' and then *drops* characters
        // outside [A-Za-z0-9_]. So a Phosphor file name like "file-plus.svg"
        // becomes the symbol "fileplus_svg" (the dash is dropped, the dot
        // becomes an underscore), while "play_pause" would keep its underscore.
        // Dropping the dash here too is why the dashed Phosphor names resolve;
        // not doing so would silently yield a blank button.
        juce::String resource;

        for (auto character : iconName)
            if (juce::CharacterFunctions::isLetterOrDigit (character) || character == '_')
                resource << character;

        return resource + "_svg";
    }

    juce::Image IconCache::getIconImage (const juce::String& name, juce::Colour tint, int sizePx)
    {
        if (name.isEmpty() || sizePx <= 0)
            return {};

        const auto key = name.toStdString() + ":" + std::to_string (tint.getARGB()) + ":"
                         + std::to_string (sizePx);

        if (auto found = imageCache.find (key); found != imageCache.end())
            return found->second;

        const auto resourceName = resourceNameFor (name);
        int dataSize = 0;
        const auto* data = RawRadioStudioAssets::getNamedResource (resourceName.toRawUTF8(), dataSize);

        if (data == nullptr || dataSize <= 0)
            return {};

        auto drawable = juce::Drawable::createFromSVGString (juce::String::fromUTF8 (data, dataSize));

        if (drawable == nullptr)
            return {};

        // SVGs are vendored with fill="#FFFFFF" on the root and every element
        // (JUCE cannot resolve `currentColor`); recolour the white shapes to the
        // requested tint.
        drawable->replaceColour (juce::Colours::white, tint);

        // Render at 2x for crisp HiDPI output; drawn back down to `sizePx`.
        const int renderSize = sizePx * 2;
        juce::Image image (juce::Image::ARGB, renderSize, renderSize, true);

        {
            juce::Graphics g (image);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            drawable->drawWithin (g, image.getBounds().toFloat(),
                                  juce::RectanglePlacement::centred, 1.0f);
        }

        auto inserted = imageCache.emplace (key, std::move (image));
        return inserted.first->second;
    }

    void IconCache::drawIcon (juce::Graphics& g, const juce::String& name, juce::Colour tint,
                              juce::Rectangle<int> area)
    {
        const auto size = juce::jmin (area.getWidth(), area.getHeight());
        const auto icon = getIconImage (name, tint, size);

        if (! icon.isValid())
            return;

        const auto target = juce::Rectangle<int> (size, size).withCentre (area.getCentre());
        g.drawImage (icon, target.toFloat());
    }
}
