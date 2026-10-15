// raw-radio-studio — uniform track-lane geometry (owner bug fix; Epic 8 phase 1a).
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
// Epic 8 phase 1a adds a **minimum lane height** and **vertical scrolling**:
// lanes never squeeze below `minHeight` (56 px) any more — when the tracks no
// longer fit at their auto height, the arrangement scrolls instead of compressing
// lanes to 1–3 px (options R-A). A **global lane-height preset** (compact 56 /
// normal 72 / large 96) overrides the auto height. The scroll model is pure
// geometry: `scrollOffsetY` shifts every lane up by a fixed amount and the
// helpers below (`maxScrollOffset`, `clampScrollOffset`, `visibleLaneRange`,
// `laneIndexAtY`) keep paint() and the y→lane hit-test mapping in lockstep.
//
// This is pure geometry (`juce::Rectangle`), separated out like MixerLayout.h /
// DevicePanelLayout.h so it is unit-testable without a Session or an engine.
// Never touched from the audio thread.

#pragma once

#include <juce_graphics/juce_graphics.h>

#include <utility>
#include <vector>

namespace rrs::track_lane
{
    /** Preferred lane height (px) — the uniform height every lane gets while it
        fits. Typical DAWs use ~50–100 px; 72 leaves room for a readable track
        name plus a comfortable clip band with usable fade/trim hit zones. */
    inline constexpr int preferredHeight = 72;

    /** Horizontal inset so lanes do not sit flush against the arrangement edge.
        This is also the ruler's `hInset` (Timeline::hInset) — the single constant
        that makes the clip zone start exactly on the gutter's right edge (see
        `laneContentX`). Keep the two bound (Timeline.h sets hInset from here). */
    inline constexpr int laneInset = 8;
    inline constexpr int hMargin = laneInset;

    /** Vertical inset applied once to the whole lane stack. */
    inline constexpr int vMargin = 6;

    /** Minimum lane height (px). Epic 8 phase 1a (owner decision R-A): the auto
        compression of `uniformLaneHeight` never goes below this — when the lanes
        can no longer fit at 56 px the arrangement **scrolls** instead of
        squeezing lanes to 1–3 px. */
    inline constexpr int minHeight = 56;

    /** Lane-height presets (owner decision R-A): compact 56 / normal 72 /
        large 96. «Compact» is 56, NOT 40 — values below `minHeight` are never
        used. The preset index is what the app persists (`ui.trackHeightPreset`):
        0 = compact, 1 = normal, 2 = large. */
    inline constexpr int presetCompact = 56;
    inline constexpr int presetNormal  = preferredHeight; // 72
    inline constexpr int presetLarge   = 96;

    /** The pixel height for a preset index (0/1/2); anything else = normal. */
    inline int presetHeight (int preset) noexcept
    {
        switch (preset)
        {
            case 0: return presetCompact;
            case 2: return presetLarge;
            default: return presetNormal;
        }
    }

    /** True when `preset` is one of the three persisted values. */
    inline bool isValidPreset (int preset) noexcept
    {
        return preset >= 0 && preset <= 2;
    }

    /** The single height shared by every lane for `numTracks` tracks inside an
        area `areaHeight` px tall.

        * `preset == normal` (the default): the auto height — never larger than
          `preferredHeight`, shrinking evenly when the tracks cannot all fit at
          72 px, but **never below `minHeight`** (the scroll floor).
        * any other preset: the explicit user height (56/96), which overrides the
          auto minimum. */
    inline int uniformLaneHeight (int areaHeight, int numTracks, int preset = 1) noexcept
    {
        if (numTracks <= 0)
            return preferredHeight;

        if (preset != 1)
            return presetHeight (preset);

        const auto usable = juce::jmax (0, areaHeight - 2 * vMargin);
        return juce::jlimit (minHeight, preferredHeight, usable / numTracks);
    }

    /** Total content height of the lane stack (both vertical margins + every
        lane at its uniform height). Independent of the viewport height, so
        `contentHeight - areaHeight` is the scrollable distance. */
    inline int laneStackHeight (int numTracks, int laneHeight) noexcept
    {
        return 2 * vMargin + juce::jmax (0, numTracks) * juce::jmax (0, laneHeight);
    }

    /** Largest legal `scrollOffsetY`: 0 when everything fits, otherwise the
        overflow of the content over the viewport. */
    inline int maxScrollOffset (int areaHeight, int numTracks, int laneHeight) noexcept
    {
        return juce::jmax (0, laneStackHeight (numTracks, laneHeight) - juce::jmax (0, areaHeight));
    }

    /** Scroll offset clamped to [0, maxScrollOffset]. */
    inline int clampScrollOffset (int offset, int areaHeight, int numTracks, int laneHeight) noexcept
    {
        return juce::jlimit (0, maxScrollOffset (areaHeight, numTracks, laneHeight),
                             juce::jmax (0, offset));
    }

    /** True when the lane stack is taller than the viewport (a scrollbar is
        wanted). */
    inline bool needsScroll (int areaHeight, int numTracks, int laneHeight) noexcept
    {
        return maxScrollOffset (areaHeight, numTracks, laneHeight) > 0;
    }

    /** Half-open index range [first, last) of the lanes that intersect the
        viewport at `scrollOffsetY`. Used by paint() to skip off-screen lanes and
        unit-tested against `laneIndexAtY`. */
    inline std::pair<int, int> visibleLaneRange (int areaHeight, int numTracks,
                                                 int laneHeight, int scrollOffsetY) noexcept
    {
        if (numTracks <= 0 || laneHeight <= 0 || areaHeight <= 0)
            return { 0, 0 };

        const int offset = clampScrollOffset (scrollOffsetY, areaHeight, numTracks, laneHeight);

        int first = 0;

        while (first < numTracks && vMargin + (first + 1) * laneHeight - offset <= 0)
            ++first;

        int last = first;

        while (last < numTracks && vMargin + last * laneHeight - offset < areaHeight)
            ++last;

        return { first, last };
    }

    /** Lane index under `yInArea` (y measured from the top of the lane area), or
        -1 outside the stack. This is the inverse of `computeLaneRects` for a
        given `scrollOffsetY`, so a drawn lane and its click hit-test can never
        disagree. */
    inline int laneIndexAtY (int yInArea, int numTracks, int laneHeight, int scrollOffsetY) noexcept
    {
        if (numTracks <= 0 || laneHeight <= 0)
            return -1;

        const int contentY = yInArea + juce::jmax (0, scrollOffsetY) - vMargin;

        if (contentY < 0)
            return -1;

        const int index = contentY / laneHeight;
        return (index >= 0 && index < numTracks) ? index : -1;
    }

    /** One rectangle per track, in engine order, all exactly the same height
        (see `uniformLaneHeight`). `scrollOffsetY` shifts the whole stack up by
        that many pixels; lanes may then lie partly above the area (negative y)
        or below it — the caller clips the paint and the scrollbar bounds the
        offset, so the stack is never drawn outside its own region. */
    inline std::vector<juce::Rectangle<int>> computeLaneRects (juce::Rectangle<int> area,
                                                               int numTracks,
                                                               int scrollOffsetY = 0,
                                                               int preset = 1)
    {
        std::vector<juce::Rectangle<int>> rects;

        if (numTracks <= 0 || area.getWidth() <= 0 || area.getHeight() <= 0)
            return rects;

        const auto width = area.getWidth() - 2 * hMargin;

        if (width <= 0)
            return rects;

        const auto height = uniformLaneHeight (area.getHeight(), numTracks, preset);
        const auto x = area.getX() + hMargin;
        const auto top = area.getY() + vMargin - juce::jmax (0, scrollOffsetY);

        rects.reserve ((size_t) numTracks);

        for (int i = 0; i < numTracks; ++i)
            rects.push_back ({ x, top + i * height, width, height });

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

    //==========================================================================
    // Epic 8 phase 1b — in-lane control band (the "gutter").
    //
    // The control band is the LEFT `gutterWidth` px of every lane. Because every
    // lane shares the same x and width (computeLaneRects), these bands line up
    // into one vertical gutter column at the left of the arrangement. The clip
    // zone starts to the right of the gutter; the x<->time map
    // (Timeline::xForSeconds) is unchanged except that its left edge is
    // `trackLaneArea.getX() + gutterWidth` (see MainComponent). This header is
    // the single source of truth for the gutter width and the control geometry,
    // shared by paint() and the hit-tests.
    inline constexpr int gutterWidth = 112;

    /** The x where a lane's clip zone (and the timeline content) begins: the
        gutter's right edge. Derived from one constant set so the gutter, the
        clip zone and the ruler can never disagree:
            laneContentX = area.x + hMargin + gutterWidth
        and the x<->time map is `timelineAreaFor(area)` + `hInset`, which lands
        s=0 exactly on `laneContentX(area)` because `hInset == hMargin == laneInset`. */
    inline int laneContentX (juce::Rectangle<int> area) noexcept
    {
        return area.getX() + hMargin + gutterWidth;
    }

    /** The area the ruler / clip map / snap threshold operate on: the lane area
        with the gutter carved off the left. Its `hInset` then starts the clips
        exactly at `laneContentX(area)`. */
    inline juce::Rectangle<int> timelineAreaFor (juce::Rectangle<int> area) noexcept
    {
        return area.withTrimmedLeft (gutterWidth);
    }

    /** Adapted sizes per lane height (owner R-A presets 56 / 72 / 96).
        `inputVisible` keeps the spec rule (< 44 px hides the input chip); with
        the 56 px floor it is always true in practice. */
    struct ControlMetrics
    {
        int arm = 20;
        int ms = 18;
        int inputW = 28;
        int inputH = 18;
        int meterW = 6;
        int pad = 4;
        int gapSmall = 2;
        int gapLarge = 4;
        bool inputVisible = true;
    };

    /** Control sizes for a lane of `laneHeight` px: compact (56), normal (72)
        and large (96) each get a slightly different scale. */
    inline ControlMetrics controlMetrics (int laneHeight) noexcept
    {
        ControlMetrics m;

        if (laneHeight < 64)            // compact (56)
        {
            m.arm = 18; m.ms = 16; m.inputW = 24; m.inputH = 16; m.meterW = 6;
        }
        else if (laneHeight < 84)       // normal (72)
        {
            m.arm = 20; m.ms = 18; m.inputW = 28; m.inputH = 18; m.meterW = 6;
        }
        else                            // large (96): full set
        {
            m.arm = 20; m.ms = 18; m.inputW = 28; m.inputH = 18; m.meterW = 6;
        }

        m.inputVisible = laneHeight >= 44;
        return m;
    }

    /** Which control a point is over (for hover + click). `band` = the gutter
        background but not a control. */
    enum class LaneControl { none, band, arm, mute, solo, input, meter };

    /** Pixel rectangles of one lane's controls. `input`/`meter` are always laid
        out (their slot is reserved for alignment) even on playback-only tracks;
        the caller decides whether to draw/act on them via `isInput`. */
    struct LaneControlRects
    {
        juce::Rectangle<int> band;
        juce::Rectangle<int> arm, mute, solo, input, meter;
    };

    /** Computes the control rectangles for `lane`. The band is the left
        `gutterWidth` px of the lane's clip band (vertically inset by
        `clipInset`). */
    inline LaneControlRects computeLaneControlRects (juce::Rectangle<int> lane) noexcept
    {
        LaneControlRects r;

        auto band = lane.reduced (0, clipInset);
        band.setWidth (juce::jmin (gutterWidth, band.getWidth()));
        r.band = band;

        const auto m = controlMetrics (lane.getHeight());
        const auto cy = band.getCentreY();
        int x = band.getX() + m.pad;

        auto take = [&x, cy] (int w, int h)
        {
            const auto rect = juce::Rectangle<int> (x, cy - h / 2, w, h);
            x += w;
            return rect;
        };

        r.arm = take (m.arm, m.arm);          x += m.gapSmall;
        r.mute = take (m.ms, m.ms);           x += m.gapSmall;
        r.solo = take (m.ms, m.ms);           x += m.gapLarge;
        r.input = take (m.inputW, m.inputH);  x += m.gapLarge;

        r.meter = { x, band.getY() + 2, m.meterW, juce::jmax (6, band.getHeight() - 4) };
        return r;
    }

    /** Which control `p` falls on inside `lane`'s gutter, or `none` when `p` is
        not in the gutter at all (i.e. it is in the clip zone / timeline). */
    inline LaneControl controlAt (juce::Rectangle<int> lane, bool isInput,
                                  juce::Point<int> p) noexcept
    {
        const auto r = computeLaneControlRects (lane);

        if (! r.band.contains (p))
            return LaneControl::none;

        if (r.arm.contains (p))   return LaneControl::arm;
        if (r.mute.contains (p))  return LaneControl::mute;
        if (r.solo.contains (p))  return LaneControl::solo;

        // Below the input-visible threshold (< 44 px) the chip/meter collapse and
        // their slot is inert; at the 56 px floor this is always visible.
        const bool inputVisible = isInput && controlMetrics (lane.getHeight()).inputVisible;

        if (inputVisible && r.input.contains (p)) return LaneControl::input;
        if (inputVisible && r.meter.contains (p)) return LaneControl::meter;

        return LaneControl::band;
    }
}
