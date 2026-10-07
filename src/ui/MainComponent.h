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

#include <memory>

#include "studio/AudioEngine.h"
#include "studio/Session.h"
#include "studio/WavExport.h"
#include "ui/BrandButton.h"
#include "ui/DevicePanel.h"
#include "ui/InputMeter.h"

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
        void showAbout();
        void showSettings();

        void loadSettings();
        void saveSettings();

        void runStartupRecovery();
        void onSessionOpened();

        /** Lays out the action row for `area` (top-left anchor), wrapping to a
            second line when the window is too narrow. Records the group-divider
            rectangles for paint() and returns the total height consumed. */
        int layoutActionRow (juce::Rectangle<int> area);

        static juce::String formatTime (double seconds);

        //==============================================================================
        AudioEngine audio;
        Session session { audio };
        DevicePanel devicePanel { audio };
        InputMeter inputMeter { session.getInputLevels(),
                                [this] { return audio.getNumActiveInputChannels(); } };

        BrandButton newButton { "New" }, openButton { "Open" };
        BrandButton closeButton { "Close" };
        BrandButton saveButton { "Save" }, saveAsButton { "Save As" };
        BrandButton importButton { "Import" };
        BrandButton exportButton { "Export WAV", BrandButton::Style::Primary };
        BrandButton settingsButton { "Settings" }, aboutButton { "About" };

        BrandButton armButton { "Arm", BrandButton::Style::Chip };
        BrandButton recordButton { "Record", BrandButton::Style::Record };
        BrandButton playButton { "Play", BrandButton::Style::Transport };
        BrandButton stopButton { "Stop", BrandButton::Style::Transport };
        BrandButton monitorButton { "Monitor", BrandButton::Style::Chip };

        juce::Label titleLabel, transportLabel, statusLabel;
        juce::TooltipWindow tooltipWindow { this, 700 };

        std::vector<juce::Rectangle<int>> actionDividers;

        std::unique_ptr<juce::PropertiesFile> settings;
        std::shared_ptr<tracktion::EditRenderer::Handle> exportHandle;
        bool exportInProgress = false;

        juce::String statusMessage;
        bool statusIsError = false;

        juce::Rectangle<int> trackLaneArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
