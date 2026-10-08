// raw-radio-studio — branded push button (see BrandButton.h).

#include "BrandButton.h"

#include "IconCache.h"

namespace rrs
{
    namespace
    {
        constexpr int iconLabelGap = 6;

        // Icon-only buttons render a fixed ~20 px glyph centred in the button's
        // hit area, instead of filling the whole square with min(w, h). The
        // owner found the icons too big once they were icon-only: the glyph no
        // longer touches the border, while the button keeps its 32/36/40 px hit
        // size.
        constexpr int iconOnlyGlyphSize = 20;
    }

    BrandButton::BrandButton (const juce::String& buttonText, Style buttonStyle)
        : juce::Button (buttonText), style (buttonStyle)
    {
    }

    void BrandButton::setIconName (const juce::String& name)
    {
        iconName = name;
        repaint();
    }

    void BrandButton::setIconSize (int pixels) noexcept
    {
        iconSize = juce::jmax (0, pixels);
        repaint();
    }

    void BrandButton::setIconOnly (bool shouldBeIconOnly, int squareSize)
    {
        iconOnly = shouldBeIconOnly;
        iconOnlySize = juce::jmax (1, squareSize);
        repaint();
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
        if (iconOnly)
            return iconOnlySize;

        const auto textWidth = juce::GlyphArrangement::getStringWidthInt (getLabelFont(), getButtonText());
        const auto iconPart = iconName.isNotEmpty() ? iconSize + iconLabelGap : 0;
        return juce::jlimit (minWidth, maxWidth, 2 * padX + iconPart + textWidth);
    }

    BrandButton::Appearance BrandButton::getAppearance (bool shouldDrawButtonAsHighlighted,
                                                       bool shouldDrawButtonAsDown,
                                                       bool focused) const
    {
        Appearance a;
        a.background      = brand::bgTertiary;
        a.borderColour    = brand::border;
        a.textColour      = brand::textPrimary;
        a.gradientTop     = brand::accent;
        a.gradientBottom  = brand::accentDeep;

        const auto enabled = isEnabled();
        const auto on = getToggleState();

        switch (style)
        {
            case Style::Primary:
                a.useGradient = true;
                a.borderColour = juce::Colours::transparentBlack;

                if (shouldDrawButtonAsDown)
                {
                    a.gradientTop = brand::accent;
                    a.gradientBottom = brand::accentDeep.darker (0.1f);
                }
                else if (shouldDrawButtonAsHighlighted)
                {
                    a.gradientTop = brand::accentHover;
                    a.gradientBottom = brand::accent;
                }
                break;

            case Style::Record:
                if (on)
                {
                    a.background = shouldDrawButtonAsHighlighted ? brand::recordHover : brand::record;
                    a.borderColour = a.background;
                    a.textColour = brand::textPrimary;
                }
                else
                {
                    a.textColour = brand::record;
                    a.borderColour = shouldDrawButtonAsHighlighted ? brand::record : brand::border;
                }
                break;

            case Style::Transport:
                a.background = shouldDrawButtonAsHighlighted ? brand::bgElevated : brand::bgTertiary;
                a.borderColour = shouldDrawButtonAsHighlighted ? brand::borderHover : brand::border;

                if (on)
                {
                    a.background = brand::accentMuted;
                    a.borderColour = brand::accent;
                    a.textColour = brand::accent;
                }
                break;

            case Style::Chip:
                if (on && hasOnColours)
                {
                    a.background = onBackground;
                    a.borderColour = onBorder;
                    a.textColour = onText;
                }
                else
                {
                    a.background = brand::bgTertiary;
                    a.borderColour = shouldDrawButtonAsHighlighted ? brand::borderHover : brand::border;
                    a.textColour = brand::textSecondary;
                }
                break;

            case Style::Secondary:
            default:
                a.background = shouldDrawButtonAsHighlighted ? brand::bgElevated : brand::bgTertiary;
                a.borderColour = shouldDrawButtonAsHighlighted ? brand::borderHover : brand::border;
                a.textColour = brand::textPrimary;
                break;
        }

        if (! enabled)
        {
            a.background = brand::bgPanel;
            a.borderColour = brand::border;
            a.textColour = brand::textDisabled;
            a.useGradient = false;
        }
        else if (focused)
        {
            // Keyboard focus is shown by thickening the ONE state border, never by
            // stacking a second accent ring on top of it. The old 2 px accent
            // (orange) focus ring over the green Monitor chip read as both the
            // Arm and Monitor states at once (BUG B).
            a.borderThickness = 2.0f;

            // Primary has no border of its own: give the focus state a visible
            // one so the thicker border is actually seen.
            if (a.borderColour.isTransparent())
                a.borderColour = brand::textPrimary;
        }

        return a;
    }

    void BrandButton::paintButton (juce::Graphics& g,
                                   bool shouldDrawButtonAsHighlighted,
                                   bool shouldDrawButtonAsDown)
    {
        // Resolve the whole appearance from the button's *actual* state first.
        // Every paint then re-fills the background and draws exactly one border,
        // so no fill/outline can survive from a previous state (BUG B).
        const auto a = getAppearance (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown,
                                      hasKeyboardFocus (false));

        auto bounds = getLocalBounds().toFloat().reduced (0.5f);

        if (a.useGradient)
        {
            juce::ColourGradient gradient (a.gradientTop, bounds.getCentreX(), bounds.getY(),
                                           a.gradientBottom, bounds.getCentreX(), bounds.getBottom(), false);
            g.setGradientFill (gradient);
        }
        else
        {
            g.setColour (a.background);
        }

        g.fillRoundedRectangle (bounds, cornerRadius);

        if (isEnabled() && ! a.borderColour.isTransparent())
        {
            g.setColour (a.borderColour);
            g.drawRoundedRectangle (bounds, cornerRadius, a.borderThickness);
        }

        // Content: optional icon + label, laid out and centred as a single unit.
        if (iconOnly)
        {
            if (iconName.isNotEmpty())
            {
                // Inset glyph (see iconOnlyGlyphSize) so the button's hit area
                // stays larger than the visible icon. Clamp for tiny buttons.
                const auto glyph = juce::jmin (iconOnlyGlyphSize, getWidth(), getHeight());
                const auto target = juce::Rectangle<int> (glyph, glyph)
                                        .withCentre (getLocalBounds().getCentre());
                IconCache::getInstance().drawIcon (g, iconName, a.textColour, target);
            }

            return;
        }

        const auto textWidth = juce::GlyphArrangement::getStringWidthInt (getLabelFont(), getButtonText());
        const auto iconPart = iconName.isNotEmpty() ? iconSize + iconLabelGap : 0;
        const auto totalWidth = iconPart + textWidth;

        auto content = getLocalBounds().withWidth (juce::jmin (getWidth(), totalWidth))
                                       .withX (getLocalBounds().getCentreX() - totalWidth / 2);

        if (iconName.isNotEmpty())
        {
            auto iconArea = content.removeFromLeft (iconSize);
            IconCache::getInstance().drawIcon (g, iconName, a.textColour, iconArea);
            content.removeFromLeft (iconLabelGap);
        }

        g.setColour (a.textColour);
        g.setFont (getLabelFont());
        g.drawText (getButtonText(), content, juce::Justification::centredLeft, false);
    }
}
