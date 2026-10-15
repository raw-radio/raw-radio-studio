// raw-radio-studio — audio device panel (Epic 1).
//
// Independent enumeration + selection of the input device, the output device,
// sample rate and buffer size, with explicit error surfacing (no silent
// fallback). Input and output are separate selectors because on CoreAudio they
// are different physical devices and an input-only USB mic has no matching
// output — see DeviceSelection.h.

#pragma once

#include <JuceHeader.h>

#include <functional>

#include "studio/AudioEngine.h"
#include "ui/BrandButton.h"

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

        /** The height this panel wants right now: the full
            `devicePanelHeight` while an error is shown, the compact height
            otherwise (Epic 8 phase 1a — no space is reserved for an empty error
            label). The sidebar reads this to size the Device section. */
        int getPreferredHeight() const;

        /** Fired when the error state toggles empty <-> non-empty, so the host
            (the sidebar) can re-layout to give the message room (or reclaim it).
            Deliberately NOT fired on every status/error text change. */
        std::function<void()> onLayoutChanged;

    private:
        void refreshAll();
        void refreshDevices();
        void refreshRatesAndBuffers();
        void applySelection();
        void updateStatus();
        void setError (const juce::String&);
        bool hasError() const;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        AudioEngine& audio;

        juce::ComboBox typeBox, inputBox, outputBox, rateBox, bufferBox;
        BrandButton applyButton { "Apply" };
        BrandButton rescanButton { "Rescan" };
        juce::Label typeLabel, inputLabel, outputLabel, rateLabel, bufferLabel;
        juce::Label statusLabel, errorLabel;

        bool updating = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DevicePanel)
    };
}
