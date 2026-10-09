// raw-radio-studio — piano-roll MIDI editor (Epic 5, FR-MIDI-2).
//
// A themed overlay that views and edits the notes of one MIDI clip on one
// track. Notes can be added, moved (pitch + time), resized (length) and deleted;
// every edit is one undoable Session call, and playback goes through whatever
// instrument the track hosts (the transport just plays the clip).
//
// All coordinates are clip-relative: beats from the clip start on the x axis,
// MIDI pitch on the y axis. The beat<->pixel map uses the edit's tempo so the
// grid lines up with the arrangement.
//
// UI-only: reads/writes the Session on the message thread. Never touches the
// audio thread.

#pragma once

#include <JuceHeader.h>

#include <functional>
#include <vector>

#include "studio/Session.h"
#include "ui/BrandButton.h"

namespace rrs
{
    class PianoRollPanel final : public juce::Component,
                                 private juce::Timer,
                                 private juce::ChangeListener
    {
    public:
        explicit PianoRollPanel (Session&);
        ~PianoRollPanel() override;

        /** Called when the Close button is pressed. */
        std::function<void()> onClose;

        /** Loads the clip `clipIndex` on `trackIndex` and shows it. */
        void open (int trackIndex, int clipIndex);

        /** Re-reads the clip notes from the Session (call on changes / show). */
        void refresh();

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        //==============================================================================
        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        // Visible pitch range: C2 (36) .. B5 (83), four octaves.
        static constexpr int lowestNote  = 36;
        static constexpr int highestNote = 83;
        static constexpr int numPitches  = highestNote - lowestNote + 1;

        static constexpr int headerHeight = 34;
        static constexpr int keyboardWidth = 52;
        static constexpr double defaultNoteBeats = 0.5;
        static constexpr double gridStepBeats = 0.25;

        juce::Rectangle<int> gridArea() const;
        int rowHeight() const;
        int xForBeat (double beats) const;
        double beatForX (int x) const;
        int yForNote (int note) const;
        int noteForY (int y) const;
        double gridLengthBeats() const;
        /** The clip's actual length in beats (the grid may be padded to a minimum
            of 4 beats for usability, but notes may not pass the real clip end). */
        double clipLengthBeats() const;
        double beatsPerSecond() const;

        juce::Rectangle<int> noteRect (const Session::MidiNoteInfo&) const;
        int hitTestNote (juce::Point<int>) const;
        bool isOnResizeEdge (int note, juce::Point<int>) const;
        void addNoteAt (juce::Point<int>);
        void commitDrag();
        double snapBeat (double beats) const;

        Session& session;

        int trackIndex = -1;
        int clipIndex = -1;
        double clipStartSeconds = 0.0;
        double clipLengthSeconds = 0.0;
        juce::String clipName;

        std::vector<Session::MidiNoteInfo> notes;
        int selectedNote = -1;

        enum class DragMode { none, move, resize };
        DragMode dragMode = DragMode::none;
        int dragNote = -1;
        double dragPreviewStart = 0.0;
        double dragPreviewLength = 0.0;
        int dragPreviewPitch = 0;
        juce::Point<int> dragMouseDown;

        juce::Label titleLabel;
        juce::Label hintLabel;
        BrandButton closeButton { "Close" };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollPanel)
    };
}
