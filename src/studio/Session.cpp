// raw-radio-studio — session model (Epic 1).

#include "Session.h"

#include "AppPaths.h"

#include <cmath>

namespace rrs
{
    namespace te = tracktion;

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

        if (edit != nullptr)
        {
            // Best-effort autosave so an accidental quit still leaves a recoverable
            // temp version (the lock file is removed, so this is not flagged unclean).
            te::EditFileOperations ops (*edit);
            ops.saveTempVersion (false);

            edit.reset();
        }

        removeLockFile();
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

        if (! createOrOpenEdit (file))
            return false;

        writeLockFile();
        configureSingleStereoTrack();
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

        if (! createOrOpenEdit (file))
            return false;

        writeLockFile();
        configureSingleStereoTrack();
        sendChangeMessage();
        return true;
    }

    bool Session::createOrOpenEdit (const juce::File& file)
    {
        detachMeter();
        edit.reset();

        auto& engine = audio.engine();

        if (file.existsAsFile())
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
        clearLastError();
        return true;
    }

    juce::String Session::getSessionName() const
    {
        return editFile != juce::File() ? editFile.getFileNameWithoutExtension()
                                        : juce::String ("No session");
    }

    //==============================================================================
    tracktion::AudioTrack* Session::getTrack() const
    {
        if (edit == nullptr)
            return nullptr;

        const auto tracks = te::getAudioTracks (*edit);
        return tracks.isEmpty() ? nullptr : tracks[0];
    }

    bool Session::configureSingleStereoTrack()
    {
        if (edit == nullptr)
            return false;

        edit->ensureNumberOfAudioTracks (1);

        auto* track = getTrack();

        if (track == nullptr)
        {
            lastError = "Could not create the audio track.";
            return false;
        }

        track->setName ("Input 1 (Stereo)");

        // The wave device list is rebuilt asynchronously by the engine on
        // startup / device change; until it exists we simply retry (see
        // timerCallback). This is not an error state.
        auto* waveIn = getSelectedWaveInputDevice();

        if (waveIn == nullptr)
            return false;

        waveIn->setEnabled (true);
        waveIn->setChannelConfiguration (te::ChannelConfiguration::stereo());
        waveIn->setMonitorMode (te::InputDevice::MonitorMode::on);

        edit->getTransport().ensureContextAllocated();

        bool assigned = false;
        for (auto* instance : edit->getAllInputDevices())
        {
            if (&instance->getInputDevice() == waveIn)
            {
                if (auto result = instance->setTarget (track->itemID, true, &edit->getUndoManager(), 0))
                    assigned = true;
                else
                    lastError = result.error();
            }
        }

        if (! assigned)
            return false;

        edit->restartPlayback();
        ensureMeterAttached();
        inputsConfigured = true;
        clearLastError();
        return true;
    }

    //==============================================================================
    void Session::reconfigureInputs()
    {
        inputsConfigured = false;

        if (configureSingleStereoTrack())
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
    bool Session::setTrackArmed (bool shouldBeArmed)
    {
        auto* track = getTrack();

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

    bool Session::isTrackArmed() const
    {
        auto* track = getTrack();

        if (edit == nullptr || track == nullptr)
            return false;

        for (auto* instance : edit->getAllInputDevices())
            if (te::isOnTargetTrack (*instance, *track, 0))
                if (instance->isRecordingEnabled (track->itemID))
                    return true;

        return false;
    }

    bool Session::setMonitoringEnabled (bool shouldMonitor)
    {
        if (auto* waveIn = getSelectedWaveInputDevice())
        {
            waveIn->setMonitorMode (shouldMonitor ? te::InputDevice::MonitorMode::on
                                                   : te::InputDevice::MonitorMode::off);
            sendChangeMessage();
            return true;
        }

        return false;
    }

    bool Session::isMonitoringEnabled() const
    {
        if (auto* waveIn = getSelectedWaveInputDevice())
            return waveIn->getMonitorMode() != te::InputDevice::MonitorMode::off;

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

        if (! isTrackArmed())
        {
            lastError = "Arm the track before recording.";
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

        editFile = file;
        audio.behaviour().setRecordingsDirectory (paths::recordingsDirectoryFor (file));
        sendChangeMessage();
        clearLastError();
        return true;
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

        for (auto& file : recordingsDir.findChildFiles (juce::File::findFiles, false, "*.wav"))
        {
            if (referenced.contains (file))
                continue;

            te::AudioFile audioFile (edit->engine, file);

            if (! audioFile.isValid() || audioFile.getLength() <= 0.0)
                continue;

            if (auto clip = track->insertWaveClip (file.getFileNameWithoutExtension(), file,
                                                   { { {}, te::TimeDuration::fromSeconds (audioFile.getLength()) }, {} },
                                                   false))
            {
                clip->setName ("Recovered: " + file.getFileNameWithoutExtension());
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
    void Session::timerCallback()
    {
        if (edit == nullptr)
            return;

        // Input assignment can only complete once the engine has rebuilt its
        // (asynchronous) wave device list. Retry until it succeeds, then persist.
        if (! inputsConfigured)
        {
            if (configureSingleStereoTrack())
            {
                save();
                sendChangeMessage();
            }
        }

        ensureMeterAttached();

        const bool playing = isPlaying();
        const bool recording = isRecording();
        const bool armed = isTrackArmed();

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
    void Session::writeLockFile()
    {
        paths::appDataDirectory().createDirectory();
        paths::lockFile().replaceWithText ("raw-radio-studio session lock\n"
                                           + juce::Time::getCurrentTime().toString (true, true, true, true) + "\n");
    }

    void Session::removeLockFile()
    {
        paths::lockFile().deleteFile();
    }
}
