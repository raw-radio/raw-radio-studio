// raw-radio-studio — uniform track-lane geometry (owner bug fix).
//
// The arrangement used to give the FIRST lane a fixed height and then shrink
// every following lane into whatever vertical space was left
// (`jmin (46, remaining)`). The armed input track is always first, so its clips
// stayed tall while imported "minus" backing clips below it were squeezed,
// making their fades/trim handles hard to hit. The fix: one *uniform* lane
// height for every track — 72 px (a sensible DAW default, in the usual
// 50–100 px range) whenever the arrangement is tall enough, shrinking evenly
// only when many tracks share a short window so the lanes always stay identical.
//
// Uniform lanes alone were not enough (owner report: input track still looked
// taller than the imported clips). The clip band was still `lane` minus a
// reserved 20 px name row minus insets, so in a 38 px lane (the default window)
// the imported clip collapsed to the 8 px floor while the armed input lane's
// background filled the lane — a tall block next to a thin strip. A clip must
// therefore *fill its lane*: the name is a top overlay drawn ON TOP of the clip,
// not a reserved row, so an input clip and an imported clip are exactly the same
// height and the trim/fade hit rectangles match what is drawn.
//
// This is pure geometry (`juce::Rectangle`), separated out like MixerLayout.h /
// DevicePanelLayout.h so it is unit-testable without a Session or an engine.
// Never touched from the audio thread.

#pragma once

#include <juce_graphics/juce_graphics.h>

#include <vector>

namespace rrs::track_lane
{
    /** Preferred lane height (px) — the uniform height every lane gets while it
        fits. Typical DAWs use ~50–100 px; 72 leaves room for a readable track
        name plus a comfortable clip band with usable fade/trim hit zones. */
    inline constexpr int preferredHeight = 72;

    /** Horizontal inset so lanes do not sit flush against the arrangement edge. */
    inline constexpr int hMargin = 8;

    /** Vertical inset applied once to the whole lane stack. */
    inline constexpr int vMargin = 6;

    /** The single height shared by every lane for `numTracks` tracks inside an
        area `areaHeight` px tall. Never larger than `preferredHeight`; shrinks
        evenly (but stays >= 1 px) when the tracks cannot all fit at 72 px. */
    inline int uniformLaneHeight (int areaHeight, int numTracks) noexcept
    {
        if (numTracks <= 0)
            return preferredHeight;

        const auto usable = juce::jmax (0, areaHeight - 2 * vMargin);
        return juce::jlimit (1, preferredHeight, usable / numTracks);
    }

    /** One rectangle per track, in engine order, all exactly the same height
        (see `uniformLaneHeight`). The rects are strictly inside `area`. */
    inline std::vector<juce::Rectangle<int>> computeLaneRects (juce::Rectangle<int> area,
                                                               int numTracks)
    {
        std::vector<juce::Rectangle<int>> rects;

        if (numTracks <= 0 || area.getWidth() <= 0 || area.getHeight() <= 0)
            return rects;

        auto lanes = area.reduced (hMargin, vMargin);
        const auto height = uniformLaneHeight (area.getHeight(), numTracks);

        rects.reserve ((size_t) numTracks);

        for (int i = 0; i < numTracks && lanes.getHeight() > 0; ++i)
            rects.push_back (lanes.removeFromTop (height));

        return rects;
    }

    /** Height of a lane's track-name overlay, when there is room for it. The
        overlay is drawn ON TOP of the clip band (see `splitLane`) and never
        shortens it. */
    inline constexpr int nameRowHeight = 20;

    /** Vertical breathing room kept above/below a clip inside its lane, so
        stacked lanes read as separate rows. Small: the clip still fills the
        lane for all but this uniform inset. */
    inline constexpr int clipInset = 2;

    /** Splits one lane into the full-height clip band and the track-name overlay
        drawn over it. The clip fills the lane (minus `clipInset` on every side),
        so an input clip and an imported clip are exactly the same height
        regardless of how many tracks share the arrangement. Shared by paint()
        and clipRectFor() so a drawn clip and its hit rectangle can never
        disagree, and the clip is always kept inside the lane. */
    inline void splitLane (juce::Rectangle<int> lane,
                           juce::Rectangle<int>& nameRow,
                           juce::Rectangle<int>& clipBand) noexcept
    {
        clipBand = lane.reduced (0, clipInset);

        auto inner = lane.reduced (10, 4);
        nameRow = inner.removeFromTop (juce::jmin (nameRowHeight, inner.getHeight()));
    }
}
