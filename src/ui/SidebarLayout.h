// raw-radio-studio — left sidebar geometry + persisted section state
// (Epic 8 phase 1a).
//
// Pure, header-only helpers so the sidebar's numeric contract (width clamps,
// header height, section on/off bitmask) can be unit-tested without a GUI. The
// actual drawing / interaction lives in SidebarPanel.cpp. Nothing here touches
// the audio thread.

#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

namespace rrs::sidebar
{
    /** Collapsed rail width (icon-only strip). */
    inline constexpr int railWidth = 44;

    /** Expanded width default / bounds (device panel content fits at 240). */
    inline constexpr int defaultWidth = 320;
    inline constexpr int minWidth = 240;
    inline constexpr int maxWidth = 420;

    /** Accordion header height. */
    inline constexpr int headerHeight = 34;

    /** Right-edge resize hit zone (visual line is 1 px). */
    inline constexpr int resizeZone = 4;

    /** Inner padding of the sidebar and the gap between sections. */
    inline constexpr int padding = 8;
    inline constexpr int sectionGap = 6;

    /** The rail icon box (32x32 hit box, 16 px icon) and its vertical padding. */
    inline constexpr int railIconSize = 32;
    inline constexpr int railIconPadding = 6;

    /** The collapse/expand toggle box at the top of the column. Expanded: a
        24x24 chevron button at the top-right — deliberately INSIDE the column
        (never overlapping the 4 px resize zone on the very edge). Collapsed: a
        32x32 box at the top of the rail, above the section icons. Kept here so
        the widget's geometry is unit-testable (see the handle-overlap test). */
    inline juce::Rectangle<int> handleRectExpanded (int width) noexcept
    {
        constexpr int size = 24;
        return { width - size - 6, padding + (headerHeight - size) / 2, size, size };
    }

    inline juce::Rectangle<int> handleRectRail() noexcept
    {
        const int x = (railWidth - railIconSize) / 2;
        return { x, railIconPadding, railIconSize, railIconSize };
    }

    /** Content heights of the two lightweight sections (the Device section asks
        the DevicePanel for its own dynamic height). */
    inline constexpr int channelsContentHeight = 56;
    inline constexpr int statusContentHeight = 76;

    /** Fixed width the transport row needs for its icons + count-in/MIDI
        combos + time read-out (measured; see MainComponent::resized). Used to
        cap the sidebar so the transport row is never clipped at the minimum
        window size. */
    inline constexpr int transportRowMinWidth = 700;

    /** Extra slack kept to the right of the sidebar, over `transportRowMinWidth`,
        so the timecode gets a few visible pixels rather than the bare minimum. */
    inline constexpr int reservedRightSlack = 8;

    /** Clamp an expanded width to the legal range. */
    inline int clampWidth (int w) noexcept
    {
        return juce::jlimit (minWidth, maxWidth, w);
    }

    /** Clamp an expanded width to [minWidth, maxW] where `maxW` is an
        already-computed effective maximum (see `effectiveMaxWidth`). */
    inline int clampWidth (int w, int maxW) noexcept
    {
        return juce::jlimit (minWidth, juce::jmax (minWidth, maxW), w);
    }

    /** The largest expanded width the sidebar may take for a given window width.
        The right work column must keep at least `transportRowMinWidth` for the
        fixed transport row, so:
            effectiveMax = clamp(minWidth..maxWidth,
                                 windowWidth - 2*12 - 8 - transportRowMinWidth - reservedRightSlack)
        (2*12 = outer margin, 8 = sidebar/right gap). At the 1100 px minimum
        window this yields 360 px instead of 420, keeping the timecode visible. */
    inline int effectiveMaxWidth (int windowWidth) noexcept
    {
        const int maxForWindow = windowWidth - 24 - 8 - transportRowMinWidth - reservedRightSlack;
        return juce::jlimit (minWidth, maxWidth, maxForWindow);
    }


    /** Which accordion sections are open. Default per the UX spec: Device and
        Channels open, Status collapsed. */
    struct Sections
    {
        bool device = true;
        bool channels = true;
        bool status = false;
    };

    /** Encodes the section state as a 3-char string for `ui.sidebarSections`:
        uppercase letter = open, lowercase = closed (e.g. "DCc"). */
    inline juce::String encodeSections (Sections s)
    {
        juce::String out;
        out << (s.device ? 'D' : 'd')
            << (s.channels ? 'C' : 'c')
            << (s.status ? 'S' : 's');
        return out;
    }

    /** Decodes `encodeSections`. An empty/short string yields the defaults, so a
        missing setting behaves like a fresh install. */
    inline Sections decodeSections (const juce::String& text)
    {
        auto t = text.trim();

        if (t.length() < 3)
            return {};

        Sections s;
        s.device   = juce::CharacterFunctions::isUpperCase (t[0]);
        s.channels = juce::CharacterFunctions::isUpperCase (t[1]);
        s.status   = juce::CharacterFunctions::isUpperCase (t[2]);
        return s;
    }
}
