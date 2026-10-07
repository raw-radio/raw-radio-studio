// raw-radio-studio — audio device panel (Epic 1).
//
// Enumeration + selection of device, sample rate and buffer size, with explicit
// error surfacing (no silent fallback).

#pragma once

#include <JuceHeader.h>

#include <functional>

#include "studio/AudioEngine.h"

namespace rrs
{
    class DevicePanel final : public juce::Component,
                              private juce::ChangeListener
    {
    public:
        explicit DevicePanel (AudioEngine&);
        ~DevicePanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Called after the active device changes so the session can re-bind inputs. */
        std::function<void()> onDeviceChanged;

    private:
        void refreshAll();
        void refreshDevices();
        void refreshRatesAndBuffers();
        void applySelection();
        void updateStatus();
        void showError (const juce::String&);
        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        AudioEngine& audio;

        juce::ComboBox typeBox, deviceBox, rateBox, bufferBox;
        juce::TextButton applyButton { "Apply" }, rescanButton { "Rescan" };
        juce::Label typeLabel, deviceLabel, rateLabel, bufferLabel;
        juce::Label statusLabel, errorLabel;

        bool updating = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DevicePanel)
    };
}
