// raw-radio-studio — minimal audio import (Epic 1 backing-track support).
//
// The studio workflow is "play a backing track (minus) from the DAW, record a
// voice take". This is the smallest possible import: an audio file is inserted
// as a `WaveAudioClip` on its own new audio track, referenced in place (the
// source file is never copied or modified). Tracktion resolves/reloads the
// reference on save/open of the native `.tracktionedit` project.
//
// Formats follow whatever the engine's `AudioFileFormatManager` supports out of
// the box: WAV, AIFF, FLAC, MP3, OGG (plus the platform native format).

#pragma once

#include <JuceHeader.h>

#include <tracktion_engine/tracktion_engine.h>

namespace rrs
{
    class AudioImport
    {
    public:
        /** File-chooser wildcard matching the engine's default readable formats. */
        static constexpr const char* fileWildcard = "*.wav;*.aiff;*.aif;*.flac;*.mp3;*.ogg";

        struct Result
        {
            bool success = false;
            juce::String error;
            tracktion::AudioTrack* track = nullptr;    ///< Owned by the Edit.
            tracktion::WaveAudioClip* clip = nullptr;  ///< Owned by the Edit.
        };

        /** Validates the file and inserts it as a clip on a new audio track at the
            end of the edit, laid down at time 0. The clip references `sourceFile`
            non-destructively.

            Performs no save and no transport restart — the caller owns those
            edit-level actions (see `Session::importAudioFile`). Call on the message
            thread only; never from the audio callback. */
        static Result import (tracktion::Edit&, const juce::File& sourceFile);
    };
}
