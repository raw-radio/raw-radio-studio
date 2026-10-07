// raw-radio-studio — audio engine + device selection (Epic 1).

#include "AudioEngine.h"

#include "DeviceError.h"

#include <iostream>

namespace rrs
{
    namespace te = tracktion;

    //==============================================================================
    void StudioEngineBehaviour::setRecordingsDirectory (juce::File newDirectory)
    {
        recordingsDirectory = std::move (newDirectory);
    }

    juce::File StudioEngineBehaviour::getFileForNewAudioRecording (te::Track& track,
                                                                   const juce::String& fileExtension)
    {
        // Returning an empty file lets the engine fall back to its default
        // pattern; we always provide one so takes land in the session's
        // Recordings/ folder, which is what the crash-recovery scan looks at.
        if (recordingsDirectory == juce::File())
            return {};

        if (! recordingsDirectory.createDirectory())
            return {};

        auto sessionName = track.edit.getName();
        if (sessionName.isEmpty())
            sessionName = "Session";

        auto trackName = track.getName().isEmpty() ? juce::String ("Track") : track.getName();

        for (int take = 1;; ++take)
        {
            const auto name = sessionName + "_" + trackName + "_Take_" + juce::String (take) + fileExtension;
            const auto file = recordingsDirectory.getChildFile (juce::File::createLegalFileName (name));

            if (! file.exists())
                return file;
        }
    }

    //==============================================================================
    AudioEngine::AudioEngine()
    {
        // A custom EngineBehaviour directs recordings into the session folder;
        // the default UIBehaviour (nullptr -> default) shows JUCE alert windows
        // for engine-level warnings.
        auto behaviour = std::make_unique<StudioEngineBehaviour>();
        enginePtr = std::make_unique<te::Engine> ("raw-radio-studio", nullptr, std::move (behaviour));

        enforceDirectAlsaOnStartup();
        ensureDefaultSampleRateOnFirstRun();

        std::cout << "Audio device: "
                  << (getCurrentDeviceName().isNotEmpty() ? getCurrentDeviceName() : juce::String ("none"))
                  << "  " << (int) getCurrentSampleRate() << " Hz / " << getCurrentBufferSize() << " samples"
                  << " / ~" << juce::String (getEstimatedRoundTripLatencyMs(), 1) << " ms round-trip"
                  << std::endl;
    }

    AudioEngine::~AudioEngine() = default;

    StudioEngineBehaviour& AudioEngine::behaviour() noexcept
    {
        return static_cast<StudioEngineBehaviour&> (enginePtr->getEngineBehaviour());
    }

    //==============================================================================
    juce::StringArray AudioEngine::getDeviceTypeNames() const
    {
        juce::StringArray names;
        auto& dm = enginePtr->getDeviceManager().deviceManager;

        for (auto* type : dm.getAvailableDeviceTypes())
            if (type != nullptr)
                names.add (type->getTypeName());

        return names;
    }

    juce::String AudioEngine::getCurrentDeviceTypeName() const
    {
        return enginePtr->getDeviceManager().deviceManager.getCurrentAudioDeviceType();
    }

    juce::String AudioEngine::setDeviceType (const juce::String& typeName)
    {
        auto& dm = enginePtr->getDeviceManager().deviceManager;
        dm.setCurrentAudioDeviceType (typeName, true);

        if (dm.getCurrentAudioDeviceType() != typeName)
        {
            lastError = "Could not switch to the \"" + typeName + "\" audio backend.";
            return lastError;
        }

        lastError.clear();
        return {};
    }

    //==============================================================================
    juce::StringArray AudioEngine::getInputDeviceNames() const
    {
        juce::StringArray names;
        auto& dm = enginePtr->getDeviceManager().deviceManager;

        if (auto* type = dm.getCurrentDeviceTypeObject())
        {
            type->scanForDevices();

            for (auto& name : type->getDeviceNames (true))
                if (isAcceptableInputDeviceName (name))
                    names.add (name);
        }

        return names;
    }

    juce::String AudioEngine::getCurrentDeviceName() const
    {
        auto& dm = enginePtr->getDeviceManager().deviceManager;

        if (auto* device = dm.getCurrentAudioDevice())
            return device->getName();

        return {};
    }

    juce::Array<double> AudioEngine::getAvailableSampleRates() const
    {
        juce::Array<double> rates;
        auto& dm = enginePtr->getDeviceManager().deviceManager;

        if (auto* device = dm.getCurrentAudioDevice())
            rates = device->getAvailableSampleRates();

        if (rates.isEmpty())
            rates.add (defaultSampleRate);

        return rates;
    }

    juce::Array<int> AudioEngine::getAvailableBufferSizes() const
    {
        juce::Array<int> sizes;
        auto& dm = enginePtr->getDeviceManager().deviceManager;

        if (auto* device = dm.getCurrentAudioDevice())
            sizes = device->getAvailableBufferSizes();

        if (sizes.isEmpty())
            sizes.add (512);

        return sizes;
    }

    double AudioEngine::getCurrentSampleRate() const
    {
        auto& dm = enginePtr->getDeviceManager().deviceManager;

        if (auto* device = dm.getCurrentAudioDevice())
            return device->getCurrentSampleRate();

        return 0.0;
    }

    int AudioEngine::getCurrentBufferSize() const
    {
        auto& dm = enginePtr->getDeviceManager().deviceManager;

        if (auto* device = dm.getCurrentAudioDevice())
            return device->getCurrentBufferSizeSamples();

        return 0;
    }

    //==============================================================================
    juce::String AudioEngine::applyDeviceSetup (const juce::String& deviceName,
                                                double sampleRate,
                                                int bufferSize)
    {
        auto& dm = enginePtr->getDeviceManager().deviceManager;
        auto setup = dm.getAudioDeviceSetup();

        setup.inputDeviceName  = deviceName;
        setup.outputDeviceName = deviceName;
        setup.useDefaultInputChannels  = true;
        setup.useDefaultOutputChannels = true;

        if (sampleRate > 0.0)
            setup.sampleRate = sampleRate;

        if (bufferSize > 0)
            setup.bufferSize = bufferSize;

        // NOTE: we pass treatAsChosenDevice=true and surface any failure. JUCE
        // may revert to the previously open device on failure, but it never
        // silently substitutes a *different* backend — and we always report it.
        const auto error = dm.setAudioDeviceSetup (setup, true);

        if (error.isNotEmpty())
        {
            lastError = classifyDeviceError (error).userMessage;
            return lastError;
        }

        lastError.clear();
        return {};
    }

    bool AudioEngine::hasActiveDevice() const
    {
        auto& dm = enginePtr->getDeviceManager().deviceManager;

        if (auto* device = dm.getCurrentAudioDevice())
            return device->isOpen();

        return false;
    }

    double AudioEngine::getEstimatedRoundTripLatencyMs() const
    {
        auto& dm = enginePtr->getDeviceManager().deviceManager;

        if (auto* device = dm.getCurrentAudioDevice())
        {
            const auto sr = device->getCurrentSampleRate();

            if (sr > 0.0)
                return (device->getInputLatencyInSamples() + device->getOutputLatencyInSamples())
                           / sr * 1000.0;
        }

        return 0.0;
    }

    //==============================================================================
    void AudioEngine::ensureDefaultSampleRateOnFirstRun()
    {
        // The spec default is 48 kHz (NFR-A-4). Only enforce it before the user
        // has chosen a device setup — once Tracktion has persisted one, respect it.
        if (enginePtr->getPropertyStorage().getXmlProperty (te::SettingID::audio_device_setup) != nullptr)
            return;

        auto& dm = enginePtr->getDeviceManager().deviceManager;
        auto* device = dm.getCurrentAudioDevice();

        if (device == nullptr)
            return;

        if (juce::approximatelyEqual (device->getCurrentSampleRate(), defaultSampleRate))
            return;

        if (! device->getAvailableSampleRates().contains (defaultSampleRate))
            return;

        // Keep the current device/name setup; only change the rate so we never
        // mismatch input vs output device names on CoreAudio.
        auto setup = dm.getAudioDeviceSetup();
        setup.sampleRate = defaultSampleRate;

        // Non-fatal: if it cannot be set, keep the system default and let the
        // user choose from the device panel.
        if (dm.setAudioDeviceSetup (setup, true).isNotEmpty())
            lastError.clear();
    }

    void AudioEngine::enforceDirectAlsaOnStartup()
    {
       #if JUCE_LINUX
        auto& dm = enginePtr->getDeviceManager().deviceManager;

        const auto alreadyDirect = isAcceptableInputDeviceName (getCurrentDeviceName())
                                && getCurrentDeviceName().isNotEmpty();

        if (alreadyDirect)
            return;

        const auto names = getInputDeviceNames();

        if (names.isEmpty())
        {
            dm.closeAudioDevice();
            lastError = "No ALSA hardware (`hw`) audio device was found. raw-radio-studio "
                        "opens ALSA `hw` directly and does not fall back to PipeWire/PulseAudio.";
            return;
        }

        if (applyDeviceSetup (names[0], defaultSampleRate, 0).isNotEmpty())
            dm.closeAudioDevice();
       #endif
    }
}
