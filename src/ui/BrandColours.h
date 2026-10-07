// raw-radio-studio — RAW Radio brand colour tokens.
//
// Single source of truth for every brand colour used by the JUCE UI. Values are
// the approved RAW Radio design tokens (dark, orange accent #FF6B35) and mirror
// the palette the RAW Radio admin/app use, so the desktop studio reads as the
// same product family.
//
// Header-only `inline const` variables (C++17) — no link-time state, safe to
// include from any UI translation unit. UI-only: never touch these from the
// real-time audio callback.

#pragma once

#include <juce_graphics/juce_graphics.h>

namespace rrs::brand
{
    // --- Surfaces ------------------------------------------------------------
    inline const juce::Colour bgWindow    { 0xff0d0d0d };
    inline const juce::Colour bgPanel     { 0xff1a1a1a };
    inline const juce::Colour bgTertiary  { 0xff2a2a2a };
    inline const juce::Colour bgElevated  { 0xff333333 };

    // --- Text ----------------------------------------------------------------
    inline const juce::Colour textPrimary   { 0xffffffff };
    inline const juce::Colour textSecondary { 0xffb3b3b3 };
    inline const juce::Colour textTertiary  { 0xff737373 };
    inline const juce::Colour textDisabled  { 0xff555555 };

    // --- Accent (brand primary) ---------------------------------------------
    inline const juce::Colour accent        { 0xffff6b35 };
    inline const juce::Colour accentHover   { 0xffff8c5a };
    inline const juce::Colour accentMuted   { 0x40ff6b35 };

    // --- Status --------------------------------------------------------------
    inline const juce::Colour success { 0xff2ecc71 };
    inline const juce::Colour warning { 0xfff1c40f };
    inline const juce::Colour error   { 0xffe74c3c };
    inline const juce::Colour offline { 0xff555555 };

    // --- Borders / focus -----------------------------------------------------
    inline const juce::Colour border      { 0xff333333 };
    inline const juce::Colour borderHover { 0x40ff6b35 };
    inline const juce::Colour borderFocus { 0xffff6b35 };

    // --- DAW surfaces (track lanes, clips) ----------------------------------
    inline const juce::Colour laneArea      { 0xff0d0d0d };
    inline const juce::Colour lane          { 0xff1a1a1a };
    inline const juce::Colour laneAlt       { 0xff222222 };
    inline const juce::Colour laneArmed     { 0xff2a2a2a };
    inline const juce::Colour clip          { 0xff2a2a2a };
    inline const juce::Colour clipSelected  { 0xff333333 };

    // --- Record / error ------------------------------------------------------
    inline const juce::Colour record      { 0xffef4444 };
    inline const juce::Colour recordHover { 0xfff87171 };
    inline const juce::Colour errorText   { 0xfff87171 };

    // --- Meters --------------------------------------------------------------
    inline const juce::Colour vuGreen     { 0xff00ff88 };
    inline const juce::Colour vuYellow    { 0xffffcc00 };
    inline const juce::Colour vuRed       { 0xffff3333 };
    inline const juce::Colour meterGreen  { 0xff2ecc71 };
    inline const juce::Colour meterTrough { 0xff0d0d0d };

    // --- Extras --------------------------------------------------------------
    inline const juce::Colour remote           { 0xff38bdf8 };
    inline const juce::Colour gainFill         { 0xffff8800 };
    inline const juce::Colour gainThumb        { 0xffffaa33 };
    inline const juce::Colour gainTrack        { 0xff1a1a35 };
    /** End stop of the primary-action accent gradient (#FF6B35 -> #FF4500). */
    inline const juce::Colour accentDeep       { 0xffff4500 };
}
