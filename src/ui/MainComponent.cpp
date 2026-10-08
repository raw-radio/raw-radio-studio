// raw-radio-studio — main window content (Epic 1).

#include "MainComponent.h"

#include "studio/AppPaths.h"
#include "studio/AudioImport.h"
#include "ui/BrandColours.h"
#include "ui/BrandFonts.h"
#include "ui/DevicePanelLayout.h"

#include <array>
#include <cmath>

#ifndef RAW_RADIO_STUDIO_VERSION
 #define RAW_RADIO_STUDIO_VERSION "0.1.0-dev"
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
                              &monitorButton, &metronomeButton })
            addAndMakeVisible (*button);

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

        // Arm and Monitor are toggle chips with distinct on-states: arm uses the
        // brand accent, monitor uses the success tint.
        armButton.setOnColours (brand::accentMuted, brand::accent, brand::accent);
        monitorButton.setOnColours (brand::success.withAlpha (0.25f), brand::success, brand::success);

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

        // Epic 3: plugin browser, routing/cue mixes, stems export.
        pluginsButton.setIconName ("waveform");
        pluginsButton.setIconOnly (true);
        pluginsButton.setTooltip ("Plugin browser (scan/insert VST3/LV2/AU; open editors)");
        routingButton.setIconName ("headphones");
        routingButton.setIconOnly (true);
        routingButton.setTooltip ("Routing & software cue mixes (outputs, sends, submixes)");
        stemsButton.setIconName ("download-simple");
        stemsButton.setIconOnly (true);
        stemsButton.setTooltip ("Export per-track stems + master mix (24-bit WAV)");

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
        removeTrackButton.setTooltip ("Remove the last track");

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

            const auto last = session.getNumAudioTracks() - 1;

            if (! session.removeAudioTrack (last))
                showStatus (session.getLastError(), true);

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
        startTimerHz (10);

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
            {
                showStatus (session.getLastError(), true);
                refreshTransportUi();
            }
        };

        monitorButton.onClick = [this]
        {
            if (exportInProgress)
                return;

            session.setMonitoringEnabled (monitorButton.getToggleState());
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
        if (exportInProgress)
            return;

        confirmUnsavedChanges ("New session", [this]
        {
            auto chooser = std::make_shared<juce::FileChooser> ("New session",
                                                                paths::projectsDirectory(), "*.tracktionedit");

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

                                      if (session.createNew (file))
                                          onSessionOpened();
                                      else
                                          showStatus (session.getLastError(), true);
                                  });
        });
    }

    void MainComponent::openSession()
    {
        if (exportInProgress)
            return;

        confirmUnsavedChanges ("Open session", [this]
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Open session",
                                                                paths::projectsDirectory(), "*.tracktionedit");

            chooser->launchAsync (juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectFiles,
                                  [this, chooser] (const juce::FileChooser& fc)
                                  {
                                      auto file = fc.getResult();

                                      if (file == juce::File())
                                          return;

                                      if (session.open (file))
                                          onSessionOpened();
                                      else
                                          showStatus (session.getLastError(), true);
                                  });
        });
    }

    void MainComponent::closeSession()
    {
        if (exportInProgress || session.getEdit() == nullptr)
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

        auto chooser = std::make_shared<juce::FileChooser> ("Save session as",
                                                            session.getEditFile(), "*.tracktionedit");

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

        auto startDir = session.getEditFile() != juce::File()
                            ? session.getEditFile().getParentDirectory()
                            : paths::projectsDirectory();

        auto chooser = std::make_shared<juce::FileChooser> ("Import audio file", startDir,
                                                            AudioImport::fileWildcard);

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this, chooser] (const juce::FileChooser& fc)
                              {
                                  auto file = fc.getResult();

                                  if (file == juce::File())
                                      return;

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

        auto chooser = std::make_shared<juce::FileChooser> ("Export WAV",
                                                            WavExport::defaultDestinationFor (session.getEditFile()),
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

        auto chooser = std::make_shared<juce::FileChooser> ("Export region WAV", destination, "*.wav");

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

        auto chooser = std::make_shared<juce::FileChooser> (
            "Export stems (choose a folder)",
            StemsExport::defaultDirectoryFor (session.getEditFile()), juce::String());

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [this, chooser] (const juce::FileChooser& fc)
                              {
                                  auto directory = fc.getResult();

                                  if (directory == juce::File())
                                      return;

                                  stemsInProgress = true;
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
                << "Contributions use the Developer Certificate of Origin (DCO); no CLA.";

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
                                  "Autosave interval in seconds (0 disables autosave):",
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
        refreshTransportUi();
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

                    // Two non-overlapping text rows inside the lane: the track
                    // name in the top ~20 px, the clip count in the ~16 px below
                    // it. Split a single inner rectangle (rather than rebuilding
                    // `lane.reduced(10)` per row) so the rows can never overlap.
                    auto inner = lane.reduced (10, 4);
                    auto nameArea = inner.removeFromTop (20);
                    auto clipArea = inner.removeFromTop (16);

                    g.setColour (brand::textPrimary);
                    g.setFont (brand::uiMedium (13.0f));
                    g.drawText (track->getName() + (armed ? "   [ARMED]" : ""),
                                nameArea, juce::Justification::centredLeft);

                    g.setColour (brand::textTertiary);
                    g.setFont (brand::uiRegular (11.0f));
                    g.drawText (juce::String (track->getClips().size()) + " clip(s)",
                                clipArea, juce::Justification::centredLeft);

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

        auto lanes = trackLaneArea.reduced (8, 6);
        const auto numTracks = session.getNumAudioTracks();

        trackLaneRects.reserve ((size_t) juce::jmax (0, numTracks));

        for (int i = 0; i < numTracks && lanes.getHeight() > 0; ++i)
            trackLaneRects.push_back (lanes.removeFromTop (juce::jmin (46, lanes.getHeight())));
    }

    void MainComponent::mouseDown (const juce::MouseEvent& e)
    {
        if (exportInProgress)
            return;

        // Empty areas (below the lanes) are ignored; only a real lane toggles.
        for (size_t i = 0; i < trackLaneRects.size(); ++i)
        {
            if (! trackLaneRects[i].contains (e.getPosition()))
                continue;

            const auto index = (int) i;

            // Imported/backing tracks are playback-only: don't attempt to arm
            // them (the Session would reject it anyway).
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
