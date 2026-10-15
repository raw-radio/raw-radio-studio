// raw-radio-studio — clip-waveform drawing math (Epic 8 phase 2).
//
// Pure, header-only helpers for turning a clip's on-screen rectangle into a list
// of pixel bins and mapping each bin to source-file time (with the clip's offset
// and, for looped clips, per-iteration wrap). Kept free of Tracktion / JUCE-GUI
// so the binning is unit-testable without an engine or a window.
//
// The actual drawing (reading pre-computed peaks from an AudioThumbnail /
// SmartThumbnail / the engine's recording thumbnail) lives in MainComponent; this
// header only owns the geometry, so paint() never rebuilds peaks.

#pragma once

#include <juce_graphics/juce_graphics.h>

#include <cmath>

namespace rrs::clipwave
{
    /** One bin = 2 px of clip width (spec §5.2): below that it is expensive and
        unreadable. */
    inline constexpr int binWidthPx = 2;

    /** Vertical padding (px) kept inside the clip band, above and below the wave. */
    inline constexpr int wavePadY = 2;

    /** Peaks below this (linear amplitude, ~ -60 dBFS) count as silence: only the
        centre line is drawn (spec §5.2). */
    inline constexpr float silencePeak = 0.001f;

    /** Number of 2 px bins that fit in `widthPx`. */
    inline int binCount (int widthPx) noexcept
    {
        return widthPx <= 0 ? 0 : (widthPx + binWidthPx - 1) / binWidthPx;
    }

    /** Timeline time (seconds) at the LEFT edge of bin `index` (of `count`). */
    inline double binStartSeconds (double clipStart, double clipEnd,
                                   int index, int count) noexcept
    {
        if (count <= 0)
            return clipStart;

        const double span = clipEnd - clipStart;
        return clipStart + span * ((double) index / (double) count);
    }

    /** Timeline time (seconds) at the RIGHT edge of bin `index`. */
    inline double binEndSeconds (double clipStart, double clipEnd,
                                 int index, int count) noexcept
    {
        return binStartSeconds (clipStart, clipEnd, index + 1, count);
    }

    /** Peak magnitude (0..1) from a min/max pair. */
    inline float peakFromMinMax (float minValue, float maxValue) noexcept
    {
        return juce::jmax (std::abs (minValue), std::abs (maxValue));
    }

    /** True when a bin's peak is effectively silence. */
    inline bool isSilent (float peak) noexcept
    {
        return peak < silencePeak;
    }

    /** True when a clip is the live take being recorded right now. Clip-level
        (not track-level): a pre-existing clip on an armed track must NOT be
        treated as live. `activeRecordingFile` is the file the track is currently
        recording to (empty when it is not recording). */
    inline bool isLiveTake (bool recording, bool isWave,
                            const juce::File& clipFile, const juce::File& activeRecordingFile) noexcept
    {
        return recording && isWave
            && activeRecordingFile != juce::File()
            && clipFile != juce::File()
            && clipFile == activeRecordingFile;
    }

    /** Maps a timeline time to source-file time for a clip that starts at
        `clipStart` with source `offsetSeconds`, wrapping per iteration when the
        clip is looped (iteration length = fileLength - offset). Clamped to
        [0, fileLength]. With an unknown/zero file length the linear mapping is
        returned unchanged. */
    inline double sourceTimeForBin (double clipStart, double timelineSeconds, double offsetSeconds,
                                    double fileLength, bool looping) noexcept
    {
        double t = offsetSeconds + (timelineSeconds - clipStart);

        if (looping && fileLength > offsetSeconds && fileLength > 0.0)
        {
            const double iteration = fileLength - offsetSeconds;

            if (iteration > 1.0e-9)
            {
                double rel = std::fmod (t - offsetSeconds, iteration);

                if (rel < 0.0)
                    rel += iteration;

                t = offsetSeconds + rel;
            }
        }

        if (fileLength > 0.0)
            t = juce::jlimit (0.0, fileLength, t);

        return juce::jmax (0.0, t);
    }
}
