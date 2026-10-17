// raw-radio-studio — clip-waveform DRAWING (WV-1..WV-3).
//
// The pixel work for a clip's wave bands, split out of MainComponent::drawClip so
// that (a) the arrangement and (b) the offline screenshot fixture in the tests draw
// the exact same pixels. It is allocation-free (no containers, no strings): the
// peaks come straight from the caller's thumbnail and everything else is scalars,
// so it is safe on the paint path.
//
// WV-3 (owner decision): one band per channel — stereo/wide clips draw L above and
// R below, mono clips a single band — and the bands OVERLAY into one (per-bin max
// across every channel) when the band is too short to split (see `bandLayout`).

#pragma once

#include <juce_graphics/juce_graphics.h>

#include "ui/ClipWaveform.h"

namespace rrs::clipwave
{
    /** Alpha of the clip's centre line (the caller derives the colour from it, so
        the arrangement and the offline screenshots cannot drift apart). */
    inline constexpr float centreLineAlpha = 0.3f;

    /** The faint per-band centre line is this fraction of `centreLineAlpha`. */
    inline constexpr float innerCentreLineAlphaRatio = 0.66f;

    /** Inputs of one clip's band drawing. */
    struct BandDrawParams
    {
        /** The clip's on-screen rectangle (bands are drawn inside it). */
        juce::Rectangle<int> clipRect;
        /** Peak source. `getApproximateMinMax` is called only (never a file read). */
        juce::AudioThumbnailBase* thumb = nullptr;
        int numChannels = 0;
        /** The clip's source offset and the file length (seconds). */
        double offsetSeconds = 0.0;
        double fileLength = 0.0;
        bool looping = false;
        /** Timeline bounds of the drawn span (the clip rect's own edges). */
        double startSeconds = 0.0;
        double endSeconds = 0.0;
        /** Wave colour (the caller picks idle / selected / loop / live). */
        juce::Colour colour;
        WaveformScale scale = WaveformScale::linear;
        /** Peak-normalise the DRAWING (never the audio). */
        bool normalise = false;
        /** Colour of the centre line(s) and the L/R separator. */
        juce::Colour lineColour = juce::Colours::white.withAlpha (centreLineAlpha);
    };

    /** Draws the clip's waveform bands. Returns the view gain used (1.0 when the
        clip was not normalised) so a caller/test can report it. */
    inline float drawClipWaveform (juce::Graphics& g, const BandDrawParams& params)
    {
        const auto r = params.clipRect;

        if (params.thumb == nullptr || params.numChannels <= 0
            || params.fileLength <= 0.0 || r.getWidth() <= 0 || r.getHeight() <= 0)
            return 1.0f;

        const auto inner = r.reduced (0, wavePadY);

        if (inner.getHeight() < 2)
            return 1.0f;

        const auto bands = bandLayout (params.numChannels, inner.getHeight());

        // View-only gain: peak-normalise the DRAWING to the clip's loudest sample so
        // a quiet take fills the band. Read once from the (cached) thumbnail summary
        // and skipped entirely when normalisation is off.
        float clipPeak = 0.0f;

        if (params.normalise)
        {
            const auto peakEnd = juce::jmax (params.offsetSeconds, params.fileLength);

            for (int ch = 0; ch < params.numChannels; ++ch)
            {
                float mn = 0.0f, mx = 0.0f;
                params.thumb->getApproximateMinMax (params.offsetSeconds, peakEnd, ch, mn, mx);
                clipPeak = juce::jmax (clipPeak, peakFromMinMax (mn, mx));
            }
        }

        const auto gain = viewGain (clipPeak, params.normalise);

        // One band. `channel >= 0` selects that channel; `channel < 0` takes the
        // per-bin max over ALL channels (the overlaid case).
        auto drawBand = [&] (juce::Rectangle<int> bandArea, int channel)
        {
            if (bandArea.getHeight() < 2)
                return;

            const auto bandCentreY = (float) bandArea.getCentreY();
            const auto bandHalfH = (float) bandArea.getHeight() * 0.5f;

            // Faint centre line inside this band (with two bands the clip-level line
            // drawn by the caller doubles as the L/R separator).
            if (bands.numBands == 2)
            {
                g.setColour (params.lineColour.withMultipliedAlpha (innerCentreLineAlphaRatio));
                g.fillRect (juce::Rectangle<float> ((float) r.getX(), bandCentreY - 0.5f,
                                                    (float) r.getWidth(), 1.0f));
            }

            const auto binPeak = [&] (double a, double b)
            {
                float peak = 0.0f;

                for (int ch = 0; ch < params.numChannels; ++ch)
                {
                    if (channel >= 0 && ch != channel)
                        continue;

                    float mn = 0.0f, mx = 0.0f;
                    params.thumb->getApproximateMinMax (a, b, ch, mn, mx);
                    peak = juce::jmax (peak, peakFromMinMax (mn, mx));
                }

                return peak;
            };

            const int bins = binCount (r.getWidth());

            for (int i = 0; i < bins; ++i)
            {
                const auto t0 = binStartSeconds (params.startSeconds, params.endSeconds, i, bins);
                const auto t1 = binEndSeconds (params.startSeconds, params.endSeconds, i, bins);
                const auto s0 = sourceTimeForBin (params.startSeconds, t0, params.offsetSeconds,
                                                  params.fileLength, params.looping);
                const auto s1 = sourceTimeForBin (params.startSeconds, t1, params.offsetSeconds,
                                                  params.fileLength, params.looping);

                float peak = 0.0f;

                if (s1 > s0)
                    peak = binPeak (s0, s1);
                else if (params.looping && params.fileLength > params.offsetSeconds)
                    // The bin crosses the loop-wrap point: cover the tail of this
                    // iteration and the head of the next, so there is no 2 px gap.
                    peak = juce::jmax (binPeak (s0, params.fileLength),
                                       binPeak (params.offsetSeconds, s1));

                if (isSilent (peak))
                    continue;

                const auto x = (float) (r.getX() + i * binWidthPx);

                if (x >= (float) r.getRight())
                    break;

                // Clamp the bar to the clip's right edge (odd width -> last bin).
                const auto w = juce::jmin ((float) binWidthPx, (float) r.getRight() - x);
                const auto h = juce::jlimit (1.0f, bandHalfH,
                                             displayNorm (peak * gain, params.scale) * bandHalfH);
                g.setColour (params.colour);
                g.fillRect (juce::Rectangle<float> (x, bandCentreY - h, w, h * 2.0f));
            }
        };

        if (bands.numBands == 2)
        {
            const auto topExtent = bandExtent (bands, inner.getY(), inner.getHeight(), 0);
            const auto bottomExtent = bandExtent (bands, inner.getY(), inner.getHeight(), 1);

            drawBand ({ r.getX(), topExtent.y, r.getWidth(), topExtent.height }, bands.topChannel);
            drawBand ({ r.getX(), bottomExtent.y, r.getWidth(), bottomExtent.height },
                      bands.bottomChannel);

            // Separator between L and R (the clip-level centre line).
            g.setColour (params.lineColour);
            g.fillRect (juce::Rectangle<float> ((float) r.getX(),
                                                (float) (topExtent.y + topExtent.height) - 0.5f,
                                                (float) r.getWidth(), 1.0f));
        }
        else
        {
            drawBand (inner, -1); // overlaid: the per-bin max across all channels
        }

        return gain;
    }
}
