// raw-radio-studio — routing + software cue mixes panel (Epic 3).

#include "RoutingPanel.h"

#include "ui/BrandColours.h"
#include "ui/BrandFonts.h"

namespace rrs
{
    namespace te = tracktion;

    namespace
    {
        // Output combo: id 1 = default output, ids 2.. = wave output devices.
        constexpr int defaultOutputItemId = 1;
    }

    RoutingPanel::RoutingPanel (Session& sessionRef)
        : session (sessionRef)
    {
        for (auto* c : { &trackBox, &outputBox, &submixBox, &cueBox, &cueOutputBox })
            addAndMakeVisible (*c);

        for (auto* b : { &addSubmixButton, &assignSubmixButton, &unassignSubmixButton,
                         &addCueButton, &removeCueButton, &closeButton })
            addAndMakeVisible (*b);

        addAndMakeVisible (sendToggle);
        addAndMakeVisible (sendSlider);
        addAndMakeVisible (sendLabel);
        addAndMakeVisible (statusLabel);

        trackBox.setTooltip ("Track whose output/send is being edited");
        outputBox.setTooltip ("Hardware output pair this track plays to");
        submixBox.setTooltip ("Submix folder (bus/group) to assign the track to");
        cueBox.setTooltip ("Software cue mix (per-performer monitor mix)");
        cueOutputBox.setTooltip ("Hardware output pair the cue is sent to");

        sendToggle.setTooltip ("Enable the selected track's send into the cue");
        sendSlider.setRange (-100.0, 12.0, 0.5);
        sendSlider.setTextValueSuffix (" dB");
        sendSlider.setTooltip ("Cue send level (post-fader)");
        sendSlider.setColour (juce::Slider::textBoxTextColourId, brand::textPrimary);
        sendSlider.setColour (juce::Slider::textBoxOutlineColourId, brand::border);

        sendLabel.setFont (brand::uiRegular (12.0f));
        sendLabel.setColour (juce::Label::textColourId, brand::textSecondary);
        sendLabel.setJustificationType (juce::Justification::centredRight);
        sendLabel.setText ("Send", juce::dontSendNotification);

        statusLabel.setFont (brand::uiRegular (12.0f));
        statusLabel.setColour (juce::Label::textColourId, brand::textSecondary);
        statusLabel.setJustificationType (juce::Justification::centredLeft);

        trackBox.onChange = [this] { refreshControlsFromSelection(); updateStatus(); };
        outputBox.onChange = [this] { applyTrackOutput(); };
        cueBox.onChange = [this] { refreshControlsFromSelection(); updateStatus(); };
        cueOutputBox.onChange = [this] { applyCueOutput(); };
        sendToggle.onClick = [this] { applySend(); };
        sendSlider.onDragEnd = [this] { applySend(); };

        addSubmixButton.onClick = [this]
        {
            session.createSubmixFolder ({});
            rebuildSubmixList();
            updateStatus();
        };
        assignSubmixButton.onClick = [this]
        {
            const auto track = selectedTrackIndex();
            const auto folder = submixBox.getSelectedId() - 1;

            if (track >= 0 && folder >= 0)
                session.addTrackToSubmix (track, folder);

            updateStatus();
        };
        unassignSubmixButton.onClick = [this]
        {
            const auto track = selectedTrackIndex();

            if (track >= 0)
                session.removeTrackFromSubmix (track);

            rebuildSubmixList();
            updateStatus();
        };
        addCueButton.onClick = [this]
        {
            session.createCueMix ({});

            rebuildCueList();

            if (session.getNumCueMixes() > 0)
                cueBox.setSelectedId (session.getNumCueMixes(), juce::dontSendNotification);

            refreshControlsFromSelection();
            updateStatus();
        };
        removeCueButton.onClick = [this]
        {
            const auto cue = selectedCueIndex();

            if (cue >= 0)
                session.removeCueMix (cue);

            rebuildCueList();
            refreshControlsFromSelection();
            updateStatus();
        };
        closeButton.onClick = [this] { if (onClose) onClose(); };

        rebuildTrackList();
        rebuildSubmixList();
        rebuildCueList();
        refreshControlsFromSelection();
        updateStatus();

        startTimerHz (4);
    }

    RoutingPanel::~RoutingPanel()
    {
        stopTimer();
    }

    void RoutingPanel::refresh()
    {
        rebuildTrackList();
        rebuildSubmixList();
        rebuildCueList();
        refreshControlsFromSelection();
        updateStatus();
    }

    void RoutingPanel::timerCallback()
    {
        if (isShowing())
            updateStatus();
    }

    //==============================================================================
    void RoutingPanel::paint (juce::Graphics& g)
    {
        g.fillAll (brand::bgPanel);
        g.setColour (brand::border);
        g.drawRect (getLocalBounds(), 1);

        g.setColour (brand::textPrimary);
        g.setFont (brand::uiSemiBold (16.0f));
        g.drawText ("Routing & Cue Mixes", getLocalBounds().removeFromTop (34).reduced (14, 0),
                    juce::Justification::centredLeft);

        g.setColour (brand::textSecondary);
        g.setFont (brand::uiSemiBold (12.0f));
        const auto submixHeader = juce::Rectangle<int> (14, 34 + 34 + 10 - 2, 200, 14);
        g.drawText ("Submix folder", submixHeader, juce::Justification::centredLeft);
    }

    void RoutingPanel::resized()
    {
        auto area = getLocalBounds().reduced (14);
        area.removeFromTop (34); // title

        auto trackRow = area.removeFromTop (30);
        trackBox.setBounds (trackRow.removeFromLeft (240));
        trackRow.removeFromLeft (10);
        sendLabel.setBounds (trackRow.removeFromLeft (44));
        sendToggle.setBounds (trackRow.removeFromLeft (80));
        trackRow.removeFromLeft (10);
        outputBox.setBounds (trackRow);

        area.removeFromTop (10);

        auto submixRow = area.removeFromTop (30);
        submixBox.setBounds (submixRow.removeFromLeft (240));
        submixRow.removeFromLeft (8);
        addSubmixButton.setBounds (submixRow.removeFromLeft (90));
        submixRow.removeFromLeft (6);
        assignSubmixButton.setBounds (submixRow.removeFromLeft (90));
        submixRow.removeFromLeft (6);
        unassignSubmixButton.setBounds (submixRow.removeFromLeft (100));

        area.removeFromTop (12);

        // Cue section.
        auto cueRow = area.removeFromTop (30);
        cueBox.setBounds (cueRow.removeFromLeft (200));
        cueRow.removeFromLeft (8);
        addCueButton.setBounds (cueRow.removeFromLeft (90));
        cueRow.removeFromLeft (6);
        removeCueButton.setBounds (cueRow.removeFromLeft (100));
        cueRow.removeFromLeft (8);
        cueOutputBox.setBounds (cueRow);

        area.removeFromTop (8);

        auto sendRow = area.removeFromTop (30);
        sendSlider.setBounds (sendRow.removeFromLeft (juce::jmin (320, sendRow.getWidth())));

        // Footer: status + close.
        auto footer = getLocalBounds().reduced (14).removeFromBottom (30);
        closeButton.setBounds (footer.removeFromRight (90));
        footer.removeFromRight (10);
        statusLabel.setBounds (footer);
    }

    //==============================================================================
    int RoutingPanel::selectedTrackIndex() const       { return trackBox.getSelectedId() - 1; }
    int RoutingPanel::selectedCueIndex() const          { return cueBox.getSelectedId() - 1; }

    void RoutingPanel::rebuildTrackList()
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);
        trackBox.clear (juce::dontSendNotification);

        for (int i = 0; i < session.getNumAudioTracks(); ++i)
        {
            auto name = session.getTrackName (i);

            if (name.isEmpty())
                name = "Track " + juce::String (i + 1);

            if (session.isCueReturnTrack (i))
                name << " (cue)";

            trackBox.addItem (name, i + 1);
        }

        if (session.getNumAudioTracks() > 0 && trackBox.getSelectedId() == 0)
            trackBox.setSelectedId (1, juce::dontSendNotification);
    }

    void RoutingPanel::rebuildOutputList (juce::ComboBox& box, const juce::String& currentID)
    {
        box.clear (juce::dontSendNotification);
        box.addItem ("Default output", defaultOutputItemId);

        const auto ids = session.getAvailableOutputDeviceIDs();
        const auto names = session.getAvailableOutputDeviceNames();

        int selected = defaultOutputItemId;

        for (int i = 0; i < ids.size(); ++i)
        {
            box.addItem (names[i], defaultOutputItemId + 1 + i);

            if (ids[i] == currentID)
                selected = defaultOutputItemId + 1 + i;
        }

        box.setSelectedId (selected, juce::dontSendNotification);
    }

    void RoutingPanel::rebuildSubmixList()
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);
        submixBox.clear (juce::dontSendNotification);

        for (int i = 0; i < session.getNumSubmixFolders(); ++i)
            submixBox.addItem (session.getSubmixFolderName (i), i + 1);

        if (session.getNumSubmixFolders() > 0 && submixBox.getSelectedId() == 0)
            submixBox.setSelectedId (1, juce::dontSendNotification);
    }

    void RoutingPanel::rebuildCueList()
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);
        const auto previous = cueBox.getSelectedId();
        cueBox.clear (juce::dontSendNotification);

        for (int i = 0; i < session.getNumCueMixes(); ++i)
            cueBox.addItem (session.getCueMix (i).name, i + 1);

        if (cueBox.getNumItems() > 0)
            cueBox.setSelectedId (juce::jlimit (1, cueBox.getNumItems(), previous > 0 ? previous : 1),
                                  juce::dontSendNotification);
    }

    void RoutingPanel::refreshControlsFromSelection()
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);
        const auto track = selectedTrackIndex();
        const auto cue = selectedCueIndex();

        if (track >= 0)
            rebuildOutputList (outputBox, session.getTrackOutputDevice (track));
        else
            rebuildOutputList (outputBox, {});

        if (cue >= 0)
        {
            rebuildOutputList (cueOutputBox, session.getCueMix (cue).outputDeviceID);

            if (track >= 0)
            {
                const auto db = session.getCueSendLevelDb (track, cue);
                sendSlider.setValue (db, juce::dontSendNotification);
                sendToggle.setToggleState (session.isCueSendEnabled (track, cue),
                                           juce::dontSendNotification);
                sendToggle.setEnabled (true);
                sendSlider.setEnabled (true);
            }
            else
            {
                sendToggle.setEnabled (false);
                sendSlider.setEnabled (false);
            }
        }
        else
        {
            rebuildOutputList (cueOutputBox, {});
            sendToggle.setEnabled (false);
            sendSlider.setEnabled (false);
        }

        repaint();
    }

    void RoutingPanel::updateStatus()
    {
        juce::String text;
        text << session.getNumAudioTracks() << " track(s), "
             << session.getNumSubmixFolders() << " bus(es), "
             << session.getNumCueMixes() << " cue mix(es), "
             << session.getAvailableOutputDeviceIDs().size() << " output device(s)";

        if (session.getLastError().isNotEmpty())
            text << "   -   " << session.getLastError();

        statusLabel.setText (text, juce::dontSendNotification);
    }

    //==============================================================================
    void RoutingPanel::applyTrackOutput()
    {
        if (updating)
            return;

        const auto track = selectedTrackIndex();
        const auto id = outputBox.getSelectedId();

        if (track < 0 || id <= 0)
            return;

        if (id == defaultOutputItemId)
        {
            session.setTrackOutputToDefault (track);
        }
        else
        {
            const auto ids = session.getAvailableOutputDeviceIDs();
            const auto deviceIndex = id - defaultOutputItemId - 1;

            if (juce::isPositiveAndBelow (deviceIndex, ids.size()))
                session.setTrackOutputToDevice (track, ids[deviceIndex]);
        }

        updateStatus();
    }

    void RoutingPanel::applyCueOutput()
    {
        if (updating)
            return;

        const auto cue = selectedCueIndex();
        const auto id = cueOutputBox.getSelectedId();

        if (cue < 0 || id <= 0)
            return;

        if (id == defaultOutputItemId)
        {
            session.setCueMixOutputDevice (cue, {});
        }
        else
        {
            const auto ids = session.getAvailableOutputDeviceIDs();
            const auto deviceIndex = id - defaultOutputItemId - 1;

            if (juce::isPositiveAndBelow (deviceIndex, ids.size()))
                session.setCueMixOutputDevice (cue, ids[deviceIndex]);
        }

        updateStatus();
    }

    void RoutingPanel::applySend()
    {
        if (updating)
            return;

        const auto track = selectedTrackIndex();
        const auto cue = selectedCueIndex();

        if (track < 0 || cue < 0)
            return;

        session.setCueSendLevelDb (track, cue, (float) sendSlider.getValue());
        session.setCueSendEnabled (track, cue, sendToggle.getToggleState());

        refreshControlsFromSelection();
        updateStatus();
    }
}
