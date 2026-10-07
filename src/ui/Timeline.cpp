// raw-radio-studio — basic clickable timeline + playhead (see Timeline.h).

#include "Timeline.h"

#include "BrandColours.h"
#include "BrandFonts.h"

#include <cmath>

namespace rrs
{
    Timeline::Timeline (Session& sessionRef)
        : session (sessionRef)
    {
        startTimerHz (30);
    }

    Timeline::~Timeline()
    {
        stopTimer();
    }

    //==============================================================================
    int Timeline::xForSeconds (double seconds, double length, juce::Rectangle<int> area) noexcept
    {
        const auto left  = area.getX() + hInset;
        const auto right = area.getRight() - hInset;
        const auto span  = juce::jmax (1, right - left);
        const auto frac  = length > 0.0 ? juce::jlimit (0.0, 1.0, seconds / length) : 0.0;

        return left + (int) std::lround (frac * (double) span);
    }

    double Timeline::secondsForX (int x, double length, juce::Rectangle<int> area) noexcept
    {
        const auto left  = area.getX() + hInset;
        const auto right = area.getRight() - hInset;
        const auto span  = (double) juce::jmax (1, right - left);
        const auto frac  = juce::jlimit (0.0, 1.0, ((double) x - (double) left) / span);

        return frac * juce::jmax (0.0, length);
    }

    //==============================================================================
    void Timeline::timerCallback()
    {
        repaint();
    }

    void Timeline::seekFromX (int x)
    {
        if (! isEnabled() || session.getEdit() == nullptr)
            return;

        session.setPositionSeconds (secondsForX (x, session.getTimelineLengthSeconds(), getLocalBounds()));
        repaint();
    }

    void Timeline::mouseDown (const juce::MouseEvent& e)
    {
        scrubbing = true;
        seekFromX (e.getPosition().x);
    }

    void Timeline::mouseDrag (const juce::MouseEvent& e)
    {
        if (scrubbing)
            seekFromX (e.getPosition().x);
    }

    void Timeline::mouseUp (const juce::MouseEvent&)
    {
        scrubbing = false;
    }

    //==============================================================================
    double Timeline::chooseTickStep (double length, int widthPx) noexcept
    {
        static constexpr double steps[] = { 1.0, 2.0, 5.0, 10.0, 15.0, 30.0,
                                            60.0, 120.0, 300.0, 600.0, 900.0, 1800.0, 3600.0 };
        constexpr int numSteps = (int) (sizeof (steps) / sizeof (steps[0]));
        const auto maxTicks = juce::jmax (2, widthPx / 70);

        for (int i = 0; i < numSteps; ++i)
            if (length / steps[i] <= (double) maxTicks)
                return steps[i];

        return steps[numSteps - 1];
    }

    juce::String Timeline::formatRulerTime (double seconds)
    {
        const auto total = (int) std::lround (juce::jmax (0.0, seconds));
        return juce::String::formatted ("%d:%02d", total / 60, total % 60);
    }

    //==============================================================================
    void Timeline::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds();
        const auto enabled = isEnabled();

        g.setColour (brand::bgPanel);
        g.fillRoundedRectangle (bounds.toFloat(), 6.0f);

        if (session.getEdit() == nullptr)
        {
            g.setColour (brand::textTertiary);
            g.setFont (brand::uiRegular (11.0f));
            g.drawText ("Timeline", bounds.reduced (10, 0), juce::Justification::centredLeft);
            return;
        }

        const auto length   = juce::jmax (1.0e-6, session.getTimelineLengthSeconds());
        const auto position = session.getPositionSeconds();

        // Ticks + labels.
        const auto step = chooseTickStep (length, bounds.getWidth());
        const auto tickY = (float) bounds.getBottom() - 7.0f;

        g.setFont (brand::monoRegular (9.0f));

        for (double t = 0.0; t <= length + 1.0e-6; t += step)
        {
            const auto tx = (float) xForSeconds (t, length, bounds);

            g.setColour (enabled ? brand::border : brand::border.withAlpha (0.4f));
            g.fillRect (juce::Rectangle<float> (tx, tickY, 1.0f, 4.0f));

            g.setColour (enabled ? brand::textTertiary : brand::textDisabled);
            g.drawText (formatRulerTime (t),
                        juce::Rectangle<int> ((int) tx - 26, bounds.getY() + 2, 52, 12),
                        juce::Justification::centred);
        }

        // Playhead: a full-height line with a small triangular head so it reads
        // as a transport marker, not a tick.
        const auto px = (float) xForSeconds (position, length, bounds);
        const auto head = (float) bounds.getY();

        g.setColour (enabled ? brand::accent : brand::textDisabled);
        g.fillRect (juce::Rectangle<float> (px - 1.0f, (float) bounds.getY(), 2.0f, (float) bounds.getHeight()));

        juce::Path marker;
        marker.addTriangle (px - 5.0f, head, px + 5.0f, head, px, head + 6.0f);
        g.fillPath (marker);
    }
}
