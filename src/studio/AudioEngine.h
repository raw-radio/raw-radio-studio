// raw-radio-studio — audio engine + device selection (Epic 1).
//
// Owns the Tracktion `Engine` and centralises everything to do with the audio
// device: enumeration, selection, sample rate / buffer size, and — critically —
// error surfacing with NO silent fallback (FR-MON-5, NFR-IO-4).
//
// The device panel drives this class; nothing else touches the JUCE
// AudioDeviceManager directly.

#pragma once

#include <JuceHeader.h>

#include <tracktion_engine/tracktion_engine.h>

#include <memory>

namespace rrs
{
    /** EngineBehaviour subclass that routes recorded takes into the session's
        `Recordings/` folder (crash-safe incremental WAVs live there). */
    class StudioEngineBehaviour final : public tracktion::EngineBehaviour
    {
    public:
        void setRecordingsDirectory (juce::File newDirectory);

        juce::File getFileForNewAudioRecording (tracktion::Track&, const juce::String& fileExtension) override;

        bool shouldOpenAudioInputByDefault() override     { return true; }
        bool autoInitialiseDeviceManager() override       { return true; }

    private:
        juce::File recordingsDirectory;
    };

    /** Wraps the Tracktion Engine and the audio device lifecycle. */
    class AudioEngine
    {
    public:
        AudioEngine();
        ~AudioEngine();

        tracktion::Engine& engine() noexcept                     { return *enginePtr; }
        tracktion::DeviceManager& deviceManager() noexcept      { return enginePtr->getDeviceManager(); }
        StudioEngineBehaviour& behaviour() noexcept;

        //==============================================================================
        // Device type enumeration (CoreAudio on macOS, ALSA on Linux).
        juce::StringArray getDeviceTypeNames() const;
        juce::String getCurrentDeviceTypeName() const;

        /** Switches device type. Returns "" on success or a classified user message. */
        juce::String setDeviceType (const juce::String& typeName);

        //==============================================================================
        // Devices of the current type.
        juce::StringArray getInputDeviceNames() const;
        juce::String getCurrentDeviceName() const;

        juce::Array<double> getAvailableSampleRates() const;
        juce::Array<int> getAvailableBufferSizes() const;

        double getCurrentSampleRate() const;
        int getCurrentBufferSize() const;

        /** The spec default for Epic 1 (NFR-A-4). */
        static constexpr double defaultSampleRate = 48000.0;

        /** Applies device + rate + buffer. Returns "" on success, else an actionable
            message. On failure the previous device is left untouched and the error is
            also stored in getLastError() — never a silent substitution. */
        juce::String applyDeviceSetup (const juce::String& deviceName,
                                       double sampleRate,
                                       int bufferSize);

        /** Applies only an *input* device, keeping the current output untouched.
            Returns "" on success, else an actionable message (also stored in
            getLastError()). Used by the headless self-test, where the input device
            may be pure capture hardware with no output channels (e.g. a USB mic),
            so forcing outputDeviceName to it would fail. */
        juce::String applyInputDeviceSetup (const juce::String& deviceName);

        bool hasActiveDevice() const;
        double getEstimatedRoundTripLatencyMs() const;

        //==============================================================================
        juce::String getLastError() const                { return lastError; }
        void clearLastError()                             { lastError.clear(); }

    private:
        void enforceDirectAlsaOnStartup();
        void ensureDefaultSampleRateOnFirstRun();

        std::unique_ptr<tracktion::Engine> enginePtr;
        juce::String lastError;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
    };
}
