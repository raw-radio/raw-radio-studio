// raw-radio-studio — input level meter (Epic 1).

#include "InputMeter.h"

#include "../studio/Session.h"
#include "BrandColours.h"
#include "BrandFonts.h"

namespace rrs
{
    namespace
    {
        constexpr float minDb = -60.0f;
        constexpr float maxDb = 6.0f;
    }

    InputMeter::InputMeter (const InputLevels& levels)
        : inputLevels (levels)
    {
        startTimerHz (30);
    }

    float InputMeter::normaliseDb (float linearGain) noexcept
    {
        const auto db = juce::Decibels::gainToDecibels (juce::jmax (linearGain, 1.0e-6f), minDb);
        return juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
    }

    void InputMeter::timerCallback()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& state = channels[(size_t) ch];

            const auto peak = inputLevels.peak[ch].load (std::memory_order_relaxed);
            const auto rms  = inputLevels.rms[ch].load (std::memory_order_relaxed);

            // UI smoothing: fast attack, slow release.
            state.peak = juce::jmax (peak, state.peak * 0.82f);
            state.rms  = juce::jmax (rms, state.rms * 0.90f);

            if (peak >= state.hold)
            {
                state.hold = peak;
                state.holdCountdown = 30; // ~1 s at 30 Hz
            }
            else if (state.holdCountdown > 0)
            {
                --state.holdCountdown;
            }
            else
            {
                state.hold *= 0.94f;
            }
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

        const auto barHeight = juce::jmax (8, bounds.getHeight() / 2 - 2);
        drawChannel (g, bounds.removeFromTop (barHeight), channels[0], "L");
        bounds.removeFromTop (4);
        drawChannel (g, bounds.removeFromTop (barHeight), channels[1], "R");
    }

    void InputMeter::drawChannel (juce::Graphics& g,
                                  juce::Rectangle<int> area,
                                  const ChannelState& state,
                                  const juce::String& name)
    {
        auto labelArea = area.removeFromLeft (16);
        auto valueArea = area.removeFromRight (52);
        auto barArea = area.reduced (1, 2);

        g.setColour (brand::textTertiary);
        g.setFont (brand::uiRegular (11.0f));
        g.drawText (name, labelArea, juce::Justification::centred);

        g.setColour (brand::meterTrough);
        g.fillRoundedRectangle (barArea.toFloat(), 2.0f);

        const auto barWidth = (float) barArea.getWidth();
        const auto rmsWidth  = barWidth * normaliseDb (state.rms);
        const auto peakWidth = barWidth * normaliseDb (state.peak);
        const auto holdX     = (float) barArea.getX() + barWidth * normaliseDb (state.hold);

        g.setColour (brand::vuGreen);
        g.fillRoundedRectangle (barArea.toFloat().withWidth (rmsWidth), 2.0f);

        g.setColour (brand::vuYellow);
        g.fillRect (juce::Rectangle<float> ((float) barArea.getX() + peakWidth - 1.0f,
                                            (float) barArea.getY(), 2.0f, (float) barArea.getHeight()));

        if (state.hold > 0.0f)
        {
            g.setColour (brand::vuRed);
            g.fillRect (juce::Rectangle<float> (holdX, (float) barArea.getY(),
                                                2.0f, (float) barArea.getHeight()));
        }

        const auto peakDb = juce::Decibels::gainToDecibels (state.peak, minDb);
        g.setColour (brand::textSecondary);
        g.setFont (brand::monoRegular (11.0f));
        g.drawText (juce::String (peakDb, 1) + " dB",
                    valueArea, juce::Justification::centredRight);
    }
}
