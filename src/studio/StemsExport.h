// raw-radio-studio — per-track stems + master export (Epic 3, FR-EXP-2/3).
//
// Renders one 24-bit WAV per stem track (the track's own output, including its
// plugin chain but not the master chain) plus a `<name> master.wav` full mix,
// into a directory. Uses Tracktion's `RenderSpecification` + `RenderQueue` so
// the offline renders run faster than real time and tracks that feed a stem
// (aux sends, sidechains) are handled by the engine.
//
// Bit depth and sample rate follow the WavExport convention (FR-EXP-1 /
// NFR-A-1/4): 24-bit at the open device's rate, or the 48 kHz Epic 1 default
// when no device is open.
//
// All renders run on background threads; completion fires on the message
// thread. Nothing here touches the audio thread.

#pragma once

#include <JuceHeader.h>

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
#include <functional>
#include <memory>

namespace rrs
{
    class StemsExport
    {
    public:
        /** Export bit depth fixed by the spec (FR-EXP-1 / NFR-A-1). */
        static constexpr int bitDepth = 24;

        using CompletionCallback = std::function<void (bool success, juce::File directory,
                                                       int numFiles, juce::String error)>;

        /** Owns a batch of stem renders. Cancelling or destroying it cancels the
            remaining jobs; completion work is then skipped. */
        class Handle
        {
        public:
            Handle (std::shared_ptr<tracktion::RenderQueue>, std::shared_ptr<std::atomic<bool>> alive);
            ~Handle();

            void cancel() noexcept;

            /** Overall progress in [0, 1]. */
            float getProgress() const;

        private:
            std::shared_ptr<tracktion::RenderQueue> queue;
            std::shared_ptr<std::atomic<bool>> alive;

            JUCE_DECLARE_NON_COPYABLE (Handle)
        };

        /** Starts the batch: one stem per track that has clips, plus (when
            `includeMaster` is set) the full master mix. The callback fires on
            the message thread. Returns a handle that must outlive the batch. */
        static std::shared_ptr<Handle> start (tracktion::Edit&, const juce::File& directory,
                                              CompletionCallback, bool includeMaster = true);

        /** Suggested default stems directory for a session file. */
        static juce::File defaultDirectoryFor (const juce::File& editFile);
    };
}
