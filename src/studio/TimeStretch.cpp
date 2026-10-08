// raw-radio-studio — offline time-stretch / pitch-shift (see TimeStretch.h).

#include "TimeStretch.h"

#include <cmath>
#include <vector>

// The pinned Signalsmith Stretch header (MIT) is third-party code compiled under
// our own warning flags; quiet its diagnostics for this translation unit only.
#if defined(__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wall"
 #pragma clang diagnostic ignored "-Wextra"
 #pragma clang diagnostic ignored "-Wshadow"
 #pragma clang diagnostic ignored "-Wconversion"
 #pragma clang diagnostic ignored "-Wsign-conversion"
 #pragma clang diagnostic ignored "-Wfloat-equal"
 #pragma clang diagnostic ignored "-Wunused-parameter"
 #pragma clang diagnostic ignored "-Wunused-variable"
#elif defined(__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wall"
 #pragma GCC diagnostic ignored "-Wextra"
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wfloat-equal"
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wunused-variable"
#endif

#include "signalsmith-stretch.h"

#if defined(__clang__)
 #pragma clang diagnostic pop
#elif defined(__GNUC__)
 #pragma GCC diagnostic pop
#endif

namespace rrs
{
    TimeStretch::Result TimeStretch::process (const juce::AudioBuffer<float>& input,
                                              double sampleRate,
                                              double timeFactor,
                                              double semitones)
    {
        Result result;
        result.sampleRate = sampleRate;

        const auto numChannels = input.getNumChannels();
        const auto inputLength = input.getNumSamples();

        if (numChannels <= 0 || inputLength <= 0 || sampleRate <= 0.0)
        {
            result.error = "No audio to process.";
            return result;
        }

        if (! std::isfinite (timeFactor) || timeFactor <= 0.0)
        {
            result.error = "Invalid time factor.";
            return result;
        }

        if (timeFactor < minFactor || timeFactor > maxFactor)
        {
            result.error = "Time factor " + juce::String (timeFactor, 3)
                         + " is outside the supported range "
                         + juce::String (minFactor, 2) + "x.."
                         + juce::String (maxFactor, 2) + "x.";
            return result;
        }

        signalsmith::stretch::SignalsmithStretch<float> stretch;
        stretch.presetDefault ((int) numChannels, (float) sampleRate);
        stretch.setTransposeSemitones ((float) semitones);

        const auto inputLatency = stretch.inputLatency();
        const auto outputLatency = stretch.outputLatency();

        const auto outputLength = (int) std::llround ((double) inputLength * timeFactor);

        if (outputLength <= 0)
        {
            result.error = "The stretched length rounds to zero.";
            return result;
        }

        // Pad the input with `inputLatency` zero samples of look-ahead so the
        // transform can read slightly ahead of the output position.
        juce::AudioBuffer<float> paddedInput ((int) numChannels, inputLength + inputLatency);
        paddedInput.clear();

        for (int ch = 0; ch < (int) numChannels; ++ch)
            paddedInput.copyFrom (ch, 0, input, ch, 0, inputLength);

        // Pad the output by `outputLatency` so the flush has room to write the
        // tail of the transform.
        const auto tailSamples = outputLatency;
        juce::AudioBuffer<float> paddedOutput ((int) numChannels, outputLength + tailSamples);
        paddedOutput.clear();

        // `seek()` supplies the first `inputLatency` samples as pre-roll so the
        // processing time aligns with the start of the input.
        {
            auto readPtrs = paddedInput.getArrayOfReadPointers();
            stretch.seek (readPtrs, inputLatency, 1.0 / timeFactor);
        }

        // Process the remainder; `process()` is called with the pointer already
        // advanced past the pre-rolled samples.
        {
            auto readPtrs = paddedInput.getArrayOfReadPointers();
            std::vector<const float*> advanced ((size_t) numChannels);

            for (int ch = 0; ch < (int) numChannels; ++ch)
                advanced[(size_t) ch] = readPtrs[ch] + inputLatency;

            auto outPtrs = paddedOutput.getArrayOfWritePointers();
            stretch.process (advanced.data(), inputLength, outPtrs, outputLength);
        }

        // Read the pending tail with no further input.
        {
            auto outPtrs = paddedOutput.getArrayOfWritePointers();
            std::vector<float*> tail ((size_t) numChannels);

            for (int ch = 0; ch < (int) numChannels; ++ch)
                tail[(size_t) ch] = outPtrs[ch] + outputLength;

            stretch.flush (tail.data(), tailSamples);
        }

        // Fold the `outputLatency` pre-roll back into the signal (reversed and
        // negated) so the rendered result has no leading gap and its first
        // samples are not the transform's latency pad.
        //
        // BLOCKER 1 (ASan heap-buffer-overflow): the fold writes
        // [outputLatency, 2*outputLatency) into a buffer of
        // `outputLength + outputLatency` samples, so a full-length fold needs
        // `outputLength >= outputLatency`. A clip shorter than the transform's
        // latency (a few tens of ms) violates that and would write past the end.
        // The kept output is [outputLatency, outputLatency + outputLength); when
        // the clip is shorter than the latency that whole span is still inside
        // the buffer, so clamping the fold to the available output length both
        // avoids the overflow and folds exactly the samples that are kept.
        const auto foldLength = juce::jmin (outputLatency, outputLength);

        for (int ch = 0; ch < (int) numChannels; ++ch)
            for (int i = 0; i < foldLength; ++i)
            {
                // `i < foldLength <= outputLatency` keeps the read index >= 0 and
                // the write index < outputLength + outputLatency (buffer size).
                const auto trimmed = paddedOutput.getSample (ch, outputLatency - 1 - i);
                paddedOutput.addSample (ch, outputLatency + i, -trimmed);
            }

        result.audio.setSize ((int) numChannels, outputLength, false, true, false);

        for (int ch = 0; ch < (int) numChannels; ++ch)
            result.audio.copyFrom (ch, 0, paddedOutput, ch, outputLatency, outputLength);

        result.ok = true;
        return result;
    }

    TimeStretch::Result TimeStretch::stretchToDuration (const juce::AudioBuffer<float>& input,
                                                        double sampleRate,
                                                        double targetSeconds,
                                                        double semitones)
    {
        if (sampleRate <= 0.0 || input.getNumSamples() <= 0)
        {
            Result result;
            result.sampleRate = sampleRate;
            result.error = "No audio to process.";
            return result;
        }

        if (! std::isfinite (targetSeconds) || targetSeconds <= 0.0)
        {
            Result result;
            result.sampleRate = sampleRate;
            result.error = "Target duration must be positive.";
            return result;
        }

        const auto sourceSeconds = (double) input.getNumSamples() / sampleRate;
        return process (input, sampleRate, targetSeconds / sourceSeconds, semitones);
    }
}
