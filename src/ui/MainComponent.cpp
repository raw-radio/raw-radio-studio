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
#include <cstdlib>
#include <iostream>

#ifndef RAW_RADIO_STUDIO_VERSION
 #define RAW_RADIO_STUDIO_VERSION "0.1.3-dev"
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
                              &midiTrackButton, &pianoRollButton,
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

        // The plugin browser's Track combo follows the timeline selection (never
        // silently defaults to track 0 / the mic input), and an instrument insert
        // onto an input track spawns a separate MIDI track — select it and report
        // clearly, so the engineer knows the input path is untouched.
        pluginBrowser.selectedTrackProvider = [this] { return selectedTrackIndex; };
        pluginBrowser.onInstrumentTrackCreated = [this] (int trackIndex)
        {
            selectTrack (trackIndex);
            pluginBrowser.setTargetTrack (trackIndex);
            showStatus ("Created a new MIDI track \"" + session.getTrackName (trackIndex)
                        + "\" for the instrument — your input track and its monitoring are untouched.");
        };
        routingPanel.onClose = [this] { routingPanel.setVisible (false); };

        // Epic 5 piano-roll editor overlay.
        addChildComponent (pianoRoll);
        pianoRoll.onClose = [this] { pianoRoll.setVisible (false); };

        addAndMakeVisible (countInBox);
        addAndMakeVisible (midiInputLabel);
        addAndMakeVisible (midiInputBox);

        midiInputLabel.setText ("MIDI in", juce::dontSendNotification);
        midiInputLabel.setFont (brand::uiRegular (12.0f));
        midiInputLabel.setColour (juce::Label::textColourId, brand::textSecondary);
        midiInputLabel.setJustificationType (juce::Justification::centredRight);
        midiInputBox.setTooltip ("MIDI input device routed to the armed MIDI track. "
                                 "Arm a MIDI track, select a device and Record to capture notes.");
        midiInputBox.onChange = [this]
        {
            session.setSelectedMidiInput (midiInputBox.getText());
            refreshTransportUi();
        };

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
                                      "clips). Removes the selected track — click a lane header "
                                      "to select one, even a track with no clips — or the last "
                                      "track when none is selected. Use it to remove an imported "
                                      "backing track. To delete only a clip, use Delete.");

        // Epic 5: add an instrument (MIDI) track and open the piano-roll editor.
        midiTrackButton.setIconName ("waveform");
        midiTrackButton.setIconOnly (true);
        midiTrackButton.setTooltip ("Add a MIDI/instrument track (no audio input; hosts a "
                                    "virtual instrument and records MIDI)");
        pianoRollButton.setIconName ("metronome");
        pianoRollButton.setIconOnly (true);
        pianoRollButton.setTooltip ("Piano roll: edit the MIDI notes of the selected clip");

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

        // --- Epic 8 phase 1a: sidebar, lane scrollbar, lane-height preset --------
        // The device panel now lives inside the sidebar's «Device» section; the
        // section re-lays out when the panel's error state changes its height.
        devicePanel.onLayoutChanged = [this] { resized(); repaint(); };

        sidebarPanel.channelsSummaryProvider = [this] { return channelsSummaryText(); };
        sidebarPanel.statusTextProvider     = [this] { return statusText(); };
        // Live drag frames: re-layout + repaint only. Committing to disk happens
        // on mouse-up / on a toggle via onStateChanged (spec §1.7).
        sidebarPanel.onLayoutChanged        = [this] { resized(); repaint(); };
        sidebarPanel.onStateChanged         = [this] { persistUiState(); resized(); repaint(); };
        addAndMakeVisible (sidebarPanel);

        // The mixer's manual collapse (header click) re-lays out the bottom row
        // and persists the state; the auto-hide-by-height fallback in resized()
        // still takes priority when the window is too short.
        mixerPanel.onCollapsedChanged = [this] { persistUiState(); resized(); repaint(); };
        // The mixer routes e.g. "why can't I arm this?" to the status line (RT-4 M2).
        mixerPanel.onStatus = [this] (const juce::String& message, bool isError) { showStatus (message, isError); };

        // Owner decision R-B: the lane-height switcher is a compact icon-dropdown
        // at the right edge of the edit row (not inside the sidebar).
        heightPresetButton.setIconName ("arrows-horizontal");
        heightPresetButton.setIconOnly (true, 32);
        heightPresetButton.setTooltip ("Track height: compact 56 / normal 72 / large 96 px");
        heightPresetButton.onClick = [this] { showHeightPresetMenu(); };
        addAndMakeVisible (heightPresetButton);

        // One vertical scrollbar for the track lanes (replaces the old
        // compress-to-1-px behaviour when many tracks share a short window).
        laneScrollBar = std::make_unique<juce::ScrollBar> (true);
        laneScrollBar->setAutoHide (false);
        laneScrollBar->setColour (juce::ScrollBar::thumbColourId, brand::bgElevated);
        laneScrollBar->setColour (juce::ScrollBar::trackColourId, brand::bgWindow);
        laneScrollBar->setColour (juce::ScrollBar::backgroundColourId, brand::bgWindow);
        laneScrollBar->addListener (this);
        addAndMakeVisible (*laneScrollBar);
        laneScrollBar->setVisible (false);

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
            {
                showStatus (session.getLastError(), true);
            }
            else
            {
                const auto name = session.getTrackName (index);

                // RT-2b D1: do not paper over a missing input with a plain success
                // message — surface the concrete reason on the status line.
                const auto problem = session.getTrackInputProblem (index);

                if (problem.isNotEmpty())
                    showStatus ("Added " + name + " \u2014 " + problem, true);
                else
                    showStatus ("Added " + name + " ("
                                + juce::String (session.getNumAudioTracks()) + " tracks)");
            }

            refreshTransportUi();
        };
        removeTrackButton.onClick = [this]
        {
            if (exportInProgress)
                return;

            // Remove the *selected* track. A track is selected by clicking its
            // lane header (even when it has no clips — owner bug: after deleting
            // an imported clip the empty track could not be targeted). When no
            // track is selected, fall back to the last track (original
            // behaviour). This is how the owner deletes an imported backing
            // track ("minus") without it having to be the last one.
            const auto numTracks = session.getNumAudioTracks();
            const int target = (selectedTrackIndex >= 0 && selectedTrackIndex < numTracks)
                                   ? selectedTrackIndex
                                   : numTracks - 1;
            const auto name = session.getTrackName (target);

            if (target < 0 || ! session.removeAudioTrack (target))
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

        // Epic 5: add a MIDI/instrument track.
        midiTrackButton.onClick = [this]
        {
            if (exportInProgress || session.getEdit() == nullptr)
                return;

            const auto index = session.addMidiTrack();

            if (index < 0)
            {
                showStatus (session.getLastError(), true);
            }
            else
            {
                selectTrack (index);
                showStatus ("Added MIDI track: " + session.getTrackName (index)
                            + " (open Plugins > Instrument to host a synth, then arm + record)");
            }

            refreshMidiInputs();
            refreshTransportUi();
        };

        pianoRollButton.onClick = [this] { openPianoRoll(); };

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

        if (laneScrollBar != nullptr)
            laneScrollBar->removeListener (this);

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

            // Arm the selected lane when one is selected (input or MIDI); fall
            // back to the first armable track otherwise so Arm does not error
            // when a playback-only backing track is selected (Epic 1 single-track
            // flow).
            const int target = armTargetTrack();

            if (! session.setTrackArmed (target, armButton.getToggleState()))
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
            pianoRoll.setVisible (false);
        }

        // Epic 5: MIDI track + piano-roll actions and the MIDI input selector.
        midiTrackButton.setEnabled (hasEdit && ! busy);
        pianoRollButton.setEnabled (hasEdit && ! busy && session.getNumMidiTracks() > 0);
        refreshMidiInputs();
        midiInputLabel.setEnabled (hasEdit && ! busy);
        midiInputBox.setEnabled (hasEdit && ! busy);

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
        armButton.setToggleState (session.isTrackArmed (armTargetTrack()),
                                  juce::dontSendNotification);
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

        // Epic 8 phase 1a/1b: the lane scrollbar / hit-rect cache / input-track
        // cache must follow the track set (this runs on every Session change and
        // at ~10 Hz), not just a window resize — otherwise adding/opening tracks
        // would leave the lanes scrolled-but-unscrollable, the scrollbar hidden,
        // or the lane controls using a stale input-track list.
        inputTrackIndexCache = session.getInputTrackIndices();
        updateLaneScrollBar();
        rebuildTrackLaneRects();

        repaint();
    }

    void MainComponent::refreshMidiInputs()
    {
        const auto names = session.getMidiInputDeviceNames();
        const auto selected = session.getSelectedMidiInputName();

        // Rebuild only when the device set changed, so an open combo popup is not
        // disturbed by the 10 Hz refresh.
        bool changed = names.size() != midiInputBox.getNumItems();

        if (! changed)
            for (int i = 0; i < names.size(); ++i)
                if (midiInputBox.getItemText (i) != names[i])
                {
                    changed = true;
                    break;
                }

        if (changed)
        {
            midiInputBox.clear (juce::dontSendNotification);

            for (int i = 0; i < names.size(); ++i)
                midiInputBox.addItem (names[i], i + 1);
        }

        if (names.isEmpty())
            midiInputBox.setText ("No MIDI inputs", juce::dontSendNotification);
        else if (selected.isNotEmpty() && names.contains (selected))
            midiInputBox.setText (selected, juce::dontSendNotification);
        else
            midiInputBox.setText (names[0], juce::dontSendNotification);
    }

    int MainComponent::armTargetTrack() const
    {
        // Prefer the selected track when it can actually be armed (input or MIDI).
        if (selectedTrackIndex >= 0
            && (session.getInputTrackIndices().contains (selectedTrackIndex)
                || session.isMidiTrack (selectedTrackIndex)))
            return selectedTrackIndex;

        // Otherwise fall back to the first armable track: an input track first
        // (Epic 1 single-track flow), then a MIDI track, finally track 0.
        const auto inputs = session.getInputTrackIndices();

        if (! inputs.isEmpty())
            return inputs[0];

        for (int i = 0; i < session.getNumAudioTracks(); ++i)
            if (session.isMidiTrack (i))
                return i;

        return 0;
    }

    void MainComponent::openPianoRoll()
    {
        if (exportInProgress || session.getEdit() == nullptr)
            return;

        int track = -1;
        int clip = -1;

        // Prefer the selected clip when it is on a MIDI track.
        if (selectedTrackIndex >= 0 && session.isMidiTrack (selectedTrackIndex)
            && selectedClipIndex >= 0)
        {
            Session::MidiClipInfo info;

            if (session.getMidiClipInfo (selectedTrackIndex, selectedClipIndex, info))
            {
                track = selectedTrackIndex;
                clip = selectedClipIndex;
            }
        }

        if (clip < 0)
        {
            int midiTrack = -1;

            if (selectedTrackIndex >= 0 && session.isMidiTrack (selectedTrackIndex))
            {
                midiTrack = selectedTrackIndex;
            }
            else
            {
                for (int i = 0; i < session.getNumAudioTracks(); ++i)
                    if (session.isMidiTrack (i))
                    {
                        midiTrack = i;
                        break;
                    }
            }

            if (midiTrack < 0)
            {
                showStatus ("Add a MIDI track first, then host an instrument and record MIDI.", true);
                return;
            }

            const auto clips = session.getMidiClips (midiTrack);

            if (clips.empty())
            {
                const auto created = session.createMidiClip (midiTrack, 0.0, 4.0);

                if (created < 0)
                {
                    showStatus (session.getLastError(), true);
                    return;
                }

                track = midiTrack;
                clip = created;
            }
            else
            {
                track = midiTrack;
                clip = clips.front().clipIndex;
            }
        }

        pianoRoll.open (track, clip);
        pluginBrowser.setVisible (false);
        routingPanel.setVisible (false);
        pianoRoll.toFront (false);
        resized();
        showStatus ("Piano roll open. Click to add notes; add a synth via Plugins > Instrument to hear them.");
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
    void MainComponent::confirmUnsavedChanges (const juce::String& title, std::function<void()> onProceed,
                                               std::function<void()> onCancel)
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
            [safe, onProceed, onCancel] (int result)
            {
                auto* self = safe.getComponent();

                if (self == nullptr)
                    return;

                if (result == 0) // Save
                {
                    if (! self->session.save())
                    {
                        self->showStatus (self->session.getLastError(), true);

                        // The operation is aborted: let the caller release any
                        // re-entrancy guard it took before showing the prompt.
                        if (onCancel)
                            onCancel();

                        return; // save failed: keep the session
                    }

                    onProceed();
                }
                else if (result == 1) // Discard
                {
                    onProceed();
                }
                else // Cancel
                {
                    if (onCancel)
                        onCancel();
                }
            });
    }

    void MainComponent::requestQuit()
    {
        // A quit already awaiting the user's decision: ignore repeat events so a
        // second Cmd+Q (or a window close racing the dialog) cannot stack prompts.
        if (quitConfirmPending)
            return;

        if (session.getEdit() == nullptr || ! session.hasUnsavedChanges())
        {
            juce::JUCEApplicationBase::getInstance()->quit();
            return;
        }

        quitConfirmPending = true;

        confirmUnsavedChanges (
            "Quit raw-radio-studio",
            [this]
            {
                quitConfirmPending = false;
                juce::JUCEApplicationBase::getInstance()->quit();
            },
            [this] { quitConfirmPending = false; });
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
        pianoRoll.setVisible (false);

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

        // Epic 8 phase 1a: restore the sidebar / mixer / lane-height UI state.
        applyLoadedUiState();
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
                    const auto report = self->session.getLastRecordingRecovery();

                    // Never a silent recovery: always report what was imported
                    // and, when a file had to be skipped, why (an empty take can
                    // simply mean a crash before any audio reached disk).
                    if (report.imported > 0 || ! report.skippedFiles.isEmpty())
                    {
                        juce::String recoveryMessage = "Recovery: " + report.summary() + ".";

                        if (! report.skippedFiles.isEmpty())
                            recoveryMessage << " Skipped: " << report.skippedFiles.joinIntoString ("; ");

                        std::cout << "[recovery] " << recoveryMessage << std::endl;
                        self->showStatus (recoveryMessage, ! report.skippedFiles.isEmpty());
                    }

                    juce::ignoreUnused (recovered);
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
        // A new/opened session has different tracks: drop any stale selection so
        // Remove track can never target a track index from the previous edit.
        selectClip (-1, -1);

        // Epic 8 phase 2: build the clip-thumbnail cache for the new session's
        // clips now (off the paint path).
        liveRecordingThumbnail = nullptr;
        liveRecordingFile = juce::File();
        preloadClipThumbnails();

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
        // The track/clip set changed (Epic 8 phase 2): forget the live take when
        // recording stopped, create the thumbnails we need OFF the paint path, and
        // drop the stale ones.
        if (! session.isRecording())
        {
            liveRecordingThumbnail = nullptr;
            liveRecordingFile = juce::File();
        }

        preloadClipThumbnails();
        pruneClipThumbnails();
        refreshTransportUi();
    }

    //==============================================================================
    // Epic 8 phase 1a — sidebar / lane scroll / height preset state.
    //==============================================================================
    void MainComponent::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
    {
        if (updatingLaneScrollBar)
            return;

        const int numTracks = session.getEdit() != nullptr ? session.getNumAudioTracks() : 0;
        const int laneHeight = track_lane::uniformLaneHeight (trackLaneArea.getHeight(), numTracks,
                                                              trackHeightPreset);
        laneScrollOffsetY = track_lane::clampScrollOffset ((int) std::lround (newRangeStart),
                                                           trackLaneArea.getHeight(), numTracks, laneHeight);
        rebuildTrackLaneRects();
        repaint();
    }

    void MainComponent::updateLaneScrollBar()
    {
        if (laneScrollBar == nullptr)
            return;

        const int numTracks = session.getEdit() != nullptr ? session.getNumAudioTracks() : 0;
        const int laneHeight = track_lane::uniformLaneHeight (trackLaneArea.getHeight(), numTracks,
                                                              trackHeightPreset);
        const int maxOffset = track_lane::maxScrollOffset (trackLaneArea.getHeight(), numTracks, laneHeight);

        laneScrollOffsetY = juce::jlimit (0, maxOffset, laneScrollOffsetY);

        const bool scroll = maxOffset > 0;
        laneScrollBar->setVisible (scroll);

        const juce::ScopedValueSetter<bool> guard (updatingLaneScrollBar, true);

        if (scroll)
        {
            laneScrollBar->setRangeLimits (0.0, (double) (maxOffset + juce::jmax (0, trackLaneArea.getHeight())));
            laneScrollBar->setCurrentRange ((double) laneScrollOffsetY,
                                            (double) juce::jmax (1, trackLaneArea.getHeight()));
            laneScrollBar->setSingleStepSize (juce::jmax (8, laneHeight / 2));
        }
        else
        {
            laneScrollBar->setRangeLimits (0.0, 1.0);
            laneScrollBar->setCurrentRange (0.0, 1.0);
        }
    }

    void MainComponent::setTrackHeightPreset (int preset)
    {
        const int clamped = juce::jlimit (0, 2, preset);

        if (clamped == trackHeightPreset)
            return;

        trackHeightPreset = clamped;
        laneScrollOffsetY = 0;

        updateLaneScrollBar();
        rebuildTrackLaneRects();
        persistUiState();
        resized(); // the Channels summary text and the lane geometry both change
        repaint();
    }

    void MainComponent::showHeightPresetMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Track height");
        menu.addItem (1, "Compact  -  56 px", true, trackHeightPreset == 0);
        menu.addItem (2, "Normal  -  72 px", true, trackHeightPreset == 1);
        menu.addItem (3, "Large  -  96 px", true, trackHeightPreset == 2);

        juce::Component::SafePointer<MainComponent> safe (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&heightPresetButton),
                            [safe] (int result)
                            {
                                if (auto* self = safe.getComponent())
                                    if (result >= 1 && result <= 3)
                                        self->setTrackHeightPreset (result - 1);
                            });
    }

    juce::String MainComponent::channelsSummaryText() const
    {
        const int numTracks = session.getNumAudioTracks();
        const int laneHeight = track_lane::uniformLaneHeight (trackLaneArea.getHeight(), numTracks,
                                                              trackHeightPreset);

        // Show the *effective* lane height (what the lanes actually get), not just
        // the preset — with the 56 px floor + scroll they can differ.
        juce::String text;
        text << "Lane height: " << laneHeight << " px\n"
             << numTracks << (numTracks == 1 ? " track" : " tracks");

        if (track_lane::needsScroll (trackLaneArea.getHeight(), numTracks, laneHeight))
            text << " \u00b7 scroll";

        return text;
    }

    juce::String MainComponent::statusText() const
    {
        juce::String text;
        text << "Tracks: " << session.getNumAudioTracks()
             << " \u00b7 Active inputs: " << audio.getNumActiveInputChannels() << "\n"
             << "Buffer " << audio.getCurrentBufferSize() << " \u00b7 "
             << juce::String ((int) audio.getCurrentSampleRate()) << " Hz\n"
             << "Latency ~" << juce::String (audio.getEstimatedRoundTripLatencyMs(), 1) << " ms";
        return text;
    }

    void MainComponent::persistUiState()
    {
        if (settings == nullptr)
            return;

        settings->setValue ("ui.sidebarWidth", sidebarPanel.getWidthValue());
        settings->setValue ("ui.sidebarCollapsed", sidebarPanel.isCollapsed());
        settings->setValue ("ui.sidebarSections", sidebar::encodeSections (sidebarPanel.getSections()));
        settings->setValue ("ui.trackHeightPreset", trackHeightPreset);
        settings->setValue ("ui.mixerCollapsed", mixerPanel.isCollapsed());
        settings->saveIfNeeded();
    }

    void MainComponent::applyLoadedUiState()
    {
        if (settings == nullptr)
            return;

        sidebarPanel.setWidthValue (settings->getIntValue ("ui.sidebarWidth", sidebar::defaultWidth));
        sidebarPanel.setCollapsed (settings->getBoolValue ("ui.sidebarCollapsed", false));
        sidebarPanel.setSections (sidebar::decodeSections (settings->getValue ("ui.sidebarSections")));
        trackHeightPreset = juce::jlimit (0, 2, settings->getIntValue ("ui.trackHeightPreset", 1));
        mixerPanel.setCollapsed (settings->getBoolValue ("ui.mixerCollapsed", false));
    }

    //==============================================================================
    void MainComponent::timerCallback()
    {
        // One clock for the ruler and the lanes (Epic 4 playhead fix). The ruler
        // used to repaint on its own 30 Hz timer and the lanes on this 10 Hz one,
        // so the ruler playhead visibly lagged the lane playhead. Both are now
        // repainted from this single ~30 Hz tick. The transport/status chrome is
        // heavier, so it is refreshed only every third tick (still ~10 Hz for the
        // time readout); the playhead still repaints on every tick.
        // Epic 8 phase 1b/1a follow-up: the metering PUMP runs every tick
        // (~30 Hz), independent of the mixer's visibility. It owns the engine's
        // read-and-clear meters and feeds both the lane mini-meters and the mixer.
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const double dt = lastLaneMeterMs > 0.0 ? (now - lastLaneMeterMs) / 1000.0 : 1.0 / 30.0;
        lastLaneMeterMs = now;
        pumpMeters (dt);

        if (++uiRefreshTicks >= 3)
        {
            uiRefreshTicks = 0;
            refreshTransportUi(); // repaints the lanes as a side effect
            // The sidebar Status section reads live device values (rate/buffer/
            // latency), so refresh its own paint at the same ~10 Hz cadence.
            sidebarPanel.repaint();
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

            // Epic 8 phase 1a: with vertical scrolling, a lane (or a clip that
            // spills past a scrolled lane) must never be drawn over the ruler
            // above or the mixer below — clip every lane/clip draw to the
            // arrangement area.
            g.saveState();
            g.reduceClipRegion (trackLaneArea);

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

                // Lane rects are kept up to date by rebuildTrackLaneRects() on
                // every path that can change the geometry (resized, session
                // change / refreshTransportUi, wheel, scrollbar, preset change) —
                // NOT here, so paint() does no per-frame allocation.
                const auto& inputTracks = inputTrackIndexCache;
                const bool recording = session.isRecording();

                bool anySolo = false;

                for (int i = 0; i < (int) trackLaneRects.size(); ++i)
                    if (session.isTrackSolo (i))
                        anySolo = true;

                int index = 0;

                for (auto* track : tracks)
                {
                    if (index >= (int) trackLaneRects.size())
                        break;

                    auto lane = trackLaneRects[(size_t) index];
                    const auto armed = session.isTrackArmed (index);
                    const auto isInput = inputTracks.contains (index);
                    // Compute the input problem ONCE per lane per frame (RT-2b #6):
                    // reused for the arm control, the chip and the mini-meter.
                    const auto inputProblem = session.getTrackInputProblem (index);
                    // RT-4: an unassigned (None) input track is valid but not
                    // armable until an input is assigned.
                    const auto inputAssigned = isInput
                                               && session.getTrackInputMapping (index).layout != InputLayout::None;
                    const auto inputReady = inputAssigned && inputProblem.isEmpty();
                    const auto armEnabled = inputReady || session.isMidiTrack (index);
                    const auto muted = session.isTrackMuted (index);
                    const auto soloed = session.isTrackSolo (index);

                    // Solo elsewhere dims this whole lane (spec §4.5).
                    const bool dim = anySolo && ! soloed;

                    if (dim)
                        g.setOpacity (0.45f);

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

                    // Control band (gutter): the left `gutterWidth` px of the lane.
                    const auto controls = track_lane::computeLaneControlRects (lane);
                    g.setColour (brand::bgPanel);
                    g.fillRoundedRectangle (controls.band.toFloat(), 5.0f);

                    // The clip fills the lane; the track name is an overlay drawn
                    // over it (below, after the clips) so an input clip and an
                    // imported clip are exactly the same height. Shared geometry
                    // with clipRectFor() so a drawn clip and its hit rect match.
                    juce::Rectangle<int> nameArea, clipBand;
                    track_lane::splitLane (lane, nameArea, clipBand);

                    // The name overlay lives in the CLIP zone, to the right of the
                    // gutter, so it never covers the controls.
                    nameArea = nameArea.withTrimmedLeft (track_lane::gutterWidth);

                    // Epic 8 phase 2: skip lanes fully outside the visible area
                    // so their clips cost nothing (no bins, no thumbnail reads).
                    const bool laneOnScreen = lane.getBottom() >= trackLaneArea.getY()
                                              && lane.getY() <= trackLaneArea.getBottom();

                    if (laneOnScreen)
                    {
                        // Epic 4: draw the track's clips on the timeline. Each clip's
                        // rectangle comes from the same x<->time map as the ruler and
                        // the playhead (its left edge is the gutter's right edge).
                        const auto timelineLength = juce::jmax (1.0e-6, session.getTimelineLengthSeconds());

                        // Clip-level live detection: only the take being written
                        // right now (its file == the track's active recording file),
                        // never a pre-existing clip on an armed track.
                        const auto activeRecordingFile = recording
                                                             ? session.getActiveRecordingFileForTrack (index)
                                                             : juce::File();

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

                            const auto x1 = Timeline::xForSeconds (startSeconds, timelineLength, trackTimelineArea);
                            const auto x2 = Timeline::xForSeconds (endSeconds, timelineLength, trackTimelineArea);
                            auto r = juce::Rectangle<int> (x1, clipBand.getY(),
                                                           juce::jmax (3, x2 - x1),
                                                           clipBand.getHeight());

                            // Horizontal cull: a clip fully outside the visible
                            // timeline costs nothing.
                            if (r.getRight() < trackTimelineArea.getX()
                                || r.getX() > trackTimelineArea.getRight())
                                continue;

                            const bool selected = (index == selectedTrackIndex
                                                   && clip.clipIndex == selectedClipIndex);
                            const bool live = clipwave::isLiveTake (recording, clip.isWave,
                                                                    clip.sourceFile, activeRecordingFile);

                            // Epic 8 phase 2: dimmed background + waveform (replaces
                            // the old solid accent fill). Same bounds as `r`, so the
                            // wave follows a drag preview.
                            drawClip (g, r, clip, startSeconds, endSeconds, selected, live);

                            // Fade ramps: a darkening triangle over each attenuated edge.
                            g.setColour (brand::bgWindow.withAlpha (0.6f));

                            if (fadeIn > 0.0)
                            {
                                const auto px = juce::jmax (0, Timeline::xForSeconds (startSeconds + fadeIn,
                                                                                      timelineLength, trackTimelineArea) - x1);
                                juce::Path p;
                                p.addTriangle ((float) x1, (float) r.getY(),
                                               (float) (x1 + px), (float) r.getY(),
                                               (float) x1, (float) r.getBottom());
                                g.fillPath (p);
                            }

                            if (fadeOut > 0.0)
                            {
                                const auto px = juce::jmax (0, x2 - Timeline::xForSeconds (endSeconds - fadeOut,
                                                                                           timelineLength, trackTimelineArea));
                                juce::Path p;
                                p.addTriangle ((float) x2, (float) r.getY(),
                                               (float) (x2 - px), (float) r.getY(),
                                               (float) x2, (float) r.getBottom());
                                g.fillPath (p);
                            }

                            // Border: selection wins, then a live take's record
                            // border. Idle clips get NO border (spec §5.4; a border
                            // is only for the top clip in an overlap, §5.5, which we
                            // do not detect yet).
                            if (selected)
                            {
                                g.setColour (brand::accent);
                                g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 2.0f);
                            }
                            else if (live)
                            {
                                g.setColour (brand::record);
                                g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.5f);
                            }

                            // The clip's own name only where the clip is roomy enough
                            // to read it without colliding with the track-name
                            // overlay (which now sits on top of the clip).
                            if (r.getWidth() > 40 && r.getHeight() >= 40)
                            {
                                g.setColour (brand::textPrimary);
                                g.setFont (brand::uiRegular (10.0f));
                                g.drawText ((clip.isLooping ? juce::String ("\u21bb ") : juce::String())
                                                + clip.name,
                                            r.reduced (6, 0), juce::Justification::centredLeft);
                            }
                        }
                    }

                    // Track-name overlay: drawn after the clips so it stays
                    // legible on top of the clip that now fills the lane. A faint
                    // backdrop keeps it readable over a bright clip.
                    {
                        const auto nameText = track->getName() + (armed ? "   [ARMED]" : "");
                        g.setFont (brand::uiMedium (13.0f));

                        const auto nameWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(),
                                                                                          nameText);
                        const auto textW = juce::jmin (nameArea.getWidth(), nameWidth + 12);

                        if (textW > 0 && nameArea.getHeight() > 0)
                        {
                            g.setColour (brand::bgWindow.withAlpha (0.55f));
                            g.fillRoundedRectangle (nameArea.withWidth (textW).toFloat(), 3.0f);
                        }

                        g.setColour (brand::textPrimary);
                        g.drawText (nameText, nameArea, juce::Justification::centredLeft);
                    }

                    // Epic 8 phase 1b: arm / mute / solo / input / mini-meter.
                    drawLaneControls (g, lane, isInput, inputReady, armEnabled, armed, recording,
                                      muted, soloed, index);

                    if (dim)
                        g.setOpacity (1.0f);

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
                                                               length, trackTimelineArea);
                const auto ex = (float) Timeline::xForSeconds (session.getSelectionEndSeconds(),
                                                               length, trackTimelineArea);
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
                                                              trackTimelineArea);
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
                                                              trackTimelineArea);
                g.setColour (brand::warning.withAlpha (0.9f));
                g.fillRect (juce::Rectangle<float> (x - 1.0f, (float) trackLaneArea.getY() + 2.0f,
                                                    2.0f, (float) trackLaneArea.getHeight() - 4.0f));
            }

            g.restoreState();
        }
    }

    void MainComponent::resized()
    {
        // Spacing scale: 4 / 8 / 12 / 16 / 24 / 32.
        auto area = getLocalBounds().reduced (12);

        // Title spans the full window width.
        titleLabel.setBounds (area.removeFromTop (26));
        area.removeFromTop (4);

        // The status line and the action row are bottom-anchored full-width, so
        // the sidebar column (Epic 8 phase 1a) can run down to the action row.
        auto outer = area;
        statusLabel.setBounds (outer.removeFromBottom (22));
        outer.removeFromBottom (4);

        const auto actionHeight = layoutActionRow (outer);
        outer.removeFromBottom (actionHeight);
        outer.removeFromBottom (8);

        // Left sidebar column + right work column. Cap the sidebar so the fixed
        // transport row (icons + count-in/MIDI + timecode) is never clipped at the
        // minimum window width (owner R: effectiveMax = f(window width)).
        auto mainRow = outer;
        sidebarPanel.setMaxWidthForWindow (sidebar::effectiveMaxWidth (getWidth()));
        sidebarPanel.setBounds (mainRow.removeFromLeft (sidebarPanel.getTotalWidth()));
        mainRow.removeFromLeft (8);

        auto right = mainRow;

        // Transport row: icon-only squares (>= 32x32) with consistent 8 px gaps.
        // Record is 40x40 to keep its emphasis; the rest are 36x36, vertically
        // centred in the 40 px row. The Click button is icon-only (metronome),
        // followed by a "Count-in" caption + compact combo, so the row never
        // overflows at the minimum window size.
        auto transportRow = right.removeFromTop (40);

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

        // Epic 5: MIDI input selector, then the transport readout takes the rest.
        midiInputLabel.setBounds (transportRow.removeFromLeft (48).withSizeKeepingCentre (48, 20));
        transportRow.removeFromLeft (6);
        midiInputBox.setBounds (transportRow.removeFromLeft (140).withSizeKeepingCentre (140, 28));
        transportRow.removeFromLeft (8);

        // Clamp so a very narrow row can never hand a label a negative width
        // (the fixed controls above already fit at the 1100 px minimum window).
        transportLabel.setBounds (transportRow.withWidth (juce::jmax (0, transportRow.getWidth())));

        right.removeFromTop (8);

        // Epic 4: arrangement edit row (Undo/Redo + clip operations). Icon-only
        // 32x32 squares with the same 4 px grouping as before; `iconOnly` makes
        // `getPreferredWidth()` return the square size, so the old text-width
        // clamp (jmax ..., 68) is gone.
        auto editRow = right.removeFromTop (32);

        for (auto* b : { &undoButton, &redoButton, &splitClipButton, &deleteClipButton,
                         &duplicateClipButton, &loopClipButton, &crossfadeButton,
                         &stretchClipButton })
        {
            b->setBounds (editRow.removeFromLeft (32).withSizeKeepingCentre (32, 32));
            editRow.removeFromLeft (4);
        }

        // Epic 8 phase 1a (owner R-B): compact lane-height dropdown at the right
        // edge of the edit row.
        heightPresetButton.setBounds (editRow.removeFromRight (32).withSizeKeepingCentre (32, 32));

        right.removeFromTop (8);

        auto bottom = right;

        // Mixer strip below the arrangement lanes. The auto-hide-by-window-height
        // behaviour is kept as a fallback (priority); otherwise the manual
        // collapse shows only the 22 px header strip (Epic 8 phase 1a).
        const int mixerHeight = mixerPanel.isCollapsed() ? MixerPanel::headerHeight
                                                         : MixerPanel::preferredHeight;

        if (bottom.getHeight() > MixerPanel::preferredHeight + 80)
        {
            mixerPanel.setVisible (true);
            mixerPanel.setBounds (bottom.removeFromBottom (mixerHeight));
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

        // Epic 5: the piano roll uses almost the whole window (it needs room for
        // four octaves of pitch rows and the beat grid).
        if (pianoRoll.isVisible())
        {
            auto r = getLocalBounds().reduced (24);
            r.removeFromTop (36);
            pianoRoll.setBounds (r);
        }

        auto middle = bottom;
        inputMeter.setBounds (middle.removeFromRight (180).reduced (2));
        middle.removeFromRight (6);

        // Lane scrollbar gutter on the right edge of the lanes, left of the meter
        // (Epic 8 phase 1a). It is hidden unless the lane stack overflows. The
        // ruler and the lanes share the reduced width so their x<->time map
        // still lines up.
        auto scrollGutter = middle.removeFromRight (8);

        // Ruler directly above the arrangement lanes. Epic 8 phase 1b: trim its
        // left by the control gutter so the ticks line up with the clip zone
        // (the map's left edge is the gutter's right edge).
        auto rulerRow = middle.removeFromTop (Timeline::preferredHeight);
        timeline.setBounds (track_lane::timelineAreaFor (rulerRow));
        middle.removeFromTop (2);

        // FR-EXP-2: selection start/end readout just under the ruler.
        selectionLabel.setBounds (middle.removeFromTop (16));

        middle.removeFromTop (2);
        trackLaneArea = middle;

        // The clip/playhead/ruler share this map: left edge = gutter's right (see
        // track_lane::timelineAreaFor / laneContentX for the explicit invariant).
        trackTimelineArea = track_lane::timelineAreaFor (trackLaneArea);

        // The scrollbar sits in the gutter beside the lanes (not beside the ruler).
        if (laneScrollBar != nullptr)
            laneScrollBar->setBounds (scrollGutter.withY (trackLaneArea.getY())
                                                  .withHeight (trackLaneArea.getHeight())
                                                  .reduced (0, 2));

        updateLaneScrollBar();
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
        // Epic 8 phase 1a: the lane height preset and the vertical scroll offset
        // are baked in here, so paint() and every hit-test share one mapping.
        trackLaneRects = track_lane::computeLaneRects (trackLaneArea, session.getNumAudioTracks(),
                                                       laneScrollOffsetY, trackHeightPreset);
    }

    juce::Rectangle<int> MainComponent::clipRectFor (int trackIndex, const Session::ClipInfo& info) const
    {
        if (trackIndex < 0 || trackIndex >= (int) trackLaneRects.size())
            return {};

        // Same split as paint(): name row on top, clip strip below. The x<->time
        // map is the gutter-trimmed timeline area (Epic 8 phase 1b).
        juce::Rectangle<int> nameRow, band;
        track_lane::splitLane (trackLaneRects[(size_t) trackIndex], nameRow, band);

        const auto length = juce::jmax (1.0e-6, session.getTimelineLengthSeconds());
        const auto x1 = Timeline::xForSeconds (info.startSeconds, length, trackTimelineArea);
        const auto x2 = Timeline::xForSeconds (info.endSeconds, length, trackTimelineArea);
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

        // With vertical scrolling a lane rect can extend past the arrangement
        // area (into the selection-label strip / before the mixer); only a point
        // actually inside `trackLaneArea` may match a lane.
        if (! trackLaneArea.contains (position))
            return juce::MouseCursor::NormalCursor;

        for (size_t i = 0; i < trackLaneRects.size(); ++i)
        {
            if (! trackLaneRects[i].contains (position))
                continue;

            const auto trackIndex = (int) i;

            // Epic 8 phase 1b: over a clickable gutter control show a hand cursor;
            // the empty band stays normal.
            const auto control = laneControlAt (trackIndex, position);

            if (control == track_lane::LaneControl::arm
                || control == track_lane::LaneControl::mute
                || control == track_lane::LaneControl::solo
                || control == track_lane::LaneControl::input)
                return juce::MouseCursor::PointingHandCursor;

            if (control != track_lane::LaneControl::none)
                return juce::MouseCursor::NormalCursor;

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
        pluginBrowser.setTargetTrack (selectedTrackIndex);
        refreshEditButtons();
        repaint();
    }

    void MainComponent::selectTrack (int trackIndex)
    {
        selectedTrackIndex = (trackIndex >= 0 && trackIndex < session.getNumAudioTracks())
                                 ? trackIndex
                                 : -1;
        selectedClipIndex = -1;
        pluginBrowser.setTargetTrack (selectedTrackIndex);
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

        const auto span = juce::jmax (1, trackTimelineArea.getWidth() - 2 * Timeline::hInset);
        return length * (double) snapThresholdPx / (double) span;
    }

    void MainComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        // Epic 8 phase 1a: the wheel scrolls the track lanes vertically. The
        // ruler / header never scroll (only the pointer over `trackLaneArea`
        // scrolls); other locations fall through so child components (mixer,
        // scrollbar, sidebar) keep their own handling.
        if (laneScrollBar != nullptr && laneScrollBar->isVisible()
            && trackLaneArea.contains (e.getPosition()))
        {
            const int numTracks = session.getNumAudioTracks();
            const int laneHeight = track_lane::uniformLaneHeight (trackLaneArea.getHeight(), numTracks,
                                                                  trackHeightPreset);
            const int step = juce::jmax (1, (int) std::round ((double) laneHeight * 2.0));

            int delta = (int) std::round (-wheel.deltaY * (float) step);

            // `deltaY != 0` would flag -Wfloat-equal; use strict ordering instead.
            if (delta == 0 && (wheel.deltaY > 0.0f || wheel.deltaY < 0.0f))
                delta = wheel.deltaY > 0.0f ? -step : step;

            if (delta != 0)
            {
                laneScrollOffsetY = track_lane::clampScrollOffset (laneScrollOffsetY + delta,
                                                                   trackLaneArea.getHeight(),
                                                                   numTracks, laneHeight);

                {
                    const juce::ScopedValueSetter<bool> guard (updatingLaneScrollBar, true);
                    laneScrollBar->setCurrentRange ((double) laneScrollOffsetY,
                                                    (double) juce::jmax (1, trackLaneArea.getHeight()));
                }

                rebuildTrackLaneRects();
                repaint();
            }

            return;
        }

        juce::Component::mouseWheelMove (e, wheel);
    }

    void MainComponent::mouseDown (const juce::MouseEvent& e)
    {
        if (exportInProgress)
            return;

        grabKeyboardFocus();

        // A scrolled lane rect can stick out past `trackLaneArea` (gaps by the
        // selection label / above the mixer); only clicks inside the arrangement
        // area may hit a lane. Outside it, drop the clip selection as before.
        if (! trackLaneArea.contains (e.getPosition()))
        {
            selectClip (-1, -1);
            return;
        }

        for (size_t i = 0; i < trackLaneRects.size(); ++i)
        {
            if (! trackLaneRects[i].contains (e.getPosition()))
                continue;

            const auto index = (int) i;

            // Epic 8 phase 1b: the gutter (the lane's left control band) handles
            // its own controls. A click there never starts a clip drag or a
            // timeline edit.
            const auto control = laneControlAt (index, e.getPosition());

            if (control != track_lane::LaneControl::none)
            {
                handleLaneControl (index, control);
                return;
            }

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
                const auto mouseSeconds = Timeline::secondsForX (e.getPosition().x, length, trackTimelineArea);

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

            // Empty lane click selects the track (owner bug fix: a track with no
            // clips — e.g. after its imported clip was deleted — must still be
            // targetable by Remove track). Input tracks keep their Epic 1/2
            // click-to-arm behaviour as well.
            selectTrack (index);

            // MIDI tracks arm exactly like input tracks (via their MIDI input);
            // only imported/backing tracks are playback-only.
            if (! inputTrackIndexCache.contains (index) && ! session.isMidiTrack (index))
            {
                showStatus (session.getTrackName (index)
                            + " selected (backing track, playback only). Remove track (trash) removes it.");
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
        const auto mouseSeconds = Timeline::secondsForX (e.getPosition().x, length, trackTimelineArea);
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

    void MainComponent::mouseMove (const juce::MouseEvent& e)
    {
        // JUCE sends no mouseEnter for painted sub-rectangles, so the hover state
        // for the lane controls is tracked here (spec §9.2).
        int newTrack = -1;
        auto newControl = track_lane::LaneControl::none;

        if (trackLaneArea.contains (e.getPosition()))
            for (size_t i = 0; i < trackLaneRects.size(); ++i)
                if (trackLaneRects[i].contains (e.getPosition()))
                {
                    newTrack = (int) i;
                    newControl = laneControlAt (newTrack, e.getPosition());
                    break;
                }

        if (newTrack != hoverTrackIndex || newControl != hoverControl)
        {
            const int oldTrack = hoverTrackIndex;

            hoverTrackIndex = newTrack;
            hoverControl = newControl;

            // Repaint only the two affected lanes' control bands, not the whole
            // arrangement (the hover state is per-lane).
            auto repaintLaneControls = [this] (int trackIndex)
            {
                if (juce::isPositiveAndBelow (trackIndex, (int) trackLaneRects.size()))
                    repaint (track_lane::computeLaneControlRects (trackLaneRects[(size_t) trackIndex])
                                 .band.expanded (2));
            };

            repaintLaneControls (oldTrack);
            repaintLaneControls (newTrack);
        }
    }

    void MainComponent::mouseExit (const juce::MouseEvent&)
    {
        if (hoverTrackIndex == -1 && hoverControl == track_lane::LaneControl::none)
            return;

        hoverTrackIndex = -1;
        hoverControl = track_lane::LaneControl::none;
        repaint();
    }

    bool MainComponent::keyPressed (const juce::KeyPress& key)
    {
        const auto mods = key.getModifiers();
        const auto code = key.getKeyCode();

        // Transport/arm hotkeys only act when MainComponent itself owns the
        // keyboard focus (never when a juce::Button has it — a focused button
        // already reacts to Space/R itself) and never while the MIDI piano-roll
        // overlay is open (Space/R would otherwise start the transport/record
        // mid-edit). S/D/L/F (clip edits) are unaffected.
        const bool transportHotkeysAllowed = (juce::Component::getCurrentlyFocusedComponent() == this)
                                             && ! pianoRoll.isVisible();

        // Auto-repeat guard: JUCE has no repeat flag, so a hotkey that is still
        // held is ignored until keyStateChanged() sees it released.
        auto isRepeat = [this] (int keyCode)
        {
            if (heldHotkeyCodes.contains (keyCode))
                return true;

            heldHotkeyCodes.addIfNotAlreadyThere (keyCode);
            return false;
        };

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

        // Epic 8 phase 1a: backslash toggles the left sidebar (Reaper-style).
        if (code == '\\' && ! mods.isCommandDown())
        {
            sidebarPanel.toggleCollapsed();
            return true;
        }

        // Epic 8 phase 1b hotkeys (no clash with Cmd+Z, Delete, \, S/D/L/F).
        // Space = play/stop.
        if (code == juce::KeyPress::spaceKey && ! mods.isCommandDown() && ! mods.isAltDown())
        {
            if (! transportHotkeysAllowed)
                return false;

            if (isRepeat (code))
                return true;

            if (exportInProgress)
                return true;

            if (session.isPlaying())
                session.stop();
            else
                session.play();

            refreshTransportUi();
            return true;
        }

        if (! mods.isCommandDown() && ! mods.isAltDown())
        {
            if (code == 's' || code == 'S') { splitSelectedAtPlayhead(); return true; }
            if (code == 'd' || code == 'D') { duplicateSelectedClip(); return true; }
            if (code == 'l' || code == 'L') { loopSelectedClip(); return true; }
            if (code == 'f' || code == 'F') { crossfadeSelectedClip(); return true; }

            // R = toggle record (same as the Record button).
            if (code == 'r' || code == 'R')
            {
                if (! transportHotkeysAllowed)
                    return false;

                if (isRepeat (code))
                    return true;

                if (! exportInProgress)
                {
                    if (session.isRecording())
                        session.stop();
                    else if (! session.record())
                        showStatus (session.getLastError(), true);
                }

                refreshTransportUi();
                return true;
            }

            // A = arm/disarm the track under the cursor (no track-selection
            // concept exists yet, so a hover target is deterministic).
            if (code == 'a' || code == 'A')
            {
                if (! transportHotkeysAllowed)
                    return false;

                if (isRepeat (code))
                    return true;

                if (! exportInProgress)
                    toggleHoveredTrackArm();

                return true;
            }
        }

        return false;
    }

    bool MainComponent::keyStateChanged (bool)
    {
        // Prune released hotkeys so the next press is treated as a fresh press
        // (auto-repeat guard for Space/R/A).
        for (int i = heldHotkeyCodes.size(); --i >= 0;)
            if (! juce::KeyPress::isKeyCurrentlyDown (heldHotkeyCodes[i]))
                heldHotkeyCodes.remove (i);

        return false;
    }

    void MainComponent::toggleHoveredTrackArm()
    {
        const int index = hoverTrackIndex;

        if (! juce::isPositiveAndBelow (index, session.getNumAudioTracks()))
        {
            showStatus ("Point at a track lane, then press A to arm/disarm it.");
            return;
        }

        if (! inputTrackIndexCache.contains (index) && ! session.isMidiTrack (index))
        {
            showStatus (session.getTrackName (index) + " is a playback-only track (cannot be armed).");
            return;
        }

        const auto nowArmed = ! session.isTrackArmed (index);

        if (session.setTrackArmed (index, nowArmed))
            showStatus ((nowArmed ? "Armed: " : "Disarmed: ") + session.getTrackName (index));
        else
            showStatus (session.getLastError(), true);

        refreshTransportUi();
    }

    //==============================================================================
    // Epic 8 phase 1b — in-lane controls (gutter).
    //==============================================================================
    track_lane::LaneControl MainComponent::laneControlAt (int trackIndex, juce::Point<int> p) const
    {
        if (! juce::isPositiveAndBelow (trackIndex, (int) trackLaneRects.size()))
            return track_lane::LaneControl::none;

        const bool isInput = inputTrackIndexCache.contains (trackIndex);
        return track_lane::controlAt (trackLaneRects[(size_t) trackIndex], isInput, p);
    }

    void MainComponent::handleLaneControl (int trackIndex, track_lane::LaneControl control)
    {
        if (exportInProgress || ! juce::isPositiveAndBelow (trackIndex, session.getNumAudioTracks()))
            return;

        const bool isInput = inputTrackIndexCache.contains (trackIndex);
        const bool inputAssigned = isInput
                                   && session.getTrackInputMapping (trackIndex).layout != InputLayout::None;
        const bool inputReady = inputAssigned && session.getTrackInputProblem (trackIndex).isEmpty();
        const bool armEnabled = inputReady || session.isMidiTrack (trackIndex);

        switch (control)
        {
            case track_lane::LaneControl::arm:
                if (! armEnabled)
                {
                    // Explain WHY an input track is not armable: unassigned
                    // ("No input") or a concrete channel problem — never a silent
                    // no-op (RT-2b D1/D2, RT-4).
                    const auto problem = session.getTrackInputProblem (trackIndex);

                    if (problem.isNotEmpty())
                        showStatus (problem, true);
                    else if (isInput && ! inputAssigned)
                        showStatus ("No input assigned. Click the input chip to choose a channel.", true);
                    else
                        selectTrack (trackIndex);

                    return;
                }

                if (session.setTrackArmed (trackIndex, ! session.isTrackArmed (trackIndex)))
                    showStatus ((session.isTrackArmed (trackIndex) ? "Armed: " : "Disarmed: ")
                                    + session.getTrackName (trackIndex));
                else
                    showStatus (session.getLastError(), true);

                refreshTransportUi();
                return;

            case track_lane::LaneControl::mute:
                session.setTrackMute (trackIndex, ! session.isTrackMuted (trackIndex));
                refreshTransportUi();
                return;

            case track_lane::LaneControl::solo:
                session.setTrackSolo (trackIndex, ! session.isTrackSolo (trackIndex));
                refreshTransportUi();
                return;

            case track_lane::LaneControl::input:
                if (isInput)
                    showLaneInputMenu (trackIndex);
                else
                    selectTrack (trackIndex);
                return;

            case track_lane::LaneControl::meter:
            case track_lane::LaneControl::band:
            case track_lane::LaneControl::none:
            default:
                selectTrack (trackIndex);
                return;
        }
    }

    void MainComponent::showLaneInputMenu (int trackIndex)
    {
        // Same menu as MixerPanel::showInputMenu (channel + layout, FR-REC-3).
        auto mapping = session.getTrackInputMapping (trackIndex);
        const auto numChannels = juce::jmax (1, session.getNumInputChannels());

        juce::PopupMenu channelMenu;
        const bool assigned = mapping.layout != InputLayout::None;

        for (int ch = 0; ch < numChannels; ++ch)
            channelMenu.addItem (100 + ch, "Input " + juce::String (ch + 1), true,
                                 assigned && ch == mapping.firstChannel);

        juce::PopupMenu layoutMenu;
        layoutMenu.addItem (200, "Auto", true, mapping.layout == InputLayout::Auto);
        layoutMenu.addItem (201, "Mono", true, mapping.layout == InputLayout::Mono);
        layoutMenu.addItem (202, "Stereo", true, mapping.layout == InputLayout::Stereo);
        layoutMenu.addItem (203, "Multi-channel", true, mapping.layout == InputLayout::MultiChannel);

        juce::PopupMenu menu;
        menu.addSectionHeader ("Input for " + session.getTrackName (trackIndex));
        menu.addSubMenu ("Channel", channelMenu);
        menu.addSubMenu ("Layout", layoutMenu);

        juce::Component::SafePointer<MainComponent> safe (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                            [safe, trackIndex, numChannels] (int result)
                            {
                                auto* self = safe.getComponent();

                                if (self == nullptr || result == 0)
                                    return;

                                auto m = self->session.getTrackInputMapping (trackIndex);

                                if (result >= 100 && result < 100 + numChannels)
                                {
                                    m.firstChannel = result - 100;

                                    // RT-4: picking a channel ASSIGNS an unassigned
                                    // track (otherwise the layout would stay None).
                                    if (m.layout == InputLayout::None)
                                        m.layout = InputLayout::Mono;
                                }
                                else if (result == 200) m.layout = InputLayout::Auto;
                                else if (result == 201) m.layout = InputLayout::Mono;
                                else if (result == 202) m.layout = InputLayout::Stereo;
                                else if (result == 203) m.layout = InputLayout::MultiChannel;

                                // Keep the channel count coherent with the layout.
                                if (m.layout == InputLayout::MultiChannel)
                                    m.numChannels = juce::jmax (1, numChannels - m.firstChannel);
                                else if (m.layout == InputLayout::Stereo)
                                    m.numChannels = 2;
                                else
                                    m.numChannels = 1;

                                self->session.setTrackInputMapping (trackIndex, m);
                                self->refreshTransportUi();
                                self->repaint();
                            });
    }

    void MainComponent::pumpMeters (double dtSeconds)
    {
        const int numTracks = session.getEdit() != nullptr ? session.getNumAudioTracks() : 0;

        if ((int) laneMeters.size() != numTracks)
        {
            laneMeters.assign ((size_t) juce::jmax (0, numTracks), MeterBallistics {});
            laneMeterClipHold.assign ((size_t) juce::jmax (0, numTracks), 0);
        }

        // OWNER of the engine's read-and-clear meters. This runs on the 30 Hz
        // tick that always runs, so metering works whether the mixer is expanded,
        // manually collapsed, auto-hidden, or was collapsed at startup. The mixer
        // strips and the lane mini-meters read these cached values (peek).
        for (int i = 0; i < numTracks; ++i)
        {
            const auto reading = session.readTrackMeter (i);
            const auto peak = juce::jmax (reading.peakDb[0], reading.peakDb[1]);

            laneMeters[(size_t) i].update (peak, dtSeconds);

            if (reading.clipped)
                laneMeterClipHold[(size_t) i] = 60;
            else if (laneMeterClipHold[(size_t) i] > 0)
                --laneMeterClipHold[(size_t) i];
        }

        // Publish the master reading too (consumed by the mixer's master strip).
        session.readMasterMeter();

        // Diagnostic hook (silent unless RRS_METER_PUMP_DEBUG is set): proves the
        // metering pump runs regardless of the mixer's collapsed/visible state.
        if (std::getenv ("RRS_METER_PUMP_DEBUG") != nullptr)
        {
            static int dbgCounter = 0;

            if (++dbgCounter % 30 == 0)
                std::cerr << "[meter-pump] tracks=" << numTracks
                          << " mixerCollapsed=" << (mixerPanel.isCollapsed() ? 1 : 0)
                          << " mixerVisible=" << (mixerPanel.isVisible() ? 1 : 0)
                          << std::endl;
        }
    }

    void MainComponent::drawMiniMeter (juce::Graphics& g, juce::Rectangle<int> area, int meterIndex)
    {
        auto bar = area.toFloat();

        g.setColour (brand::meterTrough);
        g.fillRoundedRectangle (bar, 2.0f);

        if (! juce::isPositiveAndBelow (meterIndex, (int) laneMeters.size()))
            return;

        // Invariant: the clip-hold vector is resized together with laneMeters,
        // but guard it anyway so a mid-tick track-count change can never index OOB.
        const bool hasClipHold = juce::isPositiveAndBelow (meterIndex, (int) laneMeterClipHold.size());

        const auto& meter = laneMeters[(size_t) meterIndex];
        const auto level = MeterBallistics::normaliseMeterDb (meter.getDb());
        const auto hold  = MeterBallistics::normaliseMeterDb (meter.getHoldDb());

        const auto fill = bar.getHeight() * level;
        g.setColour (brand::vuGreen);
        g.fillRoundedRectangle (bar.withTrimmedTop (bar.getHeight() - fill), 2.0f);

        if (hold > 0.0f)
        {
            const auto holdY = bar.getY() + bar.getHeight() * (1.0f - hold);
            g.setColour (brand::vuYellow);
            g.fillRect (juce::Rectangle<float> (bar.getX(), holdY - 1.0f, bar.getWidth(), 2.0f));
        }

        if (hasClipHold && laneMeterClipHold[(size_t) meterIndex] > 0)
        {
            g.setColour (brand::vuRed);
            g.fillRoundedRectangle (bar.withHeight (3.0f), 1.5f);
        }
    }

    void MainComponent::drawLaneControls (juce::Graphics& g, juce::Rectangle<int> lane,
                                          bool isInput, bool inputAvailable, bool armEnabled,
                                          bool armed, bool recording,
                                          bool muted, bool soloed, int meterIndex)
    {
        const auto r = track_lane::computeLaneControlRects (lane);

        const bool hover = (hoverTrackIndex == meterIndex);
        const bool hArm   = hover && hoverControl == track_lane::LaneControl::arm;
        const bool hMute  = hover && hoverControl == track_lane::LaneControl::mute;
        const bool hSolo  = hover && hoverControl == track_lane::LaneControl::solo;
        const bool hInput = hover && hoverControl == track_lane::LaneControl::input;

        // ARM: a filled circle when armed; a pulsing red glow while recording.
        {
            const auto a = r.arm.toFloat();
            const bool activeRec = armed && recording;

            if (activeRec)
            {
                const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;
                const float pulse = 0.5f + 0.5f * (float) std::sin (t * juce::MathConstants<double>::twoPi * 2.0);
                g.setColour (brand::record.withAlpha (0.10f + 0.30f * pulse));
                g.fillEllipse (a.expanded (3.0f));
            }

            g.setColour (armed ? brand::record : brand::bgPanel);
            g.fillEllipse (a);

            const auto ring = ! armEnabled ? brand::textDisabled
                                           : (armed ? brand::record : (hArm ? brand::recordHover : brand::border));
            g.setColour (ring);
            g.drawEllipse (a.reduced (0.5f), 1.5f);

            if (! armEnabled)
            {
                g.setColour (brand::textDisabled);
                g.fillRoundedRectangle (juce::Rectangle<float> (a.getCentreX() - 3.0f,
                                                                a.getCentreY() - 0.75f, 6.0f, 1.5f), 0.75f);
            }
        }

        auto drawSquare = [&] (juce::Rectangle<int> area, const juce::String& label, bool on,
                               bool hovered, juce::Colour onColour)
        {
            g.setColour (on ? onColour.withAlpha (0.22f) : brand::bgPanel);
            g.fillRoundedRectangle (area.toFloat(), 4.0f);
            g.setColour (on ? onColour : (hovered ? brand::borderHover : brand::border));
            g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.setColour (on ? onColour : brand::textSecondary);
            g.setFont (brand::uiSemiBold (juce::jmax (9.0f, area.getHeight() * 0.62f)));
            g.drawText (label, area, juce::Justification::centred);
        };

        drawSquare (r.mute, "M", muted, hMute, brand::record);
        drawSquare (r.solo, "S", soloed, hSolo, brand::warning);

        // INPUT chip / mini meter: live only on audio-input tracks that actually
        // HAVE a usable input (RT-2b D2), and only while the chip fits (spec §4.6
        // degradation: hidden below 44 px). The empty slot is still drawn so clips
        // on every lane line up (spec §4.1). `inputAvailable` is computed once in
        // paint() (#6).
        const bool showInput = isInput && track_lane::controlMetrics (lane.getHeight()).inputVisible;
        const bool chipUsable = showInput && inputAvailable;

        // RT-4: an unassigned track shows "No input" (valid, not an error) and can
        // be assigned later from this chip's menu.
        const bool unassigned = showInput
                                && session.getTrackInputMapping (meterIndex).layout == InputLayout::None;

        if (showInput)
        {
            g.setColour (brand::bgPanel);
            g.fillRoundedRectangle (r.input.toFloat(), 4.0f);
            g.setColour ((chipUsable || unassigned) && hInput ? brand::borderHover : brand::border);
            g.drawRoundedRectangle (r.input.toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.setFont (brand::uiMedium (10.0f));

            if (chipUsable)
            {
                g.setColour (brand::textSecondary);

                const auto mapping = session.getTrackInputMapping (meterIndex);
                juce::String code;

                switch (mapping.layout)
                {
                    case InputLayout::Mono:         code = "M"; break;
                    case InputLayout::Stereo:       code = "S"; break;
                    case InputLayout::MultiChannel: code = "N"; break;
                    case InputLayout::None:         code = ""; break; // unreachable here
                    case InputLayout::Auto:
                    default:                        code = "A"; break;
                }

                g.drawText (code + juce::String (mapping.firstChannel + 1), r.input,
                            juce::Justification::centred);
            }
            else if (unassigned)
            {
                // Valid unassigned state: suggest assignment (not an error).
                g.setColour (brand::textTertiary);
                g.setFont (brand::uiMedium (9.0f));
                g.drawText ("No input", r.input, juce::Justification::centred);
            }
            else
            {
                // Dimmed em-dash: channel off the device (invalid).
                g.setColour (brand::textDisabled);
                g.drawText ("\u2014", r.input, juce::Justification::centred);
            }
        }
        else
        {
            g.setColour (brand::bgPanel);
            g.fillRoundedRectangle (r.input.toFloat(), 4.0f);
        }

        if (chipUsable)
            drawMiniMeter (g, r.meter, meterIndex);
    }

    //==============================================================================
    // Epic 8 phase 2 — clip waveforms.
    //==============================================================================
    te::SmartThumbnail* MainComponent::ensureClipThumbnail (const juce::File& file)
    {
        if (file == juce::File())
            return nullptr;

        auto it = clipThumbnails.find (file);

        if (it == clipThumbnails.end())
        {
            auto& engine = audio.engine();
            ThumbEntry entry;
            entry.thumb = std::make_unique<te::SmartThumbnail> (engine,
                                                                te::AudioFile (engine, file),
                                                                *this,
                                                                session.getEdit());
            entry.lastUsed = ++thumbnailClock;
            it = clipThumbnails.emplace (file, std::move (entry)).first;
        }

        return it->second.thumb.get();
    }

    te::SmartThumbnail* MainComponent::findClipThumbnail (const juce::File& file)
    {
        auto it = clipThumbnails.find (file);

        if (it == clipThumbnails.end())
            return nullptr;

        it->second.lastUsed = ++thumbnailClock;
        return it->second.thumb.get();
    }

    void MainComponent::preloadClipThumbnails()
    {
        // Create the thumbnails for every clip now, so paint() only ever looks
        // them up (no SmartThumbnail construction inside a frame — it is
        // message-thread-only and can read files).
        for (int t = 0; t < session.getNumAudioTracks(); ++t)
            for (const auto& clip : session.getClips (t))
                if (clip.isWave && clip.sourceFile != juce::File())
                    ensureClipThumbnail (clip.sourceFile);
    }

    void MainComponent::pruneClipThumbnails()
    {
        if (clipThumbnails.empty())
            return;

        // A change message can arrive often; a full scan + eviction a couple of
        // times a second is plenty (and keeps clip iteration off the hot path).
        const auto now = juce::Time::getMillisecondCounterHiRes();

        if (now - lastClipPruneMs < 2000.0)
            return;

        lastClipPruneMs = now;

        juce::Array<juce::File> inUse;

        for (int t = 0; t < session.getNumAudioTracks(); ++t)
            for (const auto& clip : session.getClips (t))
                if (clip.isWave && clip.sourceFile != juce::File())
                    inUse.addIfNotAlreadyThere (clip.sourceFile);

        // 1) Drop files no longer referenced by any clip.
        for (auto it = clipThumbnails.begin(); it != clipThumbnails.end();)
        {
            if (inUse.contains (it->first))
                ++it;
            else
                it = clipThumbnails.erase (it);
        }

        // 2) Evict least-recently-used entries while over the cap — never clear
        //    the whole map (that would re-read every file on the next frame).
        constexpr int cap = 128;

        while ((int) clipThumbnails.size() > cap)
        {
            auto victim = clipThumbnails.begin();

            for (auto it = clipThumbnails.begin(); it != clipThumbnails.end(); ++it)
                if (it->second.lastUsed < victim->second.lastUsed)
                    victim = it;

            clipThumbnails.erase (victim);
        }
    }

    void MainComponent::drawClip (juce::Graphics& g, juce::Rectangle<int> r,
                                  const Session::ClipInfo& clip, double startSeconds, double endSeconds,
                                  bool selected, bool live)
    {
        // Dimmed background instead of the old solid accent fill (spec §5.3).
        g.setColour (selected ? brand::clipSelected : brand::clip);
        g.fillRoundedRectangle (r.toFloat(), 3.0f);

        if (live)
        {
            g.setColour (brand::record.withAlpha (0.12f));
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
        }

        const auto inner = r.reduced (0, clipwave::wavePadY);
        const auto centreY = (float) inner.getCentreY();
        const auto halfH = (float) inner.getHeight() * 0.5f;

        // Centre line (§5.2: 1 px, textTertiary @30%).
        g.setColour (brand::textTertiary.withAlpha (0.3f));
        g.fillRect (juce::Rectangle<float> ((float) r.getX(), centreY - 0.5f,
                                            (float) r.getWidth(), 1.0f));

        // Thumbnail: the engine's live recording thumbnail for the take being
        // written, otherwise a cached SmartThumbnail (looked up only — created off
        // the paint path in preloadClipThumbnails()).
        juce::AudioThumbnailBase* thumb = nullptr;
        double fileLength = 0.0;

        if (clip.isWave)
        {
            if (live)
            {
                // Reuse the engine's live thumbnail; re-fetch only when the take
                // file changes. (getThumbnailFor must NOT be called for non-takes:
                // it lazily creates a Thumbnail that would be destroyed the next
                // frame — an AudioThumbnail create/destroy churn at 30 Hz.)
                if (liveRecordingThumbnail == nullptr || liveRecordingFile != clip.sourceFile)
                {
                    liveRecordingFile = clip.sourceFile;
                    liveRecordingThumbnail = audio.engine()
                                                 .getRecordingThumbnailManager()
                                                 .getThumbnailFor (clip.sourceFile);
                }

                if (liveRecordingThumbnail != nullptr)
                    thumb = liveRecordingThumbnail->thumb.get();
            }
            else
            {
                thumb = findClipThumbnail (clip.sourceFile);
            }

            if (thumb != nullptr)
                fileLength = thumb->getTotalLength();
        }

        // Live takes use the full accent; selected uses full accent; loops 80%.
        const auto waveColour = (live || selected) ? brand::accent
                                                   : (clip.isLooping ? brand::accent.withAlpha (0.8f)
                                                                     : brand::waveformIdle);

        const int numChannels = (thumb != nullptr) ? thumb->getNumChannels() : 0;

        if (thumb != nullptr && fileLength > 0.0 && numChannels > 0 && halfH >= 1.0f)
        {
            const int bins = clipwave::binCount (r.getWidth());

            auto binPeak = [thumb, numChannels] (double a, double b)
            {
                float peak = 0.0f;

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float mn = 0.0f, mx = 0.0f;
                    thumb->getApproximateMinMax (a, b, ch, mn, mx);
                    peak = juce::jmax (peak, clipwave::peakFromMinMax (mn, mx));
                }

                return peak;
            };

            for (int i = 0; i < bins; ++i)
            {
                const auto t0 = clipwave::binStartSeconds (startSeconds, endSeconds, i, bins);
                const auto t1 = clipwave::binEndSeconds (startSeconds, endSeconds, i, bins);
                const auto s0 = clipwave::sourceTimeForBin (startSeconds, t0, clip.offsetSeconds,
                                                            fileLength, clip.isLooping);
                const auto s1 = clipwave::sourceTimeForBin (startSeconds, t1, clip.offsetSeconds,
                                                            fileLength, clip.isLooping);

                float peak = 0.0f;

                if (s1 > s0)
                    peak = binPeak (s0, s1);
                else if (clip.isLooping && fileLength > clip.offsetSeconds)
                    // The bin crosses the loop-wrap point: cover the tail of this
                    // iteration and the head of the next, so there is no 2 px gap.
                    peak = juce::jmax (binPeak (s0, fileLength), binPeak (clip.offsetSeconds, s1));

                if (clipwave::isSilent (peak))
                    continue;

                const auto x = (float) (r.getX() + i * clipwave::binWidthPx);

                if (x >= (float) r.getRight())
                    break;

                // Clamp the bar to the clip's right edge (odd width -> last bin).
                const auto w = juce::jmin ((float) clipwave::binWidthPx, (float) r.getRight() - x);
                const auto h = juce::jlimit (1.0f, halfH, peak * halfH);
                g.setColour (waveColour);
                g.fillRect (juce::Rectangle<float> (x, centreY - h, w, h * 2.0f));
            }
        }

        // Loop iteration hairlines (spec §5.1) — drawn whenever the iteration
        // length is known, independent of the wave block above.
        if (clip.isLooping && thumb != nullptr && fileLength > clip.offsetSeconds + 1.0e-6)
        {
            const auto iteration = fileLength - clip.offsetSeconds;
            const auto length = juce::jmax (1.0e-6, session.getTimelineLengthSeconds());

            g.setColour (brand::textTertiary.withAlpha (0.4f));

            for (double t = startSeconds + iteration; t < endSeconds - 1.0e-6; t += iteration)
            {
                const auto x = (float) Timeline::xForSeconds (t, length, trackTimelineArea);
                g.fillRect (juce::Rectangle<float> (x, (float) r.getY(), 1.0f, (float) r.getHeight()));
            }
        }

        // Live take: a pulsing accent "growing edge" at the clip's right end.
        if (live)
        {
            const double seconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
            const auto pulse = 0.5f + 0.5f * (float) std::sin (seconds * juce::MathConstants<double>::twoPi * 2.0);
            g.setColour (brand::accent.withAlpha (0.5f + 0.5f * pulse));
            g.fillRect (juce::Rectangle<float> ((float) r.getRight() - 2.0f, (float) r.getY(),
                                                2.0f, (float) r.getHeight()));
        }
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

        const auto track = selectedTrackIndex;

        if (session.deleteClip (selectedTrackIndex, selectedClipIndex))
        {
            // Keep the track selected so a now-empty imported track can be
            // removed with one click of Remove track (owner bug fix). The track
            // is intentionally NOT auto-deleted: it may be an input track, and
            // its mixer/routing state should not vanish as a side effect of a
            // clip delete. The user removes it explicitly when ready.
            selectedClipIndex = -1;
            selectedTrackIndex = track;
            showStatus ("Deleted clip. Remove track (trash) removes the now-empty track.");
        }
        else
        {
            showStatus (session.getLastError(), true);
        }

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
        // [Add track][Remove track][MIDI track][Piano roll] · [Settings][About]
        // — all icon-only now.
        BrandButton* const buttons[] = { &newButton, &openButton, &saveButton, &saveAsButton,
                                         &closeButton, &importButton, &normaliseButton, &exportButton,
                                         &exportRegionButton, &clearRegionButton,
                                         &stemsButton, &pluginsButton, &routingButton,
                                         &addTrackButton, &removeTrackButton,
                                         &midiTrackButton, &pianoRollButton,
                                         &settingsButton, &aboutButton };
        const int groups[] = { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2 };
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
