// raw-radio-studio — input level meter (Epic 1).

#include "InputMeter.h"

#include "../studio/Session.h"
#include "BrandColours.h"
#include "BrandFonts.h"

namespace rrs
{
    namespace
    {
        // Shared display scale: -60..0 dBFS, linear in dB (see MeterBallistics).
        // The bar's "full" is 0 dBFS and "half" is therefore -30 dBFS. Only the
        // floor is referenced directly (peak-hold suppression); the mapping lives
        // in MeterBallistics::normaliseMeterDb.
        constexpr float minDb = MeterBallistics::meterFloorDb;
    }

    InputMeter::InputMeter (const InputLevels& levels, std::function<int()> channelCountProvider)
        : inputLevels (levels)
        , numInputChannels (std::move (channelCountProvider))
    {
        lastUpdateMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (30);
    }

    float InputMeter::normaliseDb (float db) noexcept
    {
        // Single, documented scale shared with the mixer meters (MeterBallistics).
        return MeterBallistics::normaliseMeterDb (db);
    }

    void InputMeter::timerCallback()
    {
        // Frame-rate-independent ballistics: use the real elapsed time so the
        // meter falls at a fixed dB/s regardless of how often the timer fires.
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto dt  = lastUpdateMs > 0.0 ? (float) ((now - lastUpdateMs) / 1000.0)
                                            : 1.0f / 30.0f;
        lastUpdateMs = now;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto& state = channels[(size_t) ch];

            const auto peak = inputLevels.peak[ch].load (std::memory_order_relaxed);
            const auto rms  = inputLevels.rms[ch].load (std::memory_order_relaxed);

            const auto peakDb = juce::Decibels::gainToDecibels (juce::jmax (peak, 1.0e-6f),
                                                                MeterBallistics::floorDb);
            const auto rmsDb  = juce::Decibels::gainToDecibels (juce::jmax (rms, 1.0e-6f),
                                                                MeterBallistics::floorDb);

            state.peak.update (peakDb, dt);
            state.rms.update (rmsDb, dt);
        }

        repaint();
    }

    void InputMeter::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().reduced (4);
        auto titleRow = bounds.removeFromTop (16);

        g.setColour (brand::textSecondary);
        g.setFont (brand::uiMedium (12.0f));
        g.drawText ("Input", titleRow, juce::Justification::centredLeft);

        // A device with one input channel is mono: label it as such rather than
        // drawing a misleading stereo "L"/"R" pair. 0 (no device open) is also
        // treated as mono so the meter stays a single quiet bar.
        const auto channelCount = numInputChannels ? numInputChannels() : 2;

        if (channelCount <= 1)
        {
            drawChannel (g, bounds, channels[0], "Mono", 40);
            return;
        }

        const auto barHeight = juce::jmax (8, bounds.getHeight() / 2 - 2);
        drawChannel (g, bounds.removeFromTop (barHeight), channels[0], "L", 16);
        bounds.removeFromTop (4);
        drawChannel (g, bounds.removeFromTop (barHeight), channels[1], "R", 16);
    }

    void InputMeter::drawChannel (juce::Graphics& g,
                                  juce::Rectangle<int> area,
                                  const ChannelState& state,
                                  const juce::String& name,
                                  int labelWidth)
    {
        auto labelArea = area.removeFromLeft (labelWidth);
        auto valueArea = area.removeFromRight (52);
        auto barArea = area.reduced (1, 2);

        g.setColour (brand::textTertiary);
        g.setFont (brand::uiRegular (11.0f));
        g.drawText (name, labelArea, juce::Justification::centred);

        g.setColour (brand::meterTrough);
        g.fillRoundedRectangle (barArea.toFloat(), 2.0f);

        const auto barWidth  = (float) barArea.getWidth();
        const auto rmsWidth  = barWidth * normaliseDb (state.rms.getDb());
        const auto peakWidth = barWidth * normaliseDb (state.peak.getDb());
        const auto holdX     = (float) barArea.getX() + barWidth * normaliseDb (state.peak.getHoldDb());

        g.setColour (brand::vuGreen);
        g.fillRoundedRectangle (barArea.toFloat().withWidth (rmsWidth), 2.0f);

        g.setColour (brand::vuYellow);
        g.fillRect (juce::Rectangle<float> ((float) barArea.getX() + peakWidth - 1.0f,
                                            (float) barArea.getY(), 2.0f, (float) barArea.getHeight()));

        if (state.peak.getHoldDb() > minDb)
        {
            g.setColour (brand::vuRed);
            g.fillRect (juce::Rectangle<float> (holdX, (float) barArea.getY(),
                                                2.0f, (float) barArea.getHeight()));
        }

        const auto peakDb = state.peak.getDb();
        g.setColour (brand::textSecondary);
        g.setFont (brand::monoRegular (11.0f));
        g.drawText (juce::String (peakDb, 1) + " dB",
                    valueArea, juce::Justification::centredRight);
    }
}
