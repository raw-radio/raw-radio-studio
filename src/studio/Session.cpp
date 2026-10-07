// raw-radio-studio — session model (Epic 1 walking skeleton + Epic 2 multitrack).

#include "Session.h"

#include "AppPaths.h"
#include "AudioImport.h"
#include "InputRouting.h"

#include <algorithm>
#include <cmath>
#include <functional>
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
        auto file = requestedFile != juce::File() ? requestedFile : paths::defaultEditFile();

        if (! file.getParentDirectory().createDirectory())
        {
            lastError = "Could not create the sessions directory:\n" + file.getParentDirectory().getFullPathName();
            return false;
        }

        if (! createOrOpenEdit (file, false))
            return false;

        writeInterruptionMarker();
        configureTracks();
        save();
        sendChangeMessage();
        return true;
    }

    bool Session::open (const juce::File& file)
    {
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

    bool Session::setTrackInputMapping (int trackIndex, const InputMapping& mapping)
    {
        auto* track = getTrack (trackIndex);

        if (edit == nullptr || track == nullptr)
        {
            lastError = "No such track.";
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
        waveIn->setMonitorMode (te::InputDevice::MonitorMode::on);

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

        const auto ordinal = inputTrackIndices().size();

        // Going multitrack: a lone Epic 1 record track maps the whole device
        // (Auto -> stereo); once a second track is added it must become an
        // explicit single hardware channel so the tracks do not overlap.
        if (ordinal >= 1)
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

        auto track = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr, true);

        if (track == nullptr)
        {
            lastError = "Could not create the audio track.";
            return -1;
        }

        InputMapping mapping;
        mapping.firstChannel = ordinal;
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

    bool Session::setMonitoringEnabled (bool shouldMonitor)
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

        sendChangeMessage();
        return ! devices.empty();
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

    void Session::stop()
    {
        if (edit != nullptr)
            edit->getTransport().stop (false, false);
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

        edit->getTransport().record (false);
        return true;
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
        return track != nullptr && track->isMuted (true);
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

        if (auto volume = edit->getMasterVolumePlugin())
        {
            volume->setVolumeDb (juce::jlimit (-100.0f, 12.0f, db));
            sendChangeMessage();
            return true;
        }

        return false;
    }

    float Session::getMasterGainDb() const
    {
        if (edit != nullptr)
            if (auto volume = edit->getMasterVolumePlugin())
                return volume->getVolumeDb();

        return 0.0f;
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
        if (edit != nullptr)
        {
            edit->setCountInMode (mode);
            sendChangeMessage();
        }
    }

    tracktion::Edit::CountIn Session::getCountInMode() const
    {
        return edit != nullptr ? edit->getCountInMode() : te::Edit::CountIn::none;
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
        ops.save (true, true, false);

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
        ops.saveAs (file, true, nullptr);

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
        // also ends up OFF in the UI.
        setMonitoringEnabled (false);

        if (edit == nullptr)
            return;

        stopTimer();
        detachMeter();
        detachMeters();

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
                    ops.saveTempVersion (true);
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
