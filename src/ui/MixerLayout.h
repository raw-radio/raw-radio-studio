// raw-radio-studio — pure mixer-strip geometry (owner request / BUG A).
//
// The channel-strip vertical layout is split out here (like FaderTaper.h and
// MeterBallistics.h) so it is unit-testable without a Session or an engine.
//
// BUG A: the level readout was drawn at the top of the fader travel, so the
// thumb covered it when the fader was raised to the top. The strip now reserves
// a *dedicated* readout row ABOVE the travel: `splitLevelAndFader()` hands the
// readout its own band and the travel starts strictly below it, so the thumb —
// even at position 1.0 — can never reach the text.
//
// JUCE-GUI-free apart from `juce::Rectangle`/`jlimit`; never touched from the
// audio callback.

#pragma once

#include <juce_graphics/juce_graphics.h>

namespace rrs::mixer_layout
{
    /** Height of the level-readout row reserved above every strip's fader
        travel. Enough for the 10 pt mono readout plus vertical padding. */
    inline constexpr int levelReadoutHeight = 14;

    /** Splits the strip column left between the control rows (bottom) and the
        top of the strip into the level-readout row (top) and the fader travel
        (the rest, strictly below the readout).

        `inner` is passed by value; `levelRow` and `faderTravel` are outputs.
        The readout is intentionally reserved first so the travel can never
        extend up into it (BUG A). */
    inline void splitLevelAndFader (juce::Rectangle<int> inner,
                                    juce::Rectangle<int>& levelRow,
                                    juce::Rectangle<int>& faderTravel) noexcept
    {
        levelRow = inner.removeFromTop (levelReadoutHeight);
        faderTravel = inner;
    }

    /** Thumb centre Y for a taper position `t` (0 = bottom of travel, 1 = top)
        inside a fader area, using the strip's 4 px end inset. Shared by drawing
        and hit-testing so the two can never disagree. */
    inline float faderThumbY (juce::Rectangle<int> fader, float t) noexcept
    {
        const auto top    = (float) fader.getY() + 4.0f;
        const auto bottom = (float) fader.getBottom() - 4.0f;
        t = juce::jlimit (0.0f, 1.0f, t);
        return bottom - t * (bottom - top);
    }
}
