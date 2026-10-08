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
#include "PluginPresets.h"

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

        //==============================================================================
        // Per-input-track record gain / trim (FR-REC-4, Epic 2 GUI retest)
        /** Sets the record trim in dB (applied to the input buffer before it is
            both monitored and written to disk). RT-safe: the gain is a plain
            value applied by the engine on the audio thread; no allocation. The
            value is stored on the track's state so it survives save/open.

            When `persist` is false the gain is applied live and stored on the
            track state but the session file is not rewritten. The mixer passes
            false for every drag update and saves once on mouse-up, so a trim
            drag does not write a full `.tracktionedit` per mouse move. */
        bool setTrackInputGainDb (int trackIndex, float db, bool persist = true);
        float getTrackInputGainDb (int trackIndex) const;

        //==============================================================================
        // Take normalisation (Epic 2 GUI retest)
        /** Outcome of a take normalisation. `appliedGainDb` is the clip gain
            actually written (Tracktion clamps clip gain to [-100, +24] dB), and
            `achievedPeakDb` is the resulting peak (source peak + applied gain).
            `clamped` is true when the requested gain hit that limit, so the take
            did not reach the requested target and the UI must report the
            achieved value, not the target. */
        struct NormaliseResult
        {
            float appliedGainDb = 0.0f;
            float achievedPeakDb = -100.0f;
            bool clamped = false;
        };

        /** Peak-normalises the most recent wave clip on `trackIndex` (chosen by
            timeline position, not clip-list order, so the choice is
            deterministic) so its peak lands at `targetPeakDb` (default
            -1 dBFS). Non-destructive (clip gain). Returns false when the track
            has no readable wave clip. When `result` is non-null it is filled
            with the gain written and the achieved peak.

            KNOWN LIMITATION: the source peak is scanned synchronously on the
            calling (message) thread, so normalising a long take briefly blocks
            the UI. Moving the scan to a background thread with a
            progress/locked state is a follow-up. */
        bool normaliseTake (int trackIndex, float targetPeakDb = -1.0f,
                            NormaliseResult* result = nullptr);

        /** Peak-normalises the most recent take in the session (input tracks
            first, then any track with a wave clip). Returns false when there is
            nothing to normalise. `result` behaves as in `normaliseTake`. */
        bool normaliseLatestTake (float targetPeakDb = -1.0f,
                                  NormaliseResult* result = nullptr);

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
        /** Pause: stops the transport in place, keeping the playhead where it is
            (Play/Pause toggle semantics — unchanged). */
        void pause();
        /** Stop: stops the transport (and recording) in place, leaving the
            playhead where it stopped — Stop does NOT rewind. Returning to the
            session start (0) is the job of `goToStart()` (the "Start" button).
            Owner request: Stop must not move the playhead to 0. */
        void stop();
        /** Moves the playhead to the session start (0) without changing the
            transport state ("Go to start" / skip-back). */
        void goToStart();

        /** Current transport position in seconds (0 when there is no session). */
        double getPositionSeconds() const;

        /** Seeks the transport to `seconds` (clamped to >= 0) via
            `TransportControl::setPosition`. Used by the clickable timeline; a
            plain control change on the message thread, never the audio thread. */
        void setPositionSeconds (double seconds);

        /** Arrangement length in seconds for the timeline ruler, floored to a
            small minimum so the strip is always clickable and never divided by
            zero. */
        double getTimelineLengthSeconds() const;

        //==============================================================================
        // Offline-render transport preservation (FR-EXP-1/2/3).
        //
        // Tracktion's offline renderer builds an `Edit::ScopedRenderStatus` with
        // `shouldReallocateOnDestruction = false`, so it frees the live playback
        // context and never rebuilds it. Freeing the context also tears down the
        // input-device instances, which silences monitoring and playback until the
        // app is restarted. The app captures this snapshot on the message thread
        // *before* an export and restores it from the export completion callback.
        struct OfflineRenderTransportState
        {
            const tracktion::Edit* edit = nullptr; ///< Guards against a replaced Edit.
            bool contextWasAllocated = false;
            bool wasPlaying = false;
            bool wasRecording = false;
            double positionSeconds = 0.0;
        };

        /** Snapshots the live transport/playback-context state before an offline
            render. Safe on the message thread; never touches the audio thread. */
        OfflineRenderTransportState captureTransportForOfflineRender() const;

        /** Rebuilds the live playback context (restoring monitoring + the playback
            graph) and restores the captured transport position/play state. No-op
            when the snapshot belongs to a different Edit than the current one.
            Message-thread only. */
        void restoreTransportAfterOfflineRender (const OfflineRenderTransportState&);

        //==============================================================================
        // Region selection (FR-EXP-2)
        /** Sets the export region [start, end] in seconds. The bounds are
            order-independent (swapped if reversed) and clamped to >= 0; a
            zero- or negative-length span clears the selection. This is
            transient UI state and is not serialised with the project. */
        void setSelectionSeconds (double startSeconds, double endSeconds);
        void clearSelection();
        bool hasSelection() const noexcept          { return selectionEndSeconds > selectionStartSeconds; }
        double getSelectionStartSeconds() const noexcept { return selectionStartSeconds; }
        double getSelectionEndSeconds() const noexcept   { return selectionEndSeconds; }

        //==============================================================================
        // Arrangement clip editing (Epic 4 — FR-ED-1/2/3/6)
        //
        // Message-thread only: every operation touches the Edit's ValueTree and
        // iterator state and persists the session, and each one is wrapped in a
        // single UndoManager transaction so it is one undo step. Never call these
        // from the audio thread.
        //
        // `trackIndex` is an engine audio-track index; `clipIndex` addresses the
        // clip at that index in `track->getClips()` (the order Tracktion stores).
        // `getClips()` returns them sorted by timeline position for the UI, with
        // each info carrying its real `clipIndex`.
        struct ClipInfo
        {
            int trackIndex = -1;
            int clipIndex = -1;
            juce::String name;
            double startSeconds = 0.0;
            double endSeconds = 0.0;
            double lengthSeconds = 0.0;
            double offsetSeconds = 0.0;
            double fadeInSeconds = 0.0;
            double fadeOutSeconds = 0.0;
            bool isWave = false;
            bool isLooping = false;
            bool isMuted = false;
        };

        /** All clips on a track, sorted by timeline start. Each info records its
            real `clipIndex`, which the mutation methods below expect. */
        std::vector<ClipInfo> getClips (int trackIndex) const;
        bool getClipInfo (int trackIndex, int clipIndex, ClipInfo&) const;

        /** Moves a clip's start to `newStartSeconds`, keeping its length and the
            source content under the clip (offset) unchanged. */
        bool moveClip (int trackIndex, int clipIndex, double newStartSeconds);

        /** Trims the clip's start edge to `newStartSeconds`, keeping the source
            content stationary (the visible audio does not slide). */
        bool trimClipStart (int trackIndex, int clipIndex, double newStartSeconds);

        /** Trims the clip's end edge to `newEndSeconds`, keeping the source
            content stationary. */
        bool trimClipEnd (int trackIndex, int clipIndex, double newEndSeconds);

        /** Splits the clip at `timeSeconds` (strictly inside the clip) into two
            adjacent clips that reference the same source content. Returns false
            when the split point is not inside the clip. */
        bool splitClip (int trackIndex, int clipIndex, double timeSeconds);

        /** Duplicates the clip, placing the copy immediately after the original.
            Returns the new clip's index in `track->getClips()`, or -1 on failure. */
        int duplicateClip (int trackIndex, int clipIndex);

        bool deleteClip (int trackIndex, int clipIndex);

        /** Loops a clip `numLoops` total passes (`numLoops` <= 1 disables).
            Uses the engine's native looping when the source carries loop info,
            otherwise repeats the clip butt-joined on the timeline. */
        bool setClipLoop (int trackIndex, int clipIndex, int numLoops);

        bool setClipMuted (int trackIndex, int clipIndex, bool shouldMute);

        // --- Fades + crossfades (FR-ED-3) ---
        /** Sets the fade-in / fade-out length in seconds (clamped to the clip
            length by the engine). */
        bool setClipFadeIn (int trackIndex, int clipIndex, double seconds);
        bool setClipFadeOut (int trackIndex, int clipIndex, double seconds);
        double getClipFadeIn (int trackIndex, int clipIndex) const;
        double getClipFadeOut (int trackIndex, int clipIndex) const;

        /** Equal-power crossfade between `clipIndex` and the nearest clip to its
            right on the same track: the left clip is extended to overlap the
            right by `seconds` and complementary convex fades are applied. Returns
            false when there is no right neighbour or either clip is not audio. */
        bool crossfadeClipWithNext (int trackIndex, int clipIndex, double seconds);

        // --- Comping (FR-ED-4) ---
        /** Non-destructively assembles a master take ("comp") on a **new track**
            from `takeClipIndices` on `sourceTrackIndex`. The comp is split into
            `chosenTakes.size()` butt-joined regions by `boundariesSeconds`
            (ascending internal split points, `size()` == `chosenTakes.size() - 1`);
            region `i` plays take `chosenTakes[i]`. Each segment is a trimmed copy
            that references the take's original file — the takes and their files
            are never modified. Returns the comp track index, or -1 on failure. */
        int compTakes (int sourceTrackIndex,
                       const juce::Array<int>& takeClipIndices,
                       const juce::Array<double>& boundariesSeconds,
                       const juce::Array<int>& chosenTakes);

        /** Number of takes stored on a clip (native engine takes; 0 when none). */
        int getClipTakeCount (int trackIndex, int clipIndex) const;
        /** Human-readable take descriptions, or empty when the clip has none. */
        juce::StringArray getClipTakeDescriptions (int trackIndex, int clipIndex) const;

        // --- Time-stretch (FR-ED-5) ---
        /** Offline time-stretches the clip's visible region to `targetSeconds`
            using the pinned free library, writes the result as a new 24-bit WAV
            next to the session and replaces the clip with one referencing it (the
            original source file is preserved). `semitones` adds an independent
            pitch shift (0 = none). Message-thread only; the render blocks the
            caller (like `normaliseTake`). Returns false with getLastError() set
            on failure. */
        bool stretchClipToDuration (int trackIndex, int clipIndex, double targetSeconds,
                                    double semitones = 0.0);

        // --- Undo / redo (FR-ED-6) ---
        bool undo();
        bool redo();
        bool canUndo() const;
        bool canRedo() const;
        juce::String getUndoDescription() const;
        juce::String getRedoDescription() const;

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
        // Plugin hosting (FR-MIX-4/6, Epic 3)
        //
        // Only *hosted* (external) plugins are exposed to the UI: the default
        // volume/pan and level-meter plugins the engine adds to every track stay
        // out of the list so they can neither be removed nor shown as "plugins".
        // Plugin state serialises with the project because each plugin lives in
        // the Edit's ValueTree.
        struct PluginInfo
        {
            juce::String name;
            juce::String format;      ///< e.g. "VST3", "AudioUnit", "LV2".
            bool missing = false;     ///< Plugin file uninstalled / failed to load.
        };

        /** Number of hosted (external) plugins on a track. */
        int getNumPlugins (int trackIndex) const;
        PluginInfo getPluginInfo (int trackIndex, int pluginIndex) const;

        /** Inserts a hosted plugin onto a track's plugin chain. Appends when
            `pluginIndex` < 0. Persists the session. Returns false with
            getLastError() set when the plugin cannot be created. */
        bool insertPlugin (int trackIndex, const juce::PluginDescription&, int pluginIndex = -1);

        /** Removes the hosted plugin at `pluginIndex` (its editor window closes
            with it). Persists the session. */
        bool removePlugin (int trackIndex, int pluginIndex);

        /** Shows (or brings to front) a hosted plugin's editor window. Message
            thread only; the window is created by StudioUIBehaviour. */
        bool showPluginEditor (int trackIndex, int pluginIndex);

        //==============================================================================
        // Plugin preset management (FR-MIX-6)
        //
        // User presets store the hosted plugin's raw JUCE state
        // (`getStateInformation`/`setStateInformation`) under the app preset
        // folder, one subfolder per plugin. Message-thread only; no audio-thread
        // work.

        /** Stable key for the plugin's user-preset folder, or empty when the
            plugin does not exist. */
        juce::String getPluginPresetKey (int trackIndex, int pluginIndex) const;

        /** Saves the plugin's current state as a named user preset. */
        bool savePluginPreset (int trackIndex, int pluginIndex, const juce::String& name);

        /** Names of the user presets stored for the plugin (sorted). */
        juce::StringArray listPluginPresets (int trackIndex, int pluginIndex) const;

        /** Loads a named user preset into the plugin: applies the JUCE state,
            flushes it into the Edit so save/reopen keeps it, and persists. */
        bool loadPluginPreset (int trackIndex, int pluginIndex, const juce::String& name);

        /** Overrides the preset root directory (tests / future Settings UI). */
        void setPresetDirectory (const juce::File& rootDirectory);
        juce::File getPresetDirectory() const;

        //==============================================================================
        // Routing: output assignment + submix folders + aux sends (FR-MIX-2, Epic 3)
        //
        // `trackIndex` is an engine audio-track index (as returned by
        // `getNumAudioTracks()`); cue-return tracks are audio tracks too and are
        // included in that ordering.

        // --- Per-track output assignment (FR-MIX-4 substrate for cue mixes) ---
        /** Hardware output device IDs available on the open device (empty when
            no device is open). Used to route a track/return to a specific pair. */
        juce::StringArray getAvailableOutputDeviceIDs() const;
        /** Human-readable labels for the IDs above, in the same order. */
        juce::StringArray getAvailableOutputDeviceNames() const;

        bool setTrackOutputToDevice (int trackIndex, const juce::String& deviceID);
        /** Clears the track's output back to the default audio out. */
        bool setTrackOutputToDefault (int trackIndex);
        juce::String getTrackOutputDevice (int trackIndex) const;
        /** True when the track (or the submix it belongs to) plays to a real
            hardware device rather than the default output. */
        bool trackHasDedicatedOutput (int trackIndex) const;

        // --- Submix folders (track -> bus/group -> master, FR-MIX-2) ---
        /** Creates a submix folder at the end of the track list. Returns its
            index among submix folders, or -1 on failure. */
        int createSubmixFolder (const juce::String& name);
        int getNumSubmixFolders() const;
        juce::String getSubmixFolderName (int folderIndex) const;
        /** Moves an audio track into the given submix folder. */
        bool addTrackToSubmix (int trackIndex, int folderIndex);
        /** Moves a track out of its submix folder (back to the top level). */
        bool removeTrackFromSubmix (int trackIndex);
        /** Index of the submix folder containing `trackIndex`, or -1. */
        int getTrackSubmixFolder (int trackIndex) const;

        // --- Software cue mixes (FR-MON-3 / FR-MON-4 [hard], Epic 3) ---
        struct CueMixInfo
        {
            int index = -1;                 ///< Positional cue index (0-based).
            juce::String name;
            int busNumber = -1;             ///< Stable aux bus id.
            int returnTrackIndex = -1;      ///< Engine audio-track index of the return.
            juce::String outputDeviceID;    ///< Assigned hardware output device.
            bool hasDedicatedOutput = false;///< Routed to a device other than the main out.
        };

        int getNumCueMixes() const;
        /** Creates a cue: an aux-return bus track fed by per-track sends. The
            return is routed to the next free hardware output pair when one is
            available (otherwise it falls back to the main output — flagged in
            the returned info). Returns the cue index, or -1 on failure. */
        int createCueMix (const juce::String& name);
        bool removeCueMix (int cueIndex);
        CueMixInfo getCueMix (int cueIndex) const;
        bool setCueMixName (int cueIndex, const juce::String& name);
        bool setCueMixOutputDevice (int cueIndex, const juce::String& deviceID);

        /** True when `trackIndex` is a software cue's return/bus track. */
        bool isCueReturnTrack (int trackIndex) const;

        /** The cue send from `trackIndex` to `cueIndex`. Enabling creates the
            aux-send point on first use. */
        bool isCueSendEnabled (int trackIndex, int cueIndex) const;
        bool setCueSendEnabled (int trackIndex, int cueIndex, bool shouldEnable);
        /** Send level in dB (-100 = effectively off). Returns -100 when unset. */
        float getCueSendLevelDb (int trackIndex, int cueIndex) const;
        bool setCueSendLevelDb (int trackIndex, int cueIndex, float db);

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

        /** Sets the count-in used for the next recording pass.

            Tracktion keeps the count-in in the user-global `Settings.xml`
            (`SettingID::countInMode`), NOT on the `Edit`, so by itself it would
            leak across sessions and is never saved with the project. To make it
            a genuine per-session setting, Session stores the value on the Edit's
            state tree (`rrsCountInMode`), re-applies it to the engine whenever it
            changes and again in `record()` right before the transport rolls, and
            restores it in `createOrOpenEdit()`. The count-in is therefore
            honoured on record start regardless of the user-global value. */
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

        /** Applies the user's monitoring preference to every input device.
            Returns false when no input device is available to act on. */
        bool applyMonitoringToDevices (bool shouldMonitor);

        void ensureMeterAttached();
        void detachMeter();
        void writeInterruptionMarker();
        void clearInterruptionMarker();
        juce::Array<juce::File> findReferencedRecordings() const;

        // --- Clip-editing helpers (Epic 4) ---
        /** The clip at `clipIndex` in `track->getClips()`, or nullptr. */
        tracktion::Clip* clipAt (int trackIndex, int clipIndex) const;
        /** The same, cast to AudioClipBase (fades/loops), or nullptr. */
        tracktion::AudioClipBase* audioClipAt (int trackIndex, int clipIndex) const;
        /** Fills `info` (minus index fields) from a live clip. */
        bool fillClipInfo (tracktion::Clip&, ClipInfo&) const;
        /** Rebuilds the playback graph and persists the edit after a mutation. */
        void afterClipEdit();

        // Record-pass clip muting (owner request): a new take must not play the
        // previous take back on the same track, so the armed record tracks'
        // existing clips are muted for the duration of the pass and restored on
        // stop/pause/close. Other (backing/minus) tracks are untouched.
        void beginRecordPassMutes();
        void endRecordPassMutes();

        /** Runs `write` (a persist) with any transient record-pass clip mutes
            lifted, then reinstates them. This guarantees a pass mute is never
            serialised into the edit or the autosave `.tmp_` file: a crash
            mid-pass would otherwise recover an edit whose takes are silently
            muted, and there is no clip-unmute UI to fix it. Every persist that
            can run during a pass (save, saveAs, autosave saveTempVersion) goes
            through this. */
        template <typename WriteFn>
        void withRecordPassMutesLifted (WriteFn&& write)
        {
            const auto resumeMutes = recordPassActive;

            if (resumeMutes)
                endRecordPassMutes();

            write();

            if (resumeMutes)
                beginRecordPassMutes();
        }

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

        /** User plugin presets (FR-MIX-6); root overridable for tests. */
        PluginPresets presets;

        InputLevels inputLevels;
        int autosaveIntervalSeconds = 30;
        double lastAutosaveMs = 0.0;

        /** Per-session count-in (FR-REC-10). Source of truth for the UI and for
            `record()`; mirrored onto the engine at apply time (see
            `setCountInMode`). */
        tracktion::Edit::CountIn countInMode = tracktion::Edit::CountIn::none;

        /** Region-selection bounds (FR-EXP-2), in seconds. Empty when
            `selectionEndSeconds <= selectionStartSeconds`. Transient UI state. */
        double selectionStartSeconds = 0.0;
        double selectionEndSeconds = 0.0;

        /** Master fader value (dB) and app-level mute. Tracktion has no dedicated
            master mute, so muting is applied by overriding the master volume
            plugin's gain while the fader value is remembered here (FR-MIX-1).

            Both are persisted *independently* on the Edit's state tree: the
            plugin only ever stores the effective (possibly muted-to-silence)
            gain, so reading it back would lose the user's real fader value if
            the session was saved while muted. The explicit properties are the
            source of truth and survive save/open (FR-MIX-1). */
        float masterGainDb = 0.0f;
        bool masterMuted = false;
        void applyMasterGain();
        void storeMasterState();
        bool meterAttached = false;
        bool inputsConfigured = false;
        /** User's monitoring preference. Unlike the live device state, this
            survives reconfiguring the input tracks (reconfigure/add-track), so
            `configureTracks()` never silently re-enables monitoring the user
            turned off. Defaults to on (Epic 1 auto-monitor). */
        bool monitoringEnabled = true;
        juce::WeakReference<tracktion::InputDeviceInstance> meterInstance;

        bool lastPlaying = false, lastRecording = false, lastArmed = false;

        /** Clips on the armed record tracks that this pass muted. Only clips
            that were *unmuted* when the pass began are collected, so ending the
            pass simply unmutes them again. The ref-counted pointer keeps the
            clip alive until the pass ends. */
        struct RecordPassClip
        {
            tracktion::Clip::Ptr clip;
        };

        std::vector<RecordPassClip> recordPassClips;
        bool recordPassActive = false;

        juce::String lastError;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
    };
}
