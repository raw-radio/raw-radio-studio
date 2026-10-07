// raw-radio-studio — audio device panel (Epic 1).

#include "DevicePanel.h"

#include "BrandColours.h"
#include "BrandFonts.h"
#include "DevicePanelLayout.h"

namespace rrs
{
    namespace
    {
        /** Fills `box` with `names`, preferring (in order) the caller's previous
            selection, then the device the engine actually has open, then the
            first entry. Empty lists clear the box. Used for both the input and
            the output selector so a hot-plug removal falls back gracefully. */
        void populateDeviceBox (juce::ComboBox& box,
                                const juce::StringArray& names,
                                const juce::String& previousSelection,
                                const juce::String& currentDevice)
        {
            box.clear (juce::dontSendNotification);
            box.addItemList (names, 1);

            if (previousSelection.isNotEmpty() && names.contains (previousSelection))
                box.setText (previousSelection, juce::dontSendNotification);
            else if (currentDevice.isNotEmpty() && names.contains (currentDevice))
                box.setText (currentDevice, juce::dontSendNotification);
            else if (! names.isEmpty())
                box.setSelectedItemIndex (0, juce::dontSendNotification);
            else
                box.setText ({}, juce::dontSendNotification);
        }
    }

    DevicePanel::DevicePanel (AudioEngine& engine)
        : audio (engine)
    {
        for (auto* label : { &typeLabel, &inputLabel, &outputLabel, &rateLabel, &bufferLabel })
        {
            label->setJustificationType (juce::Justification::centredLeft);
            label->setFont (brand::uiRegular (13.0f));
            label->setColour (juce::Label::textColourId, brand::textSecondary);
            addAndMakeVisible (*label);
        }

        typeLabel.setText ("Backend", juce::dontSendNotification);
        inputLabel.setText ("Input", juce::dontSendNotification);
        outputLabel.setText ("Output", juce::dontSendNotification);
        rateLabel.setText ("Sample rate", juce::dontSendNotification);
        bufferLabel.setText ("Buffer", juce::dontSendNotification);

        addAndMakeVisible (typeBox);
        addAndMakeVisible (inputBox);
        addAndMakeVisible (outputBox);
        addAndMakeVisible (rateBox);
        addAndMakeVisible (bufferBox);
        addAndMakeVisible (applyButton);
        addAndMakeVisible (rescanButton);

        inputBox.setTooltip ("Recording input device (microphone, interface input)");
        outputBox.setTooltip ("Playback output device (speakers, headphones)");

        statusLabel.setJustificationType (juce::Justification::centredLeft);
        statusLabel.setFont (brand::uiRegular (12.0f));
        statusLabel.setColour (juce::Label::textColourId, brand::textSecondary);
        addAndMakeVisible (statusLabel);

        errorLabel.setJustificationType (juce::Justification::topLeft);
        errorLabel.setFont (brand::uiRegular (12.0f));
        errorLabel.setColour (juce::Label::textColourId, brand::errorText);
        errorLabel.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (errorLabel);

        typeBox.onChange = [this] { refreshDevices(); };
        applyButton.onClick = [this] { applySelection(); };
        rescanButton.onClick = [this] { refreshAll(); };

        rescanButton.setIconName ("refresh-cw");
        applyButton.setTooltip ("Apply the selected input device, output device, "
                                "sample rate and buffer size");
        rescanButton.setTooltip ("Rescan audio devices");

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

        // Keep the user's current choice if it still exists; otherwise follow the
        // device the engine actually has open, then the first entry. This makes
        // a hot-plug removal of the selected device fall back gracefully instead
        // of leaving a dead name in the box.
        populateDeviceBox (inputBox,
                           audio.getInputDeviceNames(),
                           inputBox.getText(),
                           audio.getCurrentInputDeviceName());

        populateDeviceBox (outputBox,
                           audio.getOutputDeviceNames(),
                           outputBox.getText(),
                           audio.getCurrentOutputDeviceName());

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
            const auto in  = audio.getCurrentInputDeviceName();
            const auto out = audio.getCurrentOutputDeviceName();

            statusLabel.setText ("In: " + (in.isNotEmpty() ? in : juce::String ("none"))
                                     + "   |   Out: " + (out.isNotEmpty() ? out : juce::String ("none"))
                                     + "   |   " + juce::String ((int) audio.getCurrentSampleRate()) + " Hz"
                                     + " / " + juce::String (audio.getCurrentBufferSize()) + " samples"
                                     + "   |   ~" + juce::String (audio.getEstimatedRoundTripLatencyMs(), 1) + " ms",
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

        const auto inputName  = inputBox.getText();
        const auto outputName = outputBox.getText();

        if (inputName.isEmpty() && outputName.isEmpty())
        {
            showError ("Select an input or output device first.");
            return;
        }

        const auto sampleRate = rateBox.getText().getDoubleValue();
        const auto bufferSize = bufferBox.getText().getIntValue();

        {
            const juce::ScopedValueSetter<bool> guard (updating, true);

            if (auto error = audio.applyDeviceSetup (inputName, outputName, sampleRate, bufferSize);
                error.isNotEmpty())
            {
                showError (error);

                // The failed selection did not take effect: reflect the device
                // that is still open (an input-only mic keeps the output, and a
                // failed apply never tore the working device down).
                refreshDevices();
                updateStatus();
                return;
            }
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
        g.setColour (brand::bgPanel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

        g.setColour (brand::textPrimary);
        g.setFont (brand::uiSemiBold (14.0f));
        g.drawText ("Audio device", getLocalBounds().reduced (12).removeFromTop (18),
                    juce::Justification::centredLeft);
    }

    void DevicePanel::resized()
    {
        const auto layout = computeDevicePanelLayout (getLocalBounds());

        typeLabel.setBounds (layout.typeLabel);
        typeBox.setBounds (layout.typeBox);
        inputLabel.setBounds (layout.inputLabel);
        inputBox.setBounds (layout.inputBox);
        outputLabel.setBounds (layout.outputLabel);
        outputBox.setBounds (layout.outputBox);
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
