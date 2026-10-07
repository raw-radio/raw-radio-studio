// raw-radio-studio — WAV export (Epic 1, FR-EXP-1).
//
// Renders the session to a **24-bit WAV at the session sample rate** using the
// Tracktion `EditRenderer` (asynchronous, faster than real time).

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
            could not be started, in which case the callback is still invoked). */
        static std::shared_ptr<tracktion::EditRenderer::Handle>
            start (tracktion::Edit& edit, const juce::File& destination, CompletionCallback);

        /** Suggested default export path for a session file. */
        static juce::File defaultDestinationFor (const juce::File& editFile);
    };
}
