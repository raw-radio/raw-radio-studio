// raw-radio-studio — input level meter (Epic 1).
//
// Reads the RT-safe InputLevels accumulator on a UI timer and draws peak + RMS
// per channel. No engine access on the UI thread beyond atomics.

#pragma once

#include <JuceHeader.h>

#include <array>

namespace rrs
{
    struct InputLevels;

    class InputMeter final : public juce::Component,
                             private juce::Timer
    {
    public:
        explicit InputMeter (const InputLevels& levels);
        ~InputMeter() override = default;

        void paint (juce::Graphics&) override;

    private:
        void timerCallback() override;

        struct ChannelState
        {
            float peak = 0.0f;
            float rms = 0.0f;
            float hold = 0.0f;
            int holdCountdown = 0;
        };

        static float normaliseDb (float linearGain) noexcept;
        void drawChannel (juce::Graphics&, juce::Rectangle<int>, const ChannelState&, const juce::String& name);

        const InputLevels& inputLevels;
        std::array<ChannelState, 2> channels;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputMeter)
    };
}
