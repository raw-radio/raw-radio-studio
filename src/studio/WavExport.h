// raw-radio-studio — WAV export (Epic 1, FR-EXP-1).
//
// Renders the session to a **24-bit WAV at the session sample rate** using the
// Tracktion `EditRenderer` (asynchronous, faster than real time). When no audio
// device is open at export time, the Epic 1 default session rate (48 kHz,
// NFR-A-4) is used instead of Tracktion's 44.1 kHz no-device placeholder.

#pragma once

#include <JuceHeader.h>

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
#include <functional>
#include <memory>

namespace rrs
{
    class WavExport
    {
    public:
        /** Export bit depth fixed by the spec (FR-EXP-1 / NFR-A-1). */
        static constexpr int bitDepth = 24;

        using CompletionCallback = std::function<void (bool success, juce::File file, juce::String error)>;

        /** Handle to an in-flight (or just-finished) export.

            Owns the Tracktion renderer handle and a liveness token shared with
            any deferred completion work. Cancelling the handle — including
            implicitly, from its destructor — invalidates that token, so a
            metronome restore queued on the message thread becomes a no-op once
            the owning component has been torn down. Without it, a deferred
            restore could re-disable the click *after* the component had already
            restored it (close-during-export). */
        class Handle
        {
        public:
            Handle (std::shared_ptr<tracktion::EditRenderer::Handle> rendererHandle,
                    std::shared_ptr<std::atomic<bool>> livenessToken)
                : renderer (std::move (rendererHandle)),
                  alive (std::move (livenessToken))
            {
            }

            ~Handle()
            {
                cancel();
            }

            /** Cancels the render and invalidates any pending deferred work. */
            void cancel() noexcept
            {
                if (alive != nullptr)
                    alive->store (false, std::memory_order_release);

                if (renderer != nullptr)
                    renderer->cancel();
            }

            /** Render progress in [0, 1]. */
            float getProgress() const
            {
                return renderer != nullptr ? renderer->getProgress() : 0.0f;
            }

        private:
            std::shared_ptr<tracktion::EditRenderer::Handle> renderer;
            std::shared_ptr<std::atomic<bool>> alive;

            JUCE_DECLARE_NON_COPYABLE (Handle)
        };

        /** Starts an asynchronous export. The callback is always invoked on the
            message thread. Returns a handle that must be kept alive for the
            duration of the render; cancelling or destroying it invalidates any
            pending deferred completion work and cancels the render (the callback
            may then be skipped).

            FR-EXP-1: the built-in metronome/click is a real node in the render
            graph, so an enabled click would be baked into the exported WAV. When
            `suppressMetronome` is true (the default) the click is forced off for
            the duration of the render and its previous state is restored in every
            completion path (success, failure, or render-start failure). Pass
            false only to render the click on purpose (used by tests). */
        static std::shared_ptr<Handle>
            start (tracktion::Edit& edit, const juce::File& destination, CompletionCallback,
                   bool suppressMetronome = true);

        /** Suggested default export path for a session file. */
        static juce::File defaultDestinationFor (const juce::File& editFile);
    };
}
