// raw-radio-studio — offline time-stretch / pitch-shift (Epic 4, FR-ED-5).
//
// Thin, app-facing wrapper around the pinned free library (Signalsmith Stretch,
// MIT — see NOTICE / DEPENDENCIES.md, OQ-2). It is deliberately *offline*:
// `process()` allocates full-length buffers and runs the STFT across the whole
// clip in one pass. Call it from the message thread (or a background thread),
// never from the audio callback.
//
// The Signalsmith header is included only in TimeStretch.cpp so the heavy
// header does not slow down the rest of the build.

#pragma once

#include <JuceHeader.h>

namespace rrs
{
    class TimeStretch
    {
    public:
        struct Result
        {
            juce::AudioBuffer<float> audio;   ///< Stretched audio (may be empty on failure).
            double sampleRate = 0.0;
            bool ok = false;
            juce::String error;

            /** Rendered duration in seconds (0 when empty). */
            double getDurationSeconds() const noexcept
            {
                return sampleRate > 0.0 ? (double) audio.getNumSamples() / sampleRate : 0.0;
            }
        };

        /** Time-stretches `input` by `timeFactor` = outputDuration / inputDuration
            (1.0 = unchanged, 2.0 = twice as long) and independently pitch-shifts
            by `semitones` (0 = no pitch change).

            The output length is exactly `round(inputSamples * timeFactor)`, with
            the library's output pre-roll folded back in so the result has no
            leading latency gap. `timeFactor` must be finite and > 0. */
        static Result process (const juce::AudioBuffer<float>& input,
                               double sampleRate,
                               double timeFactor,
                               double semitones = 0.0);

        /** Convenience: stretches `input` so its duration becomes
            `targetSeconds`. Returns an error when the input has no length or the
            target is not positive. */
        static Result stretchToDuration (const juce::AudioBuffer<float>& input,
                                         double sampleRate,
                                         double targetSeconds,
                                         double semitones = 0.0);

        //==========================================================================
        /** Soft quality bounds recommended by the library: between 0.75x and
            1.5x the artefacts are minimal; wider ratios work but degrade. */
        static constexpr double recommendedMinFactor = 0.75;
        static constexpr double recommendedMaxFactor = 1.5;

        /** Absolute accepted range (outside it the transform is refused). */
        static constexpr double minFactor = 0.25;
        static constexpr double maxFactor = 4.0;
    };
}
