// raw-radio-studio — input level meter (Epic 1).
//
// Reads the RT-safe InputLevels accumulator on a UI timer and draws peak + RMS
// per channel. No engine access on the UI thread beyond atomics.

#pragma once

#include <JuceHeader.h>

#include "MeterBallistics.h"

#include <array>
#include <functional>

namespace rrs
{
    struct InputLevels;

    class InputMeter final : public juce::Component,
                             private juce::Timer
    {
    public:
        /** @param numInputChannels  optional provider returning the number of active
            input channels on the current device. When it reports a single channel
            the meter draws one "Mono" bar instead of a misleading "L"/"R" pair
            (the Redmi Buds mic, for example, has one channel and was labelled
            "L" even though it is a mono mic). */
        explicit InputMeter (const InputLevels& levels,
                             std::function<int()> numInputChannels = {});
        ~InputMeter() override = default;

        void paint (juce::Graphics&) override;

    private:
        void timerCallback() override;

        struct ChannelState
        {
            MeterBallistics peak;
            MeterBallistics rms;
        };

        /** Maps a dBFS level to the [0,1] bar position. */
        static float normaliseDb (float db) noexcept;
        void drawChannel (juce::Graphics&, juce::Rectangle<int>, const ChannelState&,
                          const juce::String& name, int labelWidth);

        const InputLevels& inputLevels;
        std::array<ChannelState, 2> channels;
        std::function<int()> numInputChannels;
        double lastUpdateMs = 0.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputMeter)
    };
}
