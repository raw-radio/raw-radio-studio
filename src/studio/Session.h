// raw-radio-studio — session model (Epic 1).
//
// Owns the Tracktion `Edit` (the session), the single stereo track, transport
// state, input assignment/monitoring, autosave, and crash recovery. All heavy
// audio-thread work goes through the engine; the only audio-thread function we
// own is the RT-safe `acceptInputBuffer` level accumulator.

#pragma once

#include <JuceHeader.h>

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
#include <memory>

#include "AudioEngine.h"

namespace rrs
{
    /** Lock-free input level accumulator. Written on the audio thread, read by
        the UI timer. No allocations, no locks. */
    struct InputLevels
    {
        std::atomic<float> peak[2] { { 0.0f }, { 0.0f } };
        std::atomic<float> rms[2]  { { 0.0f }, { 0.0f } };

        void reset() noexcept;
        void push (const choc::buffer::ChannelArrayView<float>& buffer) noexcept;
    };

    /** The application session: one `Edit`, one stereo track. */
    class Session final : public juce::ChangeBroadcaster,
                          private juce::Timer,
                          public tracktion::InputDeviceInstance::Consumer
    {
    public:
        explicit Session (AudioEngine&);
        ~Session() override;

        //==============================================================================
        // Session lifecycle
        bool createNew (const juce::File& editFile = {});
        bool open (const juce::File& editFile);
        bool save();
        bool saveAs (const juce::File&);

        juce::File getEditFile() const noexcept                 { return editFile; }
        juce::String getSessionName() const;
        tracktion::Edit* getEdit() const noexcept               { return edit.get(); }
        tracktion::AudioTrack* getTrack() const;

        //==============================================================================
        // Recording / monitoring
        bool setTrackArmed (bool shouldBeArmed);
        bool isTrackArmed() const;

        bool setMonitoringEnabled (bool);
        bool isMonitoringEnabled() const;

        void play();
        void stop();
        bool record();

        bool isPlaying() const;
        bool isRecording() const;

        /** Returns the wave input device currently assigned to the track. */
        tracktion::WaveInputDevice* getSelectedWaveInputDevice() const;

        /** Re-resolves the input device and re-assigns it to the track. Call after
            the audio device changes (wave devices are rebuilt). */
        void reconfigureInputs();

        //==============================================================================
        // Metering
        InputLevels& getInputLevels() noexcept                  { return inputLevels; }

        //==============================================================================
        // Settings (FR-PRJ-2)
        void setAutosaveIntervalSeconds (int seconds);
        int getAutosaveIntervalSeconds() const noexcept         { return autosaveIntervalSeconds; }

        //==============================================================================
        // Crash recovery (FR-REC-8 / FR-PRJ-3)
        struct RecoveryInfo
        {
            juce::File editFile;
            juce::File tempEditFile;                     ///< `.tmp_<name>` from autosave.
            bool hasTempEdit = false;
            bool uncleanShutdown = false;                ///< Lock file was present.
            juce::Array<juce::File> candidateRecordings; ///< Unreferenced WAV candidates.

            bool hasRecoverables() const noexcept
            {
                return hasTempEdit || ! candidateRecordings.isEmpty();
            }
        };

        /** Inspects disk for a session file before it is opened. */
        static RecoveryInfo detectRecovery (const juce::File& editFile);

        /** Restores an autosaved temp edit over the session file (keeps a backup). */
        bool applyTempEditRecovery (const RecoveryInfo&);

        /** Imports unreferenced recordings from the session's Recordings folder
            into the current edit as clips. Returns the number imported. */
        int importOrphanedRecordings();

        //==============================================================================
        juce::String getLastError() const                       { return lastError; }
        void clearLastError()                                   { lastError.clear(); }

        //==============================================================================
        // InputDeviceInstance::Consumer — called on the audio thread. RT-safe only.
        void acceptInputBuffer (choc::buffer::ChannelArrayView<float>) override;

    private:
        //==============================================================================
        void timerCallback() override;
        bool createOrOpenEdit (const juce::File&, bool loadIfExists);
        bool configureSingleStereoTrack();
        void ensureMeterAttached();
        void detachMeter();
        void writeLockFile();
        void removeLockFile();
        juce::Array<juce::File> findReferencedRecordings() const;

        AudioEngine& audio;
        std::unique_ptr<tracktion::Edit> edit;
        juce::File editFile;

        InputLevels inputLevels;
        int autosaveIntervalSeconds = 30;
        double lastAutosaveMs = 0.0;
        bool meterAttached = false;
        bool inputsConfigured = false;
        juce::WeakReference<tracktion::InputDeviceInstance> meterInstance;

        bool lastPlaying = false, lastRecording = false, lastArmed = false;

        juce::String lastError;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
    };
}
