// raw-radio-studio — audio device panel (Epic 1).

#include "DevicePanel.h"

#include "DevicePanelLayout.h"

namespace rrs
{
    DevicePanel::DevicePanel (AudioEngine& engine)
        : audio (engine)
    {
        for (auto* label : { &typeLabel, &deviceLabel, &rateLabel, &bufferLabel })
        {
            label->setJustificationType (juce::Justification::centredLeft);
            label->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
            addAndMakeVisible (*label);
        }

        typeLabel.setText ("Backend", juce::dontSendNotification);
        deviceLabel.setText ("Device", juce::dontSendNotification);
        rateLabel.setText ("Sample rate", juce::dontSendNotification);
        bufferLabel.setText ("Buffer", juce::dontSendNotification);

        addAndMakeVisible (typeBox);
        addAndMakeVisible (deviceBox);
        addAndMakeVisible (rateBox);
        addAndMakeVisible (bufferBox);
        addAndMakeVisible (applyButton);
        addAndMakeVisible (rescanButton);

        statusLabel.setJustificationType (juce::Justification::centredLeft);
        statusLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        addAndMakeVisible (statusLabel);

        errorLabel.setJustificationType (juce::Justification::topLeft);
        errorLabel.setColour (juce::Label::textColourId, juce::Colour (0xfff87171));
        errorLabel.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (errorLabel);

        typeBox.onChange = [this] { refreshDevices(); };
        applyButton.onClick = [this] { applySelection(); };
        rescanButton.onClick = [this] { refreshAll(); };

        audio.deviceManager().deviceManager.addChangeListener (this);
        refreshAll();
    }

    DevicePanel::~DevicePanel()
    {
        audio.deviceManager().deviceManager.removeChangeListener (this);
    }

    //==============================================================================
    void DevicePanel::refreshAll()
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);

        auto types = audio.getDeviceTypeNames();
        typeBox.clear (juce::dontSendNotification);
        typeBox.addItemList (types, 1);
        typeBox.setText (audio.getCurrentDeviceTypeName(), juce::dontSendNotification);

        refreshDevices();
        updateStatus();
    }

    void DevicePanel::refreshDevices()
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);

        auto devices = audio.getInputDeviceNames();
        deviceBox.clear (juce::dontSendNotification);
        deviceBox.addItemList (devices, 1);

        const auto current = audio.getCurrentDeviceName();

        if (devices.contains (current))
            deviceBox.setText (current, juce::dontSendNotification);
        else if (! devices.isEmpty())
            deviceBox.setSelectedItemIndex (0, juce::dontSendNotification);

        refreshRatesAndBuffers();
    }

    void DevicePanel::refreshRatesAndBuffers()
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);

        const auto currentRate = audio.getCurrentSampleRate();
        auto rates = audio.getAvailableSampleRates();
        rateBox.clear (juce::dontSendNotification);

        for (int i = 0; i < rates.size(); ++i)
            rateBox.addItem (juce::String ((int) rates[i]) + " Hz", i + 1);

        bool rateSelected = false;
        for (int i = 0; i < rates.size(); ++i)
            if (juce::approximatelyEqual (rates[i], currentRate))
            {
                rateBox.setSelectedItemIndex (i, juce::dontSendNotification);
                rateSelected = true;
            }

        if (! rateSelected && ! rates.isEmpty())
            for (int i = 0; i < rates.size(); ++i)
                if (juce::approximatelyEqual (rates[i], AudioEngine::defaultSampleRate))
                    rateBox.setSelectedItemIndex (i, juce::dontSendNotification);

        const auto currentBuffer = audio.getCurrentBufferSize();
        auto sizes = audio.getAvailableBufferSizes();
        bufferBox.clear (juce::dontSendNotification);

        for (int i = 0; i < sizes.size(); ++i)
            bufferBox.addItem (juce::String (sizes[i]) + " samples", i + 1);

        bool bufferSelected = false;
        for (int i = 0; i < sizes.size(); ++i)
            if (sizes[i] == currentBuffer)
            {
                bufferBox.setSelectedItemIndex (i, juce::dontSendNotification);
                bufferSelected = true;
            }

        if (! bufferSelected && ! sizes.isEmpty())
            for (int i = 0; i < sizes.size(); ++i)
                if (sizes[i] == 512)
                    bufferBox.setSelectedItemIndex (i, juce::dontSendNotification);
    }

    void DevicePanel::updateStatus()
    {
        if (audio.hasActiveDevice())
        {
            statusLabel.setText (audio.getCurrentDeviceName()
                                     + "  |  " + juce::String ((int) audio.getCurrentSampleRate()) + " Hz"
                                     + "  |  " + juce::String (audio.getCurrentBufferSize()) + " samples"
                                     + "  |  ~" + juce::String (audio.getEstimatedRoundTripLatencyMs(), 1) + " ms",
                                 juce::dontSendNotification);
        }
        else
        {
            statusLabel.setText ("No audio device is open.", juce::dontSendNotification);
        }

        if (audio.getLastError().isNotEmpty())
            showError (audio.getLastError());
        else
            errorLabel.setText ({}, juce::dontSendNotification);
    }

    void DevicePanel::showError (const juce::String& message)
    {
        errorLabel.setText (message, juce::dontSendNotification);
    }

    void DevicePanel::applySelection()
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);
        errorLabel.setText ({}, juce::dontSendNotification);

        if (typeBox.getText() != audio.getCurrentDeviceTypeName())
        {
            if (auto error = audio.setDeviceType (typeBox.getText()); error.isNotEmpty())
            {
                showError (error);
                return;
            }

            refreshDevices();
        }

        const auto deviceName = deviceBox.getText();

        if (deviceName.isEmpty())
        {
            showError ("Select an audio device first.");
            return;
        }

        const auto sampleRate = rateBox.getText().getDoubleValue();
        const auto bufferSize = bufferBox.getText().getIntValue();

        if (auto error = audio.applyDeviceSetup (deviceName, sampleRate, bufferSize); error.isNotEmpty())
        {
            showError (error);
            return;
        }

        updateStatus();

        if (onDeviceChanged)
            onDeviceChanged();
    }

    void DevicePanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        if (updating)
            return;

        refreshAll();

        if (onDeviceChanged)
            onDeviceChanged();
    }

    //==============================================================================
    void DevicePanel::paint (juce::Graphics& g)
    {
        g.setColour (juce::Colour (0xff1e293b));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (14.0f);
        g.drawText ("Audio device", getLocalBounds().reduced (12).removeFromTop (18),
                    juce::Justification::centredLeft);
    }

    void DevicePanel::resized()
    {
        const auto layout = computeDevicePanelLayout (getLocalBounds());

        typeLabel.setBounds (layout.typeLabel);
        typeBox.setBounds (layout.typeBox);
        deviceLabel.setBounds (layout.deviceLabel);
        deviceBox.setBounds (layout.deviceBox);
        rateLabel.setBounds (layout.rateLabel);
        rateBox.setBounds (layout.rateBox);
        bufferLabel.setBounds (layout.bufferLabel);
        bufferBox.setBounds (layout.bufferBox);
        applyButton.setBounds (layout.applyButton);
        rescanButton.setBounds (layout.rescanButton);
        statusLabel.setBounds (layout.statusLabel);
        errorLabel.setBounds (layout.errorLabel);
    }
}
