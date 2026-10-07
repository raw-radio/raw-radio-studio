// raw-radio-studio — minimal audio import (Epic 1 backing-track support).

#include "AudioImport.h"

namespace rrs
{
    namespace te = tracktion;

    AudioImport::Result AudioImport::import (te::Edit& edit, const juce::File& sourceFile)
    {
        Result result;

        if (! sourceFile.existsAsFile())
        {
            result.error = "The file does not exist:\n" + sourceFile.getFullPathName();
            return result;
        }

        auto& formats = edit.engine.getAudioFileFormatManager();

        if (! formats.canOpen (sourceFile))
        {
            result.error = "Unsupported audio format: " + sourceFile.getFileName()
                         + "\nSupported formats: WAV, AIFF, FLAC, MP3, OGG.";
            return result;
        }

        // Reject files the format manager cannot actually decode (e.g. a
        // truncated/mislabelled file still claiming a supported extension).
        te::AudioFile audioFile (edit.engine, sourceFile);

        if (! audioFile.isValid() || audioFile.getLength() <= 0.0)
        {
            result.error = "Could not read any audio from:\n" + sourceFile.getFullPathName();
            return result;
        }

        auto name = sourceFile.getFileNameWithoutExtension();

        // Each import gets its own track so it never disturbs the armed record
        // track. `addDefaultPlugins = true` matches Tracktion's normal new-track
        // setup (volume/level metering) so the imported track is audible.
        auto track = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit),
                                               nullptr, true);

        if (track == nullptr)
        {
            result.error = "Could not create a track for the imported file.";
            return result;
        }

        track->setName (name);

        const auto length = te::TimeDuration::fromSeconds (audioFile.getLength());
        const te::ClipPosition position { { te::TimePosition(), length }, {} };

        // `insertWaveClip` references the file via a SourceFileReference; for a
        // file-based (`.tracktionedit`) project it stores a relative path when
        // sensible, otherwise an absolute one — never a copy.
        auto clip = track->insertWaveClip (name, sourceFile, position, false);

        if (clip == nullptr)
        {
            edit.deleteTrack (track.get());
            result.error = "Could not insert the audio clip.";
            return result;
        }

        clip->setName (name);

        result.success = true;
        result.track = track.get();
        result.clip = clip.get();
        return result;
    }
}
