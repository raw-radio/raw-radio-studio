// raw-radio-studio — Epic 0 engine wiring.
//
// Opens an empty JUCE window and proves that JUCE + Tracktion Engine compile and
// link (via Tracktion::Engine::getVersion()). There is no audio graph, device
// I/O, or transport yet — those arrive in Epic 1.
//
// Headless-friendly: `raw_radio_studio --version` prints the banner and exits
// without opening a window.

#include <JuceHeader.h>

#include <tracktion_engine/tracktion_engine.h>

#include <iostream>
#include <memory>

#ifndef RAW_RADIO_STUDIO_VERSION
 #define RAW_RADIO_STUDIO_VERSION "0.0.0-dev"
#endif

#ifndef RAW_RADIO_STUDIO_TRACKTION_VERSION
 #define RAW_RADIO_STUDIO_TRACKTION_VERSION "unknown"
#endif

namespace
{
    // Version banner. The Tracktion version comes from the pinned module's
    // VERSION.md (set in CMake) because upstream's runtime
    // Engine::getVersion() string is stale.
    juce::String versionBanner()
    {
        juce::String banner;
        banner << "raw-radio-studio " << RAW_RADIO_STUDIO_VERSION
               << "  (JUCE " << JUCE_MAJOR_VERSION << '.' << JUCE_MINOR_VERSION
               << '.' << JUCE_BUILDNUMBER
               << ", Tracktion Engine " << RAW_RADIO_STUDIO_TRACKTION_VERSION << ')';
        return banner;
    }

    // Placeholder view. Epic 1 replaces this with the device panel, a single
    // stereo track, transport, and an input meter.
    class PlaceholderComponent final : public juce::Component
    {
    public:
        PlaceholderComponent()
        {
            banner.setText (versionBanner(), juce::dontSendNotification);
            banner.setJustificationType (juce::Justification::centred);
            banner.setFont (juce::Font { juce::FontOptions { 16.0f } });
            addAndMakeVisible (banner);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
        }

        void resized() override
        {
            banner.setBounds (getLocalBounds().reduced (24));
        }

    private:
        juce::Label banner;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaceholderComponent)
    };
}

class RawRadioStudioApplication final : public juce::JUCEApplication
{
public:
    RawRadioStudioApplication() = default;

    const juce::String getApplicationName() override    { return "raw-radio-studio"; }
    const juce::String getApplicationVersion() override { return RAW_RADIO_STUDIO_VERSION; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& commandLine) override
    {
        // Force the Tracktion Engine module to be linked even though Epic 0 does
        // not use the engine yet. Calling a public symbol is what proves the
        // JUCE <-> Tracktion combination compiles AND links. (The returned string
        // is stale upstream, so it is not shown to users.)
        juce::ignoreUnused (tracktion::Engine::getVersion());

        std::cout << versionBanner() << std::endl;

        // Headless smoke path: print and exit without creating a window.
        if (commandLine.contains ("--version"))
        {
            quit();
            return;
        }

        mainWindow = std::make_unique<MainWindow>();
    }

    void shutdown() override { mainWindow.reset(); }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted (const juce::String&) override {}

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow()
            : juce::DocumentWindow ("raw-radio-studio",
                                    juce::Desktop::getInstance().getDefaultLookAndFeel()
                                        .findColour (juce::ResizableWindow::backgroundColourId),
                                    juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new PlaceholderComponent(), true);
            setResizable (true, false);
            setResizeLimits (480, 320, 10000, 10000);
            centreWithSize (900, 600);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (RawRadioStudioApplication)
