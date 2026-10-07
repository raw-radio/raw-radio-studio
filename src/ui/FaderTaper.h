// raw-radio-studio — fader taper (owner request).
//
// A tiny, pure, JUCE-GUI-free fader law shared by every mixer fader — track
// gain, master gain and the per-input record trim (FR-MIX-1 / FR-REC-4).
//
// Two families live here, distinguished by the `bend` parameter:
//
//   * The **level** faders (track gain, master gain) are linear-in-dB
//     (`bend == 0`), spanning -40 .. +6 dB. Unity sits at ~0.870 and equal
//     travel steps stay equal dB steps. This range was chosen after the owner
//     reported the old -60..+6 span squeezed everything usable into the top.
//
//   * The **record trim** (FR-REC-4) is a symmetric -24 .. +24 dB gain control
//     with unity at 0.5. The owner reported that, lowering it from the top, the
//     level dropped too fast: the old law was linear-in-dB, so leaving +24 dB
//     immediately gave away dB at a constant rate. The trim is now shaped by a
//     tunable **tanh S-curve around unity** (`bend > 0`), which holds the gain
//     close to the maximum for the first part of the downward travel and only
//     steepens through the middle. See `shapeForward()` below for the exact
//     formula and how to re-tune it.
//
// This header is the single source of truth for both directions (position ->
// dB and dB -> position) so the UI's drawing and its drag handling can never
// disagree, and so the curves are tuned in exactly one place.
//
// Tracktion-free / JUCE-GUI-free (only <algorithm>/<cmath>), so it is unit
// tested without an engine or a window. Never touched from the audio callback.

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace rrs::fader
{
    /** A fader law, linear-in-dB unless `bend` shapes it into an S-curve.

        The position `p` in [0, 1] maps to decibels:

            linear (bend == 0):
                dB(p) = floorDb + p * (maxDb - floorDb)

            S-curve (bend > 0):
                mid   = (floorDb + maxDb) / 2
                half  = (maxDb - floorDb) / 2
                s     = 2*p - 1                        (normalised, [-1, +1])
                dB(p) = mid + half * tanh(bend*s) / tanh(bend)

        `tanh(bend*s)/tanh(bend)` is the shaping function `g(s)`. It is odd
        (symmetric about unity), monotonic on [-1, 1], and normalised so
        `g(±1) = ±1`; therefore the endpoints (`floorDb` / `maxDb`) and the
        unity point (0 dB at `p == 0.5` for a symmetric taper) are exact for
        every `bend`.

        Tuning `bend`:
          * `bend = 0` → linear in dB (the level faders). Handled as a special
            case so their behaviour is bit-for-bit unchanged.
          * larger `bend` → flatter near the extremes (holds closer to max/min
            longer) and steeper through the middle.
          * the record trim uses `bend = 1.5`: at `p = 0.75` the trim is
            +16.8 dB (linear would be +12 dB), at `p = 0.9` it is +22.1 dB
            (linear would be +19.2 dB). Near the very top the local slope is
            ~0.14 dB per 1 % of travel versus ~0.48 dB for the old linear law.

        `dbToPos()` is the exact inverse (`atanh`), so drawing a value and
        dragging it back to the same pixel round-trips (up to float rounding).

        `unityPos()` reports where 0 dB lands: `40/46 ~= 0.870` for the
        standard level taper, `0.5` for the symmetric trim.

        `muteAtBottom` makes position 0 a hard mute detent: `posToDb(0)` returns
        -infinity dB so the level fader bottoms out to true silence rather than
        resting at `floorDb`. Any value at or below `floorDb` (including -inf)
        reads back as the bottom of the travel, so the state round-trips.
    */
    struct Taper
    {
        float floorDb = -60.0f;
        float maxDb   =   6.0f;
        bool  muteAtBottom = true;
        /** S-curve strength (0 = linear-in-dB). See the struct comment. */
        float bend    =   0.0f;

        /** Shaping function g(s) = tanh(bend*s)/tanh(bend) on s in [-1, 1].
            `bend` is assumed > 0 (callers special-case `bend <= 0`). */
        float shapeForward (float s) const noexcept
        {
            return std::tanh (bend * s) / std::tanh (bend);
        }

        /** Inverse of `shapeForward()`. `y` is clamped to [-1, 1]. */
        float shapeInverse (float y) const noexcept
        {
            y = std::clamp (y, -1.0f, 1.0f);
            return std::atanh (y * std::tanh (bend)) / bend;
        }

        /** Decibels for a fader position `p` (clamped to [0, 1]). With
            `muteAtBottom`, `p == 0` yields -infinity (mute). */
        float posToDb (float p) const noexcept
        {
            p = std::clamp (p, 0.0f, 1.0f);

            if (muteAtBottom && p <= 0.0f)
                return -std::numeric_limits<float>::infinity();

            if (bend <= 0.0f)
                return floorDb + p * (maxDb - floorDb);

            const auto mid  = 0.5f * (floorDb + maxDb);
            const auto half = 0.5f * (maxDb - floorDb);
            return mid + half * shapeForward (2.0f * p - 1.0f);
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

            if (bend <= 0.0f)
                return std::clamp ((db - floorDb) / span, 0.0f, 1.0f);

            const auto mid  = 0.5f * (floorDb + maxDb);
            const auto half = 0.5f * span;
            const auto y    = (db - mid) / half;
            return std::clamp (0.5f * (shapeInverse (y) + 1.0f), 0.0f, 1.0f);
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
        mutes), +6 dB at the top. Linear-in-dB (bend = 0). 0 dB sits at ~0.870
        of the travel; p = 0.7 is ~-7.8 dB and p = 0.1 is ~-35.4 dB. Used by the
        per-track gain and the master gain faders. */
    inline constexpr Taper level { -40.0f, 6.0f, true };

    /** S-curve strength for the record trim (owner request): see `Taper`. */
    inline constexpr float inputTrimBend = 1.5f;

    /** Per-input record trim (FR-REC-4): a symmetric, bidirectional gain control
        (-24 dB .. +24 dB) with unity 0 dB centred at 0.5, so the engineer can
        both cut and boost the input before it is monitored and recorded. It is a
        gain control, not a level fader, so it has no mute detent.

        Shaped by a tanh S-curve (`bend = inputTrimBend`) so the gain holds
        closer to the maximum when lowering from the top, rather than dropping
        linearly away from +24 dB. `dbToPos()` is the exact inverse. */
    inline constexpr Taper inputTrim { -24.0f, 24.0f, false, inputTrimBend };
}
