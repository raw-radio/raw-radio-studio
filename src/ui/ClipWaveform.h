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

    //==========================================================================
    // WV-1 — amplitude scale, view normalisation and channel bands.
    //
    // Owner reports: a −24 dBFS take drew ~1 px with the historic linear curve, and
    // stereo/mono clips need one band per channel. Everything below is pure
    // arithmetic (no audio, no GPU): the caller only multiplies ready numbers.

    /** Schematic amplitude scale of the drawn wave. `linear` is the historic
        `peak * halfHeight`; `decibel` spreads the quiet end so a soft microphone is
        no longer a flat line. */
    enum class WaveformScale { linear, decibel };

    /** Range (dB below full scale) the `decibel` scale shows. 60 dB down is exactly
        the existing silence floor (`silencePeak == 0.001f == −60 dBFS`), so the curve
        is continuous with `isSilent()` — and a ~60 dB window is the usual DAW
        convention (Ardour ships a logarithmic/dB waveform scale; Reaper's
        "scale peaks by dB" uses the same kind of range). */
    inline constexpr float waveformDbRangeDb = 60.0f;

    /** Ceiling on the view-only normalisation gain (+40 dB). Beyond that a whisper
        would be nothing but amplified noise. */
    inline constexpr float maxViewGain = 100.0f;

    /** A band must be at least this tall (px) to be split in two; below it the
        channels overlay. 10 px leaves ~5 px of visible wave under the 2 px pad and
        the centre line. */
    inline constexpr int minBandHeightPx = 10;

    // --- The ONE place the persisted view defaults live ------------------------
    // (owner decision: switching the default is a one-line change here)
    /** Default amplitude scale when nothing is stored (or the stored value is
        unreadable). */
    inline constexpr WaveformScale defaultWaveformScale = WaveformScale::decibel;

    /** Default peak-normalisation state when nothing is stored.

        OFF by default (owner decision, 2026-10): the dB curve ALONE already solves
        the "quiet take is a flat strip" complaint — −24 dBFS lands at ~12 of the
        40 px half-height instead of ~1 px — while normalisation would stretch every
        take to full scale and hide its ABSOLUTE level (a −30 dBFS take would look
        identical to a 0 dBFS one). Normalising stays available as the view toggle
        for the extreme case (a whisper, or a take recorded far too low), never as
        the default. */
    inline constexpr bool defaultWaveformNormalise = false;

    /** Maps a bin peak (linear amplitude, may exceed 1.0) to a 0..1 fraction of the
        half-height for `scale`. `decibel` reaches 0 at the silence floor and 1 at
        0 dBFS; both curves are clamped, so clipping float data cannot overshoot. */
    inline float displayNorm (float peak, WaveformScale scale) noexcept
    {
        if (! (peak > 0.0f)) // also catches NaN
            return 0.0f;

        if (scale == WaveformScale::linear)
            return juce::jlimit (0.0f, 1.0f, peak);

        const auto db = 20.0f * std::log10 (peak);
        return juce::jlimit (0.0f, 1.0f, (db + waveformDbRangeDb) / waveformDbRangeDb);
    }

    /** View-only gain that maps the clip's loudest peak to full scale, so a quiet
        take fills the band. Identity (1.0) when `normalise` is off, when the clip is
        effectively silent (`peakMax < silencePeak` — normalising noise would be a
        lie), or when `peakMax` is invalid (<= 0 / not finite). Otherwise
        `1 / max (peakMax, silencePeak)`, capped at `maxViewGain`: a peak above 1.0
        scales DOWN to 1.0 (an already-clipped take is never amplified). Purely a
        drawing gain — the audio is never touched. */
    inline float viewGain (float peakMax, bool normalise) noexcept
    {
        if (! normalise || ! std::isfinite (peakMax) || peakMax < silencePeak)
            return 1.0f;

        return juce::jmin (maxViewGain, 1.0f / juce::jmax (peakMax, silencePeak));
    }

    /** Which channel goes into which band (WV-3). */
    struct BandLayout
    {
        int numBands = 1;       ///< 1 (mono, or overlaid when there is no room) or 2
        int topChannel = 0;     ///< channel of the top band (L)
        int bottomChannel = -1; ///< channel of the bottom band (R); -1 = overlaid
    };

    /** Bands to draw for a clip with `numChannels` in a band `bandHeightPx` tall.
        Mono (or nothing) is always ONE band. Two or more channels are TWO bands
        (L above, R below — owner decision) **when** each would be at least
        `minBandHeightPx` tall; in a short lane they overlay into one band and the
        caller takes the per-bin max across ALL channels (more information than
        either single channel alone).

        DELIBERATE compromise for clips wider than stereo: the two-band case draws
        only channels 0/1 (L/R) and **silently ignores 2..N** — the app's metering
        and mixer are 2-channel (see the Epic gap "metering — only 2 channels
        (L/R)"), so the waveform must not invent channels the rest of the UI cannot
        show. The OVERLAY case, by contrast, folds in every channel via the per-bin
        max, so a signal living only on channel 2+ is still visible when the lane is
        short. */
    inline BandLayout bandLayout (int numChannels, int bandHeightPx) noexcept
    {
        BandLayout layout;

        if (numChannels <= 1)
            return layout; // one band, channel 0

        if (bandHeightPx >= 2 * minBandHeightPx)
        {
            layout.numBands = 2;
            layout.topChannel = 0;
            layout.bottomChannel = 1;
        }

        return layout;
    }

    /** Vertical extent of one band inside a `bandHeightPx`-tall band starting at
        `topY`. Band 0 is the top (L), band 1 the bottom (R); with a single band
        both entries cover the whole height. The split never overlaps and always
        fills the height exactly (an odd pixel goes to the bottom band). */
    struct BandExtent
    {
        int y = 0;
        int height = 0;
    };

    inline BandExtent bandExtent (const BandLayout& layout, int topY, int bandHeightPx,
                                  int bandIndex) noexcept
    {
        if (layout.numBands <= 1)
            return { topY, bandHeightPx };

        const int topHeight = bandHeightPx / 2;

        // Two bands: band 0 is the top (L) half, band 1 the bottom (R) half.
        if (bandIndex <= 0)
            return { topY, topHeight };

        return { topY + topHeight, bandHeightPx - topHeight };
    }

    /** Stable string form of the scale (for the persisted UI setting). */
    inline juce::String waveformScaleToString (WaveformScale scale)
    {
        return scale == WaveformScale::decibel ? "decibel" : "linear";
    }

    /** Parses `waveformScaleToString`. A MISSING or UNREADABLE value gives the
        same answer — `defaultWaveformScale` — so "no key yet" and "a key we cannot
        read" can never disagree about the default. */
    inline WaveformScale waveformScaleFromString (const juce::String& text)
    {
        if (text == "decibel")
            return WaveformScale::decibel;

        if (text == "linear")
            return WaveformScale::linear;

        return defaultWaveformScale;
    }

    /** Stable string form of the normalisation flag (persisted UI setting). */
    inline juce::String waveformNormaliseToString (bool normalise)
    {
        return normalise ? "1" : "0";
    }

    /** Parses `waveformNormaliseToString` (also accepts true/false/yes/no, in any
        case). A missing or unreadable value falls back to
        `defaultWaveformNormalise` — the same rule as the scale, so a garbled
        settings file cannot silently switch normalisation off. */
    inline bool waveformNormaliseFromString (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();

        if (t == "1" || t == "true" || t == "yes")
            return true;

        if (t == "0" || t == "false" || t == "no")
            return false;

        return defaultWaveformNormalise;
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
