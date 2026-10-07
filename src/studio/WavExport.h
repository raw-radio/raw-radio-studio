// raw-radio-studio — WAV export (Epic 1, FR-EXP-1).
//
// Renders the session to a **24-bit WAV at the session sample rate** using the
// Tracktion `EditRenderer` (asynchronous, faster than real time). When no audio
// device is open at export time, the Epic 1 default session rate (48 kHz,
// NFR-A-4) is used instead of Tracktion's 44.1 kHz no-device placeholder.

#pragma once

#include <JuceHeader.h>

#include <tracktion_engine/tracktion_engine.h>

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

        /** Starts an asynchronous export. The callback is always invoked on the
            message thread. Returns the render handle (may be null if the render
            could not be started, in which case the callback is still invoked).

            FR-EXP-1: the built-in metronome/click is a real node in the render
            graph, so an enabled click would be baked into the exported WAV. When
            `suppressMetronome` is true (the default) the click is forced off for
            the duration of the render and its previous state is restored in every
            completion path (success, failure, or render-start failure). Pass
            false only to render the click on purpose (used by tests). */
        static std::shared_ptr<tracktion::EditRenderer::Handle>
            start (tracktion::Edit& edit, const juce::File& destination, CompletionCallback,
                   bool suppressMetronome = true);

        /** Suggested default export path for a session file. */
        static juce::File defaultDestinationFor (const juce::File& editFile);
    };
}
