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
#include "ui/DevicePanel.h"
#include "ui/InputMeter.h"

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
        void saveSession();
        void saveSessionAs();
        void exportSession();
        void showAbout();
        void showSettings();

        void loadSettings();
        void saveSettings();

        void runStartupRecovery();
        void onSessionOpened();

        static juce::String formatTime (double seconds);

        //==============================================================================
        AudioEngine audio;
        Session session { audio };
        DevicePanel devicePanel { audio };
        InputMeter inputMeter { session.getInputLevels() };

        juce::TextButton newButton { "New" }, openButton { "Open" };
        juce::TextButton saveButton { "Save" }, saveAsButton { "Save As" };
        juce::TextButton exportButton { "Export WAV..." };
        juce::TextButton settingsButton { "Settings" }, aboutButton { "About" };

        juce::TextButton armButton { "Arm" }, recordButton { "Record" };
        juce::TextButton playButton { "Play" }, stopButton { "Stop" };
        juce::TextButton monitorButton { "Monitor" };

        juce::Label titleLabel, transportLabel, statusLabel;

        std::unique_ptr<juce::PropertiesFile> settings;
        std::shared_ptr<tracktion::EditRenderer::Handle> exportHandle;
        bool exportInProgress = false;

        juce::String statusMessage;
        bool statusIsError = false;

        juce::Rectangle<int> trackLaneArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
