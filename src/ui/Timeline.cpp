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
        setTooltip ("Click or drag to move the playhead. Shift-drag to select a "
                    "region, then use Export region.");
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
    void Timeline::seekFromX (int x)
    {
        if (! isEnabled() || session.getEdit() == nullptr)
            return;

        session.setPositionSeconds (secondsForX (x, session.getTimelineLengthSeconds(), getLocalBounds()));
        repaint();
    }

    void Timeline::mouseDown (const juce::MouseEvent& e)
    {
        const auto length = session.getTimelineLengthSeconds();

        // Shift-drag creates an export region (FR-EXP-2); a plain click keeps
        // the existing scrub/seek behaviour and clears any stale selection.
        if (e.mods.isShiftDown())
        {
            selecting = true;
            selectAnchorSeconds = secondsForX (e.getPosition().x, length, getLocalBounds());
            session.setSelectionSeconds (selectAnchorSeconds, selectAnchorSeconds);
        }
        else
        {
            selecting = false;
            session.clearSelection();
            scrubbing = true;
            seekFromX (e.getPosition().x);
        }

        repaint();
    }

    void Timeline::mouseDrag (const juce::MouseEvent& e)
    {
        if (selecting)
        {
            session.setSelectionSeconds (selectAnchorSeconds,
                                         secondsForX (e.getPosition().x,
                                                      session.getTimelineLengthSeconds(),
                                                      getLocalBounds()));
            repaint();
            return;
        }

        if (scrubbing)
            seekFromX (e.getPosition().x);
    }

    void Timeline::mouseUp (const juce::MouseEvent&)
    {
        scrubbing = false;
        selecting = false;
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

        // Region selection (FR-EXP-2): a translucent accent band with solid
        // edge lines, drawn under the ticks so the ruler stays legible.
        if (session.hasSelection())
        {
            const auto sx = (float) xForSeconds (session.getSelectionStartSeconds(), length, bounds);
            const auto ex = (float) xForSeconds (session.getSelectionEndSeconds(), length, bounds);
            const auto selRect = juce::Rectangle<float> (sx, (float) bounds.getY(),
                                                         juce::jmax (1.0f, ex - sx),
                                                         (float) bounds.getHeight());

            g.setColour (brand::accent.withAlpha (0.22f));
            g.fillRect (selRect);

            g.setColour (brand::accent.withAlpha (0.9f));
            g.fillRect (juce::Rectangle<float> (sx, (float) bounds.getY(), 1.5f,
                                                (float) bounds.getHeight()));
            g.fillRect (juce::Rectangle<float> (ex - 1.5f, (float) bounds.getY(), 1.5f,
                                                (float) bounds.getHeight()));

            // Show the region duration when the band is wide enough to read.
            if (selRect.getWidth() > 64.0f)
            {
                g.setColour (brand::textPrimary);
                g.setFont (brand::monoRegular (9.0f));
                g.drawText (formatRulerTime (session.getSelectionEndSeconds()
                                             - session.getSelectionStartSeconds()),
                            selRect.toNearestInt().reduced (4, 0),
                            juce::Justification::centred);
            }
        }

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
