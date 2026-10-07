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

// Custom test main (instead of DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN).
//
// On macOS, JUCE 9's `Desktop::~Desktop` tears down its
// `NSDistributedNotificationCenter` dark-mode observer during static
// destruction. In a headless *console* test process (no NSApplication run),
// that observer is already gone and the teardown crashes with EXC_BAD_ACCESS
// inside objc_msgSend. The application (a normal GUI app) exits through
// `JUCEApplicationBase::main`, which tears JUCE down cleanly; only this
// console binary hits the issue. After the tests have run and the streams are
// flushed we therefore skip the static destructors with `_Exit`.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include <cstdlib>
#include <iostream>

int main (int argc, char** argv)
{
    doctest::Context context;
    context.applyCommandLine (argc, argv);
    const int result = context.run();

    std::cout.flush();
    std::fflush (nullptr);
    std::_Exit (result);
}

#include <JuceHeader.h>

#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>

#include "studio/AppPaths.h"
#include "studio/AudioEngine.h"
#include "studio/AudioImport.h"
#include "studio/DeviceError.h"
#include "studio/InputRouting.h"
#include "studio/Session.h"
#include "studio/WavExport.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <set>
#include <thread>
#include <vector>

namespace te = tracktion;
using namespace rrs;

namespace
{
    //==========================================================================
    /** Engine behaviour that never opens a real audio device, keeping the tests
        hermetic. With no device open, Tracktion's
        `DeviceManager::getSampleRate()` reports a 44100 Hz placeholder (never
        0), which is exactly the "device closed at export time" case WavExport
        has to detect explicitly so it uses the 48 kHz Epic 1 default. */
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
TEST_CASE ("WavExport renders a synthetic edit to 24-bit WAV at the default rate when no device is open")
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

    // FR-EXP-1 / NFR-A-4: with a device open the export runs at the session
    // sample rate; headless (no device) it must use the Epic 1 default (48 kHz).
    // Tracktion's DeviceManager::getSampleRate() returns a 44100 Hz placeholder
    // when no device is open (it never returns 0), which is exactly why
    // WavExport must detect the no-device case via getCurrentAudioDevice()
    // rather than relying on a `<= 0` guard. This test is the regression guard
    // for that fix.
    CHECK (edit->engine.getDeviceManager().deviceManager.getCurrentAudioDevice() == nullptr);

    // The placeholder that used to leak through into the exported file:
    CHECK (edit->engine.getDeviceManager().getSampleRate() == doctest::Approx (44100.0));

    // ...while the export itself is the corrected 48 kHz default (NFR-A-4).
    CHECK (reader->sampleRate == doctest::Approx (AudioEngine::defaultSampleRate));
    CHECK (reader->sampleRate == doctest::Approx (48000.0));

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
TEST_CASE ("imported audio file is referenced on a new track and survives save/open")
{
    auto dir = scratchDirectory ("import");
    auto editFile = dir.getChildFile ("Import Session.tracktionedit");

    auto wavFile = writeSineWav (dir.getChildFile ("minus.wav"), 48000.0, 0.5);
    REQUIRE (wavFile.existsAsFile());

    const auto sourceSizeBefore = wavFile.getSize();

    // Start from a session that already has a track (stands in for the armed
    // record track), mirroring a real "record voice over the minus" session.
    auto edit = te::Edit::createSingleTrackEdit (testEngine(), te::Edit::EditRole::forRendering);
    REQUIRE (edit != nullptr);
    edit->editFileRetriever = [editFile] { return editFile; };
    editFile.getParentDirectory().createDirectory();
    te::EditFileOperations (*edit).save (true, true, false);
    REQUIRE (editFile.existsAsFile());
    REQUIRE_EQ (te::getAudioTracks (*edit).size(), 1);

    const auto result = AudioImport::import (*edit, wavFile);
    INFO ("import error: " << result.error);
    REQUIRE (result.success);
    REQUIRE (result.track != nullptr);
    REQUIRE (result.clip != nullptr);

    // The import created its own track alongside the existing one.
    CHECK_EQ (te::getAudioTracks (*edit).size(), 2);
    CHECK (result.track->getName() == "minus");

    // The clip references the original file (no copy) and has real length.
    CHECK (result.clip->getOriginalFile() == wavFile);
    CHECK (result.clip->getPosition().getLength().inSeconds() > 0.0);

    // The source file is untouched by the import.
    REQUIRE (wavFile.existsAsFile());
    CHECK (wavFile.getSize() == sourceSizeBefore);

    // FR-PRJ-1: the reference survives a save/open round-trip.
    te::EditFileOperations (*edit).save (true, true, false);

    auto reopened = te::loadEditFromFile (testEngine(), editFile);
    REQUIRE (reopened != nullptr);

    auto tracks = te::getAudioTracks (*reopened);
    REQUIRE_EQ (tracks.size(), 2);
    REQUIRE_EQ (tracks[1]->getClips().size(), 1);

    auto* reopenedClip = dynamic_cast<te::WaveAudioClip*> (tracks[1]->getClips()[0]);
    REQUIRE (reopenedClip != nullptr);
    CHECK (reopenedClip->getOriginalFile() == wavFile);
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
TEST_CASE ("a successful save consumes the autosave temp version (no spurious recovery)")
{
    // Regression: Session::save() runs the same `ops.save(true, true, false)`
    // path. An explicit save must move the `.tmp_` over the session file so a
    // later crash-recovery scan does NOT report a stale temp edit.
    auto dir = scratchDirectory ("save-consumes-temp");
    auto editFile = dir.getChildFile ("Consume.tracktionedit");

    auto wavFile = writeSineWav (dir.getChildFile ("sine.wav"), 48000.0, 0.5);
    REQUIRE (wavFile.existsAsFile());

    auto edit = makeEditWithClip (editFile, wavFile);
    REQUIRE (edit != nullptr);

    te::EditFileOperations ops (*edit);

    // Autosave writes the `.tmp_` sibling (FR-PRJ-2)...
    REQUIRE (ops.saveTempVersion (true));
    CHECK (paths::tempEditFileFor (editFile).existsAsFile());

    // ...and the explicit save consumes it.
    ops.save (true, true, false);
    CHECK_FALSE (paths::tempEditFileFor (editFile).existsAsFile());

    const auto info = Session::detectRecovery (editFile);
    CHECK_FALSE (info.hasTempEdit);
}

//==============================================================================
TEST_CASE ("interrupted-session detection is deterministic (sentinel or temp always prompts)")
{
    // The startup decision must key off an explicit marker, never off transient
    // autosave state that a proactive save() would have consumed. These are the
    // three cases MainComponent::runStartupRecovery distinguishes.
    {
        Session::RecoveryInfo clean;
        CHECK_FALSE (clean.interrupted());
    }
    {
        Session::RecoveryInfo unclean;
        unclean.uncleanShutdown = true; // startup sentinel present, no temp yet
        CHECK (unclean.interrupted());
    }
    {
        Session::RecoveryInfo temp;
        temp.hasTempEdit = true;        // unsaved temp left by a quit
        CHECK (temp.interrupted());
    }
}

TEST_CASE ("detectRecovery treats a leftover temp edit as an interrupted session")
{
    auto dir = scratchDirectory ("interrupt-temp");
    auto editFile = dir.getChildFile ("Interrupted.tracktionedit");

    // Simulate an autosave temp surviving a crash: no real session file needed
    // for detection to flag the session as interrupted.
    const auto tempFile = paths::tempEditFileFor (editFile);
    tempFile.replaceWithText ("<EDIT/>");

    const auto info = Session::detectRecovery (editFile);
    CHECK (info.hasTempEdit);
    CHECK (info.interrupted());
    CHECK (info.hasRecoverables());
}

//==============================================================================
TEST_CASE ("the interruption sentinel on disk is detected as an interrupted session")
{
    // Regression guard for the startup decision: when the previous run left the
    // `session.lock` sentinel behind, detectRecovery() must report the session
    // as interrupted (FR-REC-8).
    const auto sentinel = paths::lockFile();
    const bool existedBefore = sentinel.existsAsFile();
    const auto previousContent = existedBefore ? sentinel.loadFileAsString() : juce::String();

    paths::appDataDirectory().createDirectory();
    REQUIRE (sentinel.replaceWithText ("raw-radio-studio test sentinel\n"));

    auto dir = scratchDirectory ("sentinel");
    const auto info = Session::detectRecovery (dir.getChildFile ("Session.tracktionedit"));

    CHECK (info.uncleanShutdown);
    CHECK (info.interrupted());

    // Restore whatever was there before so running the suite locally does not
    // leave a false "interrupted" marker behind.
    if (existedBefore)
        sentinel.replaceWithText (previousContent);
    else
        sentinel.deleteFile();
}

//==============================================================================
TEST_CASE ("Session::close resets state and clears the interruption sentinel")
{
    auto dir = scratchDirectory ("session-close");
    auto editFile = dir.getChildFile ("Close Me.tracktionedit");

    // Preserve any pre-existing sentinel (e.g. a concurrently running app) so
    // this hermetic test does not clobber app-data state.
    const auto sentinel = paths::lockFile();
    const bool sentinelExistedBefore = sentinel.existsAsFile();
    const auto sentinelContentBefore = sentinelExistedBefore ? sentinel.loadFileAsString() : juce::String();

    // Headless engine: no audio hardware is opened.
    AudioEngine audio (false);
    Session session (audio);

    REQUIRE (session.createNew (editFile));
    CHECK (session.getEdit() != nullptr);
    CHECK (session.getEditFile() == editFile);
    REQUIRE (editFile.existsAsFile());

    // A freshly created (and saved) session has no unsaved changes, but it does
    // write the sentinel so an unexpected exit is flagged next launch.
    CHECK (sentinel.existsAsFile());
    CHECK_FALSE (session.hasUnsavedChanges());

    // Any real edit flips the unsaved-changes flag (drives the Close guard).
    session.getEdit()->markAsChanged();
    CHECK (session.hasUnsavedChanges());

    session.close();

    CHECK (session.getEdit() == nullptr);
    CHECK (session.getEditFile() == juce::File());
    CHECK_FALSE (session.hasUnsavedChanges());
    CHECK_EQ (session.getNumAudioTracks(), 0);

    // A deliberate close leaves no stale recovery state and no hardware monitoring.
    CHECK_FALSE (sentinel.existsAsFile());
    CHECK_FALSE (session.isMonitoringEnabled());

    if (sentinelExistedBefore)
        sentinel.replaceWithText (sentinelContentBefore);
}

//==============================================================================
// Device-selection fix: "fail loudly, never tear down the working device".
// The pure name-resolution logic is unit-tested in test_studio.cpp; here we
// exercise the real `AudioEngine` pre-validation path headlessly.
TEST_CASE ("applyDeviceSetup rejects a genuinely absent device without opening anything")
{
    AudioEngine audio (false); // headless: no real hardware is opened

    // A name pair that cannot exist on any machine. Pre-validation must reject it
    // before JUCE's setAudioDeviceSetup (which deletes the current device first),
    // so nothing is opened and the error is the actionable "could not be found".
    const auto error = audio.applyDeviceSetup ("no-such-input-xyz", "no-such-output-xyz", 0.0, 0);

    INFO ("error: " << error);
    CHECK (error.isNotEmpty());
    CHECK (error.containsIgnoreCase ("could not be found"));
    CHECK (audio.getLastError() == error);
    CHECK_FALSE (audio.hasActiveDevice());
}

TEST_CASE ("applyInputDeviceSetup keeps the output side and reports a missing input cleanly")
{
    AudioEngine audio (false);

    // Regression guard for the input-only-device path used by --selftest-record:
    // it must never try to open the output using the input's name, and must fail
    // cleanly (no crash, no device) when the input is absent.
    const auto error = audio.applyInputDeviceSetup ("no-such-input-xyz");

    INFO ("error: " << error);
    CHECK (error.isNotEmpty());
    CHECK (audio.getLastError() == error);
    CHECK_FALSE (audio.hasActiveDevice());
}

//==============================================================================
// Monitoring routing: a mono (1-channel) input must be duplicated to both L and
// R so it is heard centred, not hard-left. This mirrors Tracktion's
// `WaveInputDeviceInstance::copyIncomingDataIntoBuffer`, which fills each
// destination channel (by type) from the device channel named by its
// ChannelIndex — two entries pointing at device channel 0 therefore yield L == R.
TEST_CASE ("mono input is routed to both L and R (centred), stereo is unchanged")
{
    using te::ChannelConfiguration;

    // --- mono ---------------------------------------------------------------
    const auto mono = inputChannelConfigurationFor (1);
    REQUIRE (mono.getNumChannels() == 2);
    CHECK (mono[0].indexInDevice == 0);
    CHECK (mono[1].indexInDevice == 0);
    CHECK (mono[0].channel == juce::AudioChannelSet::left);
    CHECK (mono[1].channel == juce::AudioChannelSet::right);

    // The destination is a genuine stereo pair (two distinct channels)...
    const auto destSet = mono.toChannelSet();
    REQUIRE (destSet == juce::AudioChannelSet::stereo());

    // ...and both halves read device channel 0, so a mono sample lands equally in
    // L and R (centred) instead of only in L.
    juce::AudioBuffer<float> dest (2, 4);
    dest.clear();
    const float monoInput[4] = { 0.5f, -0.25f, 0.0f, 0.75f };

    for (const auto& ci : mono)
    {
        if (ci.indexInDevice != 0)
            continue;

        const auto destChannel = destSet.getChannelIndexForType (ci.channel);
        REQUIRE (destChannel >= 0);
        auto* out = dest.getWritePointer (destChannel);

        for (int i = 0; i < 4; ++i)
            out[i] = monoInput[i];
    }

    for (int i = 0; i < 4; ++i)
        CHECK (dest.getSample (0, i) == doctest::Approx (dest.getSample (1, i)));

    CHECK (dest.getSample (0, 0) == doctest::Approx (0.5f));
    CHECK (dest.getSample (1, 0) == doctest::Approx (0.5f));

    // No device open yet (0 channels) defaults to centred, never hard-left.
    const auto unknown = inputChannelConfigurationFor (0);
    CHECK (unknown[0].indexInDevice == 0);
    CHECK (unknown[1].indexInDevice == 0);

    // --- stereo (unchanged: L <- device ch 0, R <- device ch 1, no doubling) -
    const auto stereo = inputChannelConfigurationFor (2);
    CHECK (stereo == ChannelConfiguration::stereo());
    CHECK (stereo[0].indexInDevice == 0);
    CHECK (stereo[1].indexInDevice == 1);
}


//==============================================================================
// Measured monitor-path regression.
//
// The previous "mono is centred" test only asserted the input *configuration*
// (device channel 0 -> L and R) — it never inspected a single sample, so it kept
// passing while the real monitor mix stayed hard-left. This test instead drives
// the engine's *real* input path — `WaveInputDeviceInstance::copyIncomingDataIntoBuffer`,
// reached through a real `WaveInputDevice`/instance — with a known mono hardware
// buffer and captures the exact channel data Tracktion's `WaveInputDeviceNode`
// feeds into the monitor mix. That node copies its input channels 1:1 into the
// (stereo) monitor destination, so an L == R capture is what the user hears as
// centred; an L-only capture is the hard-left bug. The engine is real (a
// one-channel hosted device), no hardware is opened.
namespace
{
    struct MonitorCapture
    {
        int channels = 0;      ///< channels in the buffer the monitor node consumes.
        float l = 0.0f, r = 0.0f;
    };

    struct MonitorBufferProbe : te::InputDeviceInstance::Consumer
    {
        MonitorCapture capture;

        void acceptInputBuffer (choc::buffer::ChannelArrayView<float> b) override
        {
            capture.channels = (int) b.getNumChannels();

            if (b.getNumFrames() > 0)
            {
                capture.l = b.getNumChannels() > 0 ? b.getSample (0, 0) : 0.0f;
                capture.r = b.getNumChannels() > 1 ? b.getSample (1, 0) : -1.0f;
            }
        }
    };

    /** Pushes a per-channel constant hardware buffer through the real wave-input
        path with `cfg` and returns what the monitor node would consume.

        @param hardwareAmplitudes  one amplitude per *hardware* input channel
                                   (1 = mono device, 2 = stereo device). */
    bool measureMonitorBuffer (te::ChannelConfiguration cfg,
                               MonitorCapture& out,
                               const std::vector<float>& hardwareAmplitudes)
    {
        auto engine = std::make_unique<te::Engine> ("rrs-monitor-measure", nullptr,
                                                    std::make_unique<HeadlessBehaviour>());

        auto& dm = engine->getDeviceManager();
        auto& hosted = dm.getHostedAudioDeviceInterface();

        for (int i = 0; i < 10; ++i)
        {
            dm.dispatchPendingUpdates();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        }

        te::HostedAudioDeviceInterface::Parameters params;
        params.sampleRate = 48000.0;
        params.blockSize = 256;
        params.inputChannels = (int) hardwareAmplitudes.size();
        params.outputChannels = 2;
        hosted.initialise (params);
        hosted.prepareToPlay (48000.0, 256);

        for (int i = 0; i < 100 && dm.getNumWaveInDevices() == 0; ++i)
        {
            dm.dispatchPendingUpdates();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        }

        auto* waveIn = dm.getDefaultWaveInDevice();

        if (waveIn == nullptr)
            return false;

        waveIn->setChannelConfiguration (cfg);

        auto dir = scratchDirectory ("monitor-capture");
        auto edit = te::createEmptyEdit (*engine, dir.getChildFile ("m.tracktionedit"));
        edit->ensureNumberOfAudioTracks (1);
        edit->getTransport().ensureContextAllocated();

        auto* context = edit->getTransport().getCurrentPlaybackContext();

        if (context == nullptr)
            return false;

        auto* instance = waveIn->createInstance (*context);
        MonitorBufferProbe probe;
        instance->addConsumer (&probe);

        constexpr int numSamples = 4;
        std::vector<std::vector<float>> channelData (hardwareAmplitudes.size(),
                                                     std::vector<float> (numSamples));
        std::vector<const float*> channelPtrs;

        for (size_t c = 0; c < hardwareAmplitudes.size(); ++c)
        {
            std::fill (channelData[c].begin(), channelData[c].end(), hardwareAmplitudes[c]);
            channelPtrs.push_back (channelData[c].data());
        }

        waveIn->consumeNextAudioBlock (channelPtrs.data(), (int) channelPtrs.size(), numSamples, 0.0);

        instance->removeConsumer (&probe);
        out = probe.capture;
        delete instance;

        edit.reset();
        dm.deviceManager.closeAudioDevice();
        dm.removeHostedAudioDeviceInterface();
        return true;
    }
}

TEST_CASE ("mono input monitor signal is centred (measured L == R, not hard-left)")
{
    // A 1-channel device with the app's routing: device channel 0 must reach BOTH
    // L and R, so the monitored signal is centred.
    MonitorCapture mono;
    REQUIRE (measureMonitorBuffer (inputChannelConfigurationFor (1), mono, { 0.5f }));
    CHECK (mono.channels == 2);
    CHECK (mono.l == doctest::Approx (0.5f));
    CHECK (mono.r == doctest::Approx (0.5f)); // the hard-left bug measured exactly 0 here

    // Regression context: Tracktion's own default for a 1-channel device is a
    // SINGLE centre channel, which lands on one side only once summed into the
    // stereo track (hard-left) — this is why the routing must be overridden.
    MonitorCapture hardwareDefault;
    REQUIRE (measureMonitorBuffer (te::ChannelConfiguration::canonical (1), hardwareDefault, { 0.5f }));
    CHECK (hardwareDefault.channels == 1);

    // Regression context: the old unconditional stereo map (device 0 -> L,
    // device 1 -> R) leaves R silent on a mono device (device channel 1 does not
    // exist) — the original hard-left bug this fix replaces.
    MonitorCapture oldStereo;
    REQUIRE (measureMonitorBuffer (te::ChannelConfiguration::stereo(), oldStereo, { 0.5f }));
    CHECK (oldStereo.channels == 2);
    CHECK (oldStereo.l == doctest::Approx (0.5f));
    CHECK (oldStereo.r == doctest::Approx (0.0f));
}

TEST_CASE ("stereo input monitor signal preserves its channels (measured L != R, no widening)")
{
    // A true stereo device keeps L <- ch 0 and R <- ch 1: the channels stay
    // distinct and neither is duplicated.
    MonitorCapture stereo;
    REQUIRE (measureMonitorBuffer (inputChannelConfigurationFor (2), stereo, { 0.5f, 0.25f }));
    CHECK (stereo.channels == 2);
    CHECK (stereo.l == doctest::Approx (0.5f));
    CHECK (stereo.r == doctest::Approx (0.25f));
}

TEST_CASE ("arbitrary-channel input mapping routes the selected channels (measured, 4-in path)")
{
    // FR-REC-3 (Epic 2): a track can pick any input of an N-channel interface.
    // Drive the real wave-input path of a 4-channel hosted device with distinct
    // per-channel amplitudes and confirm the mapping selects exactly the
    // requested hardware channels (this is the 4-in acceptance path, exercised
    // headlessly because no 4-in interface is attached to this machine).
    const std::vector<float> fourInputs { 0.1f, 0.2f, 0.3f, 0.4f };

    SUBCASE ("stereo pair at hardware channels 2/3")
    {
        MonitorCapture capture;
        REQUIRE (measureMonitorBuffer (inputChannelConfigurationForMapping ({ 2, 2, InputLayout::Stereo }, 4),
                                       capture, fourInputs));
        CHECK (capture.channels == 2);
        CHECK (capture.l == doctest::Approx (0.3f));
        CHECK (capture.r == doctest::Approx (0.4f));
    }

    SUBCASE ("mono centred on hardware channel 2")
    {
        MonitorCapture capture;
        REQUIRE (measureMonitorBuffer (inputChannelConfigurationForMapping ({ 2, 1, InputLayout::Mono }, 4),
                                       capture, fourInputs));
        CHECK (capture.channels == 2);
        CHECK (capture.l == doctest::Approx (0.3f));
        CHECK (capture.r == doctest::Approx (0.3f));
    }

    SUBCASE ("4-channel map preserves all four hardware channels")
    {
        MonitorCapture capture;
        REQUIRE (measureMonitorBuffer (inputChannelConfigurationForMapping ({ 0, 4, InputLayout::MultiChannel }, 4),
                                       capture, fourInputs));
        CHECK (capture.channels == 4);
        // The probe reads the first two channels; they must be the first two
        // hardware inputs, not duplicated/downmixed.
        CHECK (capture.l == doctest::Approx (0.1f));
        CHECK (capture.r == doctest::Approx (0.2f));
    }
}

//==============================================================================
// FR-REC-1 / FR-REC-3 / FR-REC-6 (Epic 2): N tracks, each mapped to its own
// hardware input, recorded simultaneously. Driven by the real engine with a
// hosted 4-in / 2-out device, since no 4-in interface is attached.
TEST_CASE ("multitrack: four tracks map to four inputs and record simultaneously (measured)")
{
    auto dir = scratchDirectory ("multitrack-record");

    AudioEngine audio (false);
    Session session (audio);

    REQUIRE (session.createNew (dir.getChildFile ("Multi.tracktionedit")));
    REQUIRE (session.getNumAudioTracks() == 1);

    while (session.getNumAudioTracks() < 4)
        REQUIRE (session.addAudioTrack() >= 0);

    REQUIRE (session.getNumAudioTracks() == 4);

    CHECK (session.getInputTrackIndices().size() == 4);

    // Hosted 4-in / 2-out device: distinct per-channel amplitudes and no real
    // hardware. This is the Epic 2 "4 separate tracks mapped to 4 inputs" path.
    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 4;
    params.outputChannels = 2;

    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);

    auto& dm = audio.deviceManager();

    // Split the interface so each hardware input is addressable as its own
    // wave device, then let the engine rebuild its device list.
    dm.setAllWaveInputsToNumChannels (1);

    for (int i = 0; i < 300; ++i)
    {
        dm.dispatchPendingUpdates();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        if (session.isInputConfigured() && dm.getNumWaveInDevices() >= 4)
            break;

        session.reconfigureInputs();
    }

    REQUIRE (dm.getNumWaveInDevices() >= 4);
    REQUIRE (session.isInputConfigured());

    // Each input track resolves to a distinct hardware channel/device (the 4-in
    // mapping acceptance) and can be armed independently.
    std::set<int> channels;

    for (auto index : session.getInputTrackIndices())
    {
        const auto mapping = session.getTrackInputMapping (index);
        channels.insert (mapping.firstChannel);
        REQUIRE (session.setTrackArmed (index, true));
        CHECK (session.isTrackArmed (index));
    }

    CHECK (channels.size() == 4u);

    // Record a distinct constant amplitude on each input.
    const float amps[4] = { 0.5f, 0.25f, 0.125f, 0.0625f };

    session.getEdit()->getTransport().setPosition (te::TimePosition {});
    REQUIRE (session.record());

    juce::AudioBuffer<float> input (4, 256);

    for (int block = 0; block < 80; ++block)
    {
        for (int ch = 0; ch < 4; ++ch)
            for (int s = 0; s < input.getNumSamples(); ++s)
                input.setSample (ch, s, amps[ch]);

        player->process (input);
    }

    session.stop();

    // Let the recording writers flush their files.
    for (int i = 0; i < 300; ++i)
    {
        session.getEdit()->dispatchPendingUpdatesSynchronously();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }

    // Disarm + stop monitoring so the recording threads are fully released
    // before the engine is torn down.
    for (auto index : session.getInputTrackIndices())
        session.setTrackArmed (index, false);

    session.setMonitoringEnabled (false);

    // Every track received its own take, with the recorded level matching the
    // input it was mapped to (RMS of a constant-amplitude signal == amplitude).
    std::set<int> recordedChannels;

    for (auto index : session.getInputTrackIndices())
    {
        auto* track = session.getTrack (index);

        REQUIRE (track != nullptr);
        REQUIRE (track->getClips().size() >= 1);

        auto* clip = dynamic_cast<te::WaveAudioClip*> (track->getClips()[0]);
        REQUIRE (clip != nullptr);

        const auto file = clip->getOriginalFile();
        REQUIRE (file.existsAsFile());

        te::AudioFile audioFile (session.getEdit()->engine, file);
        REQUIRE (audioFile.isValid());
        REQUIRE (audioFile.getLength() > 0.1);

        // Measure the recorded level from the middle of the take (away from any
        // clip-start fade). The constant-amplitude input means peak == the
        // amplitude of the hardware channel this track was mapped to.
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (
            wav.createReaderFor (new juce::FileInputStream (file), true));
        REQUIRE (reader != nullptr);
        REQUIRE (reader->lengthInSamples > 0);

        const auto total = (juce::int64) reader->lengthInSamples;
        const auto start = total / 2;
        const auto numToRead = (int) juce::jmin ((juce::int64) 2048, total - start);
        juce::AudioBuffer<float> recorded ((int) reader->numChannels, numToRead);
        REQUIRE (reader->read (&recorded, 0, numToRead, start, true, true));

        const auto inputChannel = session.getTrackInputMapping (index).firstChannel;
        REQUIRE (juce::isPositiveAndBelow (inputChannel, 4));
        const auto peak = recorded.getMagnitude (0, numToRead);

        INFO ("track " << index << " mapped to input " << inputChannel
                       << " recorded peak " << peak << " expected " << amps[inputChannel]);
        CHECK (peak == doctest::Approx (amps[inputChannel]).epsilon (0.02f));

        recordedChannels.insert (index);
    }

    CHECK (recordedChannels.size() == 4u);

    // Tear the edit down before the hosted device is removed by ~EnginePlayer.
    session.close();
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
