// raw-radio-studio — Epic 1 headless integration tests (doctest).
//
// Unlike tests/test_studio.cpp (pure logic), these tests use the *real*
// Tracktion Engine in headless mode to exercise the acceptance-critical code
// paths that the UI would otherwise only reach with a human at the controls:
//
//   * FR-EXP-1 / NFR-A-1: `WavExport` renders a synthetic edit offline to a
//     24-bit WAV at the session (or Epic 1 default) sample rate.
//   * FR-PRJ-1: a project saved to `.tracktionedit` reopens with the recorded
//     take intact.
//   * FR-PRJ-2 / FR-REC-8: autosave writes the `.tmp_<name>` sibling and
//     `Session::detectRecovery()` finds it (crash-recovery scan).
//
// No audio hardware is opened: the engine is constructed with a behaviour that
// disables device auto-initialisation, so the tests are hermetic and CI-safe.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <JuceHeader.h>

#include <tracktion_engine/tracktion_engine.h>

#include "studio/AppPaths.h"
#include "studio/AudioEngine.h"
#include "studio/DeviceError.h"
#include "studio/Session.h"
#include "studio/WavExport.h"

#include <atomic>
#include <cmath>
#include <memory>

namespace te = tracktion;
using namespace rrs;

namespace
{
    //==========================================================================
    /** Engine behaviour that never opens a real audio device, keeping the tests
        hermetic. `getDeviceManager().getSampleRate()` then reports Tracktion's
        no-device default (44100 Hz in this Engine version), which is the exact
        "device closed at export time" case WavExport has to handle. */
    class HeadlessBehaviour final : public te::EngineBehaviour
    {
    public:
        bool autoInitialiseDeviceManager() override   { return false; }
        bool shouldOpenAudioInputByDefault() override { return false; }
    };

    struct TestContext
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<te::Engine> engine;

        TestContext()
        {
            engine = std::make_unique<te::Engine> ("raw-radio-studio-tests",
                                                   nullptr,
                                                   std::make_unique<HeadlessBehaviour>());
        }
    };

    te::Engine& testEngine()
    {
        static TestContext context;
        return *context.engine;
    }

    juce::File scratchDirectory (const juce::String& name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("raw-radio-studio-integration")
                       .getChildFile (name);
        dir.deleteRecursively();
        dir.createDirectory();
        return dir;
    }

    /** Writes a valid 24-bit stereo sine WAV so an edit can reference it. */
    juce::File writeSineWav (const juce::File& file, double sampleRate, double seconds)
    {
        file.deleteFile();

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);

        auto writer = wav.createWriterFor (stream,
                                           juce::AudioFormatWriterOptions{}
                                               .withSampleRate (sampleRate)
                                               .withNumChannels (2)
                                               .withBitsPerSample (24));

        if (writer == nullptr)
            return {};

        const auto numSamples = (int) (sampleRate * seconds);
        juce::AudioBuffer<float> buffer (2, numSamples);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < numSamples; ++i)
                buffer.setSample (ch, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                   * 440.0 * (double) i / sampleRate));

        if (! writer->writeFromAudioSampleBuffer (buffer, 0, numSamples))
            return {};

        writer.reset();
        return file;
    }

    /** Creates an edit with one stereo wave clip and returns it.
        The edit uses the `forRendering` role (playDisabled) because these tests
        run headless with no audio device: an edit for device playback cannot
        build its audio graph without one. This is the same role Tracktion's own
        offline-render tests use, and WavExport/EditRenderer build the graph
        themselves. */
    std::unique_ptr<te::Edit> makeEditWithClip (const juce::File& editFile, const juce::File& wavFile)
    {
        auto edit = te::Edit::createSingleTrackEdit (testEngine(), te::Edit::EditRole::forRendering);

        if (edit == nullptr)
            return {};

        // Retarget saves at the requested session file and materialise it on
        // disk *before* inserting the clip: Tracktion computes the source's
        // relative path from the edit file's directory only when that file
        // already exists (File::getRelativePathFrom). A real session is always
        // saved first, so this mirrors production and keeps recovery realistic.
        edit->editFileRetriever = [editFile] { return editFile; };
        editFile.getParentDirectory().createDirectory();

        te::EditFileOperations (*edit).save (true, true, false);

        if (! editFile.existsAsFile())
            return nullptr;

        edit->ensureNumberOfAudioTracks (1);

        auto tracks = te::getAudioTracks (*edit);

        if (tracks.isEmpty())
            return {};

        const auto length = te::TimeDuration::fromSeconds (0.5);
        tracks[0]->insertWaveClip ("take", wavFile,
                                   { { te::TimePosition(), length }, {} }, false);
        return edit;
    }
}

//==============================================================================
TEST_CASE ("WavExport renders a synthetic edit to 24-bit WAV at the session sample rate")
{
    auto dir = scratchDirectory ("export");
    auto editFile = dir.getChildFile ("Export Source.tracktionedit");

    auto wavFile = writeSineWav (dir.getChildFile ("sine.wav"), 48000.0, 0.5);
    REQUIRE (wavFile.existsAsFile());

    auto edit = makeEditWithClip (editFile, wavFile);
    REQUIRE (edit != nullptr);

    auto dest = dir.getChildFile ("mix.wav");

    std::atomic<bool> finished { false };
    bool succeeded = false;
    juce::String error;

    auto handle = WavExport::start (*edit, dest,
                                    [&] (bool success, juce::File, juce::String message)
                                    {
                                        succeeded = success;
                                        error = message;
                                        finished = true;
                                    });

    REQUIRE (handle != nullptr);

    for (int i = 0; i < 400 && ! finished.load(); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (25);

    REQUIRE (finished.load());
    INFO ("render error: " << error);
    CHECK (succeeded);
    REQUIRE (dest.existsAsFile());

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (
        wav.createReaderFor (new juce::FileInputStream (dest), true));

    REQUIRE (reader != nullptr);

    // FR-EXP-1 / NFR-A-1: 24-bit output.
    CHECK ((int) reader->bitsPerSample == WavExport::bitDepth);
    CHECK (reader->bitsPerSample == 24u);

    // FR-EXP-1: export at the session sample rate. WavExport uses the engine's
    // DeviceManager rate; with no device open this Engine version reports 44100
    // (DeviceManager::getSampleRate), so we assert the code faithfully follows
    // the session rate rather than a hard-coded value.
    const auto sessionRate = edit->engine.getDeviceManager().getSampleRate();
    CHECK (sessionRate > 0.0);
    CHECK (reader->sampleRate == doctest::Approx (sessionRate));

    // Documents the headless behaviour: WavExport's `<= 0 -> 48 kHz` fallback is
    // unreachable with Tracktion 3.5.0 because getSampleRate() never returns 0.
    CHECK (reader->sampleRate == doctest::Approx (44100.0));

    CHECK (reader->numChannels == 2u);
    CHECK (reader->lengthInSamples > 0);
}

//==============================================================================
TEST_CASE ("project save/open round-trip keeps the take intact")
{
    auto dir = scratchDirectory ("roundtrip");
    auto editFile = dir.getChildFile ("Round Trip.tracktionedit");

    auto wavFile = writeSineWav (dir.getChildFile ("sine.wav"), 48000.0, 0.5);
    REQUIRE (wavFile.existsAsFile());

    auto edit = makeEditWithClip (editFile, wavFile);
    REQUIRE (edit != nullptr);

    te::EditFileOperations ops (*edit);
    ops.save (true, true, false);

    // FR-PRJ-1: a real `.tracktionedit` file on disk.
    CHECK (editFile.existsAsFile());

    // A successful save moves the `.tmp_` version over the target, so no
    // autosave artifact is left behind (would otherwise trigger a spurious
    // recovery prompt on the next launch).
    CHECK_FALSE (paths::tempEditFileFor (editFile).existsAsFile());

    auto reopened = te::loadEditFromFile (testEngine(), editFile);
    REQUIRE (reopened != nullptr);

    auto tracks = te::getAudioTracks (*reopened);
    REQUIRE_EQ (tracks.size(), 1);
    REQUIRE (tracks[0] != nullptr);

    // The take survived the save/open cycle.
    CHECK_EQ (tracks[0]->getClips().size(), 1);
}

//==============================================================================
TEST_CASE ("autosave writes the .tmp_ sibling and detectRecovery finds it")
{
    auto dir = scratchDirectory ("recovery");
    auto editFile = dir.getChildFile ("Crash Session.tracktionedit");

    auto wavFile = writeSineWav (dir.getChildFile ("sine.wav"), 48000.0, 0.5);
    REQUIRE (wavFile.existsAsFile());

    auto edit = makeEditWithClip (editFile, wavFile);
    REQUIRE (edit != nullptr);

    te::EditFileOperations ops (*edit);

    // FR-PRJ-2: the periodic autosave path (Session::timerCallback calls this).
    REQUIRE (ops.saveTempVersion (true));

    const auto tempFile = paths::tempEditFileFor (editFile);

    // The real session file exists (it was saved before the crash); the
    // autosave writes the `.tmp_` sibling the recovery scan looks for.
    CHECK (editFile.existsAsFile());
    CHECK (tempFile.existsAsFile());
    CHECK (tempFile != editFile);

    const auto info = Session::detectRecovery (editFile);
    CHECK (info.editFile == editFile);
    CHECK (info.tempEditFile == tempFile);
    CHECK (info.hasTempEdit);
    CHECK (info.hasRecoverables());
}

//==============================================================================
// NFR-IO-4 / FR-MON-5: on Linux, ALSA hardware is opened directly and a failure
// to do so is surfaced loudly — never a silent fallback to PipeWire/Pulse/JACK.
// The policy logic itself is unit-tested on every platform in test_studio.cpp;
// this exercises the real `AudioEngine` startup path on Linux only.
#if JUCE_LINUX
TEST_CASE ("AudioEngine surfaces a loud error instead of silently falling back (ALSA)")
{
    AudioEngine engine;

    // A CI container has no direct ALSA hardware (or only plugin PCMs such as
    // `default`/`pulse`/`pipewire`, all excluded by design). In that case the
    // engine must report an actionable error and leave no device open rather
    // than silently opening the plugin path.
    if (! engine.hasActiveDevice())
        CHECK (engine.getLastError().isNotEmpty());

    // If real direct hardware is present, an acceptable device is opened and no
    // fallback error is recorded.
    if (engine.hasActiveDevice())
        CHECK (isAcceptableInputDeviceName (engine.getCurrentDeviceName()));
}
#endif
