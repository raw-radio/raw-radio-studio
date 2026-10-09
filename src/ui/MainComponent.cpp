// raw-radio-studio — main window content (Epic 1).

#include "MainComponent.h"

#include "studio/AppPaths.h"
#include "studio/AudioImport.h"
#include "ui/BrandColours.h"
#include "ui/BrandFonts.h"
#include "ui/ClipSnap.h"
#include "ui/DevicePanelLayout.h"
#include "ui/TrackLaneLayout.h"

#include <array>
#include <cmath>

#ifndef RAW_RADIO_STUDIO_VERSION
 #define RAW_RADIO_STUDIO_VERSION "0.1.1-dev"
#endif

#ifndef RAW_RADIO_STUDIO_TRACKTION_VERSION
 #define RAW_RADIO_STUDIO_TRACKTION_VERSION "unknown"
#endif

namespace rrs
{
    namespace te = tracktion;

    MainComponent::MainComponent()
    {
        loadSettings();

        titleLabel.setFont (brand::uiSemiBold (18.0f));
        titleLabel.setColour (juce::Label::textColourId, brand::textPrimary);
        transportLabel.setFont (brand::monoRegular (13.0f));
        transportLabel.setColour (juce::Label::textColourId, brand::textSecondary);
        transportLabel.setJustificationType (juce::Justification::centredLeft);
        statusLabel.setFont (brand::uiRegular (13.0f));
        statusLabel.setColour (juce::Label::textColourId, brand::textSecondary);
        statusLabel.setJustificationType (juce::Justification::centredLeft);

        selectionLabel.setFont (brand::monoRegular (11.0f));
        selectionLabel.setColour (juce::Label::textColourId, brand::textTertiary);
        selectionLabel.setJustificationType (juce::Justification::centredLeft);
        selectionLabel.setText ("No region selected", juce::dontSendNotification);

        addAndMakeVisible (titleLabel);
        addAndMakeVisible (devicePanel);
        addAndMakeVisible (inputMeter);
        addAndMakeVisible (mixerPanel);
        addAndMakeVisible (timeline);
        addAndMakeVisible (transportLabel);
        addAndMakeVisible (statusLabel);
        addAndMakeVisible (selectionLabel);

        for (auto* button : { &newButton, &openButton, &closeButton, &saveButton, &saveAsButton,
                              &importButton, &normaliseButton, &exportButton,
                              &exportRegionButton, &clearRegionButton,
                              &stemsButton, &pluginsButton, &routingButton,
                              &addTrackButton, &removeTrackButton,
                              &settingsButton, &aboutButton,
                              &armButton, &recordButton, &playButton, &stopButton, &goToStartButton,
                              &monitorButton, &metronomeButton,
                              &undoButton, &redoButton, &splitClipButton, &deleteClipButton,
                              &duplicateClipButton, &loopClipButton, &crossfadeButton,
                              &stretchClipButton })
            addAndMakeVisible (*button);

        // Epic 4: the arrangement accepts keyboard editing (Delete/Split/undo...).
        setWantsKeyboardFocus (true);

        // Epic 3 overlays (hidden until their action button is pressed).
        addChildComponent (pluginBrowser);
        addChildComponent (routingPanel);
        pluginBrowser.onClose = [this] { pluginBrowser.setVisible (false); };
        routingPanel.onClose = [this] { routingPanel.setVisible (false); };

        addAndMakeVisible (countInBox);

        // Transport-left "Count-in" caption so the combo next to the Click
        // button is unambiguous (the owner found the bare combo unclear). The
        // label is themed like the other transport chrome.
        countInLabel.setText ("Count-in", juce::dontSendNotification);
        countInLabel.setFont (brand::uiRegular (12.0f));
        countInLabel.setColour (juce::Label::textColourId, brand::textSecondary);
        countInLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (countInLabel);

        buildTransportUi();

        // Arm, Monitor and Click are toggle chips with distinct on-states so each
        // reads as exactly one state (BUG B): arm uses the brand accent, monitor
        // the success tint, click the warning tint. Without on-colours a Chip
        // toggle looked identical on and off.
        armButton.setOnColours (brand::accentMuted, brand::accent, brand::accent);
        monitorButton.setOnColours (brand::success.withAlpha (0.25f), brand::success, brand::success);
        metronomeButton.setOnColours (brand::warning.withAlpha (0.25f), brand::warning, brand::warning);

        // Icon-only action buttons (owner request): no text labels, each with a
        // hover tooltip describing the action. 32x32 squares in the action row.
        newButton.setIconName ("file-plus");
        newButton.setIconOnly (true);
        newButton.setTooltip ("New session");
        openButton.setIconName ("folder-open");
        openButton.setIconOnly (true);
        openButton.setTooltip ("Open session");
        saveButton.setIconName ("floppy-disk");
        saveButton.setIconOnly (true);
        saveButton.setTooltip ("Save session");
        saveAsButton.setIconName ("floppy-disk-back");
        saveAsButton.setIconOnly (true);
        saveAsButton.setTooltip ("Save session as...");
        closeButton.setIconName ("x");
        closeButton.setIconOnly (true);
        closeButton.setTooltip ("Close session");
        importButton.setIconName ("upload-simple");
        importButton.setIconOnly (true);
        importButton.setTooltip ("Import an audio file as a backing track");
        // Export keeps its primary (accent) styling, now icon-only.
        exportButton.setIconName ("download-simple");
        exportButton.setIconOnly (true);
        exportButton.setTooltip ("Export session to 24-bit WAV");

        // FR-EXP-2: export just the selected region; Clear drops the selection.
        exportRegionButton.setIconName ("download-simple");
        exportRegionButton.setIconOnly (true);
        exportRegionButton.setTooltip ("Export the selected region to 24-bit WAV "
                                       "(Shift-drag on the ruler to select)");
        clearRegionButton.setIconName ("x");
        clearRegionButton.setIconOnly (true);
        clearRegionButton.setTooltip ("Clear the region selection");

        // Epic 3: plugin browser, routing/cue mixes, stems export. Export stems is
        // the one action whose function is not obvious from an icon, so it keeps a
        // visible text label (owner could not find it as an icon-only button).
        pluginsButton.setIconName ("waveform");
        pluginsButton.setIconOnly (true);
        pluginsButton.setTooltip ("Plugin browser (scan/insert VST3/LV2/AU; open editors)");
        routingButton.setIconName ("headphones");
        routingButton.setIconOnly (true);
        routingButton.setTooltip ("Routing & software cue mixes (outputs, sends, submixes)");
        stemsButton.setIconName ("download-simple");
        stemsButton.setTooltip ("Export stems: one 24-bit WAV per track plus the master mix, "
                                "written into a folder you choose");

        // Utility buttons are icon-only too (owner request): the action row no
        // longer overflows once Normalize / Add track / Remove track drop their
        // text, and the row matches the icon-only session buttons.
        normaliseButton.setIconName ("waveform");
        normaliseButton.setIconOnly (true);
        normaliseButton.setTooltip ("Peak-normalise the most recent take to -1 dBFS");
        addTrackButton.setIconName ("plus");
        addTrackButton.setIconOnly (true);
        addTrackButton.setTooltip ("Add an input track (maps to the next free input)");
        removeTrackButton.setIconName ("trash");
        removeTrackButton.setIconOnly (true);
        removeTrackButton.setTooltip ("Remove track (deletes the whole track and all its "
                                      "clips). Removes the selected clip's track, or the last "
                                      "track when no clip is selected — use it to remove an "
                                      "imported backing track. To delete only a clip, use "
                                      "Delete.");

        settingsButton.setIconName ("gear-six");
        settingsButton.setIconOnly (true);
        settingsButton.setTooltip ("Settings");
        aboutButton.setIconName ("info");
        aboutButton.setIconOnly (true);
        aboutButton.setTooltip ("About raw-radio-studio");

        // Icon-only transport: square, each with a hover tooltip. Record is a
        // little larger to keep its emphasis; the rest are 36x36 (>= 32x32).
        armButton.setIconName ("record");
        armButton.setIconOnly (true, 36);
        armButton.setTooltip ("Arm the track for recording");
        recordButton.setIconName ("record");
        recordButton.setIconOnly (true, 40);
        recordButton.setTooltip ("Record / stop recording");
        playButton.setIconName ("play");
        playButton.setIconOnly (true, 36);
        playButton.setTooltip ("Play / pause");
        stopButton.setIconName ("stop");
        stopButton.setIconOnly (true, 36);
        stopButton.setTooltip ("Stop (keeps the playhead where it stopped)");
        goToStartButton.setIconName ("skip-back");
        goToStartButton.setIconOnly (true, 36);
        goToStartButton.setTooltip ("Go to start (return the playhead to 0)");
        monitorButton.setIconName ("headphones");
        monitorButton.setIconOnly (true, 36);
        monitorButton.setTooltip ("Toggle input monitoring");

        metronomeButton.setIconName ("metronome");
        metronomeButton.setIconOnly (true, 36);
        metronomeButton.setTooltip ("Metronome click: on = click audible during play and record; off = click only during count-in");
        countInBox.setTooltip ("Count-in before recording: click 1-2 beats or 1-2 bars before the take starts. "
                               "Saved with the session; only affects recording, not plain playback.");
        countInLabel.setTooltip ("Count-in before recording (see the combo)");

        // Shorter labels (the "Count-in: " prefix was redundant with the
        // tooltip and made the combo wider than the transport row could spare
        // at the minimum window size).
        countInBox.addItem ("Off",     (int) te::Edit::CountIn::none + 1);
        countInBox.addItem ("1 beat",  (int) te::Edit::CountIn::oneBeat + 1);
        countInBox.addItem ("2 beats", (int) te::Edit::CountIn::twoBeat + 1);
        countInBox.addItem ("1 bar",   (int) te::Edit::CountIn::oneBar + 1);
        countInBox.addItem ("2 bars",  (int) te::Edit::CountIn::twoBar + 1);

        newButton.onClick       = [this] { newSession(); };
        openButton.onClick      = [this] { openSession(); };
        closeButton.onClick     = [this] { closeSession(); };
        saveButton.onClick      = [this] { saveSession(); };
        saveAsButton.onClick    = [this] { saveSessionAs(); };
        importButton.onClick    = [this] { importAudioFile(); };
        normaliseButton.onClick = [this]
        {
            if (exportInProgress || session.getEdit() == nullptr)
                return;

            Session::NormaliseResult result;

            if (session.normaliseLatestTake (-1.0f, &result))
            {
                if (result.clamped)
                    showStatus ("Normalised the latest take: gain clamped to "
                                + juce::String (result.appliedGainDb, 1)
                                + " dB, peak " + juce::String (result.achievedPeakDb, 1)
                                + " dBFS (target -1 dBFS not reachable).");
                else
                    showStatus ("Normalised the latest take to -1 dBFS.");
            }
            else
            {
                showStatus (session.getLastError(), true);
            }

            refreshTransportUi();
        };
        exportButton.onClick    = [this] { exportSession(); };
        exportRegionButton.onClick = [this] { exportRegion(); };
        clearRegionButton.onClick = [this]
        {
            session.clearSelection();
            refreshTransportUi();
        };
        stemsButton.onClick     = [this] { exportStems(); };
        pluginsButton.onClick   = [this]
        {
            if (session.getEdit() == nullptr)
                return;

            if (! pluginBrowser.isVisible())
                pluginBrowser.refresh();

            toggleOverlay (pluginBrowser);
        };
        routingButton.onClick   = [this]
        {
            if (session.getEdit() == nullptr)
                return;

            if (! routingPanel.isVisible())
                routingPanel.refresh();

            toggleOverlay (routingPanel);
        };
        addTrackButton.onClick  = [this]
        {
            if (exportInProgress)
                return;

            const auto index = session.addAudioTrack();

            if (index < 0)
                showStatus (session.getLastError(), true);
            else
                showStatus ("Added " + session.getTrackName (index) + " ("
                            + juce::String (session.getNumAudioTracks()) + " tracks)");

            refreshTransportUi();
        };
        removeTrackButton.onClick = [this]
        {
            if (exportInProgress)
                return;

            // Remove the track the user is pointing at: the selected clip's
            // track when a clip is selected, otherwise the last track (the
            // original behaviour). This is how the owner deletes an imported
            // backing track ("minus") without it having to be the last one.
            const int target = selectedClipExists() ? selectedTrackIndex
                                                    : session.getNumAudioTracks() - 1;
            const auto name = session.getTrackName (target);

            if (! session.removeAudioTrack (target))
            {
                showStatus (session.getLastError(), true);
            }
            else
            {
                selectClip (-1, -1); // the removed track's selection is gone
                showStatus ("Removed track: " + name);
            }

            refreshTransportUi();
        };
        settingsButton.onClick  = [this] { showSettings(); };
        aboutButton.onClick     = [this] { showAbout(); };

        devicePanel.onDeviceChanged = [this]
        {
            // The panel is disabled during export; guard the callback too so an
            // unsolicited device-manager change cannot race the render thread.
            if (exportInProgress)
                return;

            session.reconfigureInputs();
            refreshTransportUi();
        };

        session.addChangeListener (this);

        setSize (1200, 900);
        // Single clock for the whole arrangement (see timerCallback): the ruler
        // and the lanes are both repainted from this timer, so their playhead
        // lines move in lockstep.
        startTimerHz (30);

        refreshTransportUi();
        runStartupRecovery();
    }

    MainComponent::~MainComponent()
    {
        stopTimer();
        session.removeChangeListener (this);

        // A render holds a raw `Edit*` on a background thread. Cancel and join it
        // before `session` (a member declared before `exportHandle`) is destroyed,
        // otherwise the render thread can touch a freed Edit.
        if (exportHandle != nullptr)
        {
            exportHandle->cancel();
            exportHandle.reset();
        }

        if (stemsHandle != nullptr)
        {
            stemsHandle->cancel();
            stemsHandle.reset();
        }

        // Cancelling skips the export completion callback, and it also invalidates
        // WavExport's own deferred metronome restore (shared liveness token), so
        // restore the click here (FR-EXP-1) rather than leaving it silently off.
        if (exportMetronomeWasEnabled)
            session.setMetronomeEnabled (true);

        saveSettings();
    }

    //==============================================================================
    void MainComponent::buildTransportUi()
    {
        armButton.setClickingTogglesState (true);
        monitorButton.setClickingTogglesState (true);
        metronomeButton.setClickingTogglesState (true);

        armButton.onClick = [this]
        {
            if (exportInProgress)
                return;

            if (! session.setTrackArmed (armButton.getToggleState()))
                showStatus (session.getLastError(), true);

            // Re-derive the chip from the session immediately: if the session
            // rejected the change, the button must not keep a stale on-state.
            refreshTransportUi();
        };

        monitorButton.onClick = [this]
        {
            if (exportInProgress)
                return;

            session.setMonitoringEnabled (monitorButton.getToggleState());

            // Single source of truth is the session, never the local toggle.
            refreshTransportUi();
        };

        metronomeButton.onClick = [this]
        {
            if (exportInProgress)
                return;

            session.setMetronomeEnabled (metronomeButton.getToggleState());
        };

        countInBox.onChange = [this]
        {
            if (exportInProgress)
                return;

            const auto mode = (te::Edit::CountIn) juce::jmax (0, countInBox.getSelectedId() - 1);
            session.setCountInMode (mode);

            // Immediate feedback so the control's effect is obvious even before
            // the next record pass: count-in is a *record* feature.
            if (mode == te::Edit::CountIn::none)
                showStatus ("Count-in: off (recording starts immediately).");
            else
                showStatus ("Count-in: " + juce::String (session.getCountInBeats())
                            + " beat(s) before recording starts.");

            refreshTransportUi();
        };

        recordButton.onClick = [this]
        {
            if (exportInProgress)
                return;

            if (session.isRecording())
                session.stop();
            else if (! session.record())
                showStatus (session.getLastError(), true);

            refreshTransportUi();
        };

        playButton.onClick = [this]
        {
            if (exportInProgress)
                return;

            if (session.isPlaying())
                session.pause(); // pause in place; Stop holds too, Start returns to 0
            else
                session.play();

            refreshTransportUi();
        };

        stopButton.onClick = [this]
        {
            if (exportInProgress)
                return;

            session.stop();
            refreshTransportUi();
        };

        goToStartButton.onClick = [this]
        {
            if (exportInProgress)
                return;

            session.goToStart();
            refreshTransportUi();
        };

        // Epic 4 arrangement edit actions. Icon-only now (owner request: the edit
        // row matches the icon-only action/transport rows). Each button keeps the
        // same tooltip as before so the action stays discoverable on hover; the
        // underlying text label is retained only as the button's accessible name
        // (`paintButton` draws the glyph alone when `iconOnly` is set).
        undoButton.setIconName ("arrow-counter-clockwise");
        undoButton.setIconOnly (true);
        undoButton.setTooltip ("Undo the last edit (Cmd/Ctrl+Z)");
        redoButton.setIconName ("arrow-clockwise");
        redoButton.setIconOnly (true);
        redoButton.setTooltip ("Redo the last undone edit (Cmd/Ctrl+Shift+Z)");
        splitClipButton.setIconName ("scissors");
        splitClipButton.setIconOnly (true);
        splitClipButton.setTooltip ("Split the selected clip at the playhead (S)");
        deleteClipButton.setIconName ("trash");
        deleteClipButton.setIconOnly (true);
        deleteClipButton.setTooltip ("Delete the selected clip only (Delete key). "
                                     "To delete the whole track use the Remove track button.");
        duplicateClipButton.setIconName ("copy");
        duplicateClipButton.setIconOnly (true);
        duplicateClipButton.setTooltip ("Duplicate the selected clip (D)");
        loopClipButton.setIconName ("repeat");
        loopClipButton.setIconOnly (true);
        loopClipButton.setTooltip ("Loop the selected clip twice (L)");
        crossfadeButton.setIconName ("arrows-left-right");
        crossfadeButton.setIconOnly (true);
        crossfadeButton.setTooltip ("Crossfade the selected clip with the next one on its track (F)");
        stretchClipButton.setIconName ("arrows-horizontal");
        stretchClipButton.setIconOnly (true);
        stretchClipButton.setTooltip ("Time-stretch the selected clip to the region selection length");

        undoButton.onClick = [this] { undoEdit(); };
        redoButton.onClick = [this] { redoEdit(); };
        splitClipButton.onClick = [this] { splitSelectedAtPlayhead(); };
        deleteClipButton.onClick = [this] { deleteSelectedClip(); };
        duplicateClipButton.onClick = [this] { duplicateSelectedClip(); };
        loopClipButton.onClick = [this] { loopSelectedClip(); };
        crossfadeButton.onClick = [this] { crossfadeSelectedClip(); };
        stretchClipButton.onClick = [this] { stretchSelectedClip(); };

        refreshEditButtons();
    }

    void MainComponent::refreshTransportUi()
    {
        const bool hasEdit = session.getEdit() != nullptr;

        // While a render holds the `Edit*`, nothing may replace or mutate it:
        // New/Open would reset the Edit (use-after-free on the render thread)
        // and the transport/Save would race it. Disable them all for the duration.
        const bool busy = exportInProgress || stemsInProgress;

        // While a render holds the `Edit*`, the device panel must be inert:
        // applying a device change runs session.reconfigureInputs(), which
        // mutates the Edit concurrently with the render thread. Disabling the
        // panel (children included) also blocks Rescan/Apply/combo changes.
        devicePanel.setEnabled (! busy);

        for (auto* button : { &saveButton, &saveAsButton, &importButton, &normaliseButton, &exportButton,
                              &exportRegionButton, &clearRegionButton,
                              &stemsButton, &pluginsButton, &routingButton,
                              &armButton, &recordButton, &playButton, &stopButton, &goToStartButton,
                              &monitorButton, &metronomeButton })
            button->setEnabled (hasEdit && ! busy);

        // FR-EXP-2: region actions require a live selection.
        exportRegionButton.setEnabled (hasEdit && ! busy && session.hasSelection());
        clearRegionButton.setEnabled (hasEdit && ! busy && session.hasSelection());

        // The overlays must not linger over a session they no longer describe.
        if (! hasEdit)
        {
            pluginBrowser.setVisible (false);
            routingPanel.setVisible (false);
        }

        countInBox.setEnabled (hasEdit && ! busy);
        countInLabel.setEnabled (hasEdit && ! busy);
        countInBox.setSelectedId ((int) session.getCountInMode() + 1, juce::dontSendNotification);

        // Visual confirmation that a count-in is armed: the selected value is
        // tinted with the accent colour (off stays neutral).
        const bool countInActive = session.getCountInMode() != te::Edit::CountIn::none;
        countInBox.setColour (juce::ComboBox::textColourId,
                              (hasEdit && countInActive) ? brand::accent : brand::textPrimary);
        countInBox.setColour (juce::ComboBox::outlineColourId,
                              (hasEdit && countInActive) ? brand::accent : brand::border);

        metronomeButton.setToggleState (session.isMetronomeEnabled(), juce::dontSendNotification);

        // FR-EXP-2: reflect the region selection (start/end + duration).
        if (session.hasSelection())
        {
            const auto start = session.getSelectionStartSeconds();
            const auto end = session.getSelectionEndSeconds();
            selectionLabel.setColour (juce::Label::textColourId, brand::accent);
            selectionLabel.setText ("Region  " + formatTime (start) + "  -  " + formatTime (end)
                                        + "   (" + juce::String (end - start, 3) + " s)",
                                    juce::dontSendNotification);
        }
        else
        {
            selectionLabel.setColour (juce::Label::textColourId, brand::textTertiary);
            selectionLabel.setText ("No region selected   (Shift-drag the ruler to select)",
                                    juce::dontSendNotification);
        }

        addTrackButton.setEnabled (hasEdit && ! busy);
        removeTrackButton.setEnabled (hasEdit && ! busy && session.getNumAudioTracks() > 1);
        mixerPanel.setEnabled (! busy);
        timeline.setEnabled (hasEdit && ! busy);

        // Epic 4: keep the arrangement edit actions in step with undo state and
        // the selected clip.
        refreshEditButtons();

        newButton.setEnabled (! busy);
        openButton.setEnabled (! busy);
        closeButton.setEnabled (hasEdit && ! busy);

        recordButton.setButtonText (session.isRecording() ? "Stop rec" : "Record");
        playButton.setButtonText (session.isPlaying() ? "Pause" : "Play");
        playButton.setIconName (session.isPlaying() ? "pause" : "play");
        recordButton.setToggleState (session.isRecording(), juce::dontSendNotification);
        playButton.setToggleState (session.isPlaying(), juce::dontSendNotification);
        armButton.setToggleState (session.isTrackArmed(), juce::dontSendNotification);
        monitorButton.setToggleState (session.isMonitoringEnabled(), juce::dontSendNotification);

        double position = 0.0;

        if (auto* edit = session.getEdit())
            position = edit->getTransport().getPosition().inSeconds();

        juce::String state = "Stopped";

        if (session.isRecording())
            state = "Recording";
        else if (session.isPlaying())
            state = "Playing";

        transportLabel.setText (state + "   " + formatTime (position),
                                juce::dontSendNotification);

        repaint();
    }

    void MainComponent::showStatus (const juce::String& message, bool isError)
    {
        statusMessage = message;
        statusIsError = isError;
        statusLabel.setColour (juce::Label::textColourId,
                               isError ? brand::errorText : brand::textSecondary);
        statusLabel.setText (message, juce::dontSendNotification);
    }

    //==============================================================================
    void MainComponent::confirmUnsavedChanges (const juce::String& title, std::function<void()> onProceed)
    {
        if (session.getEdit() == nullptr || ! session.hasUnsavedChanges())
        {
            onProceed();
            return;
        }

        juce::Component::SafePointer<MainComponent> safe (this);

        juce::NativeMessageBox::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle (title)
                .withMessage ("This session has unsaved changes.\n\n"
                              "Save them before continuing?")
                .withButton ("Save")
                .withButton ("Discard")
                .withButton ("Cancel"),
            [safe, onProceed] (int result)
            {
                auto* self = safe.getComponent();

                if (self == nullptr)
                    return;

                if (result == 0) // Save
                {
                    if (! self->session.save())
                    {
                        self->showStatus (self->session.getLastError(), true);
                        return; // save failed: keep the session
                    }

                    onProceed();
                }
                else if (result == 1) // Discard
                {
                    onProceed();
                }
                // result == 2 (Cancel): do nothing.
            });
    }

    void MainComponent::newSession()
    {
        if (exportInProgress || stemsInProgress)
            return;

        confirmUnsavedChanges ("New session", [this]
        {
            auto startDir = lastDirectories != nullptr
                                ? lastDirectories->getDirectory (LastDirectoryStore::Kind::projects,
                                                                 paths::projectsDirectory())
                                : paths::projectsDirectory();

            auto chooser = std::make_shared<juce::FileChooser> ("New session", startDir, "*.tracktionedit");

            chooser->launchAsync (juce::FileBrowserComponent::saveMode
                                      | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this, chooser] (const juce::FileChooser& fc)
                                  {
                                      auto file = fc.getResult();

                                      if (file == juce::File())
                                          return;

                                      if (! file.hasFileExtension ("tracktionedit"))
                                          file = file.withFileExtension ("tracktionedit");

                                      if (lastDirectories != nullptr)
                                          lastDirectories->rememberFile (LastDirectoryStore::Kind::projects, file);

                                      if (session.createNew (file))
                                          onSessionOpened();
                                      else
                                          showStatus (session.getLastError(), true);
                                  });
        });
    }

    void MainComponent::openSession()
    {
        if (exportInProgress || stemsInProgress)
            return;

        confirmUnsavedChanges ("Open session", [this]
        {
            auto startDir = lastDirectories != nullptr
                                ? lastDirectories->getDirectory (LastDirectoryStore::Kind::projects,
                                                                 paths::projectsDirectory())
                                : paths::projectsDirectory();

            auto chooser = std::make_shared<juce::FileChooser> ("Open session", startDir, "*.tracktionedit");

            chooser->launchAsync (juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectFiles,
                                  [this, chooser] (const juce::FileChooser& fc)
                                  {
                                      auto file = fc.getResult();

                                      if (file == juce::File())
                                          return;

                                      if (lastDirectories != nullptr)
                                          lastDirectories->rememberFile (LastDirectoryStore::Kind::projects, file);

                                      if (session.open (file))
                                          onSessionOpened();
                                      else
                                          showStatus (session.getLastError(), true);
                                  });
        });
    }

    void MainComponent::closeSession()
    {
        if (exportInProgress || stemsInProgress || session.getEdit() == nullptr)
            return;

        juce::Component::SafePointer<MainComponent> safe (this);

        // Performs the actual close: resets the session to an empty state and
        // forgets it so a subsequent launch does not silently reopen it.
        auto performClose = [safe]
        {
            auto* self = safe.getComponent();

            if (self == nullptr)
                return;

            self->session.close();
            self->titleLabel.setText ("raw-radio-studio   -   No session", juce::dontSendNotification);

            if (self->settings != nullptr)
            {
                self->settings->removeValue ("lastSession");
                self->settings->saveIfNeeded();
            }

            self->showStatus ("Session closed.");
            self->refreshTransportUi();
        };

        // Same Save/Discard/Cancel guard as New/Open (Close used to be the only
        // path that prompted).
        confirmUnsavedChanges ("Close session", performClose);
    }

    void MainComponent::saveSession()
    {
        if (exportInProgress || session.getEdit() == nullptr)
            return;

        if (session.save())
            showStatus ("Saved: " + session.getEditFile().getFullPathName());
        else
            showStatus (session.getLastError(), true);
    }

    void MainComponent::saveSessionAs()
    {
        if (exportInProgress || session.getEdit() == nullptr)
            return;

        const auto currentFile = session.getEditFile();
        const auto fallbackDir = currentFile != juce::File() ? currentFile.getParentDirectory()
                                                             : paths::projectsDirectory();
        auto startDir = lastDirectories != nullptr
                            ? lastDirectories->getDirectory (LastDirectoryStore::Kind::projects, fallbackDir)
                            : fallbackDir;

        auto suggestedName = currentFile != juce::File() ? currentFile.getFileName()
                                                         : juce::String ("Untitled Session.tracktionedit");

        auto chooser = std::make_shared<juce::FileChooser> ("Save session as",
                                                            startDir.getChildFile (suggestedName),
                                                            "*.tracktionedit");

        chooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, chooser] (const juce::FileChooser& fc)
                              {
                                  auto file = fc.getResult();

                                  if (file == juce::File())
                                      return;

                                  if (! file.hasFileExtension ("tracktionedit"))
                                      file = file.withFileExtension ("tracktionedit");

                                  if (lastDirectories != nullptr)
                                      lastDirectories->rememberFile (LastDirectoryStore::Kind::projects, file);

                                  if (session.saveAs (file))
                                  {
                                      onSessionOpened();
                                      showStatus ("Saved: " + file.getFullPathName());
                                  }
                                  else
                                  {
                                      showStatus (session.getLastError(), true);
                                  }
                              });
    }

    void MainComponent::importAudioFile()
    {
        if (exportInProgress)
            return;

        if (session.getEdit() == nullptr)
        {
            showStatus ("Open a session before importing.", true);
            return;
        }

        auto fallbackDir = session.getEditFile() != juce::File()
                               ? session.getEditFile().getParentDirectory()
                               : paths::projectsDirectory();

        auto startDir = lastDirectories != nullptr
                            ? lastDirectories->getDirectory (LastDirectoryStore::Kind::importAudio, fallbackDir)
                            : fallbackDir;

        auto chooser = std::make_shared<juce::FileChooser> ("Import audio file", startDir,
                                                            AudioImport::fileWildcard);

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this, chooser] (const juce::FileChooser& fc)
                              {
                                  auto file = fc.getResult();

                                  if (file == juce::File())
                                      return;

                                  if (lastDirectories != nullptr)
                                      lastDirectories->rememberFile (LastDirectoryStore::Kind::importAudio, file);

                                  if (session.importAudioFile (file))
                                  {
                                      showStatus ("Imported: " + file.getFileName()
                                                  + " (" + juce::String (session.getNumAudioTracks())
                                                  + " track(s))");
                                  }
                                  else
                                  {
                                      showStatus (session.getLastError(), true);
                                  }

                                  refreshTransportUi();
                              });
    }

    void MainComponent::exportSession()
    {
        // Defense-in-depth: never start a WAV export while a stems batch (or
        // another WAV export) is already consuming the Edit.
        if (exportInProgress || stemsInProgress)
            return;

        if (session.getEdit() == nullptr)
        {
            showStatus ("Open a session before exporting.", true);
            return;
        }

        const auto defaultDestination = WavExport::defaultDestinationFor (session.getEditFile());

        auto startDir = lastDirectories != nullptr
                            ? lastDirectories->getDirectory (LastDirectoryStore::Kind::exportFile,
                                                             defaultDestination.getParentDirectory())
                            : defaultDestination.getParentDirectory();

        auto chooser = std::make_shared<juce::FileChooser> ("Export WAV",
                                                            startDir.getChildFile (defaultDestination.getFileName()),
                                                            "*.wav");

        chooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, chooser] (const juce::FileChooser& fc)
                              {
                                  auto file = fc.getResult();

                                  if (file == juce::File())
                                      return;

                                  if (! file.hasFileExtension ("wav"))
                                      file = file.withFileExtension ("wav");

                                  if (lastDirectories != nullptr)
                                      lastDirectories->rememberFile (LastDirectoryStore::Kind::exportFile, file);

                                  beginWavExport (file, {}, "Exporting 24-bit WAV...");
                              });
    }

    void MainComponent::exportRegion()
    {
        // DD: never start a WAV render while a stems batch (or another render)
        // is already consuming the Edit.
        if (exportInProgress || stemsInProgress)
            return;

        if (session.getEdit() == nullptr)
        {
            showStatus ("Open a session before exporting.", true);
            return;
        }

        if (! session.hasSelection())
        {
            showStatus ("Select a region first: Shift-drag on the timeline ruler.", true);
            return;
        }

        // FR-EXP-2: clamp the region to the arrangement so the file length
        // matches what the engineer sees, and reject an empty result.
        const auto start = session.getSelectionStartSeconds();
        const auto end = juce::jmin (session.getSelectionEndSeconds(), session.getTimelineLengthSeconds());

        if (end <= start)
        {
            showStatus ("The selected region is empty.", true);
            return;
        }

        auto destination = WavExport::defaultDestinationFor (session.getEditFile());
        destination = destination.getSiblingFile (destination.getFileNameWithoutExtension()
                                                  + " region" + destination.getFileExtension());

        auto startDir = lastDirectories != nullptr
                            ? lastDirectories->getDirectory (LastDirectoryStore::Kind::exportFile,
                                                             destination.getParentDirectory())
                            : destination.getParentDirectory();

        auto chooser = std::make_shared<juce::FileChooser> ("Export region WAV",
                                                            startDir.getChildFile (destination.getFileName()),
                                                            "*.wav");

        chooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, chooser, start, end] (const juce::FileChooser& fc)
                              {
                                  auto file = fc.getResult();

                                  if (file == juce::File())
                                      return;

                                  if (! file.hasFileExtension ("wav"))
                                      file = file.withFileExtension ("wav");

                                  if (lastDirectories != nullptr)
                                      lastDirectories->rememberFile (LastDirectoryStore::Kind::exportFile, file);

                                  beginWavExport (file, { start, end },
                                                  "Exporting selected region (24-bit WAV)...");
                              });
    }

    void MainComponent::beginWavExport (const juce::File& file, WavExport::RenderRange range,
                                        const juce::String& statusText)
    {
        if (exportInProgress || stemsInProgress || session.getEdit() == nullptr)
            return;

        // Lock the session for the duration of the render: New/Open would reset
        // the Edit the render thread is reading (use-after-free).
        exportInProgress = true;

        // The offline renderer frees the live playback context (and with it input
        // monitoring) and never rebuilds it. Capture the transport state now, on
        // the message thread, and restore it in the completion callback below.
        wavExportTransportState = session.captureTransportForOfflineRender();

        // FR-EXP-1: save and disable the metronome for the render, and restore
        // it once the render ends (the completion callback below runs on success,
        // failure and cancel alike). WavExport::start also enforces this at the
        // render choke point; doing it here keeps the Session/UI state honest
        // while the click is off.
        exportMetronomeWasEnabled = session.isMetronomeEnabled();

        if (exportMetronomeWasEnabled)
            session.setMetronomeEnabled (false);

        refreshTransportUi();
        showStatus (statusText);

        juce::Component::SafePointer<MainComponent> safe (this);

        exportHandle = WavExport::start (
            *session.getEdit(), file,
            [safe] (bool success, juce::File result, juce::String error)
            {
                auto* self = safe.getComponent();

                if (self == nullptr)
                    return;

                if (self->exportMetronomeWasEnabled)
                    self->session.setMetronomeEnabled (true);

                self->exportMetronomeWasEnabled = false;
                self->exportHandle.reset();
                self->exportInProgress = false;

                // Rebuild the live playback context and restore the transport that
                // the offline render tore down, so monitoring/playback resume
                // immediately instead of staying dead until a restart.
                self->session.restoreTransportAfterOfflineRender (self->wavExportTransportState);
                self->refreshTransportUi();

                if (success)
                    self->showStatus ("Exported 24-bit WAV: " + result.getFullPathName());
                else
                    self->showStatus ("Export failed: " + error, true);
            },
            true, range);
    }

    void MainComponent::exportStems()
    {
        if (exportInProgress || stemsInProgress)
            return;

        if (session.getEdit() == nullptr)
        {
            showStatus ("Open a session before exporting.", true);
            return;
        }

        const auto defaultDir = StemsExport::defaultDirectoryFor (session.getEditFile());

        auto startDir = lastDirectories != nullptr
                            ? lastDirectories->getDirectory (LastDirectoryStore::Kind::exportStems, defaultDir)
                            : defaultDir;

        auto chooser = std::make_shared<juce::FileChooser> (
            "Export stems (choose a folder)",
            startDir, juce::String());

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [this, chooser] (const juce::FileChooser& fc)
                              {
                                  auto directory = fc.getResult();

                                  if (directory == juce::File())
                                      return;

                                  if (lastDirectories != nullptr)
                                      lastDirectories->rememberDirectory (LastDirectoryStore::Kind::exportStems,
                                                                          directory);

                                  stemsInProgress = true;

                                  // Same playback-context teardown as the WAV path
                                  // (RenderQueue also uses EditRenderer): snapshot
                                  // before the first job frees the context.
                                  stemsExportTransportState = session.captureTransportForOfflineRender();

                                  refreshTransportUi();
                                  showStatus ("Exporting stems (per-track + master, 24-bit WAV)...");

                                  juce::Component::SafePointer<MainComponent> safe (this);

                                  stemsHandle = StemsExport::start (
                                      *session.getEdit(), directory,
                                      [safe] (bool success, juce::File resultDirectory,
                                              int numFiles, juce::String error)
                                      {
                                          auto* self = safe.getComponent();

                                          if (self == nullptr)
                                              return;

                                          self->stemsHandle.reset();
                                          self->stemsInProgress = false;

                                          // Restore the playback context/transport
                                          // the stems batch tore down (see WAV path).
                                          self->session.restoreTransportAfterOfflineRender (
                                              self->stemsExportTransportState);
                                          self->refreshTransportUi();

                                          if (success)
                                              self->showStatus ("Exported " + juce::String (numFiles)
                                                                + " stems to " + resultDirectory.getFullPathName());
                                          else
                                              self->showStatus ("Stems export failed: " + error, true);
                                      });
                              });
    }

    void MainComponent::toggleOverlay (juce::Component& panel)
    {
        if (panel.isVisible())
        {
            panel.setVisible (false);
            return;
        }

        pluginBrowser.setVisible (false);
        routingPanel.setVisible (false);

        panel.setVisible (true);
        panel.toFront (false);
        resized();
    }

    //==============================================================================
    void MainComponent::showAbout()
    {
        juce::String message;
        message << "raw-radio-studio " << RAW_RADIO_STUDIO_VERSION << "\n"
                << "JUCE " << JUCE_MAJOR_VERSION << '.' << JUCE_MINOR_VERSION << '.' << JUCE_BUILDNUMBER
                << ", Tracktion Engine " << RAW_RADIO_STUDIO_TRACKTION_VERSION << "\n\n"
                << "A free, open-source (AGPLv3) tracking-first DAW.\n\n"
                << "Combines Tracktion Engine (GPLv3-or-later) and JUCE (AGPLv3) under AGPLv3 "
                << "via GPLv3 section 13. See the NOTICE and LICENSES/ files in the repository.\n\n"
                << "Contributions use the Developer Certificate of Origin (DCO); no CLA.\n\n"
                << "Projects are stored in:\n" << paths::projectsDirectory().getFullPathName();

        juce::NativeMessageBox::showAsync (juce::MessageBoxOptions()
                                               .withIconType (juce::MessageBoxIconType::InfoIcon)
                                               .withTitle ("About raw-radio-studio")
                                               .withMessage (message)
                                               .withButton ("Close"),
                                           nullptr);
    }

    void MainComponent::showSettings()
    {
        juce::AlertWindow window ("Settings",
                                  "Autosave interval in seconds (0 disables autosave):\n\n"
                                  "Projects are stored in:\n"
                                      + paths::projectsDirectory().getFullPathName(),
                                  juce::MessageBoxIconType::NoIcon);
        window.addTextEditor ("autosave", juce::String (session.getAutosaveIntervalSeconds()));
        window.addButton ("OK", 1);
        window.addButton ("Cancel", 0);

        if (window.runModalLoop() == 1)
        {
            const auto seconds = juce::jmax (0, window.getTextEditorContents ("autosave").getIntValue());
            session.setAutosaveIntervalSeconds (seconds);

            if (settings != nullptr)
            {
                settings->setValue ("autosaveSeconds", seconds);
                settings->saveIfNeeded();
            }

            showStatus ("Autosave interval: " + juce::String (seconds) + " s");
            refreshTransportUi();
        }
    }

    //==============================================================================
    void MainComponent::loadSettings()
    {
        juce::PropertiesFile::Options options;
        options.applicationName    = "raw-radio-studio";
        options.filenameSuffix     = "settings";
        options.folderName         = "raw-radio-studio";
        options.osxLibrarySubFolder = "Application Support";

        settings = std::make_unique<juce::PropertiesFile> (options);
        settingsStorage = std::make_unique<SettingsStorage> (*settings);
        lastDirectories = std::make_unique<LastDirectoryStore> (*settingsStorage);
    }

    void MainComponent::saveSettings()
    {
        if (settings != nullptr && session.getEditFile() != juce::File())
        {
            settings->setValue ("lastSession", session.getEditFile().getFullPathName());
            settings->saveIfNeeded();
        }
    }

    //==============================================================================
    void MainComponent::runStartupRecovery()
    {
        auto lastSession = paths::defaultEditFile();

        if (settings != nullptr)
        {
            const auto stored = settings->getValue ("lastSession");

            if (stored.isNotEmpty())
                lastSession = juce::File (stored);
        }

        const auto info = Session::detectRecovery (lastSession);

        // Only a clean previous exit (no startup sentinel, no unsaved temp
        // version) may auto-open the last session. Any interruption ALWAYS shows
        // the dialog below — the user must explicitly Restore or Discard.
        if (! info.interrupted())
        {
            if (lastSession.existsAsFile())
                session.open (lastSession);
            else
                session.createNew (lastSession);

            onSessionOpened();
            return;
        }

        juce::String message;

        // Only claim an unclean close when the startup sentinel was actually
        // present. A leftover temp version can also survive a *clean* exit (it
        // is written by autosave and only consumed by the next save), so use
        // neutral wording in that case rather than accusing the app of crashing.
        if (info.uncleanShutdown)
            message << "The previous session did not close cleanly.\n\n";
        else
            message << "An unsaved version of the session was found.\n\n";

        if (info.hasTempEdit)
            message << "- An autosaved version of the session is available.\n";

        if (! info.candidateRecordings.isEmpty())
            message << "- " << info.candidateRecordings.size()
                    << " recorded file(s) from the previous run are present.\n";

        message << "\nRestore the session, or discard the recovered changes?";

        juce::Component::SafePointer<MainComponent> safe (this);

        juce::NativeMessageBox::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle ("Session recovery")
                .withMessage (message)
                .withButton ("Restore")
                .withButton ("Discard"),
            [safe, info, lastSession] (int result)
            {
                auto* self = safe.getComponent();

                if (self == nullptr)
                    return;

                if (result == 0) // Restore
                {
                    bool restoreFailed = false;

                    if (info.hasTempEdit)
                        restoreFailed = ! self->session.applyTempEditRecovery (info);

                    if (lastSession.existsAsFile())
                        self->session.open (lastSession);
                    else
                        self->session.createNew (lastSession);

                    self->onSessionOpened();

                    // Never silently open the stale file: if the temp version
                    // could not be applied, say so (the last saved session is
                    // what was opened above).
                    if (restoreFailed)
                        self->showStatus ("Could not restore the autosaved version; "
                                          "opened the last saved session.", true);

                    const auto recovered = self->session.importOrphanedRecordings();

                    if (recovered > 0)
                        self->showStatus ("Recovered " + juce::String (recovered) + " recording(s).");
                }
                else // Discard: drop the recovered changes, start from the last saved state
                {
                    if (info.hasTempEdit)
                        info.tempEditFile.deleteFile();

                    if (lastSession.existsAsFile())
                        self->session.open (lastSession);
                    else
                        self->session.createNew (lastSession);

                    self->onSessionOpened();
                }
            });
    }

    void MainComponent::onSessionOpened()
    {
        titleLabel.setText ("raw-radio-studio   -   " + session.getSessionName(),
                            juce::dontSendNotification);

        if (settings != nullptr)
        {
            settings->setValue ("lastSession", session.getEditFile().getFullPathName());
            session.setAutosaveIntervalSeconds (settings->getIntValue ("autosaveSeconds", 30));
            settings->saveIfNeeded();
        }

        if (session.getLastError().isNotEmpty())
            showStatus (session.getLastError(), true);
        else
            showStatus ("Session: " + session.getEditFile().getFullPathName());

        refreshTransportUi();
    }

    //==============================================================================
    void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        refreshTransportUi();
    }

    void MainComponent::timerCallback()
    {
        // One clock for the ruler and the lanes (Epic 4 playhead fix). The ruler
        // used to repaint on its own 30 Hz timer and the lanes on this 10 Hz one,
        // so the ruler playhead visibly lagged the lane playhead. Both are now
        // repainted from this single ~30 Hz tick. The transport/status chrome is
        // heavier, so it is refreshed only every third tick (still ~10 Hz for the
        // time readout); the playhead still repaints on every tick.
        if (++uiRefreshTicks >= 3)
        {
            uiRefreshTicks = 0;
            refreshTransportUi(); // repaints the lanes as a side effect
        }
        else
        {
            repaint();
        }

        timeline.repaint();
    }

    //==============================================================================
    juce::String MainComponent::formatTime (double seconds)
    {
        const auto totalMs = (juce::int64) std::llround (juce::jmax (0.0, seconds) * 1000.0);
        return juce::String::formatted ("%02d:%02d.%03d",
                                        (int) (totalMs / 60000),
                                        (int) ((totalMs / 1000) % 60),
                                        (int) (totalMs % 1000));
    }

    //==============================================================================
    void MainComponent::paint (juce::Graphics& g)
    {
        g.fillAll (brand::bgWindow);

        // 1 px dividers between the action-row groups.
        g.setColour (brand::border);

        for (const auto& divider : actionDividers)
            g.fillRect (divider);

        // Track lanes: the armed record track first, then any imported backing
        // tracks. Each import adds its own lane so it is visible after import.
        if (! trackLaneArea.isEmpty())
        {
            g.setColour (brand::bgPanel);
            g.fillRoundedRectangle (trackLaneArea.toFloat(), 8.0f);

            auto lanes = trackLaneArea.reduced (8, 6);

            if (auto* edit = session.getEdit())
            {
                const auto tracks = te::getAudioTracks (*edit);

                if (tracks.isEmpty())
                {
                    g.setColour (brand::textSecondary);
                    g.setFont (brand::uiRegular (13.0f));
                    g.drawText ("No session loaded", lanes, juce::Justification::centred);
                }

                // Lane rects were computed in rebuildTrackLaneRects(); each one is
                // clickable (arm/disarm its track) in mouseDown().
                rebuildTrackLaneRects();

                int index = 0;

                for (auto* track : tracks)
                {
                    if (index >= (int) trackLaneRects.size())
                        break;

                    auto lane = trackLaneRects[(size_t) index];
                    const auto armed = session.isTrackArmed (index);

                    // Neutral zebra: never encode "armed" as a blue tint. Armed
                    // lanes use laneArmed plus a 3 px left accent stripe.
                    const auto laneFill = armed ? brand::laneArmed
                                                : (index % 2 == 0 ? brand::lane : brand::laneAlt);

                    auto laneRect = lane.toFloat().reduced (0.0f, 2.0f);
                    g.setColour (laneFill);
                    g.fillRoundedRectangle (laneRect, 5.0f);

                    if (armed)
                    {
                        // 3 px accent stripe flush with the rounded lane's left edge.
                        g.setColour (brand::accent);
                        g.fillRoundedRectangle (laneRect.withWidth (3.0f), 1.5f);
                    }

                    // Two non-overlapping rows inside the lane: the track name in
                    // the top row and the clip strip below it. Shared geometry
                    // with clipRectFor() so a drawn clip and its hit rect match.
                    juce::Rectangle<int> nameArea, clipBand;
                    track_lane::splitLane (lane, nameArea, clipBand);

                    g.setColour (brand::textPrimary);
                    g.setFont (brand::uiMedium (13.0f));
                    g.drawText (track->getName() + (armed ? "   [ARMED]" : ""),
                                nameArea, juce::Justification::centredLeft);

                    // Epic 4: draw the track's clips on the timeline. The clip
                    // strip sits below the name row; each clip's rectangle comes
                    // from the same x<->time map as the ruler and the playhead.
                    const auto timelineLength = juce::jmax (1.0e-6, session.getTimelineLengthSeconds());

                    for (const auto& clip : session.getClips (index))
                    {
                        auto startSeconds = clip.startSeconds;
                        auto endSeconds = clip.endSeconds;
                        auto fadeIn = clip.fadeInSeconds;
                        auto fadeOut = clip.fadeOutSeconds;

                        const bool isDraggedClip = clipDragActive
                                                    && dragTrackIndex == index
                                                    && dragClipIndex == clip.clipIndex;

                        if (isDraggedClip)
                        {
                            startSeconds = dragPreviewStart;
                            endSeconds = dragPreviewEnd;
                            fadeIn = dragPreviewFadeIn;
                            fadeOut = dragPreviewFadeOut;
                        }

                        const auto x1 = Timeline::xForSeconds (startSeconds, timelineLength, trackLaneArea);
                        const auto x2 = Timeline::xForSeconds (endSeconds, timelineLength, trackLaneArea);
                        auto r = juce::Rectangle<int> (x1, clipBand.getY(),
                                                       juce::jmax (3, x2 - x1),
                                                       clipBand.getHeight());

                        const bool selected = (index == selectedTrackIndex
                                               && clip.clipIndex == selectedClipIndex);

                        g.setColour (selected ? brand::accent.withAlpha (0.55f)
                                              : brand::accentMuted.withAlpha (0.8f));
                        g.fillRoundedRectangle (r.toFloat(), 3.0f);

                        // Fade ramps: a darkening triangle over each attenuated edge.
                        g.setColour (brand::bgWindow.withAlpha (0.6f));

                        if (fadeIn > 0.0)
                        {
                            const auto px = juce::jmax (0, Timeline::xForSeconds (startSeconds + fadeIn,
                                                                                  timelineLength, trackLaneArea) - x1);
                            juce::Path p;
                            p.addTriangle ((float) x1, (float) r.getY(),
                                           (float) (x1 + px), (float) r.getY(),
                                           (float) x1, (float) r.getBottom());
                            g.fillPath (p);
                        }

                        if (fadeOut > 0.0)
                        {
                            const auto px = juce::jmax (0, x2 - Timeline::xForSeconds (endSeconds - fadeOut,
                                                                                       timelineLength, trackLaneArea));
                            juce::Path p;
                            p.addTriangle ((float) x2, (float) r.getY(),
                                           (float) (x2 - px), (float) r.getY(),
                                           (float) x2, (float) r.getBottom());
                            g.fillPath (p);
                        }

                        if (selected)
                        {
                            g.setColour (brand::accent);
                            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 2.0f);
                        }

                        if (r.getWidth() > 40)
                        {
                            g.setColour (brand::textPrimary);
                            g.setFont (brand::uiRegular (10.0f));
                            g.drawText ((clip.isLooping ? juce::String ("\u21bb ") : juce::String())
                                            + clip.name,
                                        r.reduced (6, 0), juce::Justification::centredLeft);
                        }
                    }

                    ++index;
                }
            }
            else
            {
                g.setColour (brand::textSecondary);
                g.setFont (brand::uiRegular (13.0f));
                g.drawText ("No session loaded", lanes, juce::Justification::centred);
            }

            // FR-EXP-2: shade the selected region across the arrangement so the
            // export span is unmistakable (drawn under the playhead).
            if (session.getEdit() != nullptr && session.hasSelection())
            {
                const auto length = session.getTimelineLengthSeconds();
                const auto sx = (float) Timeline::xForSeconds (session.getSelectionStartSeconds(),
                                                               length, trackLaneArea);
                const auto ex = (float) Timeline::xForSeconds (session.getSelectionEndSeconds(),
                                                               length, trackLaneArea);
                g.setColour (brand::accent.withAlpha (0.14f));
                g.fillRect (juce::Rectangle<float> (sx, (float) trackLaneArea.getY() + 2.0f,
                                                    juce::jmax (1.0f, ex - sx),
                                                    (float) trackLaneArea.getHeight() - 4.0f));
            }

            // Playhead running down the arrangement, using the same x<->time map
            // as the ruler so the two lines up exactly.
            if (session.getEdit() != nullptr)
            {
                const auto x = (float) Timeline::xForSeconds (session.getPositionSeconds(),
                                                              session.getTimelineLengthSeconds(),
                                                              trackLaneArea);
                g.setColour (brand::accent.withAlpha (0.85f));
                g.fillRect (juce::Rectangle<float> (x - 1.0f, (float) trackLaneArea.getY() + 2.0f,
                                                    2.0f, (float) trackLaneArea.getHeight() - 4.0f));
            }

            // Snap indicator: a subtle warning-tinted line at the boundary the
            // dragged edge is being pulled onto, distinct from the accent
            // playhead.
            if (clipDragActive && snapActive)
            {
                const auto x = (float) Timeline::xForSeconds (snapTime,
                                                              session.getTimelineLengthSeconds(),
                                                              trackLaneArea);
                g.setColour (brand::warning.withAlpha (0.9f));
                g.fillRect (juce::Rectangle<float> (x - 1.0f, (float) trackLaneArea.getY() + 2.0f,
                                                    2.0f, (float) trackLaneArea.getHeight() - 4.0f));
            }
        }
    }

    void MainComponent::resized()
    {
        // Spacing scale: 4 / 8 / 12 / 16 / 24 / 32.
        auto area = getLocalBounds().reduced (12);

        titleLabel.setBounds (area.removeFromTop (26));
        area.removeFromTop (4);

        devicePanel.setBounds (area.removeFromTop (devicePanelHeight));
        area.removeFromTop (8);

        // Transport row: icon-only squares (>= 32x32) with consistent 8 px gaps.
        // Record is 40x40 to keep its emphasis; the rest are 36x36, vertically
        // centred in the 40 px row. The Click button is icon-only (metronome),
        // followed by a "Count-in" caption + compact combo, so the row never
        // overflows at the minimum window size.
        auto transportRow = area.removeFromTop (40);

        auto placeTransport = [&transportRow] (BrandButton& b, int size)
        {
            const auto w = juce::jmax (b.getPreferredWidth(), size);
            b.setBounds (transportRow.removeFromLeft (w).withSizeKeepingCentre (w, size));
        };

        placeTransport (armButton,       36);
        transportRow.removeFromLeft (8);
        placeTransport (recordButton,    40);
        transportRow.removeFromLeft (8);
        placeTransport (playButton,      36);
        transportRow.removeFromLeft (8);
        placeTransport (stopButton,      36);
        transportRow.removeFromLeft (8);
        placeTransport (goToStartButton, 36);
        transportRow.removeFromLeft (16);
        placeTransport (monitorButton,   36);
        transportRow.removeFromLeft (8);
        placeTransport (metronomeButton, 36);
        transportRow.removeFromLeft (8);
        countInLabel.setBounds (transportRow.removeFromLeft (54).withSizeKeepingCentre (54, 20));
        transportRow.removeFromLeft (6);
        countInBox.setBounds (transportRow.removeFromLeft (110).withSizeKeepingCentre (110, 28));
        transportRow.removeFromLeft (8);

        // Clamp so a very narrow row can never hand a label a negative width
        // (the fixed controls above already fit at the 1100 px minimum window).
        transportLabel.setBounds (transportRow.withWidth (juce::jmax (0, transportRow.getWidth())));

        area.removeFromTop (8);

        // Epic 4: arrangement edit row (Undo/Redo + clip operations). Icon-only
        // 32x32 squares with the same 4 px grouping as before; `iconOnly` makes
        // `getPreferredWidth()` return the square size, so the old text-width
        // clamp (jmax ..., 68) is gone.
        auto editRow = area.removeFromTop (32);

        for (auto* b : { &undoButton, &redoButton, &splitClipButton, &deleteClipButton,
                         &duplicateClipButton, &loopClipButton, &crossfadeButton,
                         &stretchClipButton })
        {
            b->setBounds (editRow.removeFromLeft (32).withSizeKeepingCentre (32, 32));
            editRow.removeFromLeft (4);
        }

        area.removeFromTop (8);

        // Bottom-anchored: status line, then the action row (which may wrap to a
        // second line when the window is narrow — layoutActionRow reports the
        // height it consumed).
        auto bottom = area;
        statusLabel.setBounds (bottom.removeFromBottom (22));
        bottom.removeFromBottom (4);

        const auto actionHeight = layoutActionRow (bottom);
        bottom.removeFromBottom (actionHeight);
        bottom.removeFromBottom (8);

        // Mixer strip below the arrangement lanes.
        if (bottom.getHeight() > MixerPanel::preferredHeight + 80)
        {
            mixerPanel.setVisible (true);
            mixerPanel.setBounds (bottom.removeFromBottom (MixerPanel::preferredHeight));
            bottom.removeFromBottom (8);
        }
        else
        {
            mixerPanel.setVisible (false);
        }

        // Epic 3 overlays: centred panels over the arrangement.
        if (pluginBrowser.isVisible() || routingPanel.isVisible())
        {
            auto overlay = getLocalBounds().reduced (20);
            const auto w = juce::jmin (780, overlay.getWidth());
            const auto h = juce::jmin (520, overlay.getHeight());
            auto r = juce::Rectangle<int> (w, h).withCentre (overlay.getCentre());
            pluginBrowser.setBounds (r);
            routingPanel.setBounds (r);
        }

        auto middle = bottom;
        inputMeter.setBounds (middle.removeFromRight (180).reduced (2));
        middle.removeFromRight (6);

        // Ruler directly above the arrangement lanes, same width as the lanes so
        // a click on the ruler and the lane playhead line share one x<->time map.
        timeline.setBounds (middle.removeFromTop (Timeline::preferredHeight));
        middle.removeFromTop (2);

        // FR-EXP-2: selection start/end readout just under the ruler.
        selectionLabel.setBounds (middle.removeFromTop (16));

        middle.removeFromTop (2);
        trackLaneArea = middle;

        rebuildTrackLaneRects();
    }

    void MainComponent::rebuildTrackLaneRects()
    {
        trackLaneRects.clear();

        if (trackLaneArea.isEmpty() || session.getEdit() == nullptr)
            return;

        // One *uniform* height for every lane (owner bug fix): the input track and
        // every imported "minus" lane used to differ, which made the shorter
        // clips' fades/trim handles hard to hit. See ui/TrackLaneLayout.h.
        trackLaneRects = track_lane::computeLaneRects (trackLaneArea, session.getNumAudioTracks());
    }

    juce::Rectangle<int> MainComponent::clipRectFor (int trackIndex, const Session::ClipInfo& info) const
    {
        if (trackIndex < 0 || trackIndex >= (int) trackLaneRects.size())
            return {};

        // Same split as paint(): name row on top, clip strip below.
        juce::Rectangle<int> nameRow, band;
        track_lane::splitLane (trackLaneRects[(size_t) trackIndex], nameRow, band);

        const auto length = juce::jmax (1.0e-6, session.getTimelineLengthSeconds());
        const auto x1 = Timeline::xForSeconds (info.startSeconds, length, trackLaneArea);
        const auto x2 = Timeline::xForSeconds (info.endSeconds, length, trackLaneArea);
        return { x1, band.getY(), juce::jmax (3, x2 - x1), band.getHeight() };
    }

    int MainComponent::hitTestClip (int trackIndex, juce::Point<int> position) const
    {
        const auto clips = session.getClips (trackIndex);

        // Topmost first: the last drawn (highest start / highest index) wins.
        for (auto it = clips.rbegin(); it != clips.rend(); ++it)
            if (clipRectFor (trackIndex, *it).contains (position))
                return it->clipIndex;

        return -1;
    }

    MainComponent::ClipDragMode MainComponent::zoneForPoint (int trackIndex,
                                                             const Session::ClipInfo& info,
                                                             juce::Point<int> position) const
    {
        const auto rect = clipRectFor (trackIndex, info);
        const auto edge = juce::jlimit (4, 10, rect.getWidth() / 4);
        const auto x = position.x;
        const bool nearTop = position.y <= rect.getY() + 8;

        // Fade corners first: they sit inside the top 8 px of the clip and win
        // over the trim edges, exactly as before (so a click and the cursor
        // agree).
        if (nearTop && x <= rect.getX() + 16)
            return ClipDragMode::fadeIn;

        if (nearTop && x >= rect.getRight() - 16)
            return ClipDragMode::fadeOut;

        if (x <= rect.getX() + edge)
            return ClipDragMode::trimStart;

        if (x >= rect.getRight() - edge)
            return ClipDragMode::trimEnd;

        return ClipDragMode::move;
    }

    juce::MouseCursor MainComponent::cursorForMode (ClipDragMode mode) noexcept
    {
        switch (mode)
        {
            case ClipDragMode::trimStart: return juce::MouseCursor::LeftEdgeResizeCursor;
            case ClipDragMode::trimEnd:   return juce::MouseCursor::RightEdgeResizeCursor;
            // Fade handles are the clip's top corners: corner-resize cursors read
            // as "grab this corner".
            case ClipDragMode::fadeIn:    return juce::MouseCursor::TopLeftCornerResizeCursor;
            case ClipDragMode::fadeOut:   return juce::MouseCursor::TopRightCornerResizeCursor;
            case ClipDragMode::move:
            case ClipDragMode::none:
            default:                      return juce::MouseCursor::NormalCursor;
        }
    }

    juce::MouseCursor MainComponent::getMouseCursor()
    {
        // While a drag is in flight the zone it started with wins, so the cursor
        // does not flicker when the pointer strays off the clip.
        if (clipDragActive && clipDragMode != ClipDragMode::none)
            return cursorForMode (clipDragMode);

        if (! isEnabled())
            return juce::MouseCursor::NormalCursor;

        const auto position = getMouseXYRelative();

        for (size_t i = 0; i < trackLaneRects.size(); ++i)
        {
            if (! trackLaneRects[i].contains (position))
                continue;

            const auto trackIndex = (int) i;
            const auto clipIndex = hitTestClip (trackIndex, position);

            if (clipIndex < 0)
                break; // empty lane area: no clip cursor

            return cursorForMode (zoneForPoint (trackIndex, clipInfoFor (trackIndex, clipIndex),
                                                position));
        }

        return juce::MouseCursor::NormalCursor;
    }

    Session::ClipInfo MainComponent::clipInfoFor (int trackIndex, int clipIndex) const
    {
        Session::ClipInfo info;
        session.getClipInfo (trackIndex, clipIndex, info);
        return info;
    }

    bool MainComponent::selectedClipExists() const
    {
        Session::ClipInfo info;
        return selectedTrackIndex >= 0 && selectedClipIndex >= 0
               && session.getClipInfo (selectedTrackIndex, selectedClipIndex, info);
    }

    Session::ClipInfo MainComponent::selectedClipInfo() const
    {
        return clipInfoFor (selectedTrackIndex, selectedClipIndex);
    }

    void MainComponent::selectClip (int trackIndex, int clipIndex)
    {
        selectedTrackIndex = clipIndex >= 0 ? trackIndex : -1;
        selectedClipIndex = clipIndex >= 0 ? clipIndex : -1;
        refreshEditButtons();
        repaint();
    }

    void MainComponent::commitClipDrag()
    {
        if (! clipDragActive)
            return;

        clipDragActive = false;
        snapActive = false;
        const auto mode = clipDragMode;
        clipDragMode = ClipDragMode::none;

        constexpr double eps = 1.0e-4;
        bool changed = false;

        // Clear any stale error so a failed drag reports the *current* failure
        // (below), not a message left over from an earlier operation.
        session.clearLastError();

        switch (mode)
        {
            case ClipDragMode::move:
                if (std::abs (dragPreviewStart - dragOriginalStart) > eps)
                    changed = session.moveClip (dragTrackIndex, dragClipIndex, dragPreviewStart);
                break;

            case ClipDragMode::trimStart:
                if (std::abs (dragPreviewStart - dragOriginalStart) > eps)
                    changed = session.trimClipStart (dragTrackIndex, dragClipIndex, dragPreviewStart);
                break;

            case ClipDragMode::trimEnd:
                if (std::abs (dragPreviewEnd - dragOriginalEnd) > eps)
                    changed = session.trimClipEnd (dragTrackIndex, dragClipIndex, dragPreviewEnd);
                break;

            case ClipDragMode::fadeIn:
                // A click on the fade corner with no movement must not create an
                // empty undo transaction.
                if (std::abs (dragPreviewFadeIn - dragOriginalFadeIn) > eps)
                    changed = session.setClipFadeIn (dragTrackIndex, dragClipIndex, dragPreviewFadeIn);
                break;

            case ClipDragMode::fadeOut:
                if (std::abs (dragPreviewFadeOut - dragOriginalFadeOut) > eps)
                    changed = session.setClipFadeOut (dragTrackIndex, dragClipIndex, dragPreviewFadeOut);
                break;

            case ClipDragMode::none:
            default:
                break;
        }

        if (changed)
            showStatus ("Clip edited.");
        else if (mode != ClipDragMode::none && ! session.getLastError().isEmpty())
            showStatus (session.getLastError(), true);

        refreshEditButtons();
        repaint();
    }

    std::vector<double> MainComponent::snapEdgesFor (int excludeTrack, int excludeClip) const
    {
        std::vector<double> edges;

        if (session.getEdit() == nullptr)
            return edges;

        // Every other clip's boundaries, across all tracks: takes on different
        // lanes usually want to line up too.
        for (int track = 0; track < session.getNumAudioTracks(); ++track)
            for (const auto& clip : session.getClips (track))
                if (! (track == excludeTrack && clip.clipIndex == excludeClip))
                {
                    edges.push_back (clip.startSeconds);
                    edges.push_back (clip.endSeconds);
                }

        // Session start, the playhead and a whole-second grid are snap anchors.
        edges.push_back (0.0);
        edges.push_back (session.getPositionSeconds());

        const auto length = session.getTimelineLengthSeconds();

        for (double t = 1.0; t < length; t += 1.0)
            edges.push_back (t);

        return edges;
    }

    double MainComponent::snapThresholdSeconds() const
    {
        const auto length = session.getTimelineLengthSeconds();

        if (length <= 0.0)
            return 0.0;

        const auto span = juce::jmax (1, trackLaneArea.getWidth() - 2 * Timeline::hInset);
        return length * (double) snapThresholdPx / (double) span;
    }

    void MainComponent::mouseDown (const juce::MouseEvent& e)
    {
        if (exportInProgress)
            return;

        grabKeyboardFocus();

        for (size_t i = 0; i < trackLaneRects.size(); ++i)
        {
            if (! trackLaneRects[i].contains (e.getPosition()))
                continue;

            const auto index = (int) i;
            const auto clipIndex = hitTestClip (index, e.getPosition());

            // Clicking a clip selects it and starts a move/trim/fade drag.
            if (clipIndex >= 0)
            {
                selectClip (index, clipIndex);

                // Discoverability hint (owner: "how do I delete the backing
                // track?"): name both delete paths while a clip is selected.
                showStatus ("Clip selected. Drag to move/trim (hold Alt to disable snapping). "
                            "Delete removes this clip; Remove track removes the whole track.");

                const auto info = clipInfoFor (index, clipIndex);
                const auto length = session.getTimelineLengthSeconds();
                const auto mouseSeconds = Timeline::secondsForX (e.getPosition().x, length, trackLaneArea);

                dragTrackIndex = index;
                dragClipIndex = clipIndex;
                dragOriginalStart = info.startSeconds;
                dragOriginalEnd = info.endSeconds;
                dragOriginalFadeIn = info.fadeInSeconds;
                dragOriginalFadeOut = info.fadeOutSeconds;
                dragMouseDownSeconds = mouseSeconds;
                dragPreviewStart = info.startSeconds;
                dragPreviewEnd = info.endSeconds;
                dragPreviewFadeIn = info.fadeInSeconds;
                dragPreviewFadeOut = info.fadeOutSeconds;
                clipDragActive = true;
                snapActive = false;
                clipDragMode = zoneForPoint (index, info, e.getPosition());

                return;
            }

            // Empty lane area keeps the Epic 1/2 arm/disarm behaviour.
            if (! session.getInputTrackIndices().contains (index))
            {
                showStatus (session.getTrackName (index) + " is a backing track (playback only).");
                return;
            }

            const auto nowArmed = ! session.isTrackArmed (index);

            if (session.setTrackArmed (index, nowArmed))
                showStatus ((nowArmed ? "Armed: " : "Disarmed: ") + session.getTrackName (index));
            else
                showStatus (session.getLastError(), true);

            refreshTransportUi();
            return;
        }

        // Clicked outside every lane: drop the clip selection.
        selectClip (-1, -1);
    }

    void MainComponent::mouseDrag (const juce::MouseEvent& e)
    {
        if (! clipDragActive)
            return;

        const auto length = session.getTimelineLengthSeconds();
        const auto mouseSeconds = Timeline::secondsForX (e.getPosition().x, length, trackLaneArea);
        const auto delta = mouseSeconds - dragMouseDownSeconds;
        constexpr double minLen = 0.01;

        // Snap is on by default; holding Alt bypasses it for one drag so clips can
        // be placed free of the grid (documented in docs/USAGE*.md).
        const bool snapping = ! e.mods.isAltDown();
        const auto threshold = snapping ? snapThresholdSeconds() : 0.0;
        const auto edges = snapping ? snapEdgesFor (dragTrackIndex, dragClipIndex)
                                    : std::vector<double> {};

        snapActive = false;

        switch (clipDragMode)
        {
            case ClipDragMode::move:
            {
                const auto clipLength = dragOriginalEnd - dragOriginalStart;
                dragPreviewStart = juce::jmax (0.0, dragOriginalStart + delta);
                dragPreviewEnd = dragPreviewStart + clipLength;

                if (snapping)
                {
                    const auto startSnap = clipsnap::snapTimeToEdges (dragPreviewStart, edges, threshold);

                    // The trailing edge is a snap candidate too; an end-snap that
                    // would push the start before 0 is rejected.
                    const auto endSnap = clipsnap::snapTimeToEdges (dragPreviewEnd, edges, threshold);
                    const bool endUsable = endSnap.snapped && endSnap.time - clipLength >= 0.0;

                    const auto startCorrection = startSnap.snapped
                                                     ? std::abs (startSnap.time - dragPreviewStart)
                                                     : threshold + 1.0;
                    const auto endCorrection = endUsable
                                                   ? std::abs (endSnap.time - dragPreviewEnd)
                                                   : threshold + 1.0;

                    // Start wins ties so the result is deterministic.
                    if (startSnap.snapped && startCorrection <= endCorrection)
                    {
                        dragPreviewStart = startSnap.time;
                        dragPreviewEnd = dragPreviewStart + clipLength;
                        snapActive = true;
                        snapTime = startSnap.time;
                    }
                    else if (endUsable)
                    {
                        dragPreviewEnd = endSnap.time;
                        dragPreviewStart = dragPreviewEnd - clipLength;
                        snapActive = true;
                        snapTime = endSnap.time;
                    }
                }
                break;
            }

            case ClipDragMode::trimStart:
            {
                dragPreviewStart = juce::jlimit (0.0, dragOriginalEnd - minLen,
                                                 dragOriginalStart + delta);
                dragPreviewEnd = dragOriginalEnd;

                if (snapping)
                {
                    const auto snap = clipsnap::snapTimeToEdges (dragPreviewStart, edges, threshold);

                    // Never let a snap collapse the clip past the minimum length.
                    if (snap.snapped && snap.time <= dragOriginalEnd - minLen)
                    {
                        dragPreviewStart = snap.time;
                        snapActive = true;
                        snapTime = snap.time;
                    }
                }
                break;
            }

            case ClipDragMode::trimEnd:
            {
                dragPreviewEnd = juce::jmax (dragOriginalStart + minLen, dragOriginalEnd + delta);
                dragPreviewStart = dragOriginalStart;

                if (snapping)
                {
                    const auto snap = clipsnap::snapTimeToEdges (dragPreviewEnd, edges, threshold);

                    if (snap.snapped && snap.time >= dragOriginalStart + minLen)
                    {
                        dragPreviewEnd = snap.time;
                        snapActive = true;
                        snapTime = snap.time;
                    }
                }
                break;
            }

            case ClipDragMode::fadeIn:
                dragPreviewFadeIn = juce::jlimit (0.0, dragPreviewEnd - dragPreviewStart,
                                                  dragOriginalFadeIn + delta);
                break;

            case ClipDragMode::fadeOut:
                dragPreviewFadeOut = juce::jlimit (0.0, dragPreviewEnd - dragPreviewStart,
                                                   dragOriginalFadeOut + delta);
                break;

            case ClipDragMode::none:
            default:
                break;
        }

        repaint();
    }

    void MainComponent::mouseUp (const juce::MouseEvent&)
    {
        commitClipDrag();
    }

    bool MainComponent::keyPressed (const juce::KeyPress& key)
    {
        const auto mods = key.getModifiers();
        const auto code = key.getKeyCode();

        if (mods.isCommandDown() && (code == 'z' || code == 'Z'))
        {
            if (mods.isShiftDown())
                redoEdit();
            else
                undoEdit();

            return true;
        }

        if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        {
            deleteSelectedClip();
            return true;
        }

        if (! mods.isCommandDown() && ! mods.isAltDown())
        {
            if (code == 's' || code == 'S') { splitSelectedAtPlayhead(); return true; }
            if (code == 'd' || code == 'D') { duplicateSelectedClip(); return true; }
            if (code == 'l' || code == 'L') { loopSelectedClip(); return true; }
            if (code == 'f' || code == 'F') { crossfadeSelectedClip(); return true; }
        }

        return false;
    }

    void MainComponent::splitSelectedAtPlayhead()
    {
        if (! selectedClipExists())
            return;

        const auto playhead = session.getPositionSeconds();

        if (! session.splitClip (selectedTrackIndex, selectedClipIndex, playhead))
            showStatus (session.getLastError().isNotEmpty() ? session.getLastError()
                                                           : "Move the playhead inside the clip to split it.",
                        session.getLastError().isNotEmpty());
        else
            showStatus ("Split clip at " + formatTime (playhead) + ".");

        selectClip (-1, -1);
        refreshTransportUi();
    }

    void MainComponent::deleteSelectedClip()
    {
        if (! selectedClipExists())
            return;

        if (session.deleteClip (selectedTrackIndex, selectedClipIndex))
            showStatus ("Deleted clip.");
        else
            showStatus (session.getLastError(), true);

        selectClip (-1, -1);
        refreshTransportUi();
    }

    void MainComponent::duplicateSelectedClip()
    {
        if (! selectedClipExists())
            return;

        const int newIndex = session.duplicateClip (selectedTrackIndex, selectedClipIndex);

        if (newIndex >= 0)
        {
            selectClip (selectedTrackIndex, newIndex);
            showStatus ("Duplicated clip.");
        }
        else
        {
            showStatus (session.getLastError(), true);
        }

        refreshTransportUi();
    }

    void MainComponent::loopSelectedClip()
    {
        if (! selectedClipExists())
            return;

        const auto info = selectedClipInfo();
        const int loops = info.isLooping ? 0 : 2; // toggle between off and x2

        if (session.setClipLoop (selectedTrackIndex, selectedClipIndex, loops))
            showStatus (loops > 1 ? "Looped clip x2." : "Looping disabled.");
        else
            showStatus (session.getLastError(), true);

        refreshTransportUi();
    }

    void MainComponent::crossfadeSelectedClip()
    {
        if (! selectedClipExists())
            return;

        if (session.crossfadeClipWithNext (selectedTrackIndex, selectedClipIndex, 0.05))
            showStatus ("Crossfaded clip with the next clip (50 ms).");
        else
            showStatus (session.getLastError().isNotEmpty() ? session.getLastError()
                                                           : "No adjacent clip to crossfade with.",
                        true);

        refreshTransportUi();
    }

    void MainComponent::stretchSelectedClip()
    {
        if (! selectedClipExists())
            return;

        if (! session.hasSelection())
        {
            showStatus ("Select a region on the ruler first; the clip stretches to the region length.", true);
            return;
        }

        const auto target = session.getSelectionEndSeconds() - session.getSelectionStartSeconds();

        if (session.stretchClipToDuration (selectedTrackIndex, selectedClipIndex, target))
            showStatus ("Time-stretched clip to " + juce::String (target, 3) + " s.");
        else
            showStatus (session.getLastError(), true);

        selectClip (-1, -1);
        refreshTransportUi();
    }

    void MainComponent::undoEdit()
    {
        if (! session.undo())
            return;

        showStatus ("Undo.");
        selectClip (-1, -1);
        refreshTransportUi();
    }

    void MainComponent::redoEdit()
    {
        if (! session.redo())
            return;

        showStatus ("Redo.");
        selectClip (-1, -1);
        refreshTransportUi();
    }

    void MainComponent::refreshEditButtons()
    {
        const bool hasEdit = session.getEdit() != nullptr
                             && ! exportInProgress && ! stemsInProgress;
        const bool hasSelection = hasEdit && selectedClipExists();

        undoButton.setEnabled (hasEdit && session.canUndo());
        redoButton.setEnabled (hasEdit && session.canRedo());
        splitClipButton.setEnabled (hasSelection);
        deleteClipButton.setEnabled (hasSelection);
        duplicateClipButton.setEnabled (hasSelection);
        loopClipButton.setEnabled (hasSelection);
        crossfadeButton.setEnabled (hasSelection);
        stretchClipButton.setEnabled (hasSelection && session.hasSelection());
    }

    int MainComponent::layoutActionRow (juce::Rectangle<int> area)
    {
        actionDividers.clear();

        constexpr int gap = 8;
        constexpr int rowHeight = 32;
        constexpr int dividerWidth = 1;

        struct Item
        {
            BrandButton* button = nullptr;
            bool divider = false;
            int width = 0;
        };

        // Groups: [New][Open][Save][Save As] · [Close][Import][Normalize][Export]
        // [Add track][Remove track] · [Settings][About] — all icon-only now.
        BrandButton* const buttons[] = { &newButton, &openButton, &saveButton, &saveAsButton,
                                         &closeButton, &importButton, &normaliseButton, &exportButton,
                                         &exportRegionButton, &clearRegionButton,
                                         &stemsButton, &pluginsButton, &routingButton,
                                         &addTrackButton, &removeTrackButton,
                                         &settingsButton, &aboutButton };
        const int groups[] = { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2 };
        constexpr int numButtons = (int) std::size (buttons);

        std::vector<Item> items;
        items.reserve (numButtons + 2);
        int lastGroup = -1;

        for (int i = 0; i < numButtons; ++i)
        {
            if (groups[i] != lastGroup)
            {
                if (lastGroup >= 0)
                    items.push_back ({ nullptr, true, dividerWidth });

                lastGroup = groups[i];
            }

            items.push_back ({ buttons[i], false, buttons[i]->getPreferredWidth() });
        }

        const int left = area.getX();
        const int right = area.getRight();
        const int bottom = area.getBottom();

        int row = 0;
        int x = left;

        for (auto& item : items)
        {
            const int advance = item.divider ? item.width + 2 * gap : item.width + gap;

            if (x > left && x + advance > right)
            {
                ++row;
                x = left;

                if (item.divider)
                    continue; // drop a divider that would land at a row start
            }

            const int rowTop = bottom - (row + 1) * rowHeight - row * gap;

            if (item.divider)
            {
                actionDividers.push_back ({ x + gap, rowTop + 6, dividerWidth, rowHeight - 12 });
            }
            else
            {
                item.button->setBounds (x, rowTop, item.width, rowHeight);
            }

            x += advance;
        }

        return (row + 1) * rowHeight + row * gap;
    }
}
