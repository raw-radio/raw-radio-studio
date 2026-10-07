// raw-radio-studio — basic clickable timeline + playhead (owner request).
//
// A thin ruler strip above the arrangement lanes. It draws the current
// transport position as a playhead line and seeks when the engineer clicks or
// drags anywhere on it (`TransportControl::setPosition`). This is deliberately
// minimal — no clip editing yet (that is Epic 4); it only positions the
// playhead.
//
// The x<->time mapping is shared with MainComponent (which draws the playhead
// down through the track lanes) via the static helpers, so the ruler and the
// lanes can never disagree about where a given time sits.
//
// UI-only: reads the position/length from the Session on its own timer and
// seeks on the message thread. Never touches the audio thread.

#pragma once

#include <JuceHeader.h>

#include "studio/Session.h"

namespace rrs
{
    class Timeline final : public juce::Component,
                           private juce::Timer
    {
    public:
        /** Fixed strip height so MainComponent can reserve the space. */
        static constexpr int preferredHeight = 28;

        /** Horizontal inset so the 0 s and end-of-session markers are not
            clipped against the strip edges. */
        static constexpr int hInset = 8;

        explicit Timeline (Session&);
        ~Timeline() override;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        /** X at which `seconds` sits inside `area`, for an arrangement of
            `length` seconds. Clamped to the strip. Shared with MainComponent so
            the lane playhead aligns with the ruler exactly. */
        static int xForSeconds (double seconds, double length, juce::Rectangle<int> area) noexcept;

        /** Inverse of xForSeconds: the time under an x position. */
        static double secondsForX (int x, double length, juce::Rectangle<int> area) noexcept;

    private:
        void timerCallback() override;
        void seekFromX (int x);

        /** A "nice" ruler tick spacing (seconds) that keeps labels legible for
            the available width. */
        static double chooseTickStep (double length, int widthPx) noexcept;

        static juce::String formatRulerTime (double seconds);

        Session& session;
        bool scrubbing = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Timeline)
    };
}
