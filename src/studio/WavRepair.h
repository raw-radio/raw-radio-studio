// raw-radio-studio — crash-safe WAV header repair (Epic 7 hardening, FR-REC-8).
//
// Tracktion encodes takes through a JUCE `WavAudioFormatWriter`, which writes a
// *placeholder* RIFF/`data` header at creation and only rewrites it on `flush()`.
// Tracktion flushes recorded audio roughly every 6 s (`numSamplesPerFlush =
// 48000 * 6` in `tracktion_AudioFile.cpp`), so a crash inside that first window
// leaves a WAV whose header still declares **0 samples** even though the sample
// data is already on disk (JUCE's `FileOutputStream` pushes it out in ~32 KB
// chunks, i.e. every ~0.1 s). Such a file reads back as `lengthInSamples == 0`
// and used to be silently discarded by recovery.
//
// This utility recomputes the RIFF and `data` chunk sizes from the file's real
// byte length, so a partially written take becomes a valid WAV that the engine
// can read back. It is pure file I/O (never called on the audio thread) and is
// used both by the recovery scan and by the in-progress recording header
// finaliser (see `Session::finaliseInProgressRecordingHeaders`).

#pragma once

#include <juce_core/juce_core.h>

namespace rrs::wavrepair
{
    struct Result
    {
        enum class Status
        {
            notRiff,    ///< Not a RIFF/WAVE container (or unreadable) — left untouched.
            consistent, ///< Header already matches the on-disk payload — no change.
            repaired,   ///< Header sizes were rewritten from the real file length.
            failed      ///< A RIFF/WAVE file with no usable `data` chunk header.
        };

        Status status = Status::notRiff;

        /** Bytes of sample payload found after the `data` chunk header (may be 0). */
        juce::int64 dataBytes = 0;

        /** Sample frames derived from the chunk's block-align (0 when unknown). */
        juce::int64 sampleFrames = 0;

        /** Human-readable explanation, suitable for a log line or status dialog. */
        juce::String detail;

        bool wasRepaired() const noexcept { return status == Status::repaired; }
        bool isUsable()    const noexcept { return status == Status::repaired || status == Status::consistent; }
    };

    /** Recomputes the RIFF + `data` chunk sizes of `file` from its actual length
        when they disagree (a crash before the first Tracktion flush). Writes only
        the two 32-bit size fields in place; never truncates or extends the file.
        A file that is already consistent, or is not RIFF/WAVE, is left as-is. */
    Result repairTruncatedHeader (const juce::File& file);
}
