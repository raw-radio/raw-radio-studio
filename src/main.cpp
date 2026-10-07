// raw-radio-studio — Epic 1 application entry point.
//
// Opens the tracking-first session window (device panel, transport, input meter,
// single stereo track, WAV export). `raw-radio-studio --version` prints the
// banner and exits without creating a window (used by the CI smoke test).

#include <JuceHeader.h>

#include <tracktion_engine/tracktion_engine.h>

#include <iostream>
#include <memory>

#include "ui/MainComponent.h"

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
}

class RawRadioStudioApplication final : public juce::JUCEApplication
{
public:
    RawRadioStudioApplication() = default;

    const juce::String getApplicationName() override    { return "raw-radio-studio"; }
    const juce::String getApplicationVersion() override { return RAW_RADIO_STUDIO_VERSION; }

    // Single instance: the session lock (AppPaths::lockFile) and the recordings
    // folder are app-global, so a second instance would clobber the first's
    // crash-recovery sentinel. A second launch forwards to the running instance
    // (see anotherInstanceStarted) and exits.
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        // Keep the Tracktion Engine module genuinely linked (Epic 0 compatibility).
        juce::ignoreUnused (tracktion::Engine::getVersion());

        std::cout << versionBanner() << std::endl;

        // Headless smoke path: print and exit without creating a window.
        const auto args = juce::StringArray::fromTokens (commandLine, true);

        if (args.contains ("--version"))
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
            setContentOwned (new rrs::MainComponent(), true);
            setResizable (true, false);
            setResizeLimits (820, 560, 10000, 10000);
            centreWithSize (980, 660);
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

// Custom entry point (macOS/Linux use a plain `main`; Windows is not a target).
//
// We must handle `--version` BEFORE JUCE's single-instance gate: with
// moreThanOneInstanceAllowed()==false, `initialiseApp()` forwards the command
// line to an already-running instance and returns false without ever calling
// initialise(), so the launching process would print nothing. The CI smoke
// contract requires `raw-radio-studio --version` to print the banner and exit 0
// unconditionally — even when another instance is running.
#if ! (JUCE_WINDOWS && ! defined (_CONSOLE))
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wmissing-prototypes")

juce::JUCEApplicationBase* juce_CreateApplication() { return new RawRadioStudioApplication(); }

int main (int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        if (juce::String (argv[i]) == "--version")
        {
            std::cout << versionBanner() << std::endl;
            return 0;
        }
    }

    juce::JUCEApplicationBase::createInstance = &juce_CreateApplication;
    return juce::JUCEApplicationBase::main (argc, (const char**) argv);
}

JUCE_END_IGNORE_WARNINGS_GCC_LIKE
#else
START_JUCE_APPLICATION (RawRadioStudioApplication)
#endif
