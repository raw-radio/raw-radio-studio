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

        /** Closes the current session, returning the app to an empty state.
            Turns hardware input monitoring OFF (it is a device-level setting, so
            it would otherwise outlive the Edit and leave the mic live in "No
            session"), deletes any autosave temp version and clears the startup
            sentinel so a deliberately-closed project leaves no stale recovery
            state. The caller is responsible for warning about unsaved changes
            first. */
        void close();

        /** True when the current edit has edits not yet written to the session
            file (drives the Close/Save unsaved-changes guard). */
        bool hasUnsavedChanges() const;

        juce::File getEditFile() const noexcept                 { return editFile; }
        juce::String getSessionName() const;
        tracktion::Edit* getEdit() const noexcept               { return edit.get(); }
        tracktion::AudioTrack* getTrack() const;
        int getNumAudioTracks() const;

        //==============================================================================
        // Audio import (Epic 1 backing-track playback)
        /** Imports an audio file as a referenced clip on a new audio track
            (WAV/AIFF/FLAC/MP3/OGG), laid down at time 0. Coexists with the armed
            record track: the imported track plays back while recording. The source
            file is referenced, never copied or modified. Returns false on failure
            with getLastError() set. */
        bool importAudioFile (const juce::File& sourceFile);

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
            bool uncleanShutdown = false;                ///< Startup sentinel was present.
            juce::Array<juce::File> candidateRecordings; ///< Unreferenced WAV candidates.

            /** True when the previous run did not end cleanly: the startup
                sentinel (session.lock) is still present, or an unsaved temp
                version was left behind. This is the authoritative signal that
                must ALWAYS trigger the recovery prompt — it is not inferred
                from transient autosave state. */
            bool interrupted() const noexcept
            {
                return uncleanShutdown || hasTempEdit;
            }

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

        /** Applies the input device's channel routing for the current number of
            active input channels (mono -> centred L+R, stereo -> L/R). Returns
            false when no wave input device is available yet. Does not touch
            monitoring/enabled state. */
        bool applyInputChannelConfiguration();

        void ensureMeterAttached();
        void detachMeter();
        /** Writes/removes the interrupted-session sentinel (AppPaths::lockFile).
            Written when a session becomes active; cleared by a clean shutdown
            and by Session::close(), so its presence on the next launch
            deterministically means "the previous run was interrupted". */
        void writeInterruptionMarker();
        void clearInterruptionMarker();
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
