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
        /** @param useAudioDevices  false keeps the device manager inert (used by
            headless tests so no real hardware is opened). */
        explicit StudioEngineBehaviour (bool useAudioDevices = true)
            : audioDevicesEnabled (useAudioDevices) {}

        void setRecordingsDirectory (juce::File newDirectory);

        juce::File getFileForNewAudioRecording (tracktion::Track&, const juce::String& fileExtension) override;

        bool shouldOpenAudioInputByDefault() override     { return audioDevicesEnabled; }
        bool autoInitialiseDeviceManager() override       { return audioDevicesEnabled; }

    private:
        bool audioDevicesEnabled = true;
        juce::File recordingsDirectory;
    };

    /** Wraps the Tracktion Engine and the audio device lifecycle. */
    class AudioEngine
    {
    public:
        /** @param useAudioDevices  when false the device manager is not
            auto-initialised: the engine is created but no hardware is opened.
            The application always uses the default (true); tests pass false to
            stay hermetic. */
        explicit AudioEngine (bool useAudioDevices = true);
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
        //
        // Input and output devices are enumerated (and selected) independently:
        // on CoreAudio a microphone and the speakers are different physical
        // devices, and an input-only USB mic has no matching output at all.
        juce::StringArray getInputDeviceNames() const;
        juce::StringArray getOutputDeviceNames() const;

        /** The input/output device names currently in use (from the open setup).
            May be empty when that side is not open. */
        juce::String getCurrentInputDeviceName() const;
        juce::String getCurrentOutputDeviceName() const;

        /** The name of the currently-open JUCE device (kept for logging/self-test). */
        juce::String getCurrentDeviceName() const;

        /** Number of active (opened) input channels on the current device; 0 when
            no device is open. Used by the input meter to label mono vs stereo. */
        int getNumActiveInputChannels() const;

        juce::Array<double> getAvailableSampleRates() const;
        juce::Array<int> getAvailableBufferSizes() const;

        double getCurrentSampleRate() const;
        int getCurrentBufferSize() const;

        /** The spec default for Epic 1 (NFR-A-4). */
        static constexpr double defaultSampleRate = 48000.0;

        /** Applies the input and output devices + rate + buffer. Returns "" on
            success, else an actionable message.

            The two device names are independent: an empty name keeps the current
            device for that side, and the input name is never copied into the
            output (so an input-only mic keeps the existing output). Both names
            are validated against the enumerated device lists *before* JUCE's
            `setAudioDeviceSetup` is called, so a stale/mismatched name can never
            make JUCE tear down the currently-working device. If an open still
            fails, the previously-working setup is restored and the error is
            reported. The error is also stored in getLastError(). */
        juce::String applyDeviceSetup (const juce::String& inputDeviceName,
                                       const juce::String& outputDeviceName,
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
