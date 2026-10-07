// raw-radio-studio — main window content (Epic 1).

#include "MainComponent.h"

#include "studio/AppPaths.h"
#include "studio/AudioImport.h"
#include "ui/DevicePanelLayout.h"

#include <array>
#include <cmath>

#ifndef RAW_RADIO_STUDIO_VERSION
 #define RAW_RADIO_STUDIO_VERSION "0.0.0-dev"
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

        titleLabel.setFont (juce::Font { juce::FontOptions { 18.0f, juce::Font::bold } });
        titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        transportLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
        transportLabel.setJustificationType (juce::Justification::centredLeft);
        statusLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
        statusLabel.setJustificationType (juce::Justification::centredLeft);

        addAndMakeVisible (titleLabel);
        addAndMakeVisible (devicePanel);
        addAndMakeVisible (inputMeter);
        addAndMakeVisible (transportLabel);
        addAndMakeVisible (statusLabel);

        for (auto* button : { &newButton, &openButton, &closeButton, &saveButton, &saveAsButton,
                              &importButton, &exportButton, &settingsButton, &aboutButton,
                              &armButton, &recordButton, &playButton, &stopButton, &monitorButton })
            addAndMakeVisible (*button);

        buildTransportUi();

        newButton.onClick       = [this] { newSession(); };
        openButton.onClick      = [this] { openSession(); };
        closeButton.onClick     = [this] { closeSession(); };
        saveButton.onClick      = [this] { saveSession(); };
        saveAsButton.onClick    = [this] { saveSessionAs(); };
        importButton.onClick    = [this] { importAudioFile(); };
        exportButton.onClick    = [this] { exportSession(); };
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

        setSize (960, 640);
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

        saveSettings();
    }

    //==============================================================================
    void MainComponent::buildTransportUi()
    {
        armButton.setClickingTogglesState (true);
        monitorButton.setClickingTogglesState (true);

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
                session.stop();
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
    }

    void MainComponent::refreshTransportUi()
    {
        const bool hasEdit = session.getEdit() != nullptr;

        // While a render holds the `Edit*`, nothing may replace or mutate it:
        // New/Open would reset the Edit (use-after-free on the render thread)
        // and the transport/Save would race it. Disable them all for the duration.
        const bool busy = exportInProgress;

        // While a render holds the `Edit*`, the device panel must be inert:
        // applying a device change runs session.reconfigureInputs(), which
        // mutates the Edit concurrently with the render thread. Disabling the
        // panel (children included) also blocks Rescan/Apply/combo changes.
        devicePanel.setEnabled (! busy);

        for (auto* button : { &saveButton, &saveAsButton, &importButton, &exportButton,
                              &armButton, &recordButton, &playButton, &stopButton, &monitorButton })
            button->setEnabled (hasEdit && ! busy);

        newButton.setEnabled (! busy);
        openButton.setEnabled (! busy);
        closeButton.setEnabled (hasEdit && ! busy);

        recordButton.setButtonText (session.isRecording() ? "Stop rec" : "Record");
        playButton.setButtonText (session.isPlaying() ? "Pause" : "Play");
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
                               isError ? juce::Colour (0xfff87171) : juce::Colours::white.withAlpha (0.8f));
        statusLabel.setText (message, juce::dontSendNotification);
    }

    //==============================================================================
    void MainComponent::newSession()
    {
        if (exportInProgress)
            return;

        // NOTE: unlike Close (see closeSession()), New does not yet guard against
        // losing unsaved changes — `createNew()` replaces the Edit directly. This
        // inconsistency is known; New/Open should prompt Save/Discard/Cancel once
        // the Close prompt is factored into a shared helper.
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
    }

    void MainComponent::openSession()
    {
        if (exportInProgress)
            return;

        // NOTE: as with New, Open does not yet guard against losing unsaved
        // changes (Close does — see closeSession()). Documented known
        // inconsistency; a shared Save/Discard/Cancel helper is the follow-up.
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

        if (! session.hasUnsavedChanges())
        {
            performClose();
            return;
        }

        juce::NativeMessageBox::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle ("Close session")
                .withMessage ("This session has unsaved changes.\n\n"
                              "Save them before closing?")
                .withButton ("Save")
                .withButton ("Discard")
                .withButton ("Cancel"),
            [safe, performClose] (int result)
            {
                auto* self = safe.getComponent();

                if (self == nullptr)
                    return;

                if (result == 0) // Save
                {
                    if (! self->session.save())
                    {
                        self->showStatus (self->session.getLastError(), true);
                        return; // save failed: keep the session open
                    }

                    performClose();
                }
                else if (result == 1) // Discard
                {
                    performClose();
                }
                // result == 2 (Cancel): do nothing.
            });
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
        if (exportInProgress)
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

                                  // Lock the session for the duration of the render:
                                  // New/Open would reset the Edit the render thread
                                  // is reading (use-after-free).
                                  exportInProgress = true;
                                  refreshTransportUi();
                                  showStatus ("Exporting 24-bit WAV...");

                                  juce::Component::SafePointer<MainComponent> safe (this);

                                  exportHandle = WavExport::start (
                                      *session.getEdit(), file,
                                      [safe] (bool success, juce::File result, juce::String error)
                                      {
                                          auto* self = safe.getComponent();

                                          if (self == nullptr)
                                              return;

                                          self->exportHandle.reset();
                                          self->exportInProgress = false;
                                          self->refreshTransportUi();

                                          if (success)
                                              self->showStatus ("Exported 24-bit WAV: " + result.getFullPathName());
                                          else
                                              self->showStatus ("Export failed: " + error, true);
                                      });
                              });
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
        g.fillAll (juce::Colour (0xff0f172a));

        // Track lanes: the armed record track first, then any imported backing
        // tracks. Each import adds its own lane so it is visible after import.
        if (! trackLaneArea.isEmpty())
        {
            g.setColour (juce::Colour (0xff1e293b));
            g.fillRoundedRectangle (trackLaneArea.toFloat(), 6.0f);

            auto lanes = trackLaneArea.reduced (8, 6);

            if (auto* edit = session.getEdit())
            {
                const auto tracks = te::getAudioTracks (*edit);

                if (tracks.isEmpty())
                {
                    g.setColour (juce::Colours::white.withAlpha (0.7f));
                    g.setFont (13.0f);
                    g.drawText ("No session loaded", lanes, juce::Justification::centred);
                }

                for (auto* track : tracks)
                {
                    if (lanes.getHeight() <= 0)
                        break;

                    auto lane = lanes.removeFromTop (juce::jmin (46, lanes.getHeight()));

                    if (track != tracks.getFirst())
                        g.setColour (juce::Colour (0xff243449));
                    else
                        g.setColour (juce::Colour (0xff2b3b52));
                    g.fillRoundedRectangle (lane.toFloat().reduced (0.0f, 2.0f), 5.0f);

                    const bool armed = track == session.getTrack() && session.isTrackArmed();

                    // Two non-overlapping text rows inside the lane: the track
                    // name in the top ~20 px, the clip count in the ~16 px below
                    // it. Split a single inner rectangle (rather than rebuilding
                    // `lane.reduced(10)` per row) so the rows can never overlap.
                    auto inner = lane.reduced (10, 4);
                    auto nameArea = inner.removeFromTop (20);
                    auto clipArea = inner.removeFromTop (16);

                    g.setColour (juce::Colours::white.withAlpha (0.9f));
                    g.setFont (13.0f);
                    g.drawText (track->getName() + (armed ? "   [ARMED]" : ""),
                                nameArea, juce::Justification::centredLeft);

                    g.setColour (juce::Colours::white.withAlpha (0.55f));
                    g.setFont (11.0f);
                    g.drawText (juce::String (track->getClips().size()) + " clip(s)",
                                clipArea, juce::Justification::centredLeft);
                }
            }
            else
            {
                g.setColour (juce::Colours::white.withAlpha (0.7f));
                g.setFont (13.0f);
                g.drawText ("No session loaded", lanes, juce::Justification::centred);
            }
        }
    }

    void MainComponent::resized()
    {
        auto area = getLocalBounds().reduced (10);

        titleLabel.setBounds (area.removeFromTop (26));
        area.removeFromTop (4);

        devicePanel.setBounds (area.removeFromTop (devicePanelHeight));
        area.removeFromTop (6);

        auto transportRow = area.removeFromTop (34);
        armButton.setBounds     (transportRow.removeFromLeft (70).reduced (2));
        transportRow.removeFromLeft (4);
        recordButton.setBounds  (transportRow.removeFromLeft (100).reduced (2));
        playButton.setBounds    (transportRow.removeFromLeft (90).reduced (2));
        stopButton.setBounds    (transportRow.removeFromLeft (80).reduced (2));
        transportRow.removeFromLeft (8);
        monitorButton.setBounds (transportRow.removeFromLeft (90).reduced (2));
        transportRow.removeFromLeft (8);
        transportLabel.setBounds (transportRow);

        area.removeFromTop (6);

        auto middle = area.removeFromTop (juce::jmax (100, area.getHeight() - 56));
        inputMeter.setBounds (middle.removeFromRight (180).reduced (2));
        middle.removeFromRight (6);
        trackLaneArea = middle;

        area.removeFromTop (6);

        auto actions = area.removeFromTop (30);

        // Content-based widths: a single fixed width per button gave every label
        // the same box, which cramped the longest ("Export WAV..."). Size each
        // button to its own text (the LookAndFeel's width-to-fit-text adds a
        // symmetric border, so the horizontal padding stays consistent) and only
        // fall back to an equal split when the window is too narrow to fit them.
        const std::array<juce::TextButton*, 9> actionButtons {
            &newButton, &openButton, &closeButton, &saveButton, &saveAsButton,
            &importButton, &exportButton, &settingsButton, &aboutButton };

        constexpr int gap = 6;
        constexpr int minWidth = 56;

        std::array<int, actionButtons.size()> widths {};
        int totalWidth = 0;

        for (size_t i = 0; i < actionButtons.size(); ++i)
        {
            widths[i] = juce::jmax (minWidth, actionButtons[i]->getBestWidthForHeight (actions.getHeight()));
            totalWidth += widths[i];
        }

        const auto available = actions.getWidth() - gap * ((int) actionButtons.size() - 1);

        if (totalWidth > available && available > 0)
            widths.fill (juce::jmax (minWidth, available / (int) actionButtons.size()));

        int x = actions.getX();

        for (size_t i = 0; i < actionButtons.size(); ++i)
        {
            actionButtons[i]->setBounds (x, actions.getY(), widths[i], actions.getHeight());
            x += widths[i] + gap;
        }

        area.removeFromTop (4);
        statusLabel.setBounds (area.removeFromTop (22));
    }
}
