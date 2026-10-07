// raw-radio-studio — fader taper (owner request).
//
// A tiny, pure, JUCE-GUI-free fader law shared by every mixer fader — track
// gain, master gain and the per-input record trim (FR-MIX-1 / FR-REC-4).
//
// The owner reported that the faders "feel wrong": lowering from the top the
// level dropped most of the way already around 0.7 of the travel and was
// practically silent at 0.1. The mapping was *already* linear in dB — and that
// was the real problem: it spanned -60 .. +6 dB, so 0 dB sat at ~0.909 of the
// travel and the bottom two thirds of the fader swept an almost inaudible range
// (-60 .. -20 dB), squeezing everything usable into the top. At p = 0.7 the old
// law was already -46.2 dB and at p = 0.1 a near-silent -53.4 dB — exactly the
// "big drop, then nothing" the owner described.
//
// The fix is a range change, not a new law: the **level** fader now spans
// -40 .. +6 dB. Unity moves to ~0.870, p = 0.7 is ~-7.8 dB and p = 0.1 is
// ~-35.4 dB, so equal travel steps stay equal dB steps but the travel is spent
// on a useful range. 0 dB therefore still lands at a predictable, documented
// point — the same on every fader.
//
// This header is the single source of truth for both directions (position ->
// dB and dB -> position) so the UI's drawing and its drag handling can never
// disagree, and so the range is tuned in exactly one place.
//
// Tracktion-free / JUCE-GUI-free (only <algorithm>/<cmath>), so it is unit
// tested without an engine or a window. Never touched from the audio callback.

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace rrs::fader
{
    /** A linear-in-dB fader law.

        The position `p` in [0, 1] maps to decibels with the same shape on every
        fader:

            dB(p) = floorDb + p * (maxDb - floorDb)        for p in (0, 1]

        i.e. equal travel steps are equal dB steps. The mapping is exact in the
        other direction too (`dbToPos` is the inverse), so drawing a value and
        dragging it back to the same pixel is lossless (up to rounding).

        `unityPos()` reports where 0 dB lands; for the standard level taper
        (-40 dB .. +6 dB) that is `(0 - -40) / (6 - -40) = 40/46 ~= 0.870` —
        comfortably under the top of the travel, the usual console layout.

        `muteAtBottom` makes position 0 a hard mute detent: `posToDb(0)` returns
        -infinity dB so the fader bottoms out to true silence rather than resting
        at `floorDb`. Any value at or below `floorDb` (including -inf) reads back
        as the bottom of the travel, so the state round-trips.
    */
    struct Taper
    {
        float floorDb = -60.0f;
        float maxDb   =   6.0f;
        bool  muteAtBottom = true;

        /** Decibels for a fader position `p` (clamped to [0, 1]). With
            `muteAtBottom`, `p == 0` yields -infinity (mute). */
        float posToDb (float p) const noexcept
        {
            p = std::clamp (p, 0.0f, 1.0f);

            if (muteAtBottom && p <= 0.0f)
                return -std::numeric_limits<float>::infinity();

            return floorDb + p * (maxDb - floorDb);
        }

        /** Fader position in [0, 1] for a dB value — the inverse of `posToDb`.
            -infinity and anything at or below `floorDb` map to 0. */
        float dbToPos (float db) const noexcept
        {
            if (std::isinf (db) && db < 0.0f)
                return 0.0f;

            const auto span = maxDb - floorDb;

            if (span <= 0.0f)
                return 0.0f;

            return std::clamp ((db - floorDb) / span, 0.0f, 1.0f);
        }

        /** Position at which this taper is exactly at 0 dB (gain unity). */
        float unityPos() const noexcept
        {
            return dbToPos (0.0f);
        }

        /** True when `db` represents the mute/bottom end of the travel. Always
            false for a non-muting taper (only the level faders mute). */
        bool isSilentDb (float db) const noexcept
        {
            if (! muteAtBottom)
                return false;

            return (std::isinf (db) && db < 0.0f) || db <= floorDb;
        }
    };

    /** Standard level-fader travel: -40 dB at the bottom (below which the fader
        mutes), +6 dB at the top. 0 dB sits at ~0.870 of the travel; p = 0.7 is
        ~-7.8 dB and p = 0.1 is ~-35.4 dB. Used by the per-track gain and the
        master gain faders. */
    inline constexpr Taper level { -40.0f, 6.0f, true };

    /** Per-input record trim (FR-REC-4): a symmetric, bidirectional gain control
        (-24 dB .. +24 dB) with unity 0 dB centred at 0.5, so the engineer can
        both cut and boost the input before it is monitored and recorded. It is a
        gain control, not a level fader, so it has no mute detent. */
    inline constexpr Taper inputTrim { -24.0f, 24.0f, false };
}
