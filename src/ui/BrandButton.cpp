// raw-radio-studio — branded push button (see BrandButton.h).

#include "BrandButton.h"

namespace rrs
{
    BrandButton::BrandButton (const juce::String& buttonText, Style buttonStyle)
        : juce::Button (buttonText), style (buttonStyle)
    {
    }

    void BrandButton::setOnColours (juce::Colour background, juce::Colour borderColour,
                                    juce::Colour textColour)
    {
        onBackground = background;
        onBorder = borderColour;
        onText = textColour;
        hasOnColours = true;
        repaint();
    }

    void BrandButton::setCornerRadius (float radius) noexcept
    {
        cornerRadius = radius;
        repaint();
    }

    void BrandButton::setLabelFont (juce::Font newFont)
    {
        labelFont = std::move (newFont);
        repaint();
    }

    juce::Font BrandButton::getLabelFont() const
    {
        return labelFont;
    }

    int BrandButton::getPreferredWidth() const
    {
        const auto textWidth = juce::GlyphArrangement::getStringWidthInt (getLabelFont(), getButtonText());
        return juce::jlimit (minWidth, maxWidth, 2 * padX + textWidth);
    }

    void BrandButton::paintButton (juce::Graphics& g,
                                   bool shouldDrawButtonAsHighlighted,
                                   bool shouldDrawButtonAsDown)
    {
        const auto enabled = isEnabled();
        const auto on = getToggleState();

        auto bounds = getLocalBounds().toFloat().reduced (0.5f);

        auto background = brand::bgTertiary;
        auto borderColour = brand::border;
        auto textColour = brand::textPrimary;
        auto useGradient = false;
        auto gradientTop = brand::accent;
        auto gradientBottom = brand::accentDeep;

        switch (style)
        {
            case Style::Primary:
                useGradient = true;
                borderColour = juce::Colours::transparentBlack;

                if (shouldDrawButtonAsDown)
                {
                    gradientTop = brand::accent;
                    gradientBottom = brand::accentDeep.darker (0.1f);
                }
                else if (shouldDrawButtonAsHighlighted)
                {
                    gradientTop = brand::accentHover;
                    gradientBottom = brand::accent;
                }
                break;

            case Style::Record:
                if (on)
                {
                    background = shouldDrawButtonAsHighlighted ? brand::recordHover : brand::record;
                    borderColour = background;
                    textColour = brand::textPrimary;
                }
                else
                {
                    textColour = brand::record;
                    borderColour = shouldDrawButtonAsHighlighted ? brand::record : brand::border;
                }
                break;

            case Style::Transport:
                background = shouldDrawButtonAsHighlighted ? brand::bgElevated : brand::bgTertiary;
                borderColour = shouldDrawButtonAsHighlighted ? brand::borderHover : brand::border;

                if (on)
                {
                    background = brand::accentMuted;
                    borderColour = brand::accent;
                    textColour = brand::accent;
                }
                break;

            case Style::Chip:
                if (on && hasOnColours)
                {
                    background = onBackground;
                    borderColour = onBorder;
                    textColour = onText;
                }
                else
                {
                    background = brand::bgTertiary;
                    borderColour = shouldDrawButtonAsHighlighted ? brand::borderHover : brand::border;
                    textColour = brand::textSecondary;
                }
                break;

            case Style::Secondary:
            default:
                background = shouldDrawButtonAsHighlighted ? brand::bgElevated : brand::bgTertiary;
                borderColour = shouldDrawButtonAsHighlighted ? brand::borderHover : brand::border;
                textColour = brand::textPrimary;
                break;
        }

        if (! enabled)
        {
            background = brand::bgPanel;
            borderColour = brand::border;
            textColour = brand::textDisabled;
            useGradient = false;
        }

        if (useGradient && enabled)
        {
            juce::ColourGradient gradient (gradientTop, bounds.getCentreX(), bounds.getY(),
                                           gradientBottom, bounds.getCentreX(), bounds.getBottom(), false);
            g.setGradientFill (gradient);
        }
        else
        {
            g.setColour (background);
        }

        g.fillRoundedRectangle (bounds, cornerRadius);

        if (enabled && ! borderColour.isTransparent())
        {
            g.setColour (borderColour);
            g.drawRoundedRectangle (bounds, cornerRadius, 1.0f);
        }

        // 2 px accent focus ring for keyboard navigation.
        if (enabled && hasKeyboardFocus (false))
        {
            g.setColour (brand::accent);
            g.drawRoundedRectangle (bounds.reduced (1.5f), juce::jmax (1.0f, cornerRadius - 1.0f), 2.0f);
        }

        g.setColour (textColour);
        g.setFont (getLabelFont());
        g.drawText (getButtonText(), getLocalBounds().reduced (padX, 0),
                    juce::Justification::centred, false);
    }
}
