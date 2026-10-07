// raw-radio-studio — session model (Epic 1 walking skeleton + Epic 2 multitrack).
//
// Owns the Tracktion `Edit` (the session), the audio tracks, transport state,
// per-track input assignment/monitoring, the basic mixer (gain/pan/mute/solo +
// master), autosave, and crash recovery. All heavy audio-thread work goes
// through the engine; the only audio-thread function we own is the RT-safe
// `acceptInputBuffer` level accumulator.

#pragma once

#include <JuceHeader.h>

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
#include <memory>
#include <vector>

#include "AudioEngine.h"
#include "InputMapping.h"

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

    /** The application session: one `Edit` with 1..N audio tracks. */
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

        //==============================================================================
        // Tracks (Epic 1: one stereo track; Epic 2: 1..N)
        /** Track 0 (Epic 1 convenience; the first *input* track). */
        tracktion::AudioTrack* getTrack() const;
        /** Track at `index` in engine order. */
        tracktion::AudioTrack* getTrack (int index) const;
        int getNumAudioTracks() const;
        juce::String getTrackName (int index) const;
        bool setTrackName (int index, const juce::String&);

        /** Adds a new input/record track mapped to the next free hardware input.
            Returns the new track index, or -1 on failure. */
        int addAudioTrack (const juce::String& name = {});
        /** Removes a track (and its clips). Returns false for the last track. */
        bool removeAudioTrack (int index);

        //==============================================================================
        // Per-track device-agnostic input mapping (FR-REC-3, Epic 2)
        bool setTrackInputMapping (int trackIndex, const InputMapping&);
        InputMapping getTrackInputMapping (int trackIndex) const;

        /** Number of currently active hardware input channels on the open device
            (0 when no device is open). Used by the UI to offer valid input
            assignments (FR-REC-3). */
        int getNumInputChannels() const;

        //==============================================================================
        // Audio import (Epic 1 backing-track playback)
        /** Imports an audio file as a referenced clip on a new audio track
            (WAV/AIFF/FLAC/MP3/OGG), laid down at time 0. Coexists with the armed
            record track: the imported track plays back while recording. The source
            file is referenced, never copied or modified. Returns false on failure
            with getLastError() set. */
        bool importAudioFile (const juce::File& sourceFile);

        //==============================================================================
        // Recording / monitoring (per-track, Epic 2; track 0 singular forms keep
        // the Epic 1 API)
        bool setTrackArmed (int trackIndex, bool shouldBeArmed);
        bool isTrackArmed (int trackIndex) const;
        bool setTrackArmed (bool shouldBeArmed)          { return setTrackArmed (0, shouldBeArmed); }
        bool isTrackArmed() const                        { return isTrackArmed (0); }

        bool setMonitoringEnabled (bool);
        bool isMonitoringEnabled() const;

        void play();
        void stop();
        bool record();
        bool isAnyTrackArmed() const;

        bool isPlaying() const;
        bool isRecording() const;

        /** Returns the wave input device currently assigned to track 0. */
        tracktion::WaveInputDevice* getSelectedWaveInputDevice() const;

        /** Re-resolves the input device and re-assigns it to the track. Call after
            the audio device changes (wave devices are rebuilt). */
        void reconfigureInputs();

        /** True once every input track has been bound to a live input device. */
        bool isInputConfigured() const noexcept                 { return inputsConfigured; }

        /** Engine-order indices of the record/input tracks. */
        juce::Array<int> getInputTrackIndices() const           { return inputTrackIndices(); }

        //==============================================================================
        // Basic mixer (FR-MIX-1/3, Epic 2)
        bool setTrackGainDb (int trackIndex, float db);
        float getTrackGainDb (int trackIndex) const;
        bool setTrackPan (int trackIndex, float pan);      ///< -1 (L) .. 0 (centre) .. 1 (R)
        float getTrackPan (int trackIndex) const;
        bool setTrackMute (int trackIndex, bool shouldMute);
        bool isTrackMuted (int trackIndex) const;
        bool setTrackSolo (int trackIndex, bool shouldSolo);
        bool isTrackSolo (int trackIndex) const;

        bool setMasterGainDb (float db);
        float getMasterGainDb() const;
        bool setMasterPan (float pan);                     ///< -1 (L) .. 0 (centre) .. 1 (R)
        float getMasterPan() const;
        bool setMasterMute (bool shouldMute);
        bool isMasterMuted() const;

        //==============================================================================
        // Metering
        InputLevels& getInputLevels() noexcept                  { return inputLevels; }

        /** Peak level (dBFS, one value per channel) + clip/overload flag for a
            track or the master bus. Reads and clears the engine's RT-safe level
            meters; call from the UI timer only (never the audio thread). */
        struct MeterReading
        {
            float peakDb[2] { -100.0f, -100.0f };
            bool clipped = false;
        };

        MeterReading readTrackMeter (int trackIndex);
        MeterReading readMasterMeter();

        //==============================================================================
        // Count-in / metronome (FR-REC-10, target)
        void setMetronomeEnabled (bool);
        bool isMetronomeEnabled() const;
        /** When true the click is only audible while recording (typical overdub). */
        void setMetronomeRecordingOnly (bool);
        bool isMetronomeRecordingOnly() const;
        void setCountInMode (tracktion::Edit::CountIn);
        tracktion::Edit::CountIn getCountInMode() const;
        int getCountInBeats() const;
        void setMetronomeVolume (float gain);
        float getMetronomeVolume() const;

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

        /** Ensures at least one input track exists, then resolves and binds the
            input device(s) for every input track (Epic 1: the single stereo track).
            Returns false while the engine has not yet built its wave-device list
            (retried from timerCallback). */
        bool configureTracks();

        /** Re-applies each input track's channel routing from its mapping. */
        bool applyInputChannelConfiguration (int trackIndex, tracktion::WaveInputDevice*& resolvedOut);

        /** Reads/writes the per-track input mapping stored on the track's state. */
        InputMapping readTrackMapping (int trackIndex) const;
        void writeTrackMapping (tracktion::AudioTrack&, const InputMapping&);

        /** True when the track is a record/input track (marked at creation). */
        bool isInputTrack (const tracktion::AudioTrack&) const;

        /** Engine-order indices of the record/input tracks (Epic 1 back-compat:
            a session with no markers treats track 0 as the input track). */
        juce::Array<int> inputTrackIndices() const;

        /** Resolves the wave input device that carries `channel`, or the best
            available fallback. */
        tracktion::WaveInputDevice* resolveInputDeviceFor (const InputMapping&) const;

        void ensureMeterAttached();
        void detachMeter();
        void writeInterruptionMarker();
        void clearInterruptionMarker();
        juce::Array<juce::File> findReferencedRecordings() const;

        // Per-track and master level metering (FR-MIX-3).
        void attachMeters();
        void detachMeters();
        void refreshMetersIfNeeded();

        struct MeterClient
        {
            tracktion::LevelMeasurer::Client client;
            tracktion::LevelMeasurer* measurer = nullptr;
        };

        std::vector<std::unique_ptr<MeterClient>> trackMeters;
        std::unique_ptr<MeterClient> masterMeter;
        tracktion::LevelMeterPlugin* masterMeterPlugin = nullptr;
        int metersTrackCount = -1;

        AudioEngine& audio;
        std::unique_ptr<tracktion::Edit> edit;
        juce::File editFile;

        InputLevels inputLevels;
        int autosaveIntervalSeconds = 30;
        double lastAutosaveMs = 0.0;

        /** Master fader value (dB) and app-level mute. Tracktion has no dedicated
            master mute, so muting is applied by overriding the master volume
            plugin's gain while the fader value is remembered here (FR-MIX-1). */
        float masterGainDb = 0.0f;
        bool masterMuted = false;
        void applyMasterGain();
        bool meterAttached = false;
        bool inputsConfigured = false;
        juce::WeakReference<tracktion::InputDeviceInstance> meterInstance;

        bool lastPlaying = false, lastRecording = false, lastArmed = false;

        juce::String lastError;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
    };
}
