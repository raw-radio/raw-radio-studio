// raw-radio-studio — session model (Epic 1 walking skeleton + Epic 2 multitrack).

#include "Session.h"

#include "AppPaths.h"
#include "AudioImport.h"
#include "InputRouting.h"
#include "PluginSelection.h"
#include "TimeStretch.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <set>

namespace rrs
{
    namespace te = tracktion;

    namespace
    {
        // Per-track state properties. Stored on the Track's ValueTree so the
        // input mapping survives save/open in the native project format.
        const juce::Identifier idInputTrack   { "rrsInputTrack" };
        const juce::Identifier idFirstChannel { "rrsInputFirstChannel" };
        const juce::Identifier idNumChannels  { "rrsInputNumChannels" };
        const juce::Identifier idLayout       { "rrsInputLayout" };
        const juce::Identifier idInputGainDb  { "rrsInputGainDb" };

        // Per-session count-in. Tracktion stores the count-in in the user-global
        // Settings.xml, so the app keeps its own copy on the Edit state tree to
        // make it survive save/open and to re-apply it at record start.
        const juce::Identifier idCountInMode  { "rrsCountInMode" };

        // Fallback clip looping (clips whose source carries no loop metadata —
        // the engine can't loop them natively, so `setClipLoop` butt-joins
        // copies on the timeline). These mark the fallback loop so it can be
        // turned off again deterministically and survive save/open:
        //   * idLoopCount on the *looped original* — total passes (> 1).
        //   * idLoopSource on each *generated copy* — the item ID of that
        //     original, so the copies belonging to it can be found and removed.
        const juce::Identifier idLoopCount  { "rrsLoopCount" };
        const juce::Identifier idLoopSource { "rrsLoopSource" };

        // Record trim range: enough to rescue a quiet mic without absurd boosts.
        constexpr float minInputGainDb = -24.0f;
        constexpr float maxInputGainDb =  24.0f;

        // Smallest timeline ruler length (seconds) so an empty edit still has a
        // usable, clickable strip and no division by zero.
        constexpr double minimumTimelineSeconds = 10.0;

        /** Peak sample magnitude of an audio file, using the engine's read
            formats, or 0 when it cannot be read.

            KNOWN LIMITATION: this scans the whole file synchronously on the
            calling (message) thread, so normalising a long take briefly blocks
            the UI. A future revision should move the scan onto a background
            thread with a progress/locked state. Never called on the audio
            thread. */
        float readFilePeak (te::Engine& engine, const juce::File& file)
        {
            if (! file.existsAsFile())
                return 0.0f;

            std::unique_ptr<juce::AudioFormatReader> reader (
                engine.getAudioFileFormatManager().readFormatManager.createReaderFor (file));

            if (reader == nullptr || reader->lengthInSamples <= 0)
                return 0.0f;

            constexpr int blockSize = 8192;
            const auto numChannels = juce::jmax (1, (int) reader->numChannels);
            juce::AudioBuffer<float> buffer (numChannels, blockSize);

            float peak = 0.0f;
            const auto total = reader->lengthInSamples;

            for (juce::int64 pos = 0; pos < total; pos += blockSize)
            {
                const auto n = (int) juce::jmin ((juce::int64) blockSize, total - pos);

                if (! reader->read (&buffer, 0, n, pos, true, true))
                    break;

                peak = juce::jmax (peak, buffer.getMagnitude (0, n));
            }

            return peak;
        }

        /** The most recent wave clip on a track, chosen by timeline position so
            the result is deterministic — Tracktion's `getClips()` order is not
            guaranteed to be chronological. A tie on the start time is broken by
            the source file name so repeated calls always pick the same take. */
        te::WaveAudioClip* latestWaveClip (te::AudioTrack& track)
        {
            te::WaveAudioClip* latest = nullptr;

            for (auto* clip : track.getClips())
            {
                auto* wave = dynamic_cast<te::WaveAudioClip*> (clip);

                if (wave == nullptr)
                    continue;

                if (latest == nullptr)
                {
                    latest = wave;
                    continue;
                }

                const auto candidateStart = wave->getPosition().getStart();
                const auto latestStart    = latest->getPosition().getStart();

                if (candidateStart > latestStart
                    || (candidateStart == latestStart
                        && wave->getOriginalFile().getFileName()
                               > latest->getOriginalFile().getFileName()))
                    latest = wave;
            }

            return latest;
        }

        // Master fader state. Stored on the Edit's root ValueTree so the user's
        // chosen fader gain and the mute state survive save/open independently of
        // the master volume plugin, whose gain bakes the effective (muted) value.
        const juce::Identifier idMasterGain  { "rrsMasterGainDb" };
        const juce::Identifier idMasterMuted { "rrsMasterMuted" };

        // Software cue-mix markers, stored on each cue's return/bus track so the
        // cue set (names, aux-bus numbers) survives save/open with the project.
        const juce::Identifier idCueReturn { "rrsCueReturn" };
        const juce::Identifier idCueBus    { "rrsCueBus" };
        const juce::Identifier idCueName   { "rrsCueName" };
    }

    //==============================================================================
    void InputLevels::reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            peak[ch].store (0.0f, std::memory_order_relaxed);
            rms[ch].store (0.0f, std::memory_order_relaxed);
        }
    }

    void InputLevels::push (const choc::buffer::ChannelArrayView<float>& buffer) noexcept
    {
        const auto numChannels = (int) buffer.getNumChannels();
        const auto numFrames   = (int) buffer.getNumFrames();

        if (numChannels <= 0 || numFrames <= 0)
            return;

        for (int ch = 0; ch < 2; ++ch)
        {
            // Mono sources feed both meters.
            const auto sourceChannel = (choc::buffer::ChannelCount) juce::jmin (ch, numChannels - 1);
            const auto iterator = buffer.getIterator (sourceChannel);

            const float* samples = iterator.sample;
            const auto stride = (size_t) iterator.stride;

            float peakValue = 0.0f;
            double sumSquares = 0.0;

            for (int i = 0; i < numFrames; ++i)
            {
                const float s = samples[(size_t) i * stride];
                const float a = std::abs (s);

                if (a > peakValue)
                    peakValue = a;

                sumSquares += (double) s * (double) s;
            }

            peak[ch].store (peakValue, std::memory_order_relaxed);
            rms[ch].store ((float) std::sqrt (sumSquares / (double) numFrames), std::memory_order_relaxed);
        }
    }

    //==============================================================================
    Session::Session (AudioEngine& engineRef)
        : audio (engineRef)
    {
    }

    Session::~Session()
    {
        stopTimer();
        detachMeter();
        detachMeters();
        endRecordPassMutes();

        if (edit != nullptr)
        {
            // Best-effort autosave so an accidental quit still leaves a recoverable
            // temp version (the lock file is removed, so this is not flagged unclean).
            // The EditFileOperations object must be destroyed *before* the Edit:
            // its destructor touches the Edit's engine/project manager.
            {
                te::EditFileOperations ops (*edit);
                ops.saveTempVersion (false);
            }

            edit.reset();
        }

        clearInterruptionMarker();
    }

    //==============================================================================
    bool Session::createNew (const juce::File& requestedFile)
    {
        // A new session must not inherit the previous session's region selection:
        // the bounds are transient UI state, so a stale [start, end] would
        // highlight/render a region that no longer exists (FR-EXP-2).
        clearSelection();

        auto file = requestedFile != juce::File() ? requestedFile : paths::defaultEditFile();

        if (! file.getParentDirectory().createDirectory())
        {
            lastError = "Could not create the sessions directory:\n" + file.getParentDirectory().getFullPathName();
            return false;
        }

        if (! createOrOpenEdit (file, false))
            return false;

        // Tracktion's `createEmptyEdit` gives a brand-new Edit a master default of
        // -3 dB (`Edit::Options::defaultMasterVolumedB`). RAW Radio sessions start
        // at unity so the mixer and the exported WAV are predictable (a fresh
        // session's render matches the engineer's monitoring with no hidden trim).
        // Reopened projects keep their stored master gain, restored in
        // `createOrOpenEdit`.
        setMasterGainDb (0.0f);

        writeInterruptionMarker();
        configureTracks();
        save();
        sendChangeMessage();
        return true;
    }

    bool Session::open (const juce::File& file)
    {
        // Drop the previous session's region selection (transient, not
        // serialised): otherwise the region highlight and Export-region would
        // use times from the session that was just replaced (FR-EXP-2).
        clearSelection();

        if (! file.existsAsFile())
        {
            lastError = "Session file does not exist:\n" + file.getFullPathName();
            return false;
        }

        if (! createOrOpenEdit (file, true))
            return false;

        writeInterruptionMarker();
        configureTracks();
        sendChangeMessage();
        return true;
    }

    // `loadIfExists == false` makes this a genuine "New": even if the chosen file
    // exists it is not loaded, an empty Edit is created (and then saved over it).
    // "Open" passes true so an existing session is loaded.
    bool Session::createOrOpenEdit (const juce::File& file, bool loadIfExists)
    {
        endRecordPassMutes();
        detachMeter();
        edit.reset();

        auto& engine = audio.engine();

        if (loadIfExists && file.existsAsFile())
            edit = te::loadEditFromFile (engine, file);
        else
            edit = te::createEmptyEdit (engine, file);

        if (edit == nullptr)
        {
            lastError = "Failed to open or create the session:\n" + file.getFullPathName();
            editFile = juce::File();
            return false;
        }

        editFile = file;
        edit->playInStopEnabled = true;
        inputsConfigured = false;

        // Restore the app-level master state. The real fader gain and the mute
        // state are read from their own Edit-state properties so they stay
        // independent of the master volume plugin (which stores -100 dB while
        // muted). Sessions written before those properties existed fall back to
        // the plugin gain with mute off.
        if (edit->state.hasProperty (idMasterGain) || edit->state.hasProperty (idMasterMuted))
        {
            masterGainDb = juce::jlimit (-100.0f, 12.0f,
                                         (float) (double) edit->state.getProperty (idMasterGain, 0.0));
            masterMuted  = (bool) edit->state.getProperty (idMasterMuted, false);
        }
        else
        {
            masterGainDb = 0.0f;

            if (auto volume = edit->getMasterVolumePlugin())
                masterGainDb = juce::jlimit (-100.0f, 12.0f, volume->getVolumeDb());

            masterMuted = false;
        }

        // Normalise the plugin to the restored state (no-op when already
        // consistent, so opening an unchanged session stays clean).
        applyMasterGain();

        // Count-in (FR-REC-10) is a per-session property. Restore it from the
        // Edit state and re-apply it to the engine, whose own copy lives in the
        // user-global Settings.xml. Sessions written before the property existed
        // seed from the engine's current value so behaviour is unchanged for
        // them, while every subsequent change is saved with the project.
        if (edit->state.hasProperty (idCountInMode))
            countInMode = static_cast<te::Edit::CountIn> (
                juce::jlimit (0, (int) te::Edit::CountIn::oneBeat,
                              (int) edit->state.getProperty (idCountInMode, 0)));
        else
            countInMode = edit->getCountInMode();

        edit->setCountInMode (countInMode);

        // Direct recorded takes into <sessionDir>/Recordings/.
        audio.behaviour().setRecordingsDirectory (paths::recordingsDirectoryFor (file));

        startTimerHz (10);
        lastAutosaveMs = juce::Time::getMillisecondCounterHiRes();
        refreshMetersIfNeeded();
        clearLastError();
        return true;
    }

    juce::String Session::getSessionName() const
    {
        return editFile != juce::File() ? editFile.getFileNameWithoutExtension()
                                        : juce::String ("No session");
    }

    //==============================================================================
    tracktion::AudioTrack* Session::getTrack (int index) const
    {
        if (edit == nullptr)
            return nullptr;

        const auto tracks = te::getAudioTracks (*edit);

        if (! juce::isPositiveAndBelow (index, tracks.size()))
            return nullptr;

        return tracks[index];
    }

    tracktion::AudioTrack* Session::getTrack() const
    {
        return getTrack (0);
    }

    int Session::getNumAudioTracks() const
    {
        return edit != nullptr ? te::getAudioTracks (*edit).size() : 0;
    }

    juce::String Session::getTrackName (int index) const
    {
        if (auto* track = getTrack (index))
            return track->getName();

        return {};
    }

    bool Session::setTrackName (int index, const juce::String& name)
    {
        auto* track = getTrack (index);

        if (track == nullptr)
            return false;

        track->setName (name);
        sendChangeMessage();
        return true;
    }

    //==============================================================================
    bool Session::isInputTrack (const te::AudioTrack& track) const
    {
        return (bool) track.state.getProperty (idInputTrack, false);
    }

    juce::Array<int> Session::inputTrackIndices() const
    {
        juce::Array<int> indices;

        if (edit == nullptr)
            return indices;

        const auto tracks = te::getAudioTracks (*edit);

        for (int i = 0; i < tracks.size(); ++i)
            if (isInputTrack (*tracks[i]))
                indices.add (i);

        // Back-compat: an Epic 1 session (or one opened before this version)
        // has no markers; the first track is the record track.
        if (indices.isEmpty() && ! tracks.isEmpty())
            indices.add (0);

        return indices;
    }

    tracktion::WaveInputDevice* Session::resolveInputDeviceFor (const InputMapping& mapping) const
    {
        auto& dm = audio.deviceManager();
        const auto numDevices = dm.getNumWaveInDevices();

        if (numDevices <= 0)
            return nullptr;

        // Prefer the device that actually carries the requested hardware channel
        // (with mono grouping every channel is its own device; with the default
        // stereo-pair grouping both channels of a pair resolve to one device).
        for (int i = 0; i < numDevices; ++i)
            if (auto* device = dm.getWaveInDevice (i))
                if (device->getChannels().containsDeviceChannel (mapping.firstChannel))
                    return device;

        // Fallback: clamp to a real device index.
        return dm.getWaveInDevice (juce::jlimit (0, numDevices - 1, mapping.firstChannel));
    }

    InputMapping Session::readTrackMapping (int trackIndex) const
    {
        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return {};

        const auto indices = inputTrackIndices();
        const auto ordinal = juce::jmax (0, indices.indexOf (trackIndex));

        InputMapping mapping;
        mapping.firstChannel = (int) track->state.getProperty (idFirstChannel, ordinal);
        mapping.numChannels  = juce::jmax (1, (int) track->state.getProperty (idNumChannels, 1));

        const auto layoutText = track->state.getProperty (idLayout, ordinal == 0 ? "auto" : "mono").toString();
        mapping.layout = inputLayoutFromString (layoutText);

        // A track created outside addAudioTrack (e.g. a crash-recovered Epic 1
        // session) still maps to a real channel.
        mapping.firstChannel = juce::jmax (0, mapping.firstChannel);
        return mapping;
    }

    void Session::writeTrackMapping (te::AudioTrack& track, const InputMapping& mapping)
    {
        track.state.setProperty (idInputTrack, true, nullptr);
        track.state.setProperty (idFirstChannel, mapping.firstChannel, nullptr);
        track.state.setProperty (idNumChannels, juce::jmax (1, mapping.numChannels), nullptr);
        track.state.setProperty (idLayout, inputLayoutToString (mapping.layout), nullptr);
    }

    InputMapping Session::getTrackInputMapping (int trackIndex) const
    {
        return readTrackMapping (trackIndex);
    }

    int Session::getNumInputChannels() const
    {
        return audio.getNumActiveInputChannels();
    }

    bool Session::setTrackInputMapping (int trackIndex, const InputMapping& mapping)
    {
        auto* track = getTrack (trackIndex);

        if (edit == nullptr || track == nullptr)
        {
            lastError = "No such track.";
            return false;
        }

        // Only record/input tracks may carry a hardware-input mapping. Without
        // this guard an imported/backing track could be turned into an input
        // track (`rrsInputTrack=true`) and bound to a hardware channel (default
        // 0), colliding with the real input track.
        if (! isInputTrack (*track))
        {
            lastError = "Only input tracks can be assigned a hardware input.";
            return false;
        }

        writeTrackMapping (*track, mapping);
        inputsConfigured = false;
        configureTracks();
        edit->restartPlayback();
        save();
        sendChangeMessage();
        return true;
    }

    //==============================================================================
    // Per-input-track record gain / trim (FR-REC-4)
    bool Session::setTrackInputGainDb (int trackIndex, float db, bool persist)
    {
        auto* track = getTrack (trackIndex);

        if (edit == nullptr || track == nullptr || ! isInputTrack (*track))
        {
            lastError = "Only input tracks have a record trim.";
            return false;
        }

        const auto clamped = juce::jlimit (minInputGainDb, maxInputGainDb, db);
        track->state.setProperty (idInputGainDb, (double) clamped, nullptr);

        // Apply to the live device now so monitoring and the next take both pick
        // it up without a reconfigure. The engine applies it on the audio thread
        // (plain gain multiply), so this is RT-safe.
        if (auto* waveIn = resolveInputDeviceFor (readTrackMapping (trackIndex)))
            waveIn->setInputGainDb (clamped);

        // Persisting rewrites the whole `.tracktionedit`. During a UI drag the
        // caller passes `persist=false` on every mouse-move and saves once on
        // mouse-up, so the live gain still tracks the pointer without writing the
        // file per frame.
        if (persist)
            save();

        sendChangeMessage();
        return true;
    }

    float Session::getTrackInputGainDb (int trackIndex) const
    {
        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return 0.0f;

        return juce::jlimit (minInputGainDb, maxInputGainDb,
                             (float) (double) track->state.getProperty (idInputGainDb, 0.0));
    }

    //==============================================================================
    // Take normalisation (peak-normalise a recorded clip, non-destructive).
    bool Session::normaliseTake (int trackIndex, float targetPeakDb, NormaliseResult* result)
    {
        auto* track = getTrack (trackIndex);

        if (edit == nullptr || track == nullptr)
        {
            lastError = "No such track.";
            return false;
        }

        // The most recent wave clip on the track is the take to normalise,
        // chosen by timeline position (deterministic) rather than clip-list order.
        auto* take = latestWaveClip (*track);

        if (take == nullptr)
        {
            lastError = "No recorded take to normalise on this track.";
            return false;
        }

        const auto peak = readFilePeak (edit->engine, take->getOriginalFile());

        if (peak <= 0.0f)
        {
            lastError = "Could not read the take's audio to normalise it.";
            return false;
        }

        const auto peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);
        const auto requestedGainDb = targetPeakDb - peakDb;

        // Tracktion clamps clip gain to [-100, +24] dB. A very quiet take (or a
        // target requiring more than +24 dB) is limited, so the take may fall
        // short of the requested target — report the achieved value instead of
        // assuming it landed on target.
        constexpr float minClipGainDb = -100.0f;
        constexpr float maxClipGainDb = 24.0f;
        const auto appliedGainDb = juce::jlimit (minClipGainDb, maxClipGainDb, requestedGainDb);

        take->setGainDB (appliedGainDb);

        if (result != nullptr)
        {
            result->appliedGainDb  = appliedGainDb;
            result->achievedPeakDb = peakDb + appliedGainDb;
            result->clamped        = std::abs (appliedGainDb - requestedGainDb) > 0.01f;
        }

        edit->restartPlayback();
        save();
        sendChangeMessage();
        clearLastError();
        return true;
    }

    bool Session::normaliseLatestTake (float targetPeakDb, NormaliseResult* result)
    {
        if (edit == nullptr)
            return false;

        const auto rows = te::getAudioTracks (*edit);
        const auto inputIndices = inputTrackIndices();

        auto hasWaveClip = [] (te::AudioTrack* track)
        {
            return latestWaveClip (*track) != nullptr;
        };

        // Prefer the tracked takes (input tracks), newest channel last.
        for (int i = inputIndices.size() - 1; i >= 0; --i)
            if (auto* track = rows[inputIndices[i]])
                if (hasWaveClip (track))
                    return normaliseTake (inputIndices[i], targetPeakDb, result);

        // Fall back to any track that carries a wave clip.
        for (int i = rows.size() - 1; i >= 0; --i)
            if (hasWaveClip (rows[i]))
                return normaliseTake (i, targetPeakDb, result);

        lastError = "No recorded take to normalise.";
        return false;
    }

    //==============================================================================
    bool Session::applyInputChannelConfiguration (int trackIndex, te::WaveInputDevice*& resolvedOut)
    {
        auto mapping = readTrackMapping (trackIndex);
        auto* waveIn = resolveInputDeviceFor (mapping);

        if (waveIn == nullptr)
            return false;

        // The channel indices in the mapping are *absolute hardware channels*,
        // so the configuration must be built against the total number of active
        // hardware inputs, not the wave device's own (possibly mono) channel
        // count. Tracktion passes the full hardware buffer to every wave device,
        // so an absolute index is valid even for a single-channel device.
        const auto hardwareChannels = juce::jmax (1, audio.getNumActiveInputChannels());
        waveIn->setChannelConfiguration (inputChannelConfigurationForMapping (mapping, hardwareChannels));

        // Honour the user's monitoring preference instead of forcing monitoring
        // on: `configureTracks()` runs on every reconfigure/add-track and used to
        // silently re-enable monitoring the engineer had turned off.
        waveIn->setMonitorMode (monitoringEnabled ? te::InputDevice::MonitorMode::on
                                                  : te::InputDevice::MonitorMode::off);

        // Record trim (FR-REC-4): applied to the input buffer before monitoring
        // *and* before the take is written to disk, so what is heard is what is
        // recorded. The engine applies it on the audio thread (a plain gain
        // multiply); re-applied here on every (re)configure so it survives
        // save/open. When two input tracks share one wave device the last one
        // wins — in practice mono multitrack splits devices per channel, so the
        // mapping is 1:1.
        waveIn->setInputGainDb (getTrackInputGainDb (trackIndex));

        resolvedOut = waveIn;
        return true;
    }

    // Splits the wave-device grouping to one channel per device when two input
    // tracks would otherwise share a device and the interface has enough inputs.
    // This is what lets a 4-in interface record four independent mono tracks;
    // a single input track (Epic 1) is never regrouped, so stereo capture keeps
    // working.
    static bool maybeSplitInputGrouping (rrs::AudioEngine& audio,
                                         const juce::Array<int>& inputIndices,
                                         const std::function<tracktion::WaveInputDevice* (int)>& resolve)
    {
        if (inputIndices.size() < 2)
            return false;

        auto& dm = audio.deviceManager();
        const auto devices = dm.getNumWaveInDevices();
        const auto totalChannels = audio.getNumActiveInputChannels();

        if (totalChannels <= devices)
            return false;

        std::set<tracktion::InputDevice*> used;
        bool duplicate = false;

        for (auto index : inputIndices)
            if (auto* device = resolve (index))
                if (! used.insert (device).second)
                    duplicate = true;

        if (! duplicate)
            return false;

        dm.setAllWaveInputsToNumChannels (1);
        return true;
    }

    bool Session::configureTracks()
    {
        if (edit == nullptr)
            return false;

        refreshMetersIfNeeded();

        // Epic 1: a brand-new session starts with one input track. Note that
        // `createEmptyEdit` already adds a default track, so we mark/rename the
        // existing first track rather than only handling the zero-track case.
        {
            auto tracks = te::getAudioTracks (*edit);

            if (tracks.isEmpty())
            {
                auto track = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr, true);

                if (track == nullptr)
                {
                    lastError = "Could not create the audio track.";
                    return false;
                }

                track->setName ("Input 1");
                writeTrackMapping (*track, InputMapping { 0, 1, InputLayout::Auto });
            }
            else
            {
                bool anyMarked = false;

                for (auto* track : tracks)
                    if (isInputTrack (*track))
                        anyMarked = true;

                // An opened Epic 1 session (or a default edit) has no markers:
                // track 0 is the record track.
                if (! anyMarked)
                {
                    tracks[0]->setName ("Input 1");
                    writeTrackMapping (*tracks[0], InputMapping { 0, 1, InputLayout::Auto });
                }
            }
        }

        const auto indices = inputTrackIndices();

        // Splitting the device grouping rebuilds the wave-device list
        // asynchronously; stop this pass and let timerCallback retry once the
        // new devices exist.
        if (maybeSplitInputGrouping (audio, indices,
                                     [this] (int index) { return resolveInputDeviceFor (readTrackMapping (index)); }))
            return false;

        bool anyAssigned = false;

        for (auto index : indices)
        {
            auto* track = getTrack (index);

            if (track == nullptr)
                continue;

            auto mapping = readTrackMapping (index);
            auto* waveIn = resolveInputDeviceFor (mapping);

            if (waveIn == nullptr)
                continue;

            waveIn->setEnabled (true);

            // `setEnabled` can (asynchronously) rebuild the wave-device list,
            // destroying this device object; re-resolve before using it.
            waveIn = resolveInputDeviceFor (mapping);

            if (waveIn == nullptr)
                continue;

            applyInputChannelConfiguration (index, waveIn);

            if (waveIn == nullptr)
                continue;

            for (auto* instance : edit->getAllInputDevices())
                if (&instance->getInputDevice() == waveIn)
                    if (auto result = instance->setTarget (track->itemID, true, &edit->getUndoManager(), 0);
                        ! result)
                        lastError = result.error();

            anyAssigned = true;
        }

        // An empty device list (headless tests / no interface) is not an error:
        // inputsConfigured stays false and timerCallback retries.
        if (! anyAssigned)
            return false;

        edit->getTransport().ensureContextAllocated();
        edit->restartPlayback();

        inputsConfigured = true;
        ensureMeterAttached();
        clearLastError();
        return true;
    }

    int Session::addAudioTrack (const juce::String& name)
    {
        if (edit == nullptr)
        {
            lastError = "Open a session before adding tracks.";
            return -1;
        }

        const auto ordinals = inputTrackIndices();

        // Going multitrack: a lone Epic 1 record track maps the whole device
        // (Auto -> stereo); once a second track is added it must become an
        // explicit single hardware channel so the tracks do not overlap.
        if (ordinals.size() >= 1)
        {
            for (auto existing : inputTrackIndices())
            {
                auto mapping = readTrackMapping (existing);

                if (mapping.layout == InputLayout::Auto)
                {
                    mapping.layout = InputLayout::Mono;
                    writeTrackMapping (*getTrack (existing), mapping);
                }
            }
        }

        // Assign the lowest hardware channel not already used by an input track.
        // The old `inputTrackIndices().size()` collided after removing a non-last
        // track (e.g. tracks on channels 0 and 2, remove the middle one, add ->
        // the new track reused channel 2).
        std::set<int> usedChannels;

        for (auto index : inputTrackIndices())
            usedChannels.insert (readTrackMapping (index).firstChannel);

        int nextChannel = 0;

        while (usedChannels.count (nextChannel) > 0)
            ++nextChannel;

        const auto ordinal = ordinals.size();

        auto track = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr, true);

        if (track == nullptr)
        {
            lastError = "Could not create the audio track.";
            return -1;
        }

        InputMapping mapping;
        mapping.firstChannel = nextChannel;
        mapping.numChannels = 1;
        mapping.layout = ordinal == 0 ? InputLayout::Auto : InputLayout::Mono;

        writeTrackMapping (*track, mapping);
        track->setName (name.isNotEmpty() ? name : ("Input " + juce::String (ordinal + 1)));

        inputsConfigured = false;
        configureTracks();
        save();
        sendChangeMessage();

        return getNumAudioTracks() - 1;
    }

    bool Session::removeAudioTrack (int index)
    {
        if (edit == nullptr)
            return false;

        auto* track = getTrack (index);

        if (track == nullptr || getNumAudioTracks() <= 1)
        {
            lastError = "Cannot remove the last track.";
            return false;
        }

        edit->deleteTrack (track);
        inputsConfigured = false;
        configureTracks();
        save();
        sendChangeMessage();
        return true;
    }

    //==============================================================================
    bool Session::importAudioFile (const juce::File& sourceFile)
    {
        if (edit == nullptr)
        {
            lastError = "Open a session before importing.";
            return false;
        }

        const auto result = AudioImport::import (*edit, sourceFile);

        if (! result.success)
        {
            lastError = result.error;
            return false;
        }

        edit->restartPlayback();
        const bool saved = save();
        sendChangeMessage();

        if (! saved)
            return false;

        clearLastError();
        return true;
    }

    //==============================================================================
    void Session::reconfigureInputs()
    {
        inputsConfigured = false;

        if (configureTracks())
            save();

        sendChangeMessage();
    }

    tracktion::WaveInputDevice* Session::getSelectedWaveInputDevice() const
    {
        auto& dm = audio.deviceManager();

        if (auto* device = dm.getDefaultWaveInDevice())
            return device;

        if (dm.getNumWaveInDevices() > 0)
            return dm.getWaveInDevice (0);

        return nullptr;
    }

    //==============================================================================
    bool Session::setTrackArmed (int trackIndex, bool shouldBeArmed)
    {
        auto* track = getTrack (trackIndex);

        if (edit == nullptr || track == nullptr)
        {
            lastError = "No track to arm.";
            return false;
        }

        // Imported/backing tracks are playback-only: arming them would try to
        // bind them to a hardware input (and used to mutate them into input
        // tracks). Reject clearly instead.
        if (! isInputTrack (*track))
        {
            lastError = "Only input tracks can be armed for recording.";
            return false;
        }

        bool found = false;

        for (auto* instance : edit->getAllInputDevices())
        {
            if (te::isOnTargetTrack (*instance, *track, 0))
            {
                instance->setRecordingEnabled (track->itemID, shouldBeArmed);
                found = true;
            }
        }

        if (! found)
        {
            lastError = "The track has no input device assigned.";
            return false;
        }

        sendChangeMessage();
        return true;
    }

    bool Session::isTrackArmed (int trackIndex) const
    {
        auto* track = getTrack (trackIndex);

        if (edit == nullptr || track == nullptr)
            return false;

        for (auto* instance : edit->getAllInputDevices())
            if (te::isOnTargetTrack (*instance, *track, 0))
                if (instance->isRecordingEnabled (track->itemID))
                    return true;

        return false;
    }

    bool Session::isAnyTrackArmed() const
    {
        for (auto index : inputTrackIndices())
            if (isTrackArmed (index))
                return true;

        return false;
    }

    bool Session::applyMonitoringToDevices (bool shouldMonitor)
    {
        const auto mode = shouldMonitor ? te::InputDevice::MonitorMode::on
                                        : te::InputDevice::MonitorMode::off;

        std::set<te::InputDevice*> devices;

        for (auto index : inputTrackIndices())
            if (auto* device = resolveInputDeviceFor (readTrackMapping (index)))
                devices.insert (device);

        if (devices.empty())
            if (auto* fallback = getSelectedWaveInputDevice())
                devices.insert (fallback);

        for (auto* device : devices)
            device->setMonitorMode (mode);

        return ! devices.empty();
    }

    bool Session::setMonitoringEnabled (bool shouldMonitor)
    {
        // Remember the preference so a later reconfigure/add-track respects it.
        monitoringEnabled = shouldMonitor;

        const bool applied = applyMonitoringToDevices (shouldMonitor);
        sendChangeMessage();
        return applied;
    }

    bool Session::isMonitoringEnabled() const
    {
        for (auto index : inputTrackIndices())
            if (auto* device = resolveInputDeviceFor (readTrackMapping (index)))
                if (device->getMonitorMode() != te::InputDevice::MonitorMode::off)
                    return true;

        if (auto* device = getSelectedWaveInputDevice())
            return device->getMonitorMode() != te::InputDevice::MonitorMode::off;

        return false;
    }

    void Session::play()
    {
        if (edit != nullptr)
            edit->getTransport().play (false);
    }

    void Session::pause()
    {
        if (edit != nullptr)
        {
            endRecordPassMutes();
            edit->getTransport().stop (false, false);
        }
    }

    void Session::stop()
    {
        if (edit == nullptr)
            return;

        endRecordPassMutes();
        edit->getTransport().stop (false, false);

        // Stop holds the playhead where it stopped (owner request): it does NOT
        // rewind. Returning to 0 is the job of the dedicated Go-to-start button
        // (`goToStart()`), so a take can be reviewed from its stop position and
        // the transport restarted from the top explicitly.
    }

    void Session::goToStart()
    {
        if (edit != nullptr)
            edit->getTransport().setPosition (te::TimePosition());
    }

    //==============================================================================
    // Timeline position / seek (Epic 4 groundwork: a clickable timeline).
    double Session::getPositionSeconds() const
    {
        return edit != nullptr ? edit->getTransport().getPosition().inSeconds() : 0.0;
    }

    void Session::setPositionSeconds (double seconds)
    {
        if (edit == nullptr)
            return;

        edit->getTransport().setPosition (te::TimePosition::fromSeconds (juce::jmax (0.0, seconds)));
    }

    //==============================================================================
    // Offline-render transport preservation.
    //
    // Root cause of the "audio dies after an export" bug: `EditRenderer::render`
    // (used by WavExport and by RenderQueue/stems) constructs
    // `Edit::ScopedRenderStatus (edit, false)`. That constructor calls
    // `transport.freePlaybackContext()`, and because `shouldReallocateOnDestruction`
    // is false it never rebuilds the context afterwards. The playback context owns
    // the live `EditPlaybackContext` (output graph + input-device instances), so
    // freeing it silences monitoring and playback and nothing reallocates it until
    // the app is restarted. WavExport runs its render on a background thread, so it
    // cannot safely reallocate on the render thread; the app instead captures this
    // snapshot on the message thread before the render and restores it from the
    // (message-thread) completion callback.
    Session::OfflineRenderTransportState Session::captureTransportForOfflineRender() const
    {
        OfflineRenderTransportState state;

        if (edit == nullptr)
            return state;

        const auto& transport = edit->getTransport();
        state.edit = edit.get();
        state.contextWasAllocated = transport.isPlayContextActive();
        state.wasPlaying = transport.isPlaying();
        state.wasRecording = transport.isRecording();
        state.positionSeconds = transport.getPosition().inSeconds();
        return state;
    }

    void Session::restoreTransportAfterOfflineRender (const OfflineRenderTransportState& state)
    {
        if (edit == nullptr || state.edit != edit.get())
            return;

        auto& transport = edit->getTransport();

        // Restore the playhead first so the rebuilt graph starts from where the
        // engineer left it, then rebuild the live playback context. Reallocating
        // the context also re-creates the input-device instances, restoring input
        // monitoring — the part that stayed dead until a restart before this fix.
        transport.setPosition (te::TimePosition::fromSeconds (juce::jmax (0.0, state.positionSeconds)));
        transport.ensureContextAllocated();

        // Resume what was running. `ensureContextAllocated` rebuilds the graph but
        // does not roll the transport, so the play/record flag has to be re-asserted
        // (it was cleared by `freePlaybackContext()` -> `clearPlayingFlags()`).
        if (state.wasRecording)
            transport.record (false);
        else if (state.wasPlaying)
            transport.play (false);

        edit->restartPlayback();
    }

    double Session::getTimelineLengthSeconds() const
    {
        if (edit == nullptr)
            return minimumTimelineSeconds;

        // Cover the material, the playhead (so seeking past the last clip stays
        // visible) and a small floor so an empty edit still has a ruler.
        return juce::jmax (minimumTimelineSeconds,
                           edit->getLength().inSeconds(),
                           getPositionSeconds());
    }

    //==============================================================================
    // Region selection (FR-EXP-2)
    void Session::setSelectionSeconds (double startSeconds, double endSeconds)
    {
        const auto a = juce::jmax (0.0, startSeconds);
        const auto b = juce::jmax (0.0, endSeconds);

        selectionStartSeconds = juce::jmin (a, b);
        selectionEndSeconds   = juce::jmax (a, b);

        if (selectionEndSeconds <= selectionStartSeconds)
            clearSelection();
    }

    void Session::clearSelection()
    {
        selectionStartSeconds = 0.0;
        selectionEndSeconds   = 0.0;
    }

    //==============================================================================
    // Arrangement clip editing (Epic 4 — FR-ED-1/2/3/6)
    namespace
    {
        /** Minimum clip length after a trim/split, so a clip can never collapse
            to zero samples (which the engine would reject or render oddly). */
        constexpr double minClipSeconds = 1.0e-3;
    }

    te::Clip* Session::clipAt (int trackIndex, int clipIndex) const
    {
        auto* track = getTrack (trackIndex);

        if (track == nullptr || clipIndex < 0)
            return nullptr;

        auto clips = track->getClips();

        if (clipIndex >= clips.size())
            return nullptr;

        return clips[clipIndex];
    }

    te::AudioClipBase* Session::audioClipAt (int trackIndex, int clipIndex) const
    {
        return dynamic_cast<te::AudioClipBase*> (clipAt (trackIndex, clipIndex));
    }

    void Session::afterClipEdit()
    {
        if (edit == nullptr)
            return;

        // A clip edit changes the graph; rebuild it, persist, and notify the UI.
        edit->restartPlayback();
        save();
        sendChangeMessage();
    }

    std::vector<Session::ClipInfo> Session::getClips (int trackIndex) const
    {
        std::vector<ClipInfo> infos;
        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return infos;

        const auto clips = track->getClips();
        infos.reserve ((size_t) clips.size());

        for (int i = 0; i < clips.size(); ++i)
        {
            ClipInfo info;
            info.trackIndex = trackIndex;
            info.clipIndex = i;

            if (fillClipInfo (*clips[i], info))
                infos.push_back (std::move (info));
        }

        std::sort (infos.begin(), infos.end(),
                   [] (const ClipInfo& a, const ClipInfo& b)
                   {
                       if (a.startSeconds < b.startSeconds)
                           return true;

                       if (b.startSeconds < a.startSeconds)
                           return false;

                       return a.clipIndex < b.clipIndex;
                   });

        return infos;
    }

    bool Session::fillClipInfo (te::Clip& clip, ClipInfo& info) const
    {
        const auto pos = clip.getPosition();

        info.name = clip.getName();
        info.startSeconds = pos.time.getStart().inSeconds();
        info.endSeconds = pos.time.getEnd().inSeconds();
        info.lengthSeconds = pos.time.getLength().inSeconds();
        info.offsetSeconds = pos.offset.inSeconds();
        // Native engine looping, or a fallback timeline loop: both must report
        // as looping so the UI toggle offers "disable" (and does not keep
        // accumulating copies).
        info.isLooping = clip.isLooping()
                         || clip.state.hasProperty (idLoopCount)
                         || clip.state.hasProperty (idLoopSource);
        info.isMuted = clip.isMuted();

        if (auto* audioClip = dynamic_cast<te::AudioClipBase*> (&clip))
        {
            info.isWave = true;
            info.fadeInSeconds = audioClip->getFadeIn().inSeconds();
            info.fadeOutSeconds = audioClip->getFadeOut().inSeconds();
        }

        return true;
    }

    bool Session::getClipInfo (int trackIndex, int clipIndex, ClipInfo& info) const
    {
        auto* clip = clipAt (trackIndex, clipIndex);

        if (clip == nullptr)
            return false;

        info = ClipInfo();
        info.trackIndex = trackIndex;
        info.clipIndex = clipIndex;
        return fillClipInfo (*clip, info);
    }

    bool Session::moveClip (int trackIndex, int clipIndex, double newStartSeconds)
    {
        auto* clip = clipAt (trackIndex, clipIndex);

        if (edit == nullptr || clip == nullptr)
            return false;

        edit->getUndoManager().beginNewTransaction ("Move clip");
        // A move keeps the clip's length and content: preserveSync=false leaves
        // the source offset untouched, keepLength=true moves both edges.
        clip->setStart (te::TimePosition::fromSeconds (juce::jmax (0.0, newStartSeconds)),
                        false, true);
        afterClipEdit();
        return true;
    }

    bool Session::trimClipStart (int trackIndex, int clipIndex, double newStartSeconds)
    {
        auto* clip = clipAt (trackIndex, clipIndex);

        if (edit == nullptr || clip == nullptr)
            return false;

        const auto pos = clip->getPosition();
        const auto start = pos.time.getStart().inSeconds();
        const auto end = pos.time.getEnd().inSeconds();
        const auto target = juce::jmax (0.0, newStartSeconds);

        if (target >= end - minClipSeconds || std::abs (target - start) < 1.0e-9)
            return false;

        edit->getUndoManager().beginNewTransaction ("Trim clip start");
        // preserveSync=true: the source content stays stationary in time, so the
        // edge reveals/hides audio instead of sliding it (offset follows the edge).
        clip->setStart (te::TimePosition::fromSeconds (target), true, false);
        afterClipEdit();
        return true;
    }

    bool Session::trimClipEnd (int trackIndex, int clipIndex, double newEndSeconds)
    {
        auto* clip = clipAt (trackIndex, clipIndex);

        if (edit == nullptr || clip == nullptr)
            return false;

        const auto pos = clip->getPosition();
        const auto start = pos.time.getStart().inSeconds();
        const auto end = pos.time.getEnd().inSeconds();
        const auto target = newEndSeconds;

        if (target <= start + minClipSeconds || std::abs (target - end) < 1.0e-9)
            return false;

        edit->getUndoManager().beginNewTransaction ("Trim clip end");
        // preserveSync=true keeps the source content stationary.
        clip->setEnd (te::TimePosition::fromSeconds (target), true);
        afterClipEdit();
        return true;
    }

    bool Session::splitClip (int trackIndex, int clipIndex, double timeSeconds)
    {
        auto* track = getTrack (trackIndex);
        te::Clip::Ptr original (clipAt (trackIndex, clipIndex));

        if (edit == nullptr || track == nullptr || original == nullptr)
            return false;

        const auto pos = original->getPosition();
        const auto start = pos.time.getStart().inSeconds();
        const auto end = pos.time.getEnd().inSeconds();

        if (timeSeconds <= start + minClipSeconds || timeSeconds >= end - minClipSeconds)
            return false;

        edit->getUndoManager().beginNewTransaction ("Split clip");

        // Insert a copy at the original position (it keeps the original's
        // settings), then trim the left clip's end and the copy's start so they
        // meet at the split point with the same source content.
        auto* copy = te::insertClipCopy (*track, te::ClipCopy::fromClip (*original)
                                                   .withNewItemID (*edit));

        if (copy == nullptr)
            return false;

        copy->setStart (te::TimePosition::fromSeconds (timeSeconds), true, false);
        original->setEnd (te::TimePosition::fromSeconds (timeSeconds), true);

        afterClipEdit();
        return true;
    }

    int Session::duplicateClip (int trackIndex, int clipIndex)
    {
        auto* track = getTrack (trackIndex);
        te::Clip::Ptr original (clipAt (trackIndex, clipIndex));

        if (edit == nullptr || track == nullptr || original == nullptr)
            return -1;

        const auto newStart = original->getPosition().time.getEnd();

        edit->getUndoManager().beginNewTransaction ("Duplicate clip");

        auto* copy = te::insertClipCopy (*track, te::ClipCopy::fromClip (*original)
                                                   .withNewItemID (*edit));

        if (copy == nullptr)
            return -1;

        // Place the copy immediately after the original (pure move, keep length).
        copy->setStart (newStart, false, true);

        afterClipEdit();

        const auto clips = track->getClips();

        for (int i = 0; i < clips.size(); ++i)
            if (clips[i] == copy)
                return i;

        return -1;
    }

    bool Session::deleteClip (int trackIndex, int clipIndex)
    {
        auto* clip = clipAt (trackIndex, clipIndex);

        if (edit == nullptr || clip == nullptr)
            return false;

        edit->getUndoManager().beginNewTransaction ("Delete clip");
        clip->removeFromParent();
        afterClipEdit();
        return true;
    }

    bool Session::setClipLoop (int trackIndex, int clipIndex, int numLoops)
    {
        auto* track = getTrack (trackIndex);
        te::Clip::Ptr original (clipAt (trackIndex, clipIndex));

        if (edit == nullptr || track == nullptr || original == nullptr)
            return false;

        if (auto* audioClip = dynamic_cast<te::AudioClipBase*> (original.get()))
        {
            // Native looping when the source carries loop information (beat
            // markers/tempo); otherwise fall through to timeline repetition.
            // `numLoops <= 1` disables the loop (the UI passes 0 to toggle off).
            if (audioClip->canLoop())
            {
                if (numLoops <= 1)
                {
                    if (! audioClip->isLooping())
                        return true; // Already off — no empty undo transaction.

                    edit->getUndoManager().beginNewTransaction ("Loop clip");
                    audioClip->disableLooping();
                }
                else
                {
                    edit->getUndoManager().beginNewTransaction ("Loop clip");
                    audioClip->setNumberOfLoops (numLoops);
                }

                afterClipEdit();
                return true;
            }
        }

        // No loop metadata: repeat the clip on the timeline `numLoops` times.
        // Find the loop's root first: the selected clip may itself be one of the
        // generated copies, in which case the operation belongs to its original.
        auto* root = original.get();

        if (root->state.hasProperty (idLoopSource))
        {
            const auto sourceId = root->state.getProperty (idLoopSource).toString();

            for (auto* candidate : track->getClips())
                if (candidate->itemID.toString() == sourceId)
                {
                    root = candidate;
                    break;
                }
        }

        if (numLoops <= 1)
        {
            // Disable: remove every generated copy of this loop and clear the
            // original's marker. Idempotent when already off (no transaction).
            juce::Array<te::Clip*> copies;
            const auto rootId = root->itemID.toString();

            for (auto* candidate : track->getClips())
                if (candidate != root
                    && candidate->state.getProperty (idLoopSource).toString() == rootId)
                    copies.add (candidate);

            if (copies.isEmpty() && ! root->state.hasProperty (idLoopCount))
                return true; // Nothing to repeat.

            edit->getUndoManager().beginNewTransaction ("Loop clip");

            for (auto* copy : copies)
                copy->removeFromParent();

            root->state.removeProperty (idLoopCount, &edit->getUndoManager());

            afterClipEdit();
            return true;
        }

        edit->getUndoManager().beginNewTransaction ("Loop clip");

        const auto rootId = root->itemID.toString();
        const auto rootName = root->getName();

        // Drop copies from a previous fallback loop first: `numLoops` is the
        // exact total, so re-enabling without an intervening off must not
        // accumulate another round of copies.
        {
            juce::Array<te::Clip*> staleCopies;

            for (auto* candidate : track->getClips())
                if (candidate != root
                    && candidate->state.getProperty (idLoopSource).toString() == rootId)
                    staleCopies.add (candidate);

            for (auto* copy : staleCopies)
                copy->removeFromParent();
        }

        auto nextStart = root->getPosition().time.getEnd();
        const auto length = root->getPosition().time.getLength();

        for (int i = 1; i < numLoops; ++i)
        {
            auto* copy = te::insertClipCopy (*track, te::ClipCopy::fromClip (*root)
                                                       .withNewItemID (*edit));

            if (copy == nullptr)
                return false;

            copy->setStart (nextStart, false, true);
            copy->setName (rootName + " (loop " + juce::String (i + 1) + ")");
            copy->state.setProperty (idLoopSource, rootId, &edit->getUndoManager());
            nextStart = nextStart + length;
        }

        // Mark the original last, *after* the copies were taken from its state,
        // so idLoopCount does not leak into the copies (they identify the loop
        // through idLoopSource).
        root->state.setProperty (idLoopCount, numLoops, &edit->getUndoManager());

        afterClipEdit();
        return true;
    }

    bool Session::setClipMuted (int trackIndex, int clipIndex, bool shouldMute)
    {
        auto* clip = clipAt (trackIndex, clipIndex);

        if (edit == nullptr || clip == nullptr)
            return false;

        edit->getUndoManager().beginNewTransaction (shouldMute ? "Mute clip" : "Unmute clip");
        clip->setMuted (shouldMute);
        afterClipEdit();
        return true;
    }

    bool Session::setClipFadeIn (int trackIndex, int clipIndex, double seconds)
    {
        auto* audioClip = audioClipAt (trackIndex, clipIndex);

        if (edit == nullptr || audioClip == nullptr || seconds < 0.0)
            return false;

        edit->getUndoManager().beginNewTransaction ("Clip fade in");
        audioClip->setFadeIn (te::TimeDuration::fromSeconds (seconds));
        afterClipEdit();
        return true;
    }

    bool Session::setClipFadeOut (int trackIndex, int clipIndex, double seconds)
    {
        auto* audioClip = audioClipAt (trackIndex, clipIndex);

        if (edit == nullptr || audioClip == nullptr || seconds < 0.0)
            return false;

        edit->getUndoManager().beginNewTransaction ("Clip fade out");
        audioClip->setFadeOut (te::TimeDuration::fromSeconds (seconds));
        afterClipEdit();
        return true;
    }

    double Session::getClipFadeIn (int trackIndex, int clipIndex) const
    {
        if (auto* audioClip = audioClipAt (trackIndex, clipIndex))
            return audioClip->getFadeIn().inSeconds();

        return 0.0;
    }

    double Session::getClipFadeOut (int trackIndex, int clipIndex) const
    {
        if (auto* audioClip = audioClipAt (trackIndex, clipIndex))
            return audioClip->getFadeOut().inSeconds();

        return 0.0;
    }

    bool Session::crossfadeClipWithNext (int trackIndex, int clipIndex, double seconds)
    {
        auto* track = getTrack (trackIndex);
        te::Clip::Ptr left (clipAt (trackIndex, clipIndex));

        if (edit == nullptr || track == nullptr || left == nullptr
            || ! (seconds > minClipSeconds))
            return false;

        auto* leftAudio = dynamic_cast<te::AudioClipBase*> (left.get());

        if (leftAudio == nullptr)
            return false;

        // Nearest clip to the right on the same track.
        const auto leftStart = left->getPosition().time.getStart().inSeconds();
        te::Clip* right = nullptr;

        for (auto* candidate : track->getClips())
        {
            if (candidate == left.get())
                continue;

            const auto candidateStart = candidate->getPosition().time.getStart().inSeconds();

            if (candidateStart < leftStart - 1.0e-9)
                continue;

            if (right == nullptr
                || candidateStart < right->getPosition().time.getStart().inSeconds())
                right = candidate;
        }

        if (right == nullptr)
        {
            lastError = "No adjacent clip to the right to crossfade with.";
            return false;
        }

        auto* rightAudio = dynamic_cast<te::AudioClipBase*> (right);

        if (rightAudio == nullptr)
            return false;

        const auto junction = right->getPosition().time.getStart();

        edit->getUndoManager().beginNewTransaction ("Crossfade clips");

        // Extend the left clip so the two overlap by `seconds`, then apply
        // complementary equal-power (convex) fades across the overlap.
        left->setEnd (junction + te::TimeDuration::fromSeconds (seconds), true);
        leftAudio->setFadeOutType (te::AudioFadeCurve::convex);
        rightAudio->setFadeInType (te::AudioFadeCurve::convex);
        leftAudio->setFadeOut (te::TimeDuration::fromSeconds (seconds));
        rightAudio->setFadeIn (te::TimeDuration::fromSeconds (seconds));

        afterClipEdit();
        return true;
    }

    //==============================================================================
    // Comping (FR-ED-4): assemble a master take on a new track, non-destructively.
    int Session::compTakes (int sourceTrackIndex,
                            const juce::Array<int>& takeClipIndices,
                            const juce::Array<double>& boundariesSeconds,
                            const juce::Array<int>& chosenTakes)
    {
        if (edit == nullptr || takeClipIndices.isEmpty() || chosenTakes.isEmpty())
            return -1;

        if (boundariesSeconds.size() != chosenTakes.size() - 1)
        {
            lastError = "A comp needs exactly one fewer boundary than chosen takes.";
            return -1;
        }

        if (getTrack (sourceTrackIndex) == nullptr)
        {
            lastError = "No such take track.";
            return -1;
        }

        // Resolve the takes and find the span they cover.
        std::vector<te::Clip::Ptr> takes ((size_t) takeClipIndices.size());
        double spanStart = std::numeric_limits<double>::max();
        double spanEnd = std::numeric_limits<double>::lowest();

        for (int i = 0; i < takeClipIndices.size(); ++i)
        {
            te::Clip::Ptr clip (clipAt (sourceTrackIndex, takeClipIndices[i]));

            if (clip == nullptr)
            {
                lastError = "Comp take not found.";
                return -1;
            }

            const auto pos = clip->getPosition();
            spanStart = juce::jmin (spanStart, pos.time.getStart().inSeconds());
            spanEnd = juce::jmax (spanEnd, pos.time.getEnd().inSeconds());
            takes[(size_t) i] = clip;
        }

        for (auto t : chosenTakes)
            if (t < 0 || t >= takeClipIndices.size())
            {
                lastError = "Comp chose a take that does not exist.";
                return -1;
            }

        double previous = spanStart;

        for (auto boundary : boundariesSeconds)
        {
            if (! (boundary > previous && boundary < spanEnd))
            {
                lastError = "Comp boundaries must be ascending and inside the takes.";
                return -1;
            }

            previous = boundary;
        }

        edit->getUndoManager().beginNewTransaction ("Comp takes");

        auto compTrack = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit),
                                                    nullptr, true);

        if (compTrack == nullptr)
        {
            lastError = "Could not create the comp track.";
            return -1;
        }

        compTrack->setName ("Comp");

        const int numRegions = chosenTakes.size();

        for (int region = 0; region < numRegions; ++region)
        {
            const auto regionStart = (region == 0) ? spanStart
                                                   : boundariesSeconds[region - 1];
            const auto regionEnd = (region == numRegions - 1) ? spanEnd
                                                              : boundariesSeconds[region];

            te::Clip& anchor = *takes[(size_t) chosenTakes[region]];

            // Trimmed copy referencing the take's original file: the take and its
            // file are never touched (non-destructive).
            auto* segment = te::insertClipCopy (*compTrack,
                                                te::ClipCopy::fromClip (anchor)
                                                    .withNewItemID (*edit));

            if (segment == nullptr)
            {
                lastError = "Could not insert a comp segment.";
                return -1;
            }

            segment->setStart (te::TimePosition::fromSeconds (regionStart), true, false);
            segment->setEnd (te::TimePosition::fromSeconds (regionEnd), true);
            segment->setName (anchor.getName() + " comp");
        }

        afterClipEdit();

        const auto tracks = te::getAudioTracks (*edit);

        for (int i = 0; i < tracks.size(); ++i)
            if (tracks[i] == compTrack.get())
                return i;

        return -1;
    }

    int Session::getClipTakeCount (int trackIndex, int clipIndex) const
    {
        if (auto* wave = dynamic_cast<te::WaveAudioClip*> (clipAt (trackIndex, clipIndex)))
            return wave->getNumTakes (true);

        return 0;
    }

    juce::StringArray Session::getClipTakeDescriptions (int trackIndex, int clipIndex) const
    {
        if (auto* wave = dynamic_cast<te::WaveAudioClip*> (clipAt (trackIndex, clipIndex)))
            return wave->getTakeDescriptions();

        return {};
    }

    //==============================================================================
    // Time-stretch (FR-ED-5): render the clip's visible region to a new file.
    bool Session::stretchClipToDuration (int trackIndex, int clipIndex, double targetSeconds,
                                         double semitones)
    {
        auto* track = getTrack (trackIndex);
        te::Clip::Ptr clip (clipAt (trackIndex, clipIndex));

        if (edit == nullptr || track == nullptr || clip == nullptr)
            return false;

        auto* wave = dynamic_cast<te::WaveAudioClip*> (clip.get());

        if (wave == nullptr)
        {
            lastError = "Only wave clips can be time-stretched.";
            return false;
        }

        if (! std::isfinite (targetSeconds) || targetSeconds <= minClipSeconds)
        {
            lastError = "The target duration must be positive.";
            return false;
        }

        const auto originalFile = wave->getOriginalFile();

        if (! originalFile.existsAsFile())
        {
            lastError = "The clip's source file is missing: " + originalFile.getFullPathName();
            return false;
        }

        const auto clipInfo = [&]
        {
            ClipInfo i;
            getClipInfo (trackIndex, clipIndex, i);
            return i;
        }();

        // Read the visible region of the source file.
        auto& readManager = edit->engine.getAudioFileFormatManager().readFormatManager;
        std::unique_ptr<juce::AudioFormatReader> reader (readManager.createReaderFor (originalFile));

        if (reader == nullptr || reader->lengthInSamples <= 0)
        {
            lastError = "Could not read the clip's source file.";
            return false;
        }

        const auto fileSampleRate = reader->sampleRate;
        const auto numChannels = (int) reader->numChannels;
        const auto startSample = (juce::int64) std::llround (clipInfo.offsetSeconds * fileSampleRate);
        const auto available = reader->lengthInSamples - startSample;

        if (startSample < 0 || available <= 0 || numChannels <= 0)
        {
            lastError = "The clip region is empty.";
            return false;
        }

        auto regionSamples = (juce::int64) std::llround (clipInfo.lengthSeconds * fileSampleRate);
        regionSamples = juce::jlimit ((juce::int64) 1, available, regionSamples);

        juce::AudioBuffer<float> region (numChannels, (int) regionSamples);
        region.clear();

        if (! reader->read (&region, 0, (int) regionSamples, startSample, true, true))
        {
            lastError = "Could not read the clip region.";
            return false;
        }

        const auto stretched = TimeStretch::stretchToDuration (region, fileSampleRate,
                                                               targetSeconds, semitones);

        if (! stretched.ok)
        {
            lastError = stretched.error;
            return false;
        }

        // Write the result next to the session, under Processed/.
        auto outputDir = editFile != juce::File()
                             ? editFile.getParentDirectory().getChildFile ("Processed")
                             : juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("raw-radio-studio-processed");

        if (! outputDir.createDirectory())
        {
            lastError = "Could not create the processed-audio directory.";
            return false;
        }

        const auto baseName = originalFile.getFileNameWithoutExtension() + "-stretch";
        auto outputFile = outputDir.getChildFile (baseName + ".wav");

        for (int suffix = 2; outputFile.existsAsFile(); ++suffix)
            outputFile = outputDir.getChildFile (baseName + "-" + juce::String (suffix) + ".wav");

        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (outputFile);

            auto writer = wav.createWriterFor (stream,
                                               juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (stretched.sampleRate)
                                                   .withNumChannels (stretched.audio.getNumChannels())
                                                   .withBitsPerSample (24));

            if (writer == nullptr
                || ! writer->writeFromAudioSampleBuffer (stretched.audio, 0, stretched.audio.getNumSamples()))
            {
                lastError = "Could not write the stretched audio file.";
                return false;
            }
        }

        // Replace the clip with one referencing the new file (the original file
        // stays on disk untouched).
        edit->getUndoManager().beginNewTransaction ("Time-stretch clip");

        const auto startSeconds = clipInfo.startSeconds;
        const auto clipName = clip->getName();

        clip->removeFromParent();

        auto newClip = track->insertWaveClip (clipName, outputFile,
                                              { { te::TimePosition::fromSeconds (startSeconds),
                                                  te::TimeDuration::fromSeconds (targetSeconds) },
                                                {} },
                                              false);

        if (newClip == nullptr)
        {
            lastError = "Could not insert the stretched clip.";
            return false;
        }

        newClip->setName (clipName);

        afterClipEdit();
        return true;
    }

    //==============================================================================
    bool Session::undo()
    {
        if (edit == nullptr || ! edit->getUndoManager().canUndo())
            return false;

        edit->undo();
        edit->restartPlayback();
        save();
        sendChangeMessage();
        return true;
    }

    bool Session::redo()
    {
        if (edit == nullptr || ! edit->getUndoManager().canRedo())
            return false;

        edit->redo();
        edit->restartPlayback();
        save();
        sendChangeMessage();
        return true;
    }

    bool Session::canUndo() const
    {
        return edit != nullptr && edit->getUndoManager().canUndo();
    }

    bool Session::canRedo() const
    {
        return edit != nullptr && edit->getUndoManager().canRedo();
    }

    juce::String Session::getUndoDescription() const
    {
        return edit != nullptr ? edit->getUndoManager().getUndoDescription() : juce::String();
    }

    juce::String Session::getRedoDescription() const
    {
        return edit != nullptr ? edit->getUndoManager().getRedoDescription() : juce::String();
    }

    bool Session::record()
    {
        if (edit == nullptr)
            return false;

        if (! isAnyTrackArmed())
        {
            lastError = "Arm a track before recording.";
            return false;
        }

        // Mute the armed record tracks' existing clips BEFORE the transport
        // rolls: a new take must not play the previous take back to the
        // performer, and muting first also keeps the old-take audio out of the
        // first block of monitoring. Other (backing/minus) tracks keep playing.
        beginRecordPassMutes();

        // Re-assert the session's count-in on the engine immediately before the
        // roll. The engine keeps it in the user-global Settings.xml, so without
        // this a value changed elsewhere would silently alter this take's
        // pre-roll (the "count-in mode not applied to the edit" bug).
        edit->setCountInMode (countInMode);

        edit->getTransport().record (false);
        return true;
    }

    void Session::beginRecordPassMutes()
    {
        if (edit == nullptr || recordPassActive)
            return;

        recordPassActive = true;

        for (auto index : inputTrackIndices())
        {
            if (! isTrackArmed (index))
                continue;

            auto* track = getTrack (index);

            if (track == nullptr)
                continue;

            for (auto* clip : track->getClips())
            {
                if (clip == nullptr || clip->isMuted())
                    continue; // already muted: leave it (and don't restore it)

                clip->setMuted (true);
                recordPassClips.push_back ({ tracktion::Clip::Ptr (clip) });
            }
        }
    }

    void Session::endRecordPassMutes()
    {
        // Only clips that were unmuted at the start of the pass are collected,
        // so restoring to unmuted is exact (a clip the user had already muted is
        // never touched).
        for (auto& entry : recordPassClips)
            if (entry.clip != nullptr)
                entry.clip->setMuted (false);

        recordPassClips.clear();
        recordPassActive = false;
    }

    bool Session::isPlaying() const
    {
        return edit != nullptr && edit->getTransport().isPlaying();
    }

    bool Session::isRecording() const
    {
        return edit != nullptr && edit->getTransport().isRecording();
    }

    //==============================================================================
    // Basic mixer (FR-MIX-1/3)
    bool Session::setTrackGainDb (int trackIndex, float db)
    {
        auto* track = getTrack (trackIndex);

        if (auto* volume = track != nullptr ? track->getVolumePlugin() : nullptr)
        {
            volume->setVolumeDb (juce::jlimit (-100.0f, 12.0f, db));
            sendChangeMessage();
            return true;
        }

        return false;
    }

    float Session::getTrackGainDb (int trackIndex) const
    {
        auto* track = getTrack (trackIndex);

        if (auto* volume = track != nullptr ? track->getVolumePlugin() : nullptr)
            return volume->getVolumeDb();

        return 0.0f;
    }

    bool Session::setTrackPan (int trackIndex, float pan)
    {
        auto* track = getTrack (trackIndex);

        if (auto* volume = track != nullptr ? track->getVolumePlugin() : nullptr)
        {
            volume->setPan (juce::jlimit (-1.0f, 1.0f, pan));
            sendChangeMessage();
            return true;
        }

        return false;
    }

    float Session::getTrackPan (int trackIndex) const
    {
        auto* track = getTrack (trackIndex);

        if (auto* volume = track != nullptr ? track->getVolumePlugin() : nullptr)
            return volume->getPan();

        return 0.0f;
    }

    bool Session::setTrackMute (int trackIndex, bool shouldMute)
    {
        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return false;

        track->setMute (shouldMute);
        sendChangeMessage();
        return true;
    }

    bool Session::isTrackMuted (int trackIndex) const
    {
        auto* track = getTrack (trackIndex);

        // Explicit mute only (FR-MIX-1): `isMuted(true)` also folds in mute-by-
        // destination/parent, which would light the strip's "M" for a track the
        // user never muted (and make the M/S toggle unrecoverable).
        return track != nullptr && track->isMuted (false);
    }

    bool Session::setTrackSolo (int trackIndex, bool shouldSolo)
    {
        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return false;

        track->setSolo (shouldSolo);
        sendChangeMessage();
        return true;
    }

    bool Session::isTrackSolo (int trackIndex) const
    {
        auto* track = getTrack (trackIndex);
        return track != nullptr && track->isSolo (true);
    }

    bool Session::setMasterGainDb (float db)
    {
        if (edit == nullptr)
            return false;

        masterGainDb = juce::jlimit (-100.0f, 12.0f, db);
        applyMasterGain();
        storeMasterState();
        sendChangeMessage();
        return true;
    }

    float Session::getMasterGainDb() const
    {
        return masterGainDb;
    }

    bool Session::setMasterPan (float pan)
    {
        if (edit == nullptr)
            return false;

        if (auto volume = edit->getMasterVolumePlugin())
        {
            volume->setPan (juce::jlimit (-1.0f, 1.0f, pan));
            sendChangeMessage();
            return true;
        }

        return false;
    }

    float Session::getMasterPan() const
    {
        if (edit != nullptr)
            if (auto volume = edit->getMasterVolumePlugin())
                return volume->getPan();

        return 0.0f;
    }

    bool Session::setMasterMute (bool shouldMute)
    {
        if (edit == nullptr)
            return false;

        masterMuted = shouldMute;
        applyMasterGain();
        storeMasterState();
        sendChangeMessage();
        return true;
    }

    bool Session::isMasterMuted() const
    {
        return masterMuted;
    }

    // Tracktion has no dedicated master mute node, so mute is applied by
    // overriding the master volume plugin's gain; the fader value itself is kept
    // in `masterGainDb` and re-applied when unmuted.
    void Session::applyMasterGain()
    {
        if (edit == nullptr)
            return;

        if (auto volume = edit->getMasterVolumePlugin())
            volume->setVolumeDb (masterMuted ? -100.0f : masterGainDb);
    }

    // The plugin gain alone cannot round-trip a muted session (it stores -100 dB),
    // so the real fader gain and the mute state are written to the Edit's root
    // state tree as first-class properties.
    void Session::storeMasterState()
    {
        if (edit == nullptr)
            return;

        edit->state.setProperty (idMasterGain, (double) masterGainDb, nullptr);
        edit->state.setProperty (idMasterMuted, masterMuted, nullptr);
    }

    //==============================================================================
    // Plugin hosting (FR-MIX-4/6, Epic 3)
    namespace
    {
        /** The `index`-th hosted (external) plugin on a track, skipping the
            engine's built-in volume/pan and level-meter plugins. */
        te::ExternalPlugin* externalPluginAt (te::AudioTrack& track, int index)
        {
            if (index < 0)
                return nullptr;

            int seen = 0;

            for (auto* plugin : track.pluginList)
                if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin))
                    if (seen++ == index)
                        return external;

            return nullptr;
        }
    }

    int Session::getNumPlugins (int trackIndex) const
    {
        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return 0;

        int count = 0;

        for (auto* plugin : track->pluginList)
            if (dynamic_cast<te::ExternalPlugin*> (plugin) != nullptr)
                ++count;

        return count;
    }

    Session::PluginInfo Session::getPluginInfo (int trackIndex, int pluginIndex) const
    {
        PluginInfo info;
        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return info;

        if (auto* external = externalPluginAt (*track, pluginIndex))
        {
            info.name = external->getName();
            info.format = external->desc.pluginFormatName;
            info.missing = external->isMissing() || external->getLoadError().isNotEmpty();
        }

        return info;
    }

    bool Session::insertPlugin (int trackIndex, const juce::PluginDescription& description, int pluginIndex)
    {
        auto* track = getTrack (trackIndex);

        if (edit == nullptr || track == nullptr)
        {
            lastError = "No such track.";
            return false;
        }

        // Effects only. An instrument/generator/panner inserted as an effect
        // either has no audio input or mangles the signal (owner bug: inserting
        // DLSMusicDevice / HRTFPanner killed the track). Reject *before* touching
        // the track so its audio graph is left exactly as it was.
        if (auto reason = plugin_selection::insertRejectionReason (description.isInstrument,
                                                                   description.category,
                                                                   description.numInputChannels,
                                                                   description.numOutputChannels);
            reason.isNotEmpty())
        {
            lastError = reason;
            return false;
        }

        auto plugin = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, description);

        if (plugin == nullptr)
        {
            lastError = "Could not create plugin: " + description.name;
            return false;
        }

        // Re-check against the live instance when one is already available: a
        // scanned description can carry unknown 0/0 counts, and this catches a
        // genuine layout mismatch before the plugin joins the chain. The plugin
        // reference is dropped on rejection, so nothing is inserted.
        if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin.get()))
        {
            if (auto* instance = external->getAudioPluginInstance())
            {
                if (auto reason = plugin_selection::insertRejectionReason (
                        description.isInstrument,
                        description.category,
                        instance->getTotalNumInputChannels(),
                        instance->getTotalNumOutputChannels());
                    reason.isNotEmpty())
                {
                    lastError = reason;
                    return false;
                }
            }
        }

        track->pluginList.insertPlugin (plugin, pluginIndex, nullptr);

        edit->restartPlayback();
        save();
        sendChangeMessage();
        clearLastError();
        return true;
    }

    bool Session::removePlugin (int trackIndex, int pluginIndex)
    {
        auto* track = getTrack (trackIndex);

        if (edit == nullptr || track == nullptr)
        {
            lastError = "No such track.";
            return false;
        }

        auto* external = externalPluginAt (*track, pluginIndex);

        if (external == nullptr)
        {
            lastError = "No such plugin.";
            return false;
        }

        // Closes the editor window (hideWindowForShutdown) and detaches it from
        // the Edit's ValueTree so the next save drops it.
        external->deleteFromParent();

        edit->restartPlayback();
        save();
        sendChangeMessage();
        clearLastError();
        return true;
    }

    bool Session::showPluginEditor (int trackIndex, int pluginIndex)
    {
        auto* track = getTrack (trackIndex);

        if (edit == nullptr || track == nullptr)
            return false;

        auto* external = externalPluginAt (*track, pluginIndex);

        if (external == nullptr)
            return false;

        external->showWindowExplicitly();
        return true;
    }

    //==============================================================================
    // Plugin preset management (FR-MIX-6)
    juce::String Session::getPluginPresetKey (int trackIndex, int pluginIndex) const
    {
        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return {};

        if (auto* external = externalPluginAt (*track, pluginIndex))
            return PluginPresets::keyFor (external->desc.name,
                                          external->desc.pluginFormatName,
                                          external->desc.fileOrIdentifier);

        return {};
    }

    bool Session::savePluginPreset (int trackIndex, int pluginIndex, const juce::String& name)
    {
        auto* track = getTrack (trackIndex);
        auto* external = track != nullptr ? externalPluginAt (*track, pluginIndex) : nullptr;
        auto* instance = external != nullptr ? external->getAudioPluginInstance() : nullptr;

        if (external == nullptr || instance == nullptr)
        {
            lastError = "No such plugin.";
            return false;
        }

        if (! PluginPresets::isValidPresetName (name))
        {
            lastError = "Enter a preset name (no path separators).";
            return false;
        }

        // Message-thread only: the plugin serialises its own state synchronously.
        juce::MemoryBlock state;
        instance->getStateInformation (state);

        if (! presets.savePreset (getPluginPresetKey (trackIndex, pluginIndex), name, state))
        {
            lastError = "Could not write the preset to disk.";
            return false;
        }

        clearLastError();
        return true;
    }

    juce::StringArray Session::listPluginPresets (int trackIndex, int pluginIndex) const
    {
        return presets.listPresets (getPluginPresetKey (trackIndex, pluginIndex));
    }

    bool Session::loadPluginPreset (int trackIndex, int pluginIndex, const juce::String& name)
    {
        auto* track = getTrack (trackIndex);
        auto* external = track != nullptr ? externalPluginAt (*track, pluginIndex) : nullptr;
        auto* instance = external != nullptr ? external->getAudioPluginInstance() : nullptr;

        if (external == nullptr || instance == nullptr)
        {
            lastError = "No such plugin.";
            return false;
        }

        juce::MemoryBlock state;

        if (! presets.loadPreset (getPluginPresetKey (trackIndex, pluginIndex), name, state))
        {
            lastError = "Could not read the preset.";
            return false;
        }

        instance->setStateInformation (state.getData(), (int) state.getSize());

        // Push the applied state back into the Edit's ValueTree (and its bus
        // layout) so save/open and the offline render see the loaded preset.
        external->flushPluginStateToValueTree();

        if (edit != nullptr)
            edit->restartPlayback();

        save();
        sendChangeMessage();
        clearLastError();
        return true;
    }

    void Session::setPresetDirectory (const juce::File& rootDirectory)
    {
        presets.setRootDirectory (rootDirectory);
    }

    juce::File Session::getPresetDirectory() const
    {
        return presets.getRootDirectory();
    }

    //==============================================================================
    // Routing: output assignment + submix folders + aux sends (FR-MIX-2, Epic 3)
    namespace
    {
        struct CueReturn
        {
            te::AudioTrack* track = nullptr;
            int bus = -1;
            juce::String name;
        };

        /** All cue return/bus tracks, in engine order (positional cue index). */
        std::vector<CueReturn> collectCueReturns (te::Edit& edit)
        {
            std::vector<CueReturn> returns;

            for (auto* track : te::getAudioTracks (edit))
                if ((bool) track->state.getProperty (idCueReturn, false))
                    returns.push_back ({ track,
                                         (int) track->state.getProperty (idCueBus, -1),
                                         track->state.getProperty (idCueName, track->getName()).toString() });

            return returns;
        }

        te::AuxSendPlugin* findAuxSend (te::AudioTrack& track, int bus)
        {
            for (auto* plugin : track.pluginList)
                if (auto* send = dynamic_cast<te::AuxSendPlugin*> (plugin))
                    if (send->getBusNumber() == bus)
                        return send;

            return nullptr;
        }

        /** The hardware output pair a cue return is currently routed to (stable
            device ID), or an empty string when it uses the default/main output. */
        juce::String cueReturnOutputDeviceID (te::AudioTrack& track)
        {
            if (auto* device = track.getOutput().getOutputDevice (false))
                return device->getDeviceID();

            return {};
        }

        /** Default cue output pair: device 0 is the control-room/main out, so a
            cue takes the first output pair not already claimed by another cue
            return. Returns an empty string when no free pair exists (the cue then
            falls back to the main output — "not physically separable" per
            EPIC2_GAPS).

            Choosing the first *free* pair (rather than `returns.size() + 1`)
            means that after a cue is removed a newly added cue reuses the freed
            pair instead of colliding with another cue's output. */
        juce::String defaultCueOutputDeviceID (AudioEngine& audio,
                                               const std::vector<CueReturn>& returns)
        {
            const auto devices = audio.deviceManager().getWaveOutputDevices();
            const auto numDevices = devices.size();

            // Only the main out exists: no separable cue pair is available.
            if (numDevices <= 1)
                return {};

            // Device 0 is the control room / main out — never hand it to a cue.
            juce::StringArray claimed;
            claimed.add (devices[0]->getDeviceID());

            for (auto& cue : returns)
                if (cue.track != nullptr)
                    claimed.addIfNotAlreadyThere (cueReturnOutputDeviceID (*cue.track));

            for (size_t i = 1; i < numDevices; ++i)
                if (! claimed.contains (devices[i]->getDeviceID()))
                    return devices[i]->getDeviceID();

            return {};
        }
    }

    juce::StringArray Session::getAvailableOutputDeviceIDs() const
    {
        juce::StringArray ids;

        for (auto* device : audio.deviceManager().getWaveOutputDevices())
            ids.add (device->getDeviceID());

        return ids;
    }

    juce::StringArray Session::getAvailableOutputDeviceNames() const
    {
        juce::StringArray names;

        for (auto* device : audio.deviceManager().getWaveOutputDevices())
            names.add (device->getName() + " (" + juce::String (device->getChannels().getNumChannels())
                       + "ch)");

        return names;
    }

    bool Session::setTrackOutputToDevice (int trackIndex, const juce::String& deviceID)
    {
        auto* track = getTrack (trackIndex);

        if (edit == nullptr || track == nullptr)
        {
            lastError = "No such track.";
            return false;
        }

        if (deviceID.isEmpty())
            return setTrackOutputToDefault (trackIndex);

        if (! getAvailableOutputDeviceIDs().contains (deviceID))
        {
            lastError = "No such output device.";
            return false;
        }

        track->getOutput().setOutputToDeviceID (deviceID);
        edit->restartPlayback();
        save();
        sendChangeMessage();
        clearLastError();
        return true;
    }

    bool Session::setTrackOutputToDefault (int trackIndex)
    {
        auto* track = getTrack (trackIndex);

        if (edit == nullptr || track == nullptr)
        {
            lastError = "No such track.";
            return false;
        }

        track->getOutput().setOutputToDefaultDevice (false);
        edit->restartPlayback();
        save();
        sendChangeMessage();
        clearLastError();
        return true;
    }

    juce::String Session::getTrackOutputDevice (int trackIndex) const
    {
        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return {};

        // `getOutputDeviceID()` returns the resolved device's *name*; return its
        // stable ID instead so callers can match it against getAvailableOutputDeviceIDs().
        if (auto* device = track->getOutput().getOutputDevice (false))
            return device->getDeviceID();

        return track->getOutput().getOutputDeviceID();
    }

    bool Session::trackHasDedicatedOutput (int trackIndex) const
    {
        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return false;

        auto& output = track->getOutput();
        return ! output.usesDefaultAudioOut() && ! output.usesDefaultMIDIOut()
            && ! output.outputsToNone();
    }

    //==============================================================================
    namespace
    {
        juce::Array<te::FolderTrack*> submixFolders (te::Edit& edit)
        {
            juce::Array<te::FolderTrack*> folders;

            for (auto* folder : te::getTracksOfType<te::FolderTrack> (edit, true))
                if (folder != nullptr && folder->isSubmixFolder())
                    folders.add (folder);

            return folders;
        }
    }

    int Session::createSubmixFolder (const juce::String& name)
    {
        if (edit == nullptr)
        {
            lastError = "Open a session before adding a submix.";
            return -1;
        }

        auto folder = edit->insertNewFolderTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr, true);

        if (folder == nullptr)
        {
            lastError = "Could not create the submix folder.";
            return -1;
        }

        folder->setName (name.isNotEmpty() ? name
                                           : ("Bus " + juce::String (getNumSubmixFolders() + 1)));

        edit->restartPlayback();
        save();
        sendChangeMessage();
        clearLastError();
        return getNumSubmixFolders() - 1;
    }

    int Session::getNumSubmixFolders() const
    {
        return edit != nullptr ? submixFolders (*edit).size() : 0;
    }

    juce::String Session::getSubmixFolderName (int folderIndex) const
    {
        if (edit == nullptr)
            return {};

        auto folders = submixFolders (*edit);

        if (! juce::isPositiveAndBelow (folderIndex, folders.size()))
            return {};

        return folders[folderIndex]->getName();
    }

    bool Session::addTrackToSubmix (int trackIndex, int folderIndex)
    {
        auto* track = getTrack (trackIndex);

        if (edit == nullptr || track == nullptr)
        {
            lastError = "No such track.";
            return false;
        }

        auto folders = submixFolders (*edit);

        if (! juce::isPositiveAndBelow (folderIndex, folders.size()))
        {
            lastError = "No such submix folder.";
            return false;
        }

        te::Track::Ptr trackPtr (track);
        edit->moveTrack (trackPtr, te::TrackInsertPoint (folders[folderIndex]->itemID, {}));

        edit->restartPlayback();
        save();
        sendChangeMessage();
        clearLastError();
        return true;
    }

    bool Session::removeTrackFromSubmix (int trackIndex)
    {
        auto* track = getTrack (trackIndex);

        if (edit == nullptr || track == nullptr || track->getParentFolderTrack() == nullptr)
        {
            lastError = "The track is not in a submix.";
            return false;
        }

        te::Track::Ptr trackPtr (track);
        edit->moveTrack (trackPtr, te::TrackInsertPoint::getEndOfTracks (*edit));

        edit->restartPlayback();
        save();
        sendChangeMessage();
        clearLastError();
        return true;
    }

    int Session::getTrackSubmixFolder (int trackIndex) const
    {
        auto* track = getTrack (trackIndex);

        if (edit == nullptr || track == nullptr)
            return -1;

        auto* parent = track->getParentFolderTrack();

        if (parent == nullptr)
            return -1;

        return submixFolders (*edit).indexOf (parent);
    }

    //==============================================================================
    // Software cue mixes (FR-MON-3 / FR-MON-4)
    int Session::getNumCueMixes() const
    {
        return edit != nullptr ? (int) collectCueReturns (*edit).size() : 0;
    }

    int Session::createCueMix (const juce::String& name)
    {
        if (edit == nullptr)
        {
            lastError = "Open a session before adding a cue.";
            return -1;
        }

        const auto returns = collectCueReturns (*edit);

        int nextBus = 0;

        for (auto& cue : returns)
            nextBus = juce::jmax (nextBus, cue.bus + 1);

        auto track = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr, true);

        if (track == nullptr)
        {
            lastError = "Could not create the cue return track.";
            return -1;
        }

        const auto cueName = name.isNotEmpty() ? name
                                               : ("Cue " + juce::String ((int) returns.size() + 1));
        track->setName (cueName);
        track->state.setProperty (idCueReturn, true, nullptr);
        track->state.setProperty (idCueBus, nextBus, nullptr);
        track->state.setProperty (idCueName, cueName, nullptr);

        // The cue's aux-return bus: sums every AuxSend whose bus number matches.
        auto plugin = edit->getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {});

        if (auto* ret = dynamic_cast<te::AuxReturnPlugin*> (plugin.get()))
            ret->busNumber = nextBus;

        if (plugin != nullptr)
            track->pluginList.insertPlugin (plugin, -1, nullptr);

        // Route to the first free hardware output pair when present.
        if (auto deviceID = defaultCueOutputDeviceID (audio, returns); deviceID.isNotEmpty())
            track->getOutput().setOutputToDeviceID (deviceID);

        edit->restartPlayback();
        save();
        sendChangeMessage();
        clearLastError();
        return (int) returns.size();
    }

    bool Session::removeCueMix (int cueIndex)
    {
        if (edit == nullptr)
            return false;

        auto returns = collectCueReturns (*edit);

        if (! juce::isPositiveAndBelow (cueIndex, (int) returns.size()))
        {
            lastError = "No such cue mix.";
            return false;
        }

        edit->deleteTrack (returns[(size_t) cueIndex].track);
        edit->restartPlayback();
        save();
        sendChangeMessage();
        clearLastError();
        return true;
    }

    Session::CueMixInfo Session::getCueMix (int cueIndex) const
    {
        CueMixInfo info;

        if (edit == nullptr)
            return info;

        auto returns = collectCueReturns (*edit);

        if (! juce::isPositiveAndBelow (cueIndex, (int) returns.size()))
            return info;

        auto& cue = returns[(size_t) cueIndex];
        info.index = cueIndex;
        info.name = cue.name;
        info.busNumber = cue.bus;
        info.returnTrackIndex = te::getAudioTracks (*edit).indexOf (cue.track);
        info.outputDeviceID = getTrackOutputDevice (info.returnTrackIndex);
        info.hasDedicatedOutput = ! cue.track->getOutput().usesDefaultAudioOut()
                               && ! cue.track->getOutput().outputsToNone();
        return info;
    }

    bool Session::setCueMixName (int cueIndex, const juce::String& name)
    {
        if (edit == nullptr)
            return false;

        auto returns = collectCueReturns (*edit);

        if (! juce::isPositiveAndBelow (cueIndex, (int) returns.size()) || name.isEmpty())
            return false;

        returns[(size_t) cueIndex].track->setName (name);
        returns[(size_t) cueIndex].track->state.setProperty (idCueName, name, nullptr);
        save();
        sendChangeMessage();
        return true;
    }

    bool Session::setCueMixOutputDevice (int cueIndex, const juce::String& deviceID)
    {
        if (edit == nullptr)
            return false;

        auto returns = collectCueReturns (*edit);

        if (! juce::isPositiveAndBelow (cueIndex, (int) returns.size()))
        {
            lastError = "No such cue mix.";
            return false;
        }

        auto* track = returns[(size_t) cueIndex].track;

        if (deviceID.isEmpty())
            track->getOutput().setOutputToDefaultDevice (false);
        else if (! getAvailableOutputDeviceIDs().contains (deviceID))
        {
            lastError = "No such output device.";
            return false;
        }
        else
            track->getOutput().setOutputToDeviceID (deviceID);

        edit->restartPlayback();
        save();
        sendChangeMessage();
        clearLastError();
        return true;
    }

    bool Session::isCueReturnTrack (int trackIndex) const
    {
        auto* track = getTrack (trackIndex);

        return track != nullptr && (bool) track->state.getProperty (idCueReturn, false);
    }

    float Session::getCueSendLevelDb (int trackIndex, int cueIndex) const
    {
        auto* track = getTrack (trackIndex);

        if (edit == nullptr || track == nullptr)
            return -100.0f;

        const auto returns = collectCueReturns (*edit);

        if (! juce::isPositiveAndBelow (cueIndex, (int) returns.size()))
            return -100.0f;

        if (auto* send = findAuxSend (*track, returns[(size_t) cueIndex].bus))
            return juce::jlimit (-100.0f, 12.0f, send->getGainDb());

        return -100.0f;
    }

    bool Session::isCueSendEnabled (int trackIndex, int cueIndex) const
    {
        auto* track = getTrack (trackIndex);

        if (edit == nullptr || track == nullptr)
            return false;

        const auto returns = collectCueReturns (*edit);

        if (! juce::isPositiveAndBelow (cueIndex, (int) returns.size()))
            return false;

        if (auto* send = findAuxSend (*track, returns[(size_t) cueIndex].bus))
            return send->isEnabled();

        return false;
    }

    bool Session::setCueSendEnabled (int trackIndex, int cueIndex, bool shouldEnable)
    {
        auto* track = getTrack (trackIndex);

        if (edit == nullptr || track == nullptr)
        {
            lastError = "No such track.";
            return false;
        }

        // The cue return must exist before a send can target its bus.
        if (auto info = getCueMix (cueIndex); info.busNumber < 0)
        {
            lastError = "No such cue mix.";
            return false;
        }

        auto* send = findAuxSend (*track, getCueMix (cueIndex).busNumber);

        if (send == nullptr && shouldEnable)
        {
            // Create the send at the end of the plugin chain so it is post-fader
            // (the spec's default), and start it at unity.
            auto plugin = edit->getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});

            if (auto* newSend = dynamic_cast<te::AuxSendPlugin*> (plugin.get()))
            {
                newSend->busNumber = getCueMix (cueIndex).busNumber;
                newSend->setGainDb (0.0f);
                track->pluginList.insertPlugin (plugin, -1, nullptr);
                send = newSend;
            }
        }

        if (send != nullptr)
            send->setEnabled (shouldEnable);

        edit->restartPlayback();
        save();
        sendChangeMessage();
        clearLastError();
        return true;
    }

    bool Session::setCueSendLevelDb (int trackIndex, int cueIndex, float db)
    {
        auto* track = getTrack (trackIndex);

        if (edit == nullptr || track == nullptr)
        {
            lastError = "No such track.";
            return false;
        }

        const auto info = getCueMix (cueIndex);

        if (info.busNumber < 0)
        {
            lastError = "No such cue mix.";
            return false;
        }

        auto* send = findAuxSend (*track, info.busNumber);

        if (send == nullptr)
        {
            auto plugin = edit->getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});

            if (auto* newSend = dynamic_cast<te::AuxSendPlugin*> (plugin.get()))
            {
                newSend->busNumber = info.busNumber;
                track->pluginList.insertPlugin (plugin, -1, nullptr);
                send = newSend;
            }
        }

        if (send == nullptr)
        {
            lastError = "Could not create the cue send.";
            return false;
        }

        send->setGainDb (juce::jlimit (-100.0f, 12.0f, db));
        send->setEnabled (true);

        edit->restartPlayback();
        save();
        sendChangeMessage();
        clearLastError();
        return true;
    }

    //==============================================================================
    // Count-in / metronome (FR-REC-10)
    void Session::setMetronomeEnabled (bool shouldEnable)
    {
        if (edit == nullptr)
            return;

        edit->clickTrackEnabled = shouldEnable;
        sendChangeMessage();
    }

    bool Session::isMetronomeEnabled() const
    {
        return edit != nullptr && edit->clickTrackEnabled.get();
    }

    void Session::setMetronomeRecordingOnly (bool recordingOnly)
    {
        if (edit != nullptr)
        {
            edit->clickTrackRecordingOnly = recordingOnly;
            sendChangeMessage();
        }
    }

    bool Session::isMetronomeRecordingOnly() const
    {
        return edit != nullptr && edit->clickTrackRecordingOnly.get();
    }

    void Session::setCountInMode (te::Edit::CountIn mode)
    {
        // Clamp to the known enum range (0..4) so a persisted/garbage value can
        // never index a bad switch arm in `getNumCountInBeats()`.
        const auto clamped = static_cast<te::Edit::CountIn> (
            juce::jlimit (0, (int) te::Edit::CountIn::oneBeat, (int) mode));

        countInMode = clamped;

        if (edit != nullptr)
        {
            // Apply immediately (so `getCountInBeats()`/the transport see it) and
            // persist on the Edit state so it survives save/open.
            edit->setCountInMode (clamped);
            edit->state.setProperty (idCountInMode, (int) clamped, nullptr);
        }

        sendChangeMessage();
    }

    tracktion::Edit::CountIn Session::getCountInMode() const
    {
        return edit != nullptr ? countInMode : te::Edit::CountIn::none;
    }

    int Session::getCountInBeats() const
    {
        return edit != nullptr ? edit->getNumCountInBeats() : 0;
    }

    void Session::setMetronomeVolume (float gain)
    {
        if (edit != nullptr)
        {
            edit->setClickTrackVolume (juce::jlimit (0.2f, 1.0f, gain));
            sendChangeMessage();
        }
    }

    float Session::getMetronomeVolume() const
    {
        return edit != nullptr ? edit->getClickTrackVolume() : 0.5f;
    }

    //==============================================================================
    bool Session::save()
    {
        if (edit == nullptr || editFile == juce::File())
            return false;

        te::EditFileOperations ops (*edit);

        // A transient record-pass clip mute must never reach the file: lift it
        // for the write and reinstate it after (see withRecordPassMutesLifted).
        withRecordPassMutesLifted ([&] { ops.save (true, true, false); });

        if (! editFile.existsAsFile())
        {
            lastError = "Failed to save the session:\n" + editFile.getFullPathName();
            return false;
        }

        clearLastError();
        return true;
    }

    bool Session::saveAs (const juce::File& file)
    {
        if (edit == nullptr)
            return false;

        if (! file.getParentDirectory().createDirectory())
        {
            lastError = "Could not create the destination directory:\n" + file.getParentDirectory().getFullPathName();
            return false;
        }

        te::EditFileOperations ops (*edit);
        withRecordPassMutesLifted ([&] { ops.saveAs (file, true, nullptr); });

        if (! file.existsAsFile())
        {
            lastError = "Failed to save the session as:\n" + file.getFullPathName();
            return false;
        }

        // Retarget subsequent saves, temp versions (autosave/recovery) and
        // relative-path resolution at the new file. Without this, Tracktion keeps
        // writing back to the original path and recovery scans the wrong place.
        edit->editFileRetriever = [file] { return file; };

        editFile = file;
        audio.behaviour().setRecordingsDirectory (paths::recordingsDirectoryFor (file));
        sendChangeMessage();
        clearLastError();
        return true;
    }

    //==============================================================================
    void Session::close()
    {
        // Hardware monitoring is a *device-level* setting (WaveInputDevice), not
        // part of the Edit, so it would survive the teardown below and leave the
        // microphone live-monitored in the "No session" state (feedback risk).
        // Turn it off explicitly, before the early-out, so the Monitor button
        // also ends up OFF in the UI. This is a device teardown, not a user
        // preference change, so the remembered preference is left intact.
        applyMonitoringToDevices (false);

        // The selection is transient UI state tied to the Edit: clear it before
        // the early-out so a later open/new never sees stale region bounds.
        clearSelection();

        if (edit == nullptr)
            return;

        stopTimer();
        detachMeter();
        detachMeters();

        // Restore any clips muted for an in-progress record pass before the Edit
        // is torn down, so the saved/closed state never carries a stale mute.
        endRecordPassMutes();

        // Halt playback/recording before the Edit is torn down.
        edit->getTransport().stop (false, false);

        // Drop any autosave temp version *before* the Edit goes away: a
        // deliberately-closed project must leave no stale recovery artifact
        // behind (otherwise the next launch would prompt for a resolved session).
        // Scoped so EditFileOperations is destroyed before the Edit it references
        // (its destructor touches the Edit's engine/project manager).
        {
            te::EditFileOperations ops (*edit);
            ops.deleteTempVersion();
        }

        edit.reset();
        editFile = juce::File();
        inputsConfigured = false;
        masterGainDb = 0.0f;
        masterMuted = false;

        // The session is gone: clear the sentinel so a crash while empty does not
        // masquerade as an interrupted session.
        clearInterruptionMarker();

        clearLastError();
        sendChangeMessage();
    }

    bool Session::hasUnsavedChanges() const
    {
        return edit != nullptr && edit->hasChangedSinceSaved();
    }

    //==============================================================================
    void Session::setAutosaveIntervalSeconds (int seconds)
    {
        autosaveIntervalSeconds = juce::jmax (0, seconds);
    }

    //==============================================================================
    Session::RecoveryInfo Session::detectRecovery (const juce::File& file)
    {
        RecoveryInfo info;
        info.editFile = file;
        info.tempEditFile = paths::tempEditFileFor (file);
        info.hasTempEdit = info.tempEditFile.existsAsFile();
        info.uncleanShutdown = paths::lockFile().existsAsFile();

        if (info.uncleanShutdown)
        {
            const auto recordingsDir = paths::recordingsDirectoryFor (file);

            if (recordingsDir.isDirectory())
                info.candidateRecordings = recordingsDir.findChildFiles (juce::File::findFiles, false, "*.wav");
        }

        return info;
    }

    bool Session::applyTempEditRecovery (const RecoveryInfo& info)
    {
        if (! info.hasTempEdit || ! info.tempEditFile.existsAsFile())
            return false;

        juce::File target = info.editFile != juce::File() ? info.editFile : paths::defaultEditFile();
        target.getParentDirectory().createDirectory();

        if (target.existsAsFile())
        {
            const auto backup = target.getSiblingFile (target.getFileNameWithoutExtension()
                                                       + ".recovered-backup.tracktionedit");
            backup.deleteFile();
            target.copyFileTo (backup);
        }

        target.deleteFile();

        if (! info.tempEditFile.copyFileTo (target))
            return false;

        info.tempEditFile.deleteFile();
        return true;
    }

    juce::Array<juce::File> Session::findReferencedRecordings() const
    {
        juce::Array<juce::File> files;

        if (edit == nullptr)
            return files;

        for (auto* track : te::getAudioTracks (*edit))
        {
            for (auto* clip : track->getClips())
            {
                if (auto* wave = dynamic_cast<te::WaveAudioClip*> (clip))
                    files.addIfNotAlreadyThere (wave->getOriginalFile());
            }
        }

        return files;
    }

    int Session::importOrphanedRecordings()
    {
        if (edit == nullptr || editFile == juce::File())
            return 0;

        const auto recordingsDir = paths::recordingsDirectoryFor (editFile);

        if (! recordingsDir.isDirectory())
            return 0;

        auto* track = getTrack();

        if (track == nullptr)
            return 0;

        const auto referenced = findReferencedRecordings();
        int imported = 0;

        // `findChildFiles` returns filesystem order, which is not chronological.
        // Sort by modification time (file name as tie-breaker) so recovered takes
        // are laid down the timeline in the order they were recorded.
        auto orphanFiles = recordingsDir.findChildFiles (juce::File::findFiles, false, "*.wav");

        std::sort (orphanFiles.begin(), orphanFiles.end(),
                   [] (const juce::File& a, const juce::File& b)
                   {
                       const auto ta = a.getLastModificationTime();
                       const auto tb = b.getLastModificationTime();

                       return ta != tb ? ta < tb : a.getFileName() < b.getFileName();
                   });

        // Lay recovered takes down the timeline one after another so multiple
        // takes never overlap at time 0.
        te::TimePosition nextStart {};

        for (auto& file : orphanFiles)
        {
            if (referenced.contains (file))
                continue;

            te::AudioFile audioFile (edit->engine, file);

            if (! audioFile.isValid() || audioFile.getLength() <= 0.0)
            {
                // FR-REC-8 crash window: Tracktion flushes recorded audio roughly
                // every 6 s, so a crash inside the first window can leave a WAV
                // whose header claims 0 samples and which cannot be recovered.
                // Accepted for Epic 1 "basics"; hardened in Epic 7.
                continue;
            }

            const auto length = te::TimeDuration::fromSeconds (audioFile.getLength());
            const te::ClipPosition position { { nextStart, length }, {} };

            if (auto clip = track->insertWaveClip (file.getFileNameWithoutExtension(), file,
                                                   position, false))
            {
                clip->setName ("Recovered: " + file.getFileNameWithoutExtension());
                nextStart = nextStart + length;
                ++imported;
            }
        }

        if (imported > 0)
        {
            edit->restartPlayback();
            save();
            sendChangeMessage();
        }

        return imported;
    }

    //==============================================================================
    void Session::ensureMeterAttached()
    {
        if (edit == nullptr)
            return;

        if (meterAttached)
        {
            // The playback context (and thus the instance) may have been rebuilt.
            if (meterInstance.get() == nullptr)
                meterAttached = false;
            else
                return;
        }

        auto* waveIn = getSelectedWaveInputDevice();

        if (waveIn == nullptr)
            return;

        if (auto* instance = edit->getCurrentInstanceForInputDevice (waveIn))
        {
            instance->addConsumer (this);
            meterInstance = instance;
            meterAttached = true;
        }
    }

    void Session::detachMeter()
    {
        if (edit != nullptr)
            for (auto* instance : edit->getAllInputDevices())
                instance->removeConsumer (this);

        inputLevels.reset();
        meterAttached = false;
        meterInstance = nullptr;
    }

    void Session::acceptInputBuffer (choc::buffer::ChannelArrayView<float> buffer)
    {
        inputLevels.push (buffer);
    }

    //==============================================================================
    // Per-track / master metering (FR-MIX-3). Clients are attached on the message
    // thread; the engine updates them on the audio thread under a spin lock and
    // we read+clear them on the UI timer (never the audio thread).
    void Session::detachMeters()
    {
        for (auto& meter : trackMeters)
            if (meter != nullptr && meter->measurer != nullptr)
                meter->measurer->removeClient (meter->client);

        trackMeters.clear();
        metersTrackCount = -1;

        if (masterMeter != nullptr && masterMeter->measurer != nullptr)
            masterMeter->measurer->removeClient (masterMeter->client);

        masterMeter.reset();
    }

    void Session::attachMeters()
    {
        detachMeters();

        if (edit == nullptr)
            return;

        const auto tracks = te::getAudioTracks (*edit);
        trackMeters.reserve ((size_t) tracks.size());

        for (auto* track : tracks)
        {
            auto* meterPlugin = track->getLevelMeterPlugin();

            if (meterPlugin == nullptr)
            {
                trackMeters.push_back (nullptr);
                continue;
            }

            auto meter = std::make_unique<MeterClient>();
            meter->measurer = &meterPlugin->measurer;
            meterPlugin->measurer.setMode (te::LevelMeasurer::peakMode);
            // Pre-size the per-client storage on the message thread so the audio
            // thread never allocates on its first update.
            meter->client.setNumChannelsUsed (2);
            meterPlugin->measurer.addClient (meter->client);
            trackMeters.push_back (std::move (meter));
        }

        // Master: reuse an existing master meter plugin if present (it may have
        // been saved into the project), otherwise add one pass-through meter.
        te::LevelMeterPlugin* masterPlugin = nullptr;

        for (auto* plugin : edit->getMasterPluginList().getPlugins())
            if (auto* lm = dynamic_cast<te::LevelMeterPlugin*> (plugin))
            {
                masterPlugin = lm;
                break;
            }

        if (masterPlugin == nullptr)
        {
            auto created = edit->getMasterPluginList().insertPlugin (te::LevelMeterPlugin::create(), -1);
            masterPlugin = dynamic_cast<te::LevelMeterPlugin*> (created.get());
            edit->restartPlayback();
        }

        if (masterPlugin != nullptr)
        {
            masterMeter = std::make_unique<MeterClient>();
            masterMeter->measurer = &masterPlugin->measurer;
            masterPlugin->measurer.setMode (te::LevelMeasurer::peakMode);
            masterMeter->client.setNumChannelsUsed (2);
            masterPlugin->measurer.addClient (masterMeter->client);
            masterMeterPlugin = masterPlugin;
        }

        metersTrackCount = tracks.size();
    }

    void Session::refreshMetersIfNeeded()
    {
        if (edit == nullptr)
        {
            if (metersTrackCount != -1)
                detachMeters();

            return;
        }

        if ((int) te::getAudioTracks (*edit).size() != metersTrackCount)
            attachMeters();
    }

    Session::MeterReading Session::readTrackMeter (int trackIndex)
    {
        MeterReading reading;

        if (! juce::isPositiveAndBelow (trackIndex, (int) trackMeters.size())
             || trackMeters[(size_t) trackIndex] == nullptr)
            return reading;

        auto& meter = *trackMeters[(size_t) trackIndex];
        const auto channels = meter.client.getNumChannelsUsed();

        for (int ch = 0; ch < 2; ++ch)
            if (ch < channels)
                reading.peakDb[ch] = meter.client.getAndClearAudioLevel (ch).dB;

        reading.clipped = meter.client.getAndClearOverload();
        return reading;
    }

    Session::MeterReading Session::readMasterMeter()
    {
        MeterReading reading;

        if (masterMeter == nullptr)
            return reading;

        const auto channels = masterMeter->client.getNumChannelsUsed();

        for (int ch = 0; ch < 2; ++ch)
            if (ch < channels)
                reading.peakDb[ch] = masterMeter->client.getAndClearAudioLevel (ch).dB;

        reading.clipped = masterMeter->client.getAndClearOverload();
        return reading;
    }

    //==============================================================================
    void Session::timerCallback()
    {
        if (edit == nullptr)
            return;

        refreshMetersIfNeeded();

        // Input assignment can only complete once the engine has rebuilt its
        // (asynchronous) wave device list. Retry until it succeeds, then persist.
        if (! inputsConfigured)
        {
            if (configureTracks())
            {
                save();
                sendChangeMessage();
            }
        }
        else
        {
            // The device can (re)open or be rebuilt after the session was
            // configured, leaving the input routing at the hardware default (a
            // mono source would then be hard-left). Re-apply whenever the live
            // routing differs from the mapping's route for this device.
            bool changed = false;

            for (auto index : inputTrackIndices())
            {
                auto mapping = readTrackMapping (index);
                auto* waveIn = resolveInputDeviceFor (mapping);

                if (waveIn == nullptr)
                    continue;

                const auto hardwareChannels = juce::jmax (1, audio.getNumActiveInputChannels());

                if (waveIn->getChannels() != inputChannelConfigurationForMapping (mapping, hardwareChannels))
                {
                    applyInputChannelConfiguration (index, waveIn);
                    changed = true;
                }
            }

            if (changed)
            {
                edit->restartPlayback();
                save();
                sendChangeMessage();
            }
        }

        ensureMeterAttached();

        const bool playing = isPlaying();
        const bool recording = isRecording();
        const bool armed = isAnyTrackArmed();

        if (playing != lastPlaying || recording != lastRecording || armed != lastArmed)
        {
            lastPlaying = playing;
            lastRecording = recording;
            lastArmed = armed;
            sendChangeMessage();
        }

        if (autosaveIntervalSeconds > 0)
        {
            const auto now = juce::Time::getMillisecondCounterHiRes();

            if (now - lastAutosaveMs >= (double) autosaveIntervalSeconds * 1000.0)
            {
                lastAutosaveMs = now;

                if (edit->hasChangedSinceSaved())
                {
                    te::EditFileOperations ops (*edit);

                    // Autosave can fire during a record pass: lift the transient
                    // pass mutes so the recovered `.tmp_` edit has audible takes.
                    withRecordPassMutesLifted ([&] { ops.saveTempVersion (true); });
                }
            }
        }
    }

    //==============================================================================
    void Session::writeInterruptionMarker()
    {
        paths::appDataDirectory().createDirectory();
        paths::lockFile().replaceWithText ("raw-radio-studio session lock\n"
                                           + juce::Time::getCurrentTime().toString (true, true, true, true) + "\n");
    }

    void Session::clearInterruptionMarker()
    {
        paths::lockFile().deleteFile();
    }
}
