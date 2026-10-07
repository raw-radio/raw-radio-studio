// raw-radio-studio — branded push button.
//
// A small juce::Button subclass that implements the RAW Radio control styles
// (secondary/primary/transport/record/chip) with the brand radius, border,
// focus ring and hover states. Used for the action row, the transport and the
// arm/monitor chips so the theme is not dependent on LookAndFeel defaults.
//
// UI-only; never touches the audio thread.

#pragma once

#include <JuceHeader.h>

#include "BrandColours.h"

namespace rrs
{
    class BrandButton final : public juce::Button
    {
    public:
        enum class Style
        {
            Secondary,  ///< Action-row button: bgTertiary fill + border.
            Primary,    ///< Accent gradient (#FF6B35 -> #FF4500) call-to-action.
            Transport,  ///< Play/stop: neutral fill, accent tint when active.
            Record,     ///< Red text normally, filled red while active (recording).
            Chip        ///< Arm/monitor toggle chip; on-state supplied by the caller.
        };

        explicit BrandButton (const juce::String& buttonText, Style style = Style::Secondary);

        /** Overrides for the "on" appearance of a Chip. */
        void setOnColours (juce::Colour background, juce::Colour borderColour, juce::Colour textColour);

        void setCornerRadius (float radius) noexcept;
        void setLabelFont (juce::Font newFont);

        /** Content-based width: 2*padX + text width, clamped [72,168]. */
        int getPreferredWidth() const;

        void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

        // Shared spacing scale used by the layout code.
        static constexpr int padX = 12;
        static constexpr int minWidth = 72;
        static constexpr int maxWidth = 168;

    private:
        juce::Font getLabelFont() const;

        Style style;
        float cornerRadius = 8.0f;
        juce::Font labelFont { juce::FontOptions { 14.0f } };

        juce::Colour onBackground { brand::accentMuted };
        juce::Colour onBorder { brand::accent };
        juce::Colour onText { brand::accent };
        bool hasOnColours = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrandButton)
    };
}
