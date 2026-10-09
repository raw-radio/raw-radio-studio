// raw-radio-studio — piano-roll MIDI editor (Epic 5).

#include "PianoRollPanel.h"

#include "ui/BrandColours.h"
#include "ui/BrandFonts.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rrs
{
    namespace te = tracktion;

    PianoRollPanel::PianoRollPanel (Session& sessionRef)
        : session (sessionRef)
    {
        setWantsKeyboardFocus (true);

        titleLabel.setFont (brand::uiRegular (14.0f));
        titleLabel.setColour (juce::Label::textColourId, brand::textPrimary);
        titleLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (titleLabel);

        hintLabel.setFont (brand::monoRegular (11.0f));
        hintLabel.setColour (juce::Label::textColourId, brand::textTertiary);
        hintLabel.setJustificationType (juce::Justification::centredLeft);
        hintLabel.setText ("Click: add  -  drag body: move  -  drag right edge: length  -  Delete: remove",
                           juce::dontSendNotification);
        addAndMakeVisible (hintLabel);

        closeButton.setIconName ("x");
        closeButton.setIconOnly (true);
        closeButton.setTooltip ("Close the piano roll");
        closeButton.onClick = [this]
        {
            setVisible (false);

            if (onClose)
                onClose();
        };
        addAndMakeVisible (closeButton);

        session.addChangeListener (this);
        startTimerHz (15);
    }

    PianoRollPanel::~PianoRollPanel()
    {
        stopTimer();
        session.removeChangeListener (this);
    }

    void PianoRollPanel::open (int track, int clip)
    {
        trackIndex = track;
        clipIndex = clip;
        selectedNote = -1;
        dragMode = DragMode::none;

        Session::MidiClipInfo info;

        if (session.getMidiClipInfo (trackIndex, clipIndex, info))
        {
            clipStartSeconds = info.startSeconds;
            clipLengthSeconds = info.lengthSeconds;
            clipName = info.name;
        }
        else
        {
            clipStartSeconds = 0.0;
            clipLengthSeconds = 2.0;
            clipName = {};
        }

        refresh();
        setVisible (true);
        grabKeyboardFocus();
    }

    void PianoRollPanel::refresh()
    {
        notes = session.getMidiNotes (trackIndex, clipIndex);

        if (selectedNote >= (int) notes.size())
            selectedNote = -1;

        const auto trackName = session.getTrackName (trackIndex);
        titleLabel.setText ("Piano roll  -  " + trackName + "  /  "
                                + (clipName.isNotEmpty() ? clipName : juce::String ("MIDI clip")),
                            juce::dontSendNotification);

        repaint();
    }

    double PianoRollPanel::beatsPerSecond() const
    {
        if (auto* edit = session.getEdit())
        {
            const auto bps = edit->tempoSequence.getBeatsPerSecondAt (
                te::TimePosition::fromSeconds (clipStartSeconds));

            if (bps > 0.0)
                return bps;
        }

        return 2.0; // 120 bpm fallback
    }

    double PianoRollPanel::gridLengthBeats() const
    {
        return juce::jmax (4.0, clipLengthSeconds * beatsPerSecond());
    }

    double PianoRollPanel::clipLengthBeats() const
    {
        return juce::jmax (0.0, clipLengthSeconds * beatsPerSecond());
    }

    juce::Rectangle<int> PianoRollPanel::gridArea() const
    {
        auto area = getLocalBounds();
        area.removeFromTop (headerHeight);
        area.removeFromLeft (keyboardWidth);
        return area.reduced (2, 2);
    }

    int PianoRollPanel::rowHeight() const
    {
        return juce::jmax (1, gridArea().getHeight() / numPitches);
    }

    int PianoRollPanel::xForBeat (double beats) const
    {
        const auto grid = gridArea();
        const auto length = gridLengthBeats();

        if (grid.getWidth() <= 0 || length <= 0.0)
            return grid.getX();

        return grid.getX() + (int) std::lround ((beats / length) * (double) grid.getWidth());
    }

    double PianoRollPanel::beatForX (int x) const
    {
        const auto grid = gridArea();

        if (grid.getWidth() <= 0)
            return 0.0;

        return ((double) (x - grid.getX()) / (double) grid.getWidth()) * gridLengthBeats();
    }

    int PianoRollPanel::yForNote (int note) const
    {
        const auto grid = gridArea();
        const auto rh = rowHeight();
        return grid.getY() + (highestNote - note) * rh;
    }

    int PianoRollPanel::noteForY (int y) const
    {
        const auto grid = gridArea();
        const auto rh = rowHeight();

        if (rh <= 0)
            return 60;

        return juce::jlimit (lowestNote, highestNote, highestNote - (y - grid.getY()) / rh);
    }

    juce::Rectangle<int> PianoRollPanel::noteRect (const Session::MidiNoteInfo& note) const
    {
        const auto rh = rowHeight();
        const auto x1 = xForBeat (note.startBeats);
        const auto x2 = xForBeat (note.startBeats + juce::jmax (gridStepBeats, note.lengthBeats));
        const auto y = yForNote (note.noteNumber);

        return { x1, y + 1, juce::jmax (3, x2 - x1 - 1), juce::jmax (2, rh - 2) };
    }

    int PianoRollPanel::hitTestNote (juce::Point<int> position) const
    {
        for (int i = (int) notes.size(); --i >= 0;)
            if (noteRect (notes[(size_t) i]).contains (position))
                return i;

        return -1;
    }

    bool PianoRollPanel::isOnResizeEdge (int note, juce::Point<int> position) const
    {
        if (! juce::isPositiveAndBelow (note, (int) notes.size()))
            return false;

        const auto rect = noteRect (notes[(size_t) note]);
        constexpr int edge = 6;
        return position.x >= rect.getRight() - edge;
    }

    double PianoRollPanel::snapBeat (double beats) const
    {
        return std::round (beats / gridStepBeats) * gridStepBeats;
    }

    void PianoRollPanel::addNoteAt (juce::Point<int> position)
    {
        const auto clipBeats = clipLengthBeats();
        const auto pitch = noteForY (position.y);

        // Clamp so the new note (start + default length) cannot pass the clip end
        // (out-of-bounds notes never sound).
        auto beat = juce::jmax (0.0, snapBeat (beatForX (position.x)));

        if (clipBeats > 0.0)
            beat = juce::jmin (beat, juce::jmax (0.0, clipBeats - defaultNoteBeats));

        const auto index = session.addMidiNote (trackIndex, clipIndex, pitch, beat,
                                                defaultNoteBeats, 100);

        refresh();
        selectedNote = index;
        repaint();
    }

    void PianoRollPanel::commitDrag()
    {
        if (dragNote < 0)
            return;

        if (dragMode == DragMode::move)
            session.moveMidiNote (trackIndex, clipIndex, dragNote,
                                  dragPreviewPitch, dragPreviewStart);
        else if (dragMode == DragMode::resize)
            session.resizeMidiNote (trackIndex, clipIndex, dragNote, dragPreviewLength);

        dragMode = DragMode::none;
        dragNote = -1;
        refresh();
    }

    void PianoRollPanel::mouseDown (const juce::MouseEvent& e)
    {
        grabKeyboardFocus();

        if (! gridArea().contains (e.getPosition()))
            return;

        const auto hit = hitTestNote (e.getPosition());

        if (hit >= 0)
        {
            selectedNote = hit;
            dragNote = hit;
            dragPreviewPitch = notes[(size_t) hit].noteNumber;
            dragPreviewStart = notes[(size_t) hit].startBeats;
            dragPreviewLength = notes[(size_t) hit].lengthBeats;
            dragMouseDown = e.getPosition();
            dragMode = isOnResizeEdge (hit, e.getPosition()) ? DragMode::resize : DragMode::move;
            repaint();
            return;
        }

        addNoteAt (e.getPosition());
    }

    void PianoRollPanel::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragMode == DragMode::none || dragNote < 0)
            return;

        if (dragMode == DragMode::move)
        {
            const auto capture = notes[(size_t) dragNote];
            const auto deltaBeats = beatForX (e.getPosition().x) - beatForX (dragMouseDown.x);
            const auto deltaPitch = noteForY (e.getPosition().y) - noteForY (dragMouseDown.y);
            const auto clipBeats = clipLengthBeats();

            dragPreviewPitch = juce::jlimit (0, 127, capture.noteNumber + deltaPitch);

            // Keep the note body inside the clip: start in [0, clipBeats - length].
            auto start = juce::jmax (0.0, snapBeat (capture.startBeats + deltaBeats));

            if (clipBeats > 0.0)
                start = juce::jmin (start, juce::jmax (0.0, clipBeats - capture.lengthBeats));

            dragPreviewStart = start;
        }
        else if (dragMode == DragMode::resize)
        {
            const auto capture = notes[(size_t) dragNote];
            const auto targetEnd = snapBeat (beatForX (e.getPosition().x));
            const auto clipBeats = clipLengthBeats();

            // Clamp the length so start + length cannot pass the clip end.
            const auto maxLength = clipBeats > 0.0
                                       ? juce::jmax (gridStepBeats, clipBeats - capture.startBeats)
                                       : std::numeric_limits<double>::max();

            dragPreviewLength = juce::jlimit (gridStepBeats, maxLength,
                                              targetEnd - capture.startBeats);
        }

        repaint();
    }

    void PianoRollPanel::mouseUp (const juce::MouseEvent&)
    {
        if (dragMode != DragMode::none)
            commitDrag();
    }

    bool PianoRollPanel::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            if (juce::isPositiveAndBelow (selectedNote, (int) notes.size()))
            {
                session.removeMidiNote (trackIndex, clipIndex, selectedNote);
                selectedNote = -1;
                refresh();
            }

            return true;
        }

        // Pitch nudge with the arrow keys (no modifier = semitone, Shift = octave).
        if (juce::isPositiveAndBelow (selectedNote, (int) notes.size()))
        {
            const int step = key.getModifiers().isShiftDown() ? 12 : 1;

            if (key.getKeyCode() == juce::KeyPress::upKey || key.getKeyCode() == juce::KeyPress::downKey)
            {
                const auto delta = (key.getKeyCode() == juce::KeyPress::upKey) ? step : -step;
                const auto capture = notes[(size_t) selectedNote];
                session.moveMidiNote (trackIndex, clipIndex, selectedNote,
                                      juce::jlimit (0, 127, capture.noteNumber + delta),
                                      capture.startBeats);
                refresh();
                return true;
            }
        }

        return false;
    }

    void PianoRollPanel::timerCallback()
    {
        if (isVisible() && session.isPlaying())
            repaint();
    }

    void PianoRollPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        // The Session persists/clips changed: re-read (e.g. a recording landed).
        if (isVisible())
            refresh();
    }

    void PianoRollPanel::resized()
    {
        auto header = getLocalBounds().removeFromTop (headerHeight).reduced (8, 4);

        closeButton.setBounds (header.removeFromRight (header.getHeight()));
        header.removeFromRight (8);
        titleLabel.setBounds (header.removeFromLeft (juce::jmax (80, header.getWidth() / 2)));
        hintLabel.setBounds (header);
    }

    void PianoRollPanel::paint (juce::Graphics& g)
    {
        g.fillAll (brand::bgPanel);

        auto header = getLocalBounds().removeFromTop (headerHeight);
        g.setColour (brand::bgTertiary);
        g.fillRect (header);

        const auto grid = gridArea();
        const auto rh = rowHeight();

        // Keyboard gutter + pitch rows (black/white key stripes).
        for (int note = lowestNote; note <= highestNote; ++note)
        {
            const auto y = yForNote (note);
            const bool isBlack = juce::MidiMessage::isMidiNoteBlack (note);

            g.setColour (isBlack ? brand::bgWindow : brand::bgTertiary);
            g.fillRect (0, y, keyboardWidth, rh);

            g.setColour (brand::border.withAlpha (0.6f));
            g.drawLine (0.0f, (float) (y + rh), (float) keyboardWidth, (float) (y + rh));

            // Row background: slightly alternate and highlight octave C rows.
            g.setColour (note % 12 == 0 ? brand::laneArmed : (isBlack ? brand::lane : brand::laneAlt));
            g.fillRect (grid.getX(), y, grid.getWidth(), rh);

            if (note % 12 == 0)
            {
                g.setColour (brand::textTertiary);
                g.setFont (brand::monoRegular (9.0f));
                g.drawText (juce::MidiMessage::getMidiNoteName (note, true, true, 3),
                            juce::Rectangle<int> (0, y, keyboardWidth - 4, rh),
                            juce::Justification::centredRight);
            }
        }

        // Beat grid lines.
        const auto length = gridLengthBeats();
        g.setColour (brand::border.withAlpha (0.5f));

        for (double beat = 0.0; beat <= length + 1.0e-6; beat += gridStepBeats)
        {
            const auto x = xForBeat (beat);
            const bool wholeBeat = std::abs (beat - std::round (beat)) < 1.0e-6;
            const bool bar = wholeBeat && std::fmod (std::round (beat), 4.0) == 0.0;

            g.setColour (brand::border.withAlpha (bar ? 0.9f : (wholeBeat ? 0.55f : 0.25f)));
            g.drawLine ((float) x, (float) grid.getY(), (float) x, (float) grid.getBottom());
        }

        // Notes.
        for (int i = 0; i < (int) notes.size(); ++i)
        {
            auto info = notes[(size_t) i];

            if (i == dragNote && dragMode == DragMode::move)
            {
                info.noteNumber = dragPreviewPitch;
                info.startBeats = dragPreviewStart;
            }
            else if (i == dragNote && dragMode == DragMode::resize)
            {
                info.lengthBeats = dragPreviewLength;
            }

            const auto rect = noteRect (info).toFloat();
            const auto shade = juce::jmap ((float) juce::jlimit (30, 127, info.velocity) / 127.0f,
                                           0.0f, 1.0f, 0.55f, 1.0f);

            g.setColour (brand::accent.withMultipliedBrightness (shade));
            g.fillRoundedRectangle (rect, 2.0f);

            if (i == selectedNote)
            {
                g.setColour (brand::textPrimary);
                g.drawRoundedRectangle (rect.reduced (0.5f), 2.0f, 1.5f);
            }
        }

        // Playhead (only while the transport is inside this clip).
        if (session.isPlaying())
        {
            const auto beats = (session.getPositionSeconds() - clipStartSeconds) * beatsPerSecond();

            if (beats >= 0.0 && beats <= length)
            {
                const auto x = xForBeat (beats);
                g.setColour (brand::record);
                g.drawLine ((float) x, (float) grid.getY(), (float) x, (float) grid.getBottom(), 1.5f);
            }
        }

        g.setColour (brand::border);
        g.drawRect (grid, 1);
    }
}
