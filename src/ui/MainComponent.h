// raw-radio-studio — main window content (Epic 1).
//
// Minimal tracking-first UI per STUDIO_SPEC §6.4:
//   * audio-device / input panel,
//   * transport bar (arm, record, play, stop),
//   * input level meter (peak + RMS),
//   * single stereo track lane,
//   * session (new/open/save/save-as), WAV export, settings, About.

#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>

#include "studio/AudioEngine.h"
#include "studio/PluginHost.h"
#include "studio/Session.h"
#include "studio/StemsExport.h"
#include "studio/WavExport.h"
#include "ui/BrandButton.h"
#include "ui/DevicePanel.h"
#include "ui/InputMeter.h"
#include "ui/MixerPanel.h"
#include "ui/PluginBrowser.h"
#include "ui/RoutingPanel.h"
#include "ui/Timeline.h"

#include <vector>

namespace rrs
{
    class MainComponent final : public juce::Component,
                                private juce::ChangeListener,
                                private juce::Timer
    {
    public:
        MainComponent();
        ~MainComponent() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;

        void buildTransportUi();
        void refreshTransportUi();
        void showStatus (const juce::String&, bool isError = false);

        void newSession();
        void openSession();
        void closeSession();
        void saveSession();
        void saveSessionAs();
        void importAudioFile();
        void exportSession();
        void exportRegion();
        void exportStems();
        void showAbout();
        void showSettings();

        /** Starts an offline WAV render of `range` to `file` and manages the
            busy/metronome state shared by full-session and region export
            (FR-EXP-1/2). Caller must have checked the edit exists and is not
            already rendering. */
        void beginWavExport (const juce::File& file, WavExport::RenderRange range,
                             const juce::String& statusText);

        /** Shows `panel` centred (hiding the other overlay first) or hides it
            when already visible. */
        void toggleOverlay (juce::Component& panel);

        /** Shared unsaved-changes guard for New/Open/Close. Runs `onProceed`
            immediately when there is nothing to lose, otherwise shows a
            Save/Discard/Cancel prompt. A failed Save keeps the session (and does
            not run `onProceed`); Discard and a successful Save proceed; Cancel
            does nothing. */
        void confirmUnsavedChanges (const juce::String& title, std::function<void()> onProceed);

        void loadSettings();
        void saveSettings();

        void runStartupRecovery();
        void onSessionOpened();

        /** Lays out the action row for `area` (top-left anchor), wrapping to a
            second line when the window is too narrow. Records the group-divider
            rectangles for paint() and returns the total height consumed. */
        int layoutActionRow (juce::Rectangle<int> area);

        /** Recomputes the per-track lane rectangles inside `trackLaneArea`.
            One rect per engine audio track (up to the available height), in
            engine order, so lane clicks map straight onto track indices. */
        void rebuildTrackLaneRects();

        static juce::String formatTime (double seconds);

        //==============================================================================
        AudioEngine audio;
        Session session { audio };
        DevicePanel devicePanel { audio };
        InputMeter inputMeter { session.getInputLevels(),
                                [this] { return audio.getNumActiveInputChannels(); } };
        MixerPanel mixerPanel { session };

        /** Epic 3: plugin hosting + routing/cue-mix overlays. */
        PluginHost pluginHost { audio.engine() };
        PluginBrowser pluginBrowser { pluginHost, session };
        RoutingPanel routingPanel { session };

        /** Clickable ruler + playhead above the arrangement lanes (owner request:
            position anywhere in the track). */
        Timeline timeline { session };

        BrandButton newButton { "New" }, openButton { "Open" };
        BrandButton closeButton { "Close" };
        BrandButton saveButton { "Save" }, saveAsButton { "Save As" };
        BrandButton importButton { "Import" };
        BrandButton normaliseButton { "Normalize" };
        BrandButton exportButton { "Export WAV", BrandButton::Style::Primary };
        BrandButton exportRegionButton { "Export region" };
        BrandButton clearRegionButton { "Clear region" };
        BrandButton stemsButton { "Export stems" };
        BrandButton pluginsButton { "Plugins" };
        BrandButton routingButton { "Routing" };
        BrandButton addTrackButton { "Add track" }, removeTrackButton { "Remove track" };
        BrandButton settingsButton { "Settings" }, aboutButton { "About" };

        BrandButton armButton { "Arm", BrandButton::Style::Chip };
        BrandButton recordButton { "Record", BrandButton::Style::Record };
        BrandButton playButton { "Play", BrandButton::Style::Transport };
        BrandButton stopButton { "Stop", BrandButton::Style::Transport };
        BrandButton goToStartButton { "Start", BrandButton::Style::Transport };
        BrandButton monitorButton { "Monitor", BrandButton::Style::Chip };
        BrandButton metronomeButton { "Click", BrandButton::Style::Chip };
        juce::Label countInLabel;
        juce::ComboBox countInBox;

        juce::Label titleLabel, transportLabel, statusLabel;

        /** Shows the current region-selection start/end (FR-EXP-2). */
        juce::Label selectionLabel;
        juce::TooltipWindow tooltipWindow { this, 700 };

        std::vector<juce::Rectangle<int>> actionDividers;

        std::unique_ptr<juce::PropertiesFile> settings;
        std::shared_ptr<WavExport::Handle> exportHandle;
        bool exportInProgress = false;
        std::shared_ptr<StemsExport::Handle> stemsHandle;
        bool stemsInProgress = false;
        /** Metronome state saved when an export starts, restored when it ends so
            the click can never leak into the rendered WAV (FR-EXP-1). */
        bool exportMetronomeWasEnabled = false;

        juce::String statusMessage;
        bool statusIsError = false;

        juce::Rectangle<int> trackLaneArea;

        /** Lane rectangles in `trackLaneArea`, engine-track order (FR-REC-2:
            click a lane to arm/disarm it). */
        std::vector<juce::Rectangle<int>> trackLaneRects;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
