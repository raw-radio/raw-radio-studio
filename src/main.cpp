// raw-radio-studio — Epic 1 application entry point.
//
// Opens the tracking-first session window (device panel, transport, input meter,
// single stereo track, WAV export). `raw-radio-studio --version` prints the
// banner and exits without creating a window (used by the CI smoke test).

#include <JuceHeader.h>

#include <tracktion_engine/tracktion_engine.h>

#include <cmath>
#include <atomic>
#include <iostream>
#include <memory>

#include "studio/AudioEngine.h"
#include "ui/BrandLookAndFeel.h"
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

    // =========================================================================
    // DIAGNOSTIC HOOKS — headless hardware self-test (NOT a user-facing feature)
    //
    // These paths exist purely to exercise the real capture pipeline on a
    // developer/QA machine:
    //   * `--list-devices`                    enumerate the audio backends and
    //                                         their input/output device names;
    //   * `--selftest-record <s> <out.wav>`   capture <s> seconds from the
    //                                         selected input and write a WAV.
    //
    // They deliberately reuse `rrs::AudioEngine` (the same Tracktion
    // Engine + DeviceManager path the GUI uses), so a green self-test means the
    // real capture path works. They are handled in `main()` before JUCE's
    // single-instance gate, mirroring `--version`.
    // =========================================================================

    // Audio-thread capture sink for `--selftest-record`. All storage is
    // pre-allocated before the callback is registered, so the callback itself
    // performs no allocation/locking/I/O.
    class CaptureCallback final : public juce::AudioIODeviceCallback
    {
    public:
        void audioDeviceAboutToStart (juce::AudioIODevice* device) override
        {
            const auto channels = juce::jmax (1, device->getActiveInputChannels().countNumberOfSetBits());
            numChannels = channels;

            // Normally already sized by the caller; only (re)allocate if needed.
            if (captureBuffer.getNumChannels() != channels
                || captureBuffer.getNumSamples() < totalFrames)
            {
                captureBuffer.setSize (channels, totalFrames, false, true, false);
            }

            captureBuffer.clear();
        }

        void audioDeviceStopped() override {}

        void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                               int numInputChannels,
                                               float* const* /*outputChannelData*/,
                                               int /*numOutputChannels*/,
                                               int numSamples,
                                               const juce::AudioIODeviceCallbackContext& /*context*/) override
        {
            if (finished.load())
                return;

            const auto remaining = totalFrames - written;

            if (remaining <= 0)
            {
                finished.store (true);
                return;
            }

            const auto n = juce::jmin (numSamples, remaining);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* dst = captureBuffer.getWritePointer (ch) + written;
                const auto* src = ch < numInputChannels ? inputChannelData[ch] : nullptr;

                for (int i = 0; i < n; ++i)
                {
                    const auto sample = src != nullptr ? src[i] : 0.0f;
                    dst[i] = sample;

                    const auto magnitude = std::abs (sample);
                    peak = juce::jmax (peak, magnitude);
                    sumOfSquares += static_cast<double> (sample) * static_cast<double> (sample);
                    ++sampleCount;
                }
            }

            written += n;

            if (written >= totalFrames)
                finished.store (true);
        }

        juce::AudioBuffer<float> captureBuffer;

        int numChannels = 0;
        int totalFrames = 0;

        // Written on the audio thread, read only after the callback is removed.
        int written = 0;
        float peak = 0.0f;
        double sumOfSquares = 0.0;
        juce::int64 sampleCount = 0;

        std::atomic<bool> finished { false };
    };

    void printAvailableInputDevices (const rrs::AudioEngine& audio)
    {
        const auto names = audio.getInputDeviceNames();

        if (names.isEmpty())
        {
            std::cout << "  (no input devices found)" << std::endl;
            return;
        }

        for (auto& name : names)
            std::cout << "    - \"" << name << "\"" << std::endl;
    }

    // `--list-devices`: print every audio backend and its input/output names.
    int runListDevices()
    {
        const juce::ScopedJuceInitialiser_GUI juceInit;
        rrs::AudioEngine audio;

        auto& dm = audio.deviceManager().deviceManager;

        std::cout << "raw-radio-studio device enumeration\n";
        std::cout << "Current backend: " << audio.getCurrentDeviceTypeName() << "\n";

        for (auto* type : dm.getAvailableDeviceTypes())
        {
            if (type == nullptr)
                continue;

            type->scanForDevices();

            const bool current = type->getTypeName() == audio.getCurrentDeviceTypeName();
            std::cout << (current ? "* " : "  ")
                      << "[" << type->getTypeName() << "]\n";

            const auto inputs = type->getDeviceNames (true);
            const auto outputs = type->getDeviceNames (false);

            std::cout << "    inputs:\n";

            if (inputs.isEmpty())
                std::cout << "      (none)\n";

            for (auto& name : inputs)
                std::cout << "      - \"" << name << "\"\n";

            std::cout << "    outputs:\n";

            if (outputs.isEmpty())
                std::cout << "      (none)\n";

            for (auto& name : outputs)
                std::cout << "      - \"" << name << "\"\n";
        }

        return 0;
    }

    float linearToDb (float linear)
    {
        return linear > 0.0f ? juce::Decibels::gainToDecibels (linear) : -1000.0f;
    }

    // `--selftest-record <seconds> <out.wav> [--device "<name substring>"]`:
    // open the input, capture synchronously, write a 24-bit WAV, print stats.
    int runSelfTestRecord (const juce::String& secondsText,
                           const juce::String& outputPath,
                           const juce::String& deviceSubstring)
    {
        const auto seconds = secondsText.getDoubleValue();

        if (seconds <= 0.0 || seconds > 3600.0 || outputPath.isEmpty())
        {
            std::cerr << "error: usage: --selftest-record <seconds> <out.wav> "
                         "[--device \"<name substring>\"]\n";
            return 2;
        }

        const auto outputFile = juce::File::getCurrentWorkingDirectory().getChildFile (outputPath);

        const juce::ScopedJuceInitialiser_GUI juceInit;
        rrs::AudioEngine audio;
        auto& dm = audio.deviceManager().deviceManager;

        // --- Select the input device (no silent fallback) --------------------
        juce::String deviceName;

        if (deviceSubstring.isNotEmpty())
        {
            for (auto& name : audio.getInputDeviceNames())
                if (name.containsIgnoreCase (deviceSubstring))
                {
                    deviceName = name;
                    break;
                }

            if (deviceName.isEmpty())
            {
                std::cerr << "error: no input device matching \"" << deviceSubstring << "\".\n"
                          << "Available input devices:\n";
                printAvailableInputDevices (audio);
                return 3;
            }
        }
        else
        {
            deviceName = audio.getCurrentDeviceName();

            if (deviceName.isEmpty())
            {
                std::cerr << "error: no default input device is available.\n"
                          << "Available input devices:\n";
                printAvailableInputDevices (audio);
                return 3;
            }
        }

        if (auto error = audio.applyInputDeviceSetup (deviceName); error.isNotEmpty())
        {
            std::cerr << "error: could not open input device \"" << deviceName << "\":\n"
                      << error << std::endl;
            return 4;
        }

        auto* device = dm.getCurrentAudioDevice();

        if (device == nullptr || ! device->isOpen())
        {
            // Last resort: ask JUCE to (re)open a default input+output pair.
            const auto initError = dm.initialiseWithDefaultDevices (2, 0);
            device = dm.getCurrentAudioDevice();

            if (device == nullptr || ! device->isOpen())
            {
                std::cerr << "error: could not open an input device"
                          << (initError.isNotEmpty() ? (": " + initError) : juce::String())
                          << std::endl;
                return 4;
            }
        }

        const auto sampleRate = device->getCurrentSampleRate();
        const auto bufferSize = device->getCurrentBufferSizeSamples();

        if (sampleRate <= 0.0)
        {
            std::cerr << "error: the open device reports an invalid sample rate ("
                      << sampleRate << ")." << std::endl;
            return 4;
        }

        const auto channels = juce::jmax (1, device->getActiveInputChannels().countNumberOfSetBits());
        const auto totalFrames = (int) std::llround (seconds * sampleRate);

        CaptureCallback capture;
        capture.numChannels = channels;
        capture.totalFrames = totalFrames;
        capture.captureBuffer.setSize (channels, totalFrames, false, true, false);
        capture.captureBuffer.clear();

        std::cout << "Recording " << seconds << " s from \"" << device->getName() << "\""
                  << " (" << (int) sampleRate << " Hz, " << channels << " ch, buffer "
                  << bufferSize << ")..." << std::endl;

        dm.addAudioCallback (&capture);

        // Real-time wait with a bounded grace period so a stalled device cannot
        // hang the CLI forever.
        const auto deadlineMs = juce::Time::getMillisecondCounterHiRes() + (seconds + 5.0) * 1000.0;

        while (! capture.finished.load()
               && juce::Time::getMillisecondCounterHiRes() < deadlineMs)
            juce::Thread::sleep (20);

        dm.removeAudioCallback (&capture);

        const auto capturedFrames = capture.written;

        if (capturedFrames <= 0)
        {
            std::cerr << "error: the device delivered no audio frames. On macOS this is "
                         "usually a microphone (TCC) permission denial — grant mic access "
                         "to raw-radio-studio in System Settings > Privacy & Security > "
                         "Microphone, or run from a terminal that has permission."
                      << std::endl;
            return 5;
        }

        // --- Write the 24-bit WAV --------------------------------------------
        outputFile.getParentDirectory().createDirectory();
        outputFile.deleteFile();

        auto fileStream = outputFile.createOutputStream();

        if (fileStream == nullptr || fileStream->failedToOpen())
        {
            std::cerr << "error: could not create the output file: "
                      << outputFile.getFullPathName() << std::endl;
            return 6;
        }

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::OutputStream> stream = std::move (fileStream);

        // The writer takes ownership of `stream` on success (it is left null).
        auto writer = wavFormat.createWriterFor (
            stream,
            juce::AudioFormatWriterOptions{}
                .withSampleRate (sampleRate)
                .withNumChannels (channels)
                .withBitsPerSample (24));

        if (writer == nullptr)
        {
            std::cerr << "error: could not create a 24-bit WAV writer for "
                      << outputFile.getFullPathName() << std::endl;
            return 6;
        }

        writer->writeFromAudioSampleBuffer (capture.captureBuffer, 0, capturedFrames);
        writer->flush();
        writer.reset(); // finalises the file

        const auto rms = capture.sampleCount > 0
                             ? std::sqrt (capture.sumOfSquares / (double) capture.sampleCount)
                             : 0.0;

        std::cout << "Self-test recording complete\n"
                  << "  device:           " << device->getName() << "\n"
                  << "  sample rate:      " << (int) sampleRate << " Hz\n"
                  << "  buffer size:      " << bufferSize << " samples\n"
                  << "  channels:         " << channels << "\n"
                  << "  recorded frames:  " << capturedFrames
                  << " (" << juce::String (capturedFrames / sampleRate, 3) << " s)\n"
                  << "  peak:             " << juce::String (linearToDb (capture.peak), 2)
                  << " dBFS (linear " << juce::String (capture.peak, 6) << ")\n"
                  << "  RMS:              " << juce::String (linearToDb ((float) rms), 2)
                  << " dBFS (linear " << juce::String (rms, 6) << ")\n"
                  << "  output:           " << outputFile.getFullPathName() << std::endl;

        return 0;
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

        // Brand theme: install the RAW Radio LookAndFeel before any window (and
        // therefore before any child component) is created, so the DocumentWindow
        // background and every stock component resolve the brand colour IDs.
        brandLookAndFeel = std::make_unique<rrs::BrandLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (brandLookAndFeel.get());

        mainWindow = std::make_unique<MainWindow>();
    }

    void shutdown() override
    {
        // Destroy the window (and its components) before the LookAndFeel it
        // resolves colours from.
        mainWindow.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        brandLookAndFeel.reset();
    }

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
    std::unique_ptr<rrs::BrandLookAndFeel> brandLookAndFeel;
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
    juce::StringArray args;

    for (int i = 1; i < argc; ++i)
        args.add (juce::String (argv[i]));

    // `--version` is handled first and unconditionally (see note above).
    if (args.contains ("--version"))
    {
        std::cout << versionBanner() << std::endl;
        return 0;
    }

    // Diagnostic hooks — handled before the single-instance gate, like --version,
    // so they run headless without a window and without forwarding to a running
    // instance. Clearly marked diagnostics, not user-facing features.
    if (args.contains ("--list-devices"))
        return runListDevices();

    if (args.contains ("--selftest-record"))
    {
        const auto index = args.indexOf ("--selftest-record");

        if (index + 2 >= args.size())
        {
            std::cerr << "error: usage: --selftest-record <seconds> <out.wav> "
                         "[--device \"<name substring>\"]\n";
            return 2;
        }

        const auto secondsText = args[index + 1];
        const auto outputPath  = args[index + 2];

        juce::String deviceSubstring;
        const auto deviceIndex = args.indexOf ("--device");

        if (deviceIndex >= 0 && deviceIndex + 1 < args.size())
            deviceSubstring = args[deviceIndex + 1];

        return runSelfTestRecord (secondsText, outputPath, deviceSubstring);
    }

    juce::JUCEApplicationBase::createInstance = &juce_CreateApplication;
    return juce::JUCEApplicationBase::main (argc, (const char**) argv);
}

JUCE_END_IGNORE_WARNINGS_GCC_LIKE
#else
START_JUCE_APPLICATION (RawRadioStudioApplication)
#endif
