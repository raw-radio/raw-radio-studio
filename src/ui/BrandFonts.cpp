// raw-radio-studio — brand typography (see BrandFonts.h).

#include "BrandFonts.h"

#include <RawRadioStudioAssets.h>

#include <array>

namespace rrs::brand
{
    namespace
    {
        constexpr int numFamilies = 2;
        constexpr int numWeights = 4;

        // Typeface slots are function-local statics so there is no cross-TU
        // static-initialisation-order hazard. Deliberately leaked: the
        // Typeface::Ptrs must outlive JUCE's global state (destroying them after
        // JUCE shutdown aborts at exit).
        std::array<juce::Typeface::Ptr, numFamilies * numWeights>& typefaces()
        {
            static auto* slots = new std::array<juce::Typeface::Ptr, numFamilies * numWeights> {};
            return *slots;
        }

        int indexOf (FontFamily family, FontWeight weight) noexcept
        {
            return (int) family * numWeights + (int) weight;
        }
    }

    void initialiseFonts()
    {
        struct Entry
        {
            FontFamily family;
            FontWeight weight;
            const char* data;
            int size;
        };

        const Entry entries[] =
        {
            { FontFamily::Inter,          FontWeight::Regular,  RawRadioStudioAssets::InterRegular_ttf,       RawRadioStudioAssets::InterRegular_ttfSize },
            { FontFamily::Inter,          FontWeight::Medium,   RawRadioStudioAssets::InterMedium_ttf,        RawRadioStudioAssets::InterMedium_ttfSize },
            { FontFamily::Inter,          FontWeight::SemiBold, RawRadioStudioAssets::InterSemiBold_ttf,      RawRadioStudioAssets::InterSemiBold_ttfSize },
            { FontFamily::Inter,          FontWeight::Bold,     RawRadioStudioAssets::InterBold_ttf,          RawRadioStudioAssets::InterBold_ttfSize },
            { FontFamily::JetBrainsMono,  FontWeight::Regular,  RawRadioStudioAssets::JetBrainsMonoRegular_ttf, RawRadioStudioAssets::JetBrainsMonoRegular_ttfSize },
            { FontFamily::JetBrainsMono,  FontWeight::Medium,   RawRadioStudioAssets::JetBrainsMonoMedium_ttf,  RawRadioStudioAssets::JetBrainsMonoMedium_ttfSize },
        };

        auto& slots = typefaces();

        for (const auto& entry : entries)
        {
            auto& slot = slots[(size_t) indexOf (entry.family, entry.weight)];

            if (slot == nullptr)
                slot = juce::Typeface::createSystemTypefaceFor (entry.data, (size_t) entry.size);
        }
    }

    juce::Typeface::Ptr getTypeface (FontFamily family, FontWeight weight)
    {
        initialiseFonts();

        auto& slot = typefaces()[(size_t) indexOf (family, weight)];
        return slot;
    }

    juce::FontOptions fontOptions (FontFamily family, FontWeight weight, float height)
    {
        auto typeface = getTypeface (family, weight);

        if (typeface == nullptr)
            return juce::FontOptions { height };

        return juce::FontOptions { typeface }.withHeight (height);
    }

    juce::Font font (FontFamily family, FontWeight weight, float height)
    {
        return juce::Font { fontOptions (family, weight, height) };
    }
}
