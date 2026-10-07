// raw-radio-studio — audio engine + device selection (Epic 1).

#include "AudioEngine.h"

#include "DeviceError.h"
#include "DeviceSelection.h"

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
    AudioEngine::AudioEngine (bool useAudioDevices)
    {
        // A custom EngineBehaviour directs recordings into the session folder;
        // the default UIBehaviour (nullptr -> default) shows JUCE alert windows
        // for engine-level warnings. When `useAudioDevices` is false (tests) the
        // behaviour also suppresses device auto-initialisation.
        auto behaviour = std::make_unique<StudioEngineBehaviour> (useAudioDevices);
        enginePtr = std::make_unique<te::Engine> ("raw-radio-studio", nullptr, std::move (behaviour));

        enforceDirectAlsaOnStartup();
        ensureDefaultSampleRateOnFirstRun();

        std::cout << "Audio device: in=\""
                  << (getCurrentInputDeviceName().isNotEmpty() ? getCurrentInputDeviceName() : juce::String ("none"))
                  << "\" out=\""
                  << (getCurrentOutputDeviceName().isNotEmpty() ? getCurrentOutputDeviceName() : juce::String ("none"))
                  << "\"  " << (int) getCurrentSampleRate() << " Hz / " << getCurrentBufferSize() << " samples"
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

    juce::StringArray AudioEngine::getOutputDeviceNames() const
    {
        juce::StringArray names;
        auto& dm = enginePtr->getDeviceManager().deviceManager;

        if (auto* type = dm.getCurrentDeviceTypeObject())
        {
            type->scanForDevices();

            for (auto& name : type->getDeviceNames (false))
                if (isAcceptableOutputDeviceName (name))
                    names.add (name);
        }

        return names;
    }

    juce::String AudioEngine::getCurrentInputDeviceName() const
    {
        return enginePtr->getDeviceManager().deviceManager.getAudioDeviceSetup().inputDeviceName;
    }

    juce::String AudioEngine::getCurrentOutputDeviceName() const
    {
        return enginePtr->getDeviceManager().deviceManager.getAudioDeviceSetup().outputDeviceName;
    }

    juce::String AudioEngine::getCurrentDeviceName() const
    {
        auto& dm = enginePtr->getDeviceManager().deviceManager;

        if (auto* device = dm.getCurrentAudioDevice())
            return device->getName();

        return {};
    }

    int AudioEngine::getNumActiveInputChannels() const
    {
        auto& dm = enginePtr->getDeviceManager().deviceManager;

        if (auto* device = dm.getCurrentAudioDevice())
            return device->getActiveInputChannels().countNumberOfSetBits();

        return 0;
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
    juce::String AudioEngine::applyDeviceSetup (const juce::String& inputDeviceName,
                                                const juce::String& outputDeviceName,
                                                double sampleRate,
                                                int bufferSize)
    {
        auto& dm = enginePtr->getDeviceManager().deviceManager;

        const auto current = dm.getAudioDeviceSetup();

        // Input and output are independent: an empty request keeps the current
        // device on that side, and the input name is never copied into the
        // output (the original input-only-mic bug).
        const auto selection = resolveDeviceNames (inputDeviceName, outputDeviceName,
                                                   current.inputDeviceName, current.outputDeviceName);

        // Validate BOTH effective names against the enumerated lists *before*
        // calling JUCE. `setAudioDeviceSetup` deletes the current device before
        // it validates names, so a stale/mismatched name would otherwise tear
        // down the working device and stop playback. Returning here leaves the
        // current device running (FR-MON-5).
        if (selection.input.isNotEmpty() && ! getInputDeviceNames().contains (selection.input))
        {
            lastError = classifyDeviceError ("No such device: " + selection.input).userMessage;
            return lastError;
        }

        if (selection.output.isNotEmpty() && ! getOutputDeviceNames().contains (selection.output))
        {
            lastError = classifyDeviceError ("No such device: " + selection.output).userMessage;
            return lastError;
        }

        auto setup = current;
        setup.inputDeviceName  = selection.input;
        setup.outputDeviceName = selection.output;
        setup.useDefaultInputChannels  = true;
        setup.useDefaultOutputChannels = true;

        if (sampleRate > 0.0)
            setup.sampleRate = sampleRate;

        if (bufferSize > 0)
            setup.bufferSize = bufferSize;

        const auto hadWorkingDevice = dm.getCurrentAudioDevice() != nullptr;
        const auto error = dm.setAudioDeviceSetup (setup, true);

        if (error.isNotEmpty())
        {
            lastError = classifyDeviceError (error).userMessage;

            // A genuine open failure (e.g. the device became busy) still made
            // JUCE delete the current device. Restore the previously-working
            // setup so playback continues instead of going silent.
            if (hadWorkingDevice
                && (current.inputDeviceName.isNotEmpty() || current.outputDeviceName.isNotEmpty()))
            {
                auto restore = current;
                restore.useDefaultInputChannels  = true;
                restore.useDefaultOutputChannels = true;
                dm.setAudioDeviceSetup (restore, false);
            }

            return lastError;
        }

        lastError.clear();
        return {};
    }

    juce::String AudioEngine::applyInputDeviceSetup (const juce::String& deviceName)
    {
        // Keep the current output device: pure input hardware (a USB mic) has no
        // output channels, so forcing outputDeviceName to it would fail to open.
        return applyDeviceSetup (deviceName, {}, 0.0, 0);
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

        // `getInputDeviceNames()` already excludes plugin/routed PCMs
        // (PipeWire/PulseAudio/JACK, dmix/dsnoop), so a name accepted here is a
        // confirmed direct-hardware PCM.
        const auto currentName = getCurrentDeviceName();
        const auto alreadyDirect = currentName.isNotEmpty()
                                && isAcceptableInputDeviceName (currentName);

        if (alreadyDirect)
            return;

        const auto names = getInputDeviceNames();

        if (names.isEmpty())
        {
            // Fail loudly: never fall back to the plugin/PipeWire path.
            dm.closeAudioDevice();
            lastError = "No direct ALSA hardware audio device was found. raw-radio-studio "
                        "opens ALSA hardware directly; PipeWire/PulseAudio/JACK and "
                        "dmix/dsnoop PCM devices are excluded by design (no fallback).";
            return;
        }

        // `names[0]` is the first confirmed direct-hardware device. If it cannot
        // be opened, close the device rather than silently keeping a plugin one.
        //
        // Unlike the user-facing panel, the Linux auto-selection deliberately
        // opens the same direct-hardware PCM for capture and playback (the
        // original behaviour): a `hw:`/analog PCM is normally full-duplex, and
        // keeping a PipeWire/Pulse output would be the plugin fallback the spec
        // forbids. If it is not usable as an output the apply fails and we close
        // the device — loudly, never half-open.
        if (applyDeviceSetup (names[0], names[0], defaultSampleRate, 0).isNotEmpty())
            dm.closeAudioDevice();
       #endif
    }
}
