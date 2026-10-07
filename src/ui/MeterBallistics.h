// raw-radio-studio — meter ballistics (Epic 2 GUI retest).
//
// A tiny, pure, frame-rate-independent level-meter envelope in the dB domain.
// The old UI meters smoothed linearly in the *gain* domain with a fixed
// per-frame multiplier and no dt, and the mixer reset its meter state on every
// change message (volume moves emit one), so meters flickered/jittered while
// the engineer rode the faders.
//
// Ballistics: fast attack (catch transients), rate-limited release measured in
// dB per second (a sensible ~24 dB/s fall), plus a peak-hold that latches the
// top for a moment and then decays. Updating with the real elapsed time makes
// the motion independent of the UI refresh rate, so the value can never jump
// from one block to the next.
//
// Tracktion-free / JUCE-GUI-free (only Decibels + jlimit), so it is unit tested
// without an engine or a window.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>

namespace rrs
{
    class MeterBallistics
    {
    public:
        /** Floor used for "silence" and the very first frame. */
        static constexpr float floorDb = -100.0f;

        /** Fast rise; ~0.5 s to climb 250 dB, so transients register at once. */
        static constexpr float attackDbPerSec = 500.0f;

        /** Fall speed; within the owner's requested ~20-40 dB/s window. */
        static constexpr float releaseDbPerSec = 24.0f;

        /** How long the peak marker stays latched before it starts to fall. */
        static constexpr float holdSeconds = 1.0f;

        void reset() noexcept
        {
            valueDb = floorDb;
            holdDb  = floorDb;
            holdTimer = 0.0f;
        }

        /** Feeds a newly-measured peak (dBFS) and advances by `dtSeconds`. */
        void update (float newPeakDb, float dtSeconds) noexcept
        {
            // Clamp dt so a message-loop stall cannot make the meter teleport;
            // also guards against a zero/negative timer delta.
            const auto dt = juce::jlimit (0.0f, 0.25f, dtSeconds);
            const auto peak = juce::jmax (newPeakDb, floorDb);

            if (peak >= valueDb)
            {
                // Rise toward the peak, rate-limited (fast but not a teleport).
                valueDb = std::min (peak, valueDb + attackDbPerSec * dt);
            }
            else
            {
                // Fall at the release rate. This is the flicker fix: the meter
                // can only move releaseDbPerSec*dt per update, monotonically.
                valueDb = std::max (peak, valueDb - releaseDbPerSec * dt);
            }

            if (peak >= holdDb)
            {
                holdDb = peak;
                holdTimer = holdSeconds;
            }
            else if (holdTimer > 0.0f)
            {
                holdTimer -= dt;
            }
            else
            {
                holdDb = std::max (valueDb, holdDb - releaseDbPerSec * dt);
            }

            // The hold marker never sits below the live bar.
            if (holdDb < valueDb)
                holdDb = valueDb;
        }

        /** Smoothed level (dBFS) for the bar. */
        float getDb() const noexcept     { return valueDb; }

        /** Peak-hold marker (dBFS). */
        float getHoldDb() const noexcept { return holdDb; }

    private:
        float valueDb = floorDb;
        float holdDb  = floorDb;
        float holdTimer = 0.0f;
    };
}
