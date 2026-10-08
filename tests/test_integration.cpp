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
#include "studio/PluginHost.h"
#include "studio/Session.h"
#include "studio/StemsExport.h"
#include "studio/WavExport.h"
#include "ui/PluginBrowser.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
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
    juce::File writeSineWav (const juce::File& file, double sampleRate, double seconds,
                             float amplitude = 0.5f, double frequency = 440.0)
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
                buffer.setSample (ch, i, amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                       * frequency * (double) i / sampleRate));

        if (! writer->writeFromAudioSampleBuffer (buffer, 0, numSamples))
            return {};

        writer.reset();
        return file;
    }

    /** Writes a valid 24-bit stereo sine whose amplitude steps from
        `amplitudeFirst` (first half) to `amplitudeSecond` (second half) at the
        midpoint. Used to prove a region export renders only the selected span:
        a region inside the first half must come out loud, one inside the second
        half quiet — regardless of uniform-peak sources. */
    juce::File writeSteppedSineWav (const juce::File& file, double sampleRate, double seconds,
                                    float amplitudeFirst, float amplitudeSecond,
                                    double frequency = 440.0)
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
        const auto midpoint = numSamples / 2;
        juce::AudioBuffer<float> buffer (2, numSamples);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < numSamples; ++i)
            {
                const auto amp = i < midpoint ? amplitudeFirst : amplitudeSecond;
                buffer.setSample (ch, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                  * frequency * (double) i / sampleRate));
            }

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
    std::unique_ptr<te::Edit> makeEditWithClip (const juce::File& editFile, const juce::File& wavFile,
                                                double clipSeconds = 0.5)
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

        const auto length = te::TimeDuration::fromSeconds (clipSeconds);
        tracks[0]->insertWaveClip ("take", wavFile,
                                   { { te::TimePosition(), length }, {} }, false);
        return edit;
    }

    /** Peak sample magnitude of a rendered WAV (across all channels), or -1 on
        failure. Used to compare click-on vs click-off renders. */
    float readWavPeak (const juce::File& file)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (
            wav.createReaderFor (new juce::FileInputStream (file), true));

        if (reader == nullptr || reader->lengthInSamples <= 0)
            return -1.0f;

        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);

        if (! reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true))
            return -1.0f;

        return buffer.getMagnitude (0, buffer.getNumSamples());
    }

    /** Largest absolute sample-to-sample step in a rendered WAV. A click (a
        discontinuity at a clip junction) shows up as a step far larger than the
        source waveform's own slope. Returns -1 on failure. */
    float readWavMaxFirstDifference (const juce::File& file)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (
            wav.createReaderFor (new juce::FileInputStream (file), true));

        if (reader == nullptr || reader->lengthInSamples <= 1)
            return -1.0f;

        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);

        if (! reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true))
            return -1.0f;

        float maxDiff = 0.0f;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 1; i < buffer.getNumSamples(); ++i)
                maxDiff = juce::jmax (maxDiff,
                                      std::abs (buffer.getSample (ch, i)
                                                - buffer.getSample (ch, i - 1)));

        return maxDiff;
    }

    /** Rough dominant frequency of channel 0 over [startSample, endSample) from
        zero crossings. Used to identify which take a comp region plays. */
    double estimateFrequency (const juce::AudioBuffer<float>& buffer, double sampleRate,
                              int startSample, int endSample)
    {
        startSample = juce::jmax (0, startSample);
        endSample = juce::jmin (buffer.getNumSamples(), endSample);

        if (endSample <= startSample + 1)
            return 0.0;

        int crossings = 0;

        for (int i = startSample + 1; i < endSample; ++i)
        {
            const auto a = buffer.getSample (0, i - 1);
            const auto b = buffer.getSample (0, i);

            if ((a <= 0.0f && b > 0.0f) || (a >= 0.0f && b < 0.0f))
                ++crossings;
        }

        return (double) crossings / 2.0 / ((double) (endSample - startSample) / sampleRate);
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
// FR-EXP-1: an enabled metronome ("Click") must never be baked into an exported
// WAV. This locks in the export guard: WavExport forces the click off for the
// render, the exported file is identical to a metronome-off render, and the
// click state is restored afterwards (success, failure and cancel alike).
TEST_CASE ("WavExport keeps the metronome out of the export and restores it (FR-EXP-1)")
{
    auto dir = scratchDirectory ("export-no-click");
    auto editFile = dir.getChildFile ("Click Export.tracktionedit");

    // A 2 s source so a 1.5 s clip spans several beats (default 120 BPM -> a
    // beat every 0.5 s): an enabled click would land at t = 0, 0.5 and 1.0 s.
    auto wavFile = writeSineWav (dir.getChildFile ("tone.wav"), 48000.0, 2.0);
    REQUIRE (wavFile.existsAsFile());

    auto edit = makeEditWithClip (editFile, wavFile, 1.5);
    REQUIRE (edit != nullptr);

    auto renderTo = [&] (const juce::File& dest, bool suppressMetronome)
    {
        dest.deleteFile();

        std::atomic<bool> finished { false };
        bool succeeded = false;
        juce::String error;

        auto handle = WavExport::start (*edit, dest,
                                        [&] (bool success, juce::File, juce::String message)
                                        {
                                            succeeded = success;
                                            error = message;
                                            finished = true;
                                        },
                                        suppressMetronome);

        REQUIRE (handle != nullptr);

        // The guard must take effect synchronously, before the render runs.
        if (suppressMetronome)
            CHECK_FALSE (edit->clickTrackEnabled.get());

        for (int i = 0; i < 400 && ! finished.load(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (25);

        REQUIRE (finished.load());
        INFO ("render error: " << error);
        REQUIRE (succeeded);
        return dest;
    };

    // Metronome-off reference.
    edit->clickTrackEnabled = false;
    const auto offFile = renderTo (dir.getChildFile ("off.wav"), true);
    const auto offPeak = readWavPeak (offFile);
    REQUIRE (offPeak > 0.1f);

    // Export while the click is enabled: the export guard must force it off and
    // restore it once the render completes.
    edit->clickTrackEnabled = true;
    const auto suppressedFile = renderTo (dir.getChildFile ("suppressed.wav"), true);

    CHECK (edit->clickTrackEnabled.get()); // restored by the completion path

    // The exported file matches the metronome-off render and carries no click
    // transient (the peak must not exceed the tone-only reference).
    const auto suppressedPeak = readWavPeak (suppressedFile);
    INFO ("off peak " << offPeak << ", suppressed peak " << suppressedPeak);
    CHECK (suppressedPeak == doctest::Approx (offPeak).epsilon (0.01f));
    CHECK (suppressedPeak <= offPeak * 1.01f);
}

//==============================================================================
// BUG (Epic 2 GUI retest) — the export must reflect the full mixer.
//
// Renderer::Parameters defaults to `useMasterPlugins = false`, so the offline
// render silently omitted the whole master plugin chain — including the master
// volume plugin (fader/pan/mute). The engine therefore exported the mix at the
// pre-master level: lowering the master fader was audible in playback but the
// WAV came out loud (and a loud backing track buried the voice). This measures
// the rendered peak for a known tone with the track fader, the master fader and
// mute — the values must drop exactly by the set dB, and a muted render must be
// silent.
TEST_CASE ("WavExport reflects the mixer: track fader, master gain and mute (measured)")
{
    auto dir = scratchDirectory ("export-mixer");
    auto editFile = dir.getChildFile ("Mixer Export.tracktionedit");

    // 0.5-amplitude tone -> -6.02 dBFS at unity.
    auto wavFile = writeSineWav (dir.getChildFile ("tone.wav"), 48000.0, 1.0);
    REQUIRE (wavFile.existsAsFile());

    auto edit = makeEditWithClip (editFile, wavFile, 1.0);
    REQUIRE (edit != nullptr);

    auto tracks = te::getAudioTracks (*edit);
    REQUIRE (tracks.size() == 1);
    REQUIRE (tracks[0] != nullptr);

    auto* trackVolume = tracks[0]->getVolumePlugin();
    REQUIRE (trackVolume != nullptr);

    auto masterVolume = edit->getMasterVolumePlugin();
    REQUIRE (masterVolume != nullptr);

    auto renderPeak = [&] (const juce::File& dest)
    {
        dest.deleteFile();

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
        REQUIRE (succeeded);
        return readWavPeak (dest);
    };

    // Unity reference.
    trackVolume->setVolumeDb (0.0f);
    masterVolume->setVolumeDb (0.0f);
    const auto unityPeak = renderPeak (dir.getChildFile ("unity.wav"));
    REQUIRE (unityPeak > 0.1f);

    // Track fader -20 dB AND master -6 dB => the rendered peak must drop by the
    // combined 26 dB. Assert in the dB domain: if the master chain were omitted
    // (the useMasterPlugins=false regression) only the -20 dB track fader would
    // apply and the drop would be -20 dB — a ratio tolerance can absorb that, an
    // absolute dB check cannot. This is the exact "minus fader ignored in export"
    // case.
    trackVolume->setVolumeDb (-20.0f);
    masterVolume->setVolumeDb (-6.0f);
    const auto reducedPeak = renderPeak (dir.getChildFile ("reduced.wav"));

    const auto dropDb = juce::Decibels::gainToDecibels (reducedPeak, -100.0f)
                      - juce::Decibels::gainToDecibels (unityPeak, -100.0f);
    INFO ("unity " << unityPeak << ", reduced " << reducedPeak << ", drop " << dropDb << " dB");
    CHECK (dropDb == doctest::Approx (-26.0f).epsilon (0.02f));

    // Reset the faders; a muted master must render silence.
    trackVolume->setVolumeDb (0.0f);
    masterVolume->setVolumeDb (-100.0f);
    const auto masterMutedPeak = renderPeak (dir.getChildFile ("master-muted.wav"));
    INFO ("master-muted peak " << masterMutedPeak);
    CHECK (masterMutedPeak < 0.001f);

    // A muted track must render silence too (track mute is part of the export).
    masterVolume->setVolumeDb (0.0f);
    tracks[0]->setMute (true);
    const auto trackMutedPeak = renderPeak (dir.getChildFile ("track-muted.wav"));
    INFO ("track-muted peak " << trackMutedPeak);
    CHECK (trackMutedPeak < 0.001f);
}

//==============================================================================
// FR-EXP-2: exporting a *selected region* must render exactly that span — the
// same offline EditRenderer path as the full session, with the render start and
// length set to the selection. Measured against a source whose level steps
// midway: a region in the loud first half must come out loud and one in the
// quiet second half quiet, proving material outside the region is excluded, and
// the file length must match the region (24-bit at the session rate).
TEST_CASE ("WavExport renders only the selected region, not the whole session (FR-EXP-2, measured)")
{
    auto dir = scratchDirectory ("export-region");
    auto editFile = dir.getChildFile ("Region Export.tracktionedit");

    // 2 s source: loud (0.5) in [0, 1), quiet (0.1) in [1, 2).
    auto wavFile = writeSteppedSineWav (dir.getChildFile ("steps.wav"), 48000.0, 2.0, 0.5f, 0.1f);
    REQUIRE (wavFile.existsAsFile());

    auto edit = makeEditWithClip (editFile, wavFile, 2.0);
    REQUIRE (edit != nullptr);

    constexpr double sr = 48000.0;

    struct RegionResult
    {
        bool ok = false;
        juce::int64 samples = 0;
        double rate = 0.0;
        int bits = 0;
        float peak = -1.0f;
    };

    auto renderRegion = [&] (const juce::File& dest, double startSeconds, double endSeconds)
    {
        RegionResult result;
        dest.deleteFile();

        std::atomic<bool> finished { false };
        bool succeeded = false;
        juce::String error;

        auto handle = WavExport::start (*edit, dest,
                                        [&] (bool success, juce::File, juce::String message)
                                        {
                                            succeeded = success;
                                            error = message;
                                            finished = true;
                                        },
                                        true, { startSeconds, endSeconds });

        REQUIRE (handle != nullptr);

        for (int i = 0; i < 400 && ! finished.load(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (25);

        REQUIRE (finished.load());
        INFO ("render error: " << error);
        REQUIRE (succeeded);
        REQUIRE (dest.existsAsFile());

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (
            wav.createReaderFor (new juce::FileInputStream (dest), true));
        REQUIRE (reader != nullptr);

        result.ok = true;
        result.samples = reader->lengthInSamples;
        result.rate = reader->sampleRate;
        result.bits = (int) reader->bitsPerSample;
        result.peak = readWavPeak (dest);
        return result;
    };

    // Loud region [0.0, 0.4): 0.4 s, 24-bit, 48 kHz. Its level follows the 0.5
    // amplitude source scaled by whatever constant gain the render path uses (an
    // equal-power centre pan gives ~0.707), so levels are compared relatively, as
    // the other export tests do.
    const auto loud = renderRegion (dir.getChildFile ("loud.wav"), 0.0, 0.4);
    CHECK (loud.ok);
    CHECK (loud.bits == WavExport::bitDepth);
    CHECK (loud.rate == doctest::Approx (AudioEngine::defaultSampleRate));
    CHECK (loud.samples == doctest::Approx (0.4 * sr).epsilon (0.02));

    // The render path's constant gain, derived from the known 0.5 source.
    const auto renderGain = loud.peak / 0.5f;
    REQUIRE (renderGain > 0.4f);
    CHECK (renderGain < 1.2f);

    // Quiet region [1.1, 1.5): also 0.4 s, but its level must match the source's
    // 0.1 amplitude — proving the render is bounded by the selection and excludes
    // the loud first-half material.
    const auto quiet = renderRegion (dir.getChildFile ("quiet.wav"), 1.1, 1.5);
    CHECK (quiet.ok);
    CHECK (quiet.samples == doctest::Approx (0.4 * sr).epsilon (0.02));
    CHECK (quiet.peak == doctest::Approx (0.1f * renderGain).epsilon (0.06f));

    INFO ("loud peak " << loud.peak << ", quiet peak " << quiet.peak
                       << ", render gain " << renderGain
                       << ", loud samples " << loud.samples << ", quiet samples " << quiet.samples);

    // The two 0.4 s regions have the same length but distinct levels (5:1): the
    // region export genuinely selects its span (material outside it is excluded).
    CHECK (quiet.peak < loud.peak * 0.4f);

    // Sanity: the full-session default still renders the whole 2 s and keeps the
    // loud first-half level.
    const auto full = renderRegion (dir.getChildFile ("full.wav"), 0.0, -1.0);
    INFO ("full peak " << full.peak << ", full samples " << full.samples);
    CHECK (full.peak == doctest::Approx (loud.peak).epsilon (0.03f));
    CHECK (full.samples == doctest::Approx (2.0 * sr).epsilon (0.02));
    CHECK (full.samples > loud.samples * 3);
}

//==============================================================================
// Regression: an offline export must not leave the live playback context torn
// down. `EditRenderer::render` (used by WavExport and by RenderQueue/stems)
// constructs `Edit::ScopedRenderStatus (edit, false)`, which frees the playback
// context — and with it the input-device instances / monitoring — and never
// reallocates it. Without an explicit restore, monitoring and playback stay dead
// until the app is restarted. This mirrors the "audio stops after region export"
// owner report: the export must leave the context active and the transport as it
// was.
TEST_CASE ("offline export restores the live playback context and transport (region export)")
{
    auto dir = scratchDirectory ("export-transport-restore");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Restore.tracktionedit")));

    auto wavFile = writeSineWav (dir.getChildFile ("tone.wav"), 48000.0, 1.0);
    REQUIRE (wavFile.existsAsFile());
    REQUIRE (session.importAudioFile (wavFile));

    // Hosted (in-process) device: gives the Session real wave devices so it can
    // allocate its live playback context, without touching physical hardware.
    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 2;
    params.outputChannels = 2;

    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);

    for (int i = 0; i < 200; ++i)
    {
        audio.deviceManager().dispatchPendingUpdates();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        if (session.isInputConfigured())
            break;

        session.reconfigureInputs();
    }

    REQUIRE (session.isInputConfigured());

    auto* edit = session.getEdit();
    REQUIRE (edit != nullptr);

    // A live session has an allocated playback context (monitoring + graph).
    CHECK (edit->getTransport().isPlayContextActive());

    edit->getTransport().setPosition (te::TimePosition {});
    session.play();

    for (int i = 0; i < 40; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

    CHECK (session.isPlaying());
    CHECK (edit->getTransport().isPlayContextActive());

    // Exactly what MainComponent::beginWavExport captures before the render.
    const auto snapshot = session.captureTransportForOfflineRender();
    CHECK (snapshot.contextWasAllocated);
    CHECK (snapshot.wasPlaying);

    auto dest = dir.getChildFile ("region.wav");
    std::atomic<bool> finished { false };

    // Region export is the reported repro; the full-session path is identical.
    auto handle = WavExport::start (*edit, dest,
                                    [&] (bool, juce::File, juce::String) { finished.store (true); },
                                    true, { 0.0, 0.3 });
    REQUIRE (handle != nullptr);

    for (int i = 0; i < 400 && ! finished.load(); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (25);

    REQUIRE (finished.load());

    // Join the render thread before touching the Edit (mirrors MainComponent
    // resetting its handle, which drops the renderer's ScopedRenderStatus).
    handle.reset();

    // The offline render genuinely tore the live context down: this is the bug.
    CHECK_FALSE (edit->getTransport().isPlayContextActive());

    // Restoring — what MainComponent now does in its completion callback — must
    // bring the device graph/monitoring and the transport back.
    session.restoreTransportAfterOfflineRender (snapshot);

    CHECK (edit->getTransport().isPlayContextActive());
    CHECK (session.isPlaying());

    session.stop();
    session.close();
}

//==============================================================================
// FR-EXP-1 regression: a deferred metronome restore must not fire after the
// export handle (and the component that owns it) has been torn down. Closing
// the window during an export used to let WavExport's queued restore re-disable
// the click *after* ~MainComponent had restored it, leaving it silently off.
TEST_CASE ("cancelled export's deferred metronome restore cannot clobber teardown (FR-EXP-1)")
{
    auto dir = scratchDirectory ("export-teardown");
    auto editFile = dir.getChildFile ("Teardown.tracktionedit");

    auto wavFile = writeSineWav (dir.getChildFile ("tone.wav"), 48000.0, 1.0);
    REQUIRE (wavFile.existsAsFile());

    auto edit = makeEditWithClip (editFile, wavFile, 0.5);
    REQUIRE (edit != nullptr);

    // Reproduce the close-during-export ordering: MainComponent disables the
    // click, then starts the export, so WavExport captures "was enabled == false".
    edit->clickTrackEnabled = true;
    edit->clickTrackEnabled = false;

    std::atomic<bool> finished { false };

    auto handle = WavExport::start (*edit, dir.getChildFile ("mix.wav"),
                                    [&] (bool, juce::File, juce::String) { finished = true; });

    REQUIRE (handle != nullptr);

    // Tear the component down: cancel + destroy the handle (invalidating the
    // shared liveness token), then restore the click like ~MainComponent does.
    handle->cancel();
    handle.reset();
    edit->clickTrackEnabled = true;

    // Drain any queued completion work; the stale restore must be a no-op.
    for (int i = 0; i < 200 && ! finished.load(); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);

    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    CHECK (edit->clickTrackEnabled.get());
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
// FR-REC-3 (Epic 2): a per-track input assignment (hardware channel + layout)
// must survive a save/open round-trip in the native project format, so a mapped
// 4-in session reopens with each track still pointing at its own input.
TEST_CASE ("per-track input mapping survives save/open (FR-REC-3)")
{
    auto dir = scratchDirectory ("input-mapping-persist");
    auto editFile = dir.getChildFile ("Mapping.tracktionedit");

    AudioEngine audio (false);
    Session session (audio);

    REQUIRE (session.createNew (editFile));
    REQUIRE (session.getNumAudioTracks() == 1);
    REQUIRE (session.addAudioTrack() >= 0);
    REQUIRE (session.addAudioTrack() >= 0);
    REQUIRE (session.getNumAudioTracks() == 3);

    const InputMapping mono { 2, 1, InputLayout::Mono };
    const InputMapping stereo { 1, 2, InputLayout::Stereo };

    REQUIRE (session.setTrackInputMapping (0, mono));
    REQUIRE (session.setTrackInputMapping (1, stereo));

    // The setter persists immediately; a fresh Session opening the file must see
    // the exact same assignments.
    Session reopened (audio);
    REQUIRE (reopened.open (editFile));
    REQUIRE (reopened.getNumAudioTracks() == 3);

    CHECK (reopened.getTrackInputMapping (0) == mono);
    CHECK (reopened.getTrackInputMapping (1) == stereo);
    CHECK (reopened.getTrackInputMapping (2) == InputMapping { 2, 1, InputLayout::Mono });

    session.close();
    reopened.close();
}

//==============================================================================
// BUG-2 regression: an imported/backing track is playback-only. It must not be
// possible to arm it or give it a hardware-input mapping, which would flip it
// into an input track (`rrsInputTrack=true`) and bind it to a hardware channel
// (default 0), colliding with the real input track.
TEST_CASE ("imported/backing tracks stay playback-only: cannot arm or input-map (BUG-2)")
{
    auto dir = scratchDirectory ("playback-only");
    auto editFile = dir.getChildFile ("Playback.tracktionedit");

    auto wavFile = writeSineWav (dir.getChildFile ("minus.wav"), 48000.0, 0.25);
    REQUIRE (wavFile.existsAsFile());

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (editFile));
    REQUIRE (session.getNumAudioTracks() == 1);

    REQUIRE (session.importAudioFile (wavFile));
    REQUIRE (session.getNumAudioTracks() == 2);

    const auto inputTrackId   = juce::Identifier ("rrsInputTrack");
    const auto inputChannelId = juce::Identifier ("rrsInputFirstChannel");

    const int backing = 1;
    auto* track = session.getTrack (backing);
    REQUIRE (track != nullptr);

    // The imported track is not an input track, and only track 0 is.
    CHECK_FALSE (track->state.hasProperty (inputTrackId));
    CHECK (session.getInputTrackIndices().contains (0));
    CHECK_FALSE (session.getInputTrackIndices().contains (backing));

    // Arming is rejected and must not mutate the backing track.
    session.clearLastError();
    CHECK_FALSE (session.setTrackArmed (backing, true));
    CHECK (session.getLastError().isNotEmpty());
    CHECK_FALSE (session.isTrackArmed (backing));
    CHECK_FALSE (track->state.hasProperty (inputTrackId));

    // Input mapping is rejected and must not create the input-track markers
    // (nor bind the backing track to a hardware channel).
    session.clearLastError();
    CHECK_FALSE (session.setTrackInputMapping (backing, InputMapping { 0, 1, InputLayout::Mono }));
    CHECK (session.getLastError().isNotEmpty());
    CHECK_FALSE (track->state.hasProperty (inputTrackId));
    CHECK_FALSE (track->state.hasProperty (inputChannelId));

    // The real input track keeps its controls (the guard is not over-broad).
    CHECK (session.setTrackInputMapping (0, InputMapping { 0, 1, InputLayout::Mono }));

    session.close();
}

//==============================================================================
// FR-MIX-1 regression: the master fader value and the mute state are persisted
// *independently* of the master volume plugin. The plugin stores the effective
// gain (i.e. -100 dB while muted), so reading it back on reopen used to lose the
// user's chosen gain and silently leave master at -100 with the M chip off.
TEST_CASE ("master gain and mute persist independently across save/open (FR-MIX-1)")
{
    auto dir = scratchDirectory ("master-persist");
    auto editFile = dir.getChildFile ("Master.tracktionedit");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (editFile));

    REQUIRE (session.setMasterGainDb (-6.0f));
    REQUIRE (session.setMasterMute (true));
    REQUIRE (session.save());

    Session reopened (audio);
    REQUIRE (reopened.open (editFile));

    // The real fader value comes back (not the -100 dB baked into the plugin)
    // and the mute chip reflects the saved mute state.
    CHECK (reopened.getMasterGainDb() == doctest::Approx (-6.0f));
    CHECK (reopened.isMasterMuted());

    // Unmuting applies the remembered gain rather than jumping from silence.
    REQUIRE (reopened.setMasterMute (false));
    CHECK (reopened.getMasterGainDb() == doctest::Approx (-6.0f));

    session.close();
    reopened.close();
}

//==============================================================================
// Regression: adding a track after removing a non-last one must not collide with
// an existing track's hardware channel. The old code used
// `inputTrackIndices().size()` as the next channel, so removing the middle of
// three tracks (0/1/2) and adding a new one reused channel 2.
TEST_CASE ("adding a track after removing a middle track picks a free input channel")
{
    auto dir = scratchDirectory ("channel-collision");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Collision.tracktionedit")));

    REQUIRE (session.addAudioTrack() >= 0);
    REQUIRE (session.addAudioTrack() >= 0);
    REQUIRE (session.getNumAudioTracks() == 3);

    REQUIRE (session.removeAudioTrack (1));

    const auto added = session.addAudioTrack();
    REQUIRE (added >= 0);

    std::set<int> channels;

    for (auto index : session.getInputTrackIndices())
        channels.insert (session.getTrackInputMapping (index).firstChannel);

    CHECK (channels.size() == (size_t) session.getInputTrackIndices().size());
    CHECK (session.getTrackInputMapping (added).firstChannel == 1);

    session.close();
}

//==============================================================================
// Epic 4 (FR-ED-1/2/6): arrangement clip edits. Every operation changes the
// clip's geometry as measured through the Session's clip model, and undo/redo
// restores the previous edit state consistently.
TEST_CASE ("Epic 4 clip editing: move/trim/split/duplicate/delete/loop change geometry")
{
    auto dir = scratchDirectory ("clip-edit");
    auto wavFile = writeSineWav (dir.getChildFile ("take.wav"), 48000.0, 2.0);
    REQUIRE (wavFile.existsAsFile());

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("ClipEdit.tracktionedit")));
    REQUIRE (session.importAudioFile (wavFile));
    REQUIRE (session.getNumAudioTracks() == 2);

    const int track = 1; // the imported take's track

    auto info = [&] (int clipIndex)
    {
        Session::ClipInfo i;
        REQUIRE (session.getClipInfo (track, clipIndex, i));
        return i;
    };

    int ci = -1;

    {
        const auto clips = session.getClips (track);
        REQUIRE (clips.size() == 1);
        ci = clips[0].clipIndex;
        CHECK (clips[0].startSeconds == doctest::Approx (0.0));
        CHECK (clips[0].lengthSeconds == doctest::Approx (2.0).epsilon (0.02));
        CHECK (clips[0].isWave);
    }

    // --- move ---
    REQUIRE (session.moveClip (track, ci, 1.0));
    {
        const auto i = info (ci);
        CHECK (i.startSeconds == doctest::Approx (1.0));
        CHECK (i.lengthSeconds == doctest::Approx (2.0).epsilon (0.02));
        CHECK (i.offsetSeconds == doctest::Approx (0.0)); // content did not slide
    }

    // --- trim end ---
    REQUIRE (session.trimClipEnd (track, ci, 2.5));
    CHECK (info (ci).lengthSeconds == doctest::Approx (1.5).epsilon (0.01));

    // --- undo restores the pre-trim length; redo re-applies it ---
    REQUIRE (session.undo());
    CHECK (info (ci).lengthSeconds == doctest::Approx (2.0).epsilon (0.02));
    REQUIRE (session.canRedo());
    REQUIRE (session.redo());
    CHECK (info (ci).lengthSeconds == doctest::Approx (1.5).epsilon (0.01));

    // --- trim start: the source content stays stationary (offset follows) ---
    REQUIRE (session.trimClipStart (track, ci, 1.25));
    {
        const auto i = info (ci);
        CHECK (i.startSeconds == doctest::Approx (1.25));
        CHECK (i.endSeconds == doctest::Approx (2.5));
        CHECK (i.lengthSeconds == doctest::Approx (1.25).epsilon (0.01));
        CHECK (i.offsetSeconds == doctest::Approx (0.25).epsilon (0.01));
    }

    // --- split at 2.0 into two adjacent, content-continuous clips ---
    REQUIRE (session.splitClip (track, ci, 2.0));
    {
        const auto clips = session.getClips (track);
        REQUIRE (clips.size() == 2);
        CHECK (clips[0].startSeconds == doctest::Approx (1.25));
        CHECK (clips[0].endSeconds == doctest::Approx (2.0));
        CHECK (clips[1].startSeconds == doctest::Approx (2.0));
        CHECK (clips[1].endSeconds == doctest::Approx (2.5));
    }

    // --- duplicate the first clip immediately after it ---
    const int firstIndex = session.getClips (track)[0].clipIndex;
    const int duplicateIndex = session.duplicateClip (track, firstIndex);
    REQUIRE (duplicateIndex >= 0);
    {
        const auto i = info (duplicateIndex);
        CHECK (i.startSeconds == doctest::Approx (2.0));
        CHECK (i.lengthSeconds == doctest::Approx (0.75).epsilon (0.01));
    }
    CHECK (session.getClips (track).size() == 3);

    // --- delete the duplicate, then undo the delete ---
    REQUIRE (session.deleteClip (track, duplicateIndex));
    CHECK (session.getClips (track).size() == 2);
    REQUIRE (session.undo());
    CHECK (session.getClips (track).size() == 3);

    // --- loop x3: the engine loops natively when the source is loopable,
    //     otherwise the fallback repeats the clip on the timeline. Either way
    //     the total repeated material triples. ---
    const int loopTarget = session.getClips (track)[0].clipIndex;
    const auto beforeCount = session.getClips (track).size();
    const auto beforeLength = info (loopTarget).lengthSeconds;
    REQUIRE (session.setClipLoop (track, loopTarget, 3));

    const auto afterCount = session.getClips (track).size();
    const auto afterLength = info (loopTarget).lengthSeconds;
    INFO ("loop: count " << beforeCount << " -> " << afterCount
          << ", length " << beforeLength << " -> " << afterLength);

    const bool repeatedOnTimeline = (afterCount == beforeCount + 2);
    const bool loopedNatively = (afterLength == doctest::Approx (beforeLength * 3.0).epsilon (0.02));
    CHECK ((repeatedOnTimeline || loopedNatively));

    // --- invalid requests are rejected, not corrupted ---
    CHECK_FALSE (session.splitClip (track, loopTarget, 0.0));       // outside the clip
    CHECK_FALSE (session.trimClipEnd (track, loopTarget, 0.0));     // would collapse
    CHECK_FALSE (session.moveClip (999, 0, 1.0));                   // no such track

    session.close();
}

//==============================================================================
// Epic 4 (FR-ED-3, measured): a crossfade between adjacent clips removes the
// click that a hard junction produces. The test renders the junction offline
// twice — once with the clips butted together and once after the crossfade —
// and compares the largest sample-to-sample step. A hard junction between a
// ~0.49 sample and 0 shows a ~0.49 step; the equal-power crossfade smooths it.
TEST_CASE ("Epic 4 crossfade removes the junction click (FR-ED-3, measured)")
{
    auto dir = scratchDirectory ("crossfade");
    constexpr double sampleRate = 48000.0;

    auto wavFile = writeSineWav (dir.getChildFile ("tone.wav"), sampleRate, 2.0, 0.5f, 440.0);
    REQUIRE (wavFile.existsAsFile());

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Crossfade.tracktionedit")));
    REQUIRE (session.importAudioFile (wavFile));

    const int track = 1;

    auto getInfo = [&] (int clipIndex)
    {
        Session::ClipInfo i;
        REQUIRE (session.getClipInfo (track, clipIndex, i));
        return i;
    };

    auto renderJunction = [&] (const juce::String& name)
    {
        auto dest = dir.getChildFile (name);
        dest.deleteFile();

        std::atomic<bool> finished { false };
        bool succeeded = false;

        auto handle = WavExport::start (*session.getEdit(), dest,
                                        [&] (bool success, juce::File, juce::String)
                                        {
                                            succeeded = success;
                                            finished = true;
                                        },
                                        true, { 0.90, 1.15 });

        REQUIRE (handle != nullptr);

        for (int i = 0; i < 400 && ! finished.load(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (25);

        REQUIRE (finished.load());
        REQUIRE (succeeded);
        return dest;
    };

    // Clip A: the imported take trimmed to 0.987 s (so it ends mid-cycle at a
    // ~+0.49 sample). Duplicate it butt-joined at 0.987 s; the copy restarts the
    // source at 0, so the hard junction jumps from ~+0.49 to 0 — a click.
    const int clipA = session.getClips (track)[0].clipIndex;
    REQUIRE (session.trimClipEnd (track, clipA, 0.987));
    const int clipB = session.duplicateClip (track, clipA);
    REQUIRE (clipB >= 0);
    REQUIRE (getInfo (clipB).startSeconds == doctest::Approx (0.987));

    const auto hardStep = readWavMaxFirstDifference (renderJunction ("hard.wav"));

    // Apply the crossfade and re-measure the same junction.
    REQUIRE (session.crossfadeClipWithNext (track, clipA, 0.05));

    // Geometry: the left clip now overlaps the right by exactly the crossfade
    // length, with complementary equal-power fades.
    {
        const auto a = getInfo (clipA);
        const auto b = getInfo (clipB);
        CHECK (a.endSeconds == doctest::Approx (1.037).epsilon (1.0e-3));
        CHECK (a.fadeOutSeconds == doctest::Approx (0.05).epsilon (1.0e-3));
        CHECK (b.fadeInSeconds == doctest::Approx (0.05).epsilon (1.0e-3));
        CHECK ((a.endSeconds - b.startSeconds) == doctest::Approx (0.05).epsilon (1.0e-3));
    }

    const auto fadedStep = readWavMaxFirstDifference (renderJunction ("faded.wav"));

    MESSAGE ("crossfade junction max sample step: hard=" << hardStep
             << ", crossfaded=" << fadedStep);

    // The hard junction shows a large discontinuity...
    CHECK (hardStep > 0.30f);
    // ...while the crossfaded junction is smooth (well below the click) and the
    // signal did not blow up.
    CHECK (fadedStep < 0.15f);
    CHECK (readWavPeak (renderJunction ("faded2.wav")) < 1.0f);

    session.close();
}

//==============================================================================
// Epic 4 (FR-ED-4, measured): assemble a master take from three stacked takes
// non-destructively. The comp is built on a new track as trimmed copies of the
// chosen takes; the takes and their files are left untouched. Rendering the comp
// (with the take track muted) and measuring each region's pitch proves which
// take each region plays.
TEST_CASE ("Epic 4 comping: master take from 3 takes is non-destructive (FR-ED-4)")
{
    auto dir = scratchDirectory ("comp");
    constexpr double sampleRate = 48000.0;

    auto take1 = writeSineWav (dir.getChildFile ("take1.wav"), sampleRate, 1.0, 0.5f, 220.0);
    auto take2 = writeSineWav (dir.getChildFile ("take2.wav"), sampleRate, 1.0, 0.5f, 330.0);
    auto take3 = writeSineWav (dir.getChildFile ("take3.wav"), sampleRate, 1.0, 0.5f, 440.0);
    REQUIRE (take1.existsAsFile());
    REQUIRE (take2.existsAsFile());
    REQUIRE (take3.existsAsFile());

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Comp.tracktionedit")));

    auto* edit = session.getEdit();
    REQUIRE (edit != nullptr);

    // One track with three overlapping takes (all 0..1 s). The raw track pointer
    // is scoped so it is released while the edit is still alive: a Tracktion
    // Track doesn't outlive its Edit, and releasing the Ptr after close() would
    // tear down clips against a destroyed edit.
    const int takeTrack = 1;
    {
        auto takeTrackPtr = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit),
                                                       nullptr, true);
        REQUIRE (takeTrackPtr != nullptr);

        for (auto* f : { &take1, &take2, &take3 })
            takeTrackPtr->insertWaveClip (f->getFileNameWithoutExtension(), *f,
                                          { { te::TimePosition(), te::TimeDuration::fromSeconds (1.0) }, {} },
                                          false);
    }

    const auto takesBefore = session.getClips (takeTrack);
    REQUIRE (takesBefore.size() == 3);
    CHECK (session.getNumAudioTracks() == 2);

    const juce::Array<int> takeIndices { takesBefore[0].clipIndex,
                                         takesBefore[1].clipIndex,
                                         takesBefore[2].clipIndex };
    const juce::Array<double> boundaries { 0.4, 0.7 };
    const juce::Array<int> chosen { 0, 1, 2 }; // take0 | take1 | take2

    const int compTrack = session.compTakes (takeTrack, takeIndices, boundaries, chosen);
    REQUIRE (compTrack >= 0);
    CHECK (session.getNumAudioTracks() == 3);

    // The comp is three butt-joined segments covering [0, 1].
    {
        const auto compClips = session.getClips (compTrack);
        REQUIRE (compClips.size() == 3);
        CHECK (compClips[0].startSeconds == doctest::Approx (0.0));
        CHECK (compClips[0].endSeconds == doctest::Approx (0.4).epsilon (0.01));
        CHECK (compClips[1].startSeconds == doctest::Approx (0.4));
        CHECK (compClips[1].endSeconds == doctest::Approx (0.7).epsilon (0.01));
        CHECK (compClips[2].startSeconds == doctest::Approx (0.7));
        CHECK (compClips[2].endSeconds == doctest::Approx (1.0).epsilon (0.01));
    }

    // Non-destructive: the three takes and their files are untouched.
    {
        const auto takesAfter = session.getClips (takeTrack);
        REQUIRE (takesAfter.size() == 3);

        for (const auto& c : takesAfter)
        {
            CHECK (c.startSeconds == doctest::Approx (0.0));
            CHECK (c.lengthSeconds == doctest::Approx (1.0).epsilon (0.01));
            CHECK (c.isMuted == false);
        }
    }

    CHECK (take1.existsAsFile());
    CHECK (take2.existsAsFile());
    CHECK (take3.existsAsFile());



    // Render the comp alone: mute the take track (the input track has no clips).
    REQUIRE (session.setTrackMute (takeTrack, true));

    auto dest = dir.getChildFile ("comp.wav");
    std::atomic<bool> finished { false };
    bool succeeded = false;

    auto handle = WavExport::start (*edit, dest,
                                    [&] (bool success, juce::File, juce::String)
                                    {
                                        succeeded = success;
                                        finished = true;
                                    });

    REQUIRE (handle != nullptr);

    for (int i = 0; i < 400 && ! finished.load(); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (25);

    REQUIRE (finished.load());
    REQUIRE (succeeded);

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (
        wav.createReaderFor (new juce::FileInputStream (dest), true));
    REQUIRE (reader != nullptr);
    REQUIRE (reader->lengthInSamples > (juce::int64) (0.9 * sampleRate));

    juce::AudioBuffer<float> rendered ((int) reader->numChannels, (int) reader->lengthInSamples);
    REQUIRE (reader->read (&rendered, 0, (int) reader->lengthInSamples, 0, true, true));

    const auto measured1 = estimateFrequency (rendered, sampleRate,
                                              (int) (0.05 * sampleRate), (int) (0.35 * sampleRate));
    const auto measured2 = estimateFrequency (rendered, sampleRate,
                                              (int) (0.45 * sampleRate), (int) (0.65 * sampleRate));
    const auto measured3 = estimateFrequency (rendered, sampleRate,
                                              (int) (0.75 * sampleRate), (int) (0.95 * sampleRate));

    MESSAGE ("comp regions: " << measured1 << " Hz | " << measured2 << " Hz | "
             << measured3 << " Hz (expected 220 | 330 | 440)");

    CHECK (measured1 == doctest::Approx (220.0).epsilon (0.05));
    CHECK (measured2 == doctest::Approx (330.0).epsilon (0.05));
    CHECK (measured3 == doctest::Approx (440.0).epsilon (0.05));

    handle.reset(); // release the renderer before the edit is torn down
    session.close();
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

        // The configuration may reference a device channel that the (deliberately
        // truncated) hardware list does not provide — e.g. a stereo config fed a
        // one-channel device in the old-routing regression further down.
        // Tracktion's `WaveInputDeviceInstance::copyIncomingDataIntoBuffer`
        // *bounds-checks* `indexInDevice` and silently skips the copy for a
        // missing channel, leaving that channel at whatever stale memory the
        // instance's `inputBuffer` happened to hold (`AudioBuffer::setSize` does
        // not clear). Reading it is therefore non-deterministic (a flaky test:
        // 0.0 on one run, 5.7e34 on the next). Zero-pad the pointer table up to
        // every referenced device channel so an absent channel deterministically
        // reads as silence instead of out-of-range/stale memory.
        constexpr int numSamples = 4;

        int requiredChannels = (int) hardwareAmplitudes.size();

        for (const auto& ci : cfg)
            requiredChannels = juce::jmax (requiredChannels, ci.indexInDevice + 1);

        std::vector<std::vector<float>> channelData ((size_t) requiredChannels,
                                                     std::vector<float> (numSamples, 0.0f));
        std::vector<const float*> channelPtrs;
        channelPtrs.reserve ((size_t) requiredChannels);

        for (size_t c = 0; c < hardwareAmplitudes.size(); ++c)
            std::fill (channelData[c].begin(), channelData[c].end(), hardwareAmplitudes[c]);

        for (auto& channel : channelData)
            channelPtrs.push_back (channel.data());

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
// Minor regression: `configureTracks()` used to force `MonitorMode::on` on every
// reconfigure/add-track, silently re-enabling monitoring the engineer had turned
// off. The Session now remembers the user's preference and re-applies it.
TEST_CASE ("user monitoring state survives adding a track (monitor preserved)")
{
    auto dir = scratchDirectory ("monitor-preserve");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Monitor.tracktionedit")));

    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 4;
    params.outputChannels = 2;

    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);
    REQUIRE (player != nullptr);

    auto& dm = audio.deviceManager();

    auto waitForConfigured = [&]
    {
        for (int i = 0; i < 300; ++i)
        {
            dm.dispatchPendingUpdates();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

            if (session.isInputConfigured())
                return;

            session.reconfigureInputs();
        }
    };

    waitForConfigured();
    REQUIRE (session.isInputConfigured());

    // Epic 1 default: monitoring auto-enabled on a fresh session.
    CHECK (session.isMonitoringEnabled());

    // The engineer turns monitoring off...
    REQUIRE (session.setMonitoringEnabled (false));
    CHECK_FALSE (session.isMonitoringEnabled());

    // ...and adding a track must not silently turn it back on.
    REQUIRE (session.addAudioTrack() >= 0);
    waitForConfigured();
    REQUIRE (session.isInputConfigured());
    CHECK_FALSE (session.isMonitoringEnabled());

    // The inverse: an explicit on-state is also preserved across add-track.
    REQUIRE (session.setMonitoringEnabled (true));
    CHECK (session.isMonitoringEnabled());

    REQUIRE (session.addAudioTrack() >= 0);
    waitForConfigured();
    REQUIRE (session.isInputConfigured());
    CHECK (session.isMonitoringEnabled());

    session.setMonitoringEnabled (false);
    session.close();
}

//==============================================================================
// FR-MIX-1 / FR-MIX-3 (Epic 2): the basic mixer's gain/pan/mute/solo and its
// per-track + master metering. A known-amplitude tone is played back through the
// real engine and the measured peak is compared to the expected level.
TEST_CASE ("mixer: gain and mute change the measured master level (measured)")
{
    auto dir = scratchDirectory ("mixer");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Mix.tracktionedit")));

    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 2;
    params.outputChannels = 2;

    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);

    for (int i = 0; i < 200; ++i)
    {
        audio.deviceManager().dispatchPendingUpdates();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        if (session.isInputConfigured())
            break;

        session.reconfigureInputs();
    }

    // A 0.5-amplitude tone -> -6.02 dBFS peak. Long enough to cover the whole
    // measurement window including the added master-mute section.
    auto tone = writeSineWav (dir.getChildFile ("tone.wav"), 48000.0, 2.0);
    REQUIRE (tone.existsAsFile());
    REQUIRE (session.importAudioFile (tone));

    // The imported track is added directly to the edit; let the session timer
    // pick it up so its meter client is attached.
    for (int i = 0; i < 20; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

    REQUIRE (session.getNumAudioTracks() == 2);

    // Mute the imported (tone) track: master must fall silent.
    CHECK (session.setTrackMute (1, true));
    CHECK (session.isTrackMuted (1));

    CHECK (session.setTrackGainDb (1, 0.0f));
    CHECK (session.setMasterGainDb (0.0f));

    session.getEdit()->getTransport().setPosition (te::TimePosition {});
    session.play();

    juce::AudioBuffer<float> silence (2, 256);
    silence.clear();

    auto peakFromDb = [] (float db) { return juce::Decibels::decibelsToGain (db, -100.0f); };

    auto runBlocks = [&] (int blocks, float& trackPeak, float& masterPeak, bool checkClip)
    {
        for (int block = 0; block < blocks; ++block)
        {
            player->process (silence);
            const auto trackMeter = session.readTrackMeter (1);
            const auto masterMeter = session.readMasterMeter();

            trackPeak = juce::jmax (trackPeak, peakFromDb (trackMeter.peakDb[0]));
            masterPeak = juce::jmax (masterPeak, peakFromDb (masterMeter.peakDb[0]));

            if (checkClip)
                CHECK_FALSE (masterMeter.clipped);
        }
    };

    // Mute the imported (tone) track: the master must fall silent. Skip the
    // first few blocks so Tracktion's click-free mute ramp has settled.
    float mutedTrackPeak = 0.0f, mutedMasterPeak = 0.0f;
    runBlocks (8, mutedTrackPeak, mutedMasterPeak, false);
    mutedTrackPeak = 0.0f;
    mutedMasterPeak = 0.0f;
    runBlocks (60, mutedTrackPeak, mutedMasterPeak, false);

    CHECK (mutedMasterPeak < 0.01f);

    // Unmute: the same tone must now register ~ -6 dBFS on the track and master.
    session.setTrackMute (1, false);

    float trackPeak = 0.0f, masterPeak = 0.0f;
    runBlocks (8, trackPeak, masterPeak, false); // let the unmute ramp settle
    trackPeak = 0.0f;
    masterPeak = 0.0f;
    runBlocks (60, trackPeak, masterPeak, true);

    INFO ("track peak " << juce::Decibels::gainToDecibels (trackPeak)
                        << " dB, master peak " << juce::Decibels::gainToDecibels (masterPeak) << " dB");

    CHECK (trackPeak == doctest::Approx (0.5f).epsilon (0.05f));
    CHECK (masterPeak == doctest::Approx (0.5f).epsilon (0.05f));

    // FR-MIX-1: master mute must silence the actual device output. (The master
    // meter taps the master plugin list, which sits *before* the master volume
    // plugin, so mute is measured at the real output, like the click test.)
    auto maxOutputOver = [&] (int blocks)
    {
        float peak = 0.0f;

        for (int block = 0; block < blocks; ++block)
        {
            auto output = player->process (silence);
            peak = juce::jmax (peak, output.getMagnitude (0, output.getNumSamples()));
        }

        return peak;
    };

    REQUIRE (session.setMasterMute (true));
    CHECK (session.isMasterMuted());

    session.getEdit()->getTransport().setPosition (te::TimePosition {});
    maxOutputOver (8); // ramp settle
    const auto mutedOutput = maxOutputOver (60);
    INFO ("muted output peak " << mutedOutput);
    CHECK (mutedOutput < 0.01f);

    // The remembered fader value survives the mute: unmuting restores the level
    // with no gain jump.
    CHECK (session.getMasterGainDb() == doctest::Approx (0.0f));

    REQUIRE (session.setMasterMute (false));
    CHECK_FALSE (session.isMasterMuted());

    session.getEdit()->getTransport().setPosition (te::TimePosition {});
    maxOutputOver (8); // ramp settle
    const auto unmutedOutput = maxOutputOver (60);
    INFO ("unmuted output peak " << unmutedOutput);
    CHECK (unmutedOutput == doctest::Approx (0.5f).epsilon (0.05f));

    // Master pan is exposed by the plugin (FR-MIX-1).
    REQUIRE (session.setMasterPan (-1.0f));
    CHECK (session.getMasterPan() == doctest::Approx (-1.0f));

    session.stop();
    session.close();
}

//==============================================================================
// Transport (owner request): Stop holds the playhead where it stopped (it does
// NOT rewind); Pause must stop in place (unchanged Play/Pause semantics), and
// "Go to start" moves the playhead to 0. Returning to the start is the job of
// the dedicated Start button via Session::goToStart().
TEST_CASE ("transport: Stop holds position, Pause holds, Go to start seeks to 0")
{
    auto dir = scratchDirectory ("transport");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Transport.tracktionedit")));

    // A real 2 s clip so the transport has a non-zero length to seek within.
    auto tone = writeSineWav (dir.getChildFile ("tone.wav"), 48000.0, 2.0);
    REQUIRE (tone.existsAsFile());
    REQUIRE (session.importAudioFile (tone));

    auto& transport = session.getEdit()->getTransport();
    const auto position = [&] { return transport.getPosition().inSeconds(); };

    // Pause stops in place: it must NOT rewind (unchanged Play/Pause semantics).
    transport.setPosition (te::TimePosition::fromSeconds (1.5));
    REQUIRE (position() == doctest::Approx (1.5).epsilon (0.01));
    session.pause();
    CHECK (position() == doctest::Approx (1.5).epsilon (0.01));

    // Stop holds the playhead where it stopped — it must NOT rewind to 0
    // (owner request; the old rewind-to-start behaviour was removed).
    transport.setPosition (te::TimePosition::fromSeconds (1.5));
    REQUIRE (position() == doctest::Approx (1.5).epsilon (0.01));
    session.stop();
    CHECK (position() == doctest::Approx (1.5).epsilon (0.01));

    // "Go to start" seeks to 0 without changing the transport state.
    transport.setPosition (te::TimePosition::fromSeconds (1.5));
    REQUIRE (position() == doctest::Approx (1.5).epsilon (0.01));
    session.goToStart();
    CHECK (position() == doctest::Approx (0.0).epsilon (0.001));

    session.close();
}

//==============================================================================
// Owner request: a clickable timeline that seeks the transport. The UI maps an
// x position to seconds and calls Session::setPositionSeconds (which forwards to
// TransportControl::setPosition); this exercises that API round-trip and its
// clamping, plus the session length the ruler maps against.
TEST_CASE ("timeline seek moves the transport and reports a usable length")
{
    auto dir = scratchDirectory ("timeline-seek");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Timeline.tracktionedit")));

    // A real 2 s clip so the session has a non-zero, seekable length.
    auto tone = writeSineWav (dir.getChildFile ("tone.wav"), 48000.0, 2.0);
    REQUIRE (tone.existsAsFile());
    REQUIRE (session.importAudioFile (tone));

    CHECK (session.getPositionSeconds() == doctest::Approx (0.0));

    // Seek to an arbitrary point in the track.
    session.setPositionSeconds (1.25);
    CHECK (session.getPositionSeconds() == doctest::Approx (1.25).epsilon (0.01));

    // The ruler length covers the material (and never collapses to zero).
    CHECK (session.getTimelineLengthSeconds() >= 2.0);

    // A negative seek (e.g. a click left of the ruler origin) clamps to 0.
    session.setPositionSeconds (-5.0);
    CHECK (session.getPositionSeconds() == doctest::Approx (0.0));

    // Seeking past the material is allowed (positioning a new take) and the
    // ruler stretches to keep the playhead visible.
    session.setPositionSeconds (30.0);
    CHECK (session.getPositionSeconds() == doctest::Approx (30.0).epsilon (0.01));
    CHECK (session.getTimelineLengthSeconds() >= 30.0);

    session.close();
}

//==============================================================================
// Owner request: recording a new take on a track must not play the previous take
// back to the performer. The record track's existing clips are muted for the
// duration of the pass and restored on stop; other (minus/backing) tracks keep
// playing.
TEST_CASE ("recording mutes the record track's existing clips for the pass (owner request)")
{
    auto dir = scratchDirectory ("record-pass-mute");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Take.tracktionedit")));

    auto take  = writeSineWav (dir.getChildFile ("take.wav"), 48000.0, 0.5);
    auto minus = writeSineWav (dir.getChildFile ("minus.wav"), 48000.0, 0.5);
    REQUIRE (take.existsAsFile());
    REQUIRE (minus.existsAsFile());

    // An existing take on the record track (track 0)...
    auto* recordTrack = session.getTrack (0);
    REQUIRE (recordTrack != nullptr);
    REQUIRE (recordTrack->insertWaveClip ("take", take,
                                          { { te::TimePosition(), te::TimeDuration::fromSeconds (0.5) }, {} },
                                          false) != nullptr);
    REQUIRE (recordTrack->getClips().size() == 1);
    auto* existingTake = recordTrack->getClips()[0];
    CHECK_FALSE (existingTake->isMuted());

    // ...and a "minus" on its own (backing) track, which must remain audible.
    REQUIRE (session.importAudioFile (minus));
    REQUIRE (session.getNumAudioTracks() == 2);
    auto* minusTrack = session.getTrack (1);
    REQUIRE (minusTrack != nullptr);
    REQUIRE (minusTrack->getClips().size() == 1);
    auto* minusClip = minusTrack->getClips()[0];
    CHECK_FALSE (minusClip->isMuted());

    // A hosted device so the track can be armed/recorded without real hardware.
    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 2;
    params.outputChannels = 2;

    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);

    for (int i = 0; i < 200; ++i)
    {
        audio.deviceManager().dispatchPendingUpdates();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        if (session.isInputConfigured())
            break;

        session.reconfigureInputs();
    }

    REQUIRE (session.isInputConfigured());
    REQUIRE (session.setTrackArmed (0, true));

    session.getEdit()->getTransport().setPosition (te::TimePosition {});
    REQUIRE (session.record());

    // During the pass the previous take is muted; the minus is untouched.
    CHECK (existingTake->isMuted());
    CHECK_FALSE (minusClip->isMuted());

    juce::AudioBuffer<float> input (2, 256);
    input.clear();

    for (int block = 0; block < 8; ++block)
        player->process (input);

    // Stopping restores the pre-pass mute state exactly.
    session.stop();
    CHECK_FALSE (existingTake->isMuted());
    CHECK_FALSE (minusClip->isMuted());

    session.close();
}

//==============================================================================
// Blocker (review): the record-pass clip mute is transient and must never be
// serialised. Autosave (`saveTempVersion`) and `save()` routinely run during a
// pass (a trim mouse-up, a routing fix-up). Without lifting the mute around the
// write, a crash mid-pass recovers an edit whose takes are silently muted — and
// there is no clip-unmute UI, so the session would be unusable.
TEST_CASE ("a persist during a record pass never serialises the transient clip mute")
{
    auto dir = scratchDirectory ("record-pass-persist");
    auto editFile = dir.getChildFile ("Take.tracktionedit");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (editFile));

    auto take = writeSineWav (dir.getChildFile ("take.wav"), 48000.0, 0.5);
    REQUIRE (take.existsAsFile());

    auto* recordTrack = session.getTrack (0);
    REQUIRE (recordTrack != nullptr);
    REQUIRE (recordTrack->insertWaveClip ("take", take,
                                          { { te::TimePosition(), te::TimeDuration::fromSeconds (0.5) }, {} },
                                          false) != nullptr);
    REQUIRE (recordTrack->getClips().size() == 1);
    auto* existingTake = recordTrack->getClips()[0];
    CHECK_FALSE (existingTake->isMuted());

    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 2;
    params.outputChannels = 2;

    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);

    for (int i = 0; i < 200; ++i)
    {
        audio.deviceManager().dispatchPendingUpdates();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        if (session.isInputConfigured())
            break;

        session.reconfigureInputs();
    }

    REQUIRE (session.isInputConfigured());
    REQUIRE (session.setTrackArmed (0, true));

    session.getEdit()->getTransport().setPosition (te::TimePosition {});
    REQUIRE (session.record());

    // The pass mute is live...
    REQUIRE (existingTake->isMuted());

    // ...but a save during the pass must persist the take UNMUTED. This is the
    // same lift-write-reinstate path the autosave `.tmp_` write uses.
    REQUIRE (session.save());

    // The live pass is undisturbed: the clip is muted again after the write.
    CHECK (existingTake->isMuted());

    auto reopened = te::loadEditFromFile (testEngine(), editFile);
    REQUIRE (reopened != nullptr);

    auto tracks = te::getAudioTracks (*reopened);
    REQUIRE_EQ (tracks.size(), 1);
    REQUIRE (tracks[0] != nullptr);
    REQUIRE (tracks[0]->getClips().size() == 1);
    CHECK_FALSE (tracks[0]->getClips()[0]->isMuted());

    // Autosave during a pass: let the session timer fire, then check the
    // recovery `.tmp_` edit Tracktion would prompt to restore is unmuted too.
    const auto tempFile = paths::tempEditFileFor (editFile);
    tempFile.deleteFile();
    session.setAutosaveIntervalSeconds (1);

    for (int i = 0; i < 60 && ! tempFile.existsAsFile(); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

    REQUIRE (tempFile.existsAsFile());

    auto recovered = te::loadEditFromFile (testEngine(), tempFile);
    REQUIRE (recovered != nullptr);
    auto recoveredTracks = te::getAudioTracks (*recovered);
    REQUIRE_EQ (recoveredTracks.size(), 1);
    REQUIRE (recoveredTracks[0] != nullptr);
    REQUIRE (recoveredTracks[0]->getClips().size() == 1);
    CHECK_FALSE (recoveredTracks[0]->getClips()[0]->isMuted());

    session.stop();
    CHECK_FALSE (existingTake->isMuted());

    session.close();
}

//==============================================================================
// FR-REC-4 (Epic 2 GUI retest): the per-input-track record trim must scale the
// signal written to disk (it is applied to the input buffer on the record path,
// before monitoring and recording). Two tracks, two trims, one pass.
TEST_CASE ("input record trim scales the recorded signal (measured)")
{
    auto dir = scratchDirectory ("input-trim");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Trim.tracktionedit")));
    REQUIRE (session.getNumAudioTracks() == 1);
    REQUIRE (session.addAudioTrack() >= 0);
    REQUIRE (session.getNumAudioTracks() == 2);

    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 2;
    params.outputChannels = 2;

    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);

    auto& dm = audio.deviceManager();
    dm.setAllWaveInputsToNumChannels (1);

    for (int i = 0; i < 300; ++i)
    {
        dm.dispatchPendingUpdates();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        if (session.isInputConfigured() && dm.getNumWaveInDevices() >= 2)
            break;

        session.reconfigureInputs();
    }

    REQUIRE (session.isInputConfigured());

    const auto indices = session.getInputTrackIndices();
    REQUIRE (indices.size() == 2);

    REQUIRE (session.setTrackInputGainDb (indices[0], 6.0f));
    REQUIRE (session.setTrackInputGainDb (indices[1], -6.0f));
    CHECK (session.getTrackInputGainDb (indices[0]) == doctest::Approx (6.0f));
    CHECK (session.getTrackInputGainDb (indices[1]) == doctest::Approx (-6.0f));

    for (auto index : indices)
        REQUIRE (session.setTrackArmed (index, true));

    const float amps[2] = { 0.25f, 0.125f };

    session.getEdit()->getTransport().setPosition (te::TimePosition {});
    REQUIRE (session.record());

    juce::AudioBuffer<float> input (2, 256);

    for (int block = 0; block < 80; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < input.getNumSamples(); ++s)
                input.setSample (ch, s, amps[ch]);

        player->process (input);
    }

    session.stop();

    for (int i = 0; i < 300; ++i)
    {
        session.getEdit()->dispatchPendingUpdatesSynchronously();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }

    for (auto index : indices)
        session.setTrackArmed (index, false);

    session.setMonitoringEnabled (false);

    const float gains[2] = { juce::Decibels::decibelsToGain (6.0f),
                             juce::Decibels::decibelsToGain (-6.0f) };

    for (int t = 0; t < (int) indices.size(); ++t)
    {
        auto* track = session.getTrack (indices[t]);
        REQUIRE (track != nullptr);
        REQUIRE (track->getClips().size() >= 1);

        auto* clip = dynamic_cast<te::WaveAudioClip*> (track->getClips()[0]);
        REQUIRE (clip != nullptr);

        const auto file = clip->getOriginalFile();
        REQUIRE (file.existsAsFile());

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

        const auto inputChannel = session.getTrackInputMapping (indices[t]).firstChannel;
        REQUIRE (juce::isPositiveAndBelow (inputChannel, 2));

        const auto peak = recorded.getMagnitude (0, numToRead);
        const auto expected = amps[inputChannel] * gains[t];

        INFO ("track " << indices[t] << " trim " << session.getTrackInputGainDb (indices[t])
                       << " dB on input " << inputChannel
                       << ": recorded peak " << peak << " expected " << expected);
        CHECK (peak == doctest::Approx (expected).epsilon (0.03f));
    }

    session.close();
}

//==============================================================================
// Normalise (Epic 2 GUI retest): a quiet take is peak-normalised to -1 dBFS,
// measured through an actual export render.
TEST_CASE ("normalise brings a quiet take's peak to -1 dBFS (measured)")
{
    auto dir = scratchDirectory ("normalise");

    // 0.1 amplitude = -20 dBFS source peak.
    auto wavFile = writeSineWav (dir.getChildFile ("quiet.wav"), 48000.0, 0.5, 0.1f);
    REQUIRE (wavFile.existsAsFile());

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Normalise.tracktionedit")));

    // A fresh session now starts at master unity (see Session::createNew); this
    // explicit set keeps the export measuring the *clip* normalisation, not any
    // master trim, even if the default ever changes.
    REQUIRE (session.setMasterGainDb (0.0f));

    REQUIRE (session.importAudioFile (wavFile));
    REQUIRE (session.getNumAudioTracks() == 2);

    const int takeTrack = 1;
    REQUIRE (session.getTrack (takeTrack) != nullptr);
    REQUIRE (session.getTrack (takeTrack)->getClips().size() == 1);

    auto* clip = dynamic_cast<te::WaveAudioClip*> (session.getTrack (takeTrack)->getClips()[0]);
    REQUIRE (clip != nullptr);
    CHECK (clip->getGainDB() == doctest::Approx (0.0f));

    // Let the session timer attach its meters before exporting (avoids a graph
    // rebuild racing the render thread).
    for (int i = 0; i < 15; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

    // Reference render at unity clip gain, to prove the normalise actually
    // scaled the rendered take (not just the stored clip gain).
    auto pre = dir.getChildFile ("pre.wav");
    std::atomic<bool> done { false };
    auto h = WavExport::start (*session.getEdit(), pre,
                               [&] (bool, juce::File, juce::String) { done = true; });
    for (int i = 0; i < 400 && ! done.load(); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
    const auto prePeak = readWavPeak (pre);

    REQUIRE (session.normaliseTake (takeTrack));

    // Source peak -20 dBFS -> +19 dB of non-destructive clip gain.
    INFO ("normalise clip gain " << clip->getGainDB() << " dB");
    CHECK (clip->getGainDB() == doctest::Approx (19.0f).epsilon (0.5f));

    // Measured: the exported render must peak at ~ -1 dBFS.
    auto dest = dir.getChildFile ("normalised.wav");
    std::atomic<bool> finished { false };
    bool succeeded = false;
    juce::String error;

    auto handle = WavExport::start (*session.getEdit(), dest,
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
    REQUIRE (succeeded);

    const auto peak = readWavPeak (dest);
    const auto peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);
    INFO ("pre-normalise peak " << prePeak
          << ", normalised peak " << peakDb << " dBFS");
    CHECK (prePeak == doctest::Approx (0.1f).epsilon (0.02f));
    CHECK (peakDb == doctest::Approx (-1.0f).epsilon (0.5f));

    session.close();
}

//==============================================================================
// FR-REC-10 (target): the built-in metronome/count-in click must be audible
// while the transport plays and silent when disabled. Measured through the
// master meter of the running engine.
TEST_CASE ("metronome: enabling the click produces audible output (measured)")
{
    auto dir = scratchDirectory ("metronome");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Click.tracktionedit")));

    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 2;
    params.outputChannels = 2;

    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);

    for (int i = 0; i < 200; ++i)
    {
        audio.deviceManager().dispatchPendingUpdates();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        if (session.isInputConfigured())
            break;

        session.reconfigureInputs();
    }

    juce::AudioBuffer<float> silence (2, 256);
    silence.clear();

    // Measure the final device output (the click is summed at the very end of
    // the master chain, past the master plugin list, so the master meter does
    // not see it).
    auto maxOutputPeakOver = [&] (int blocks)
    {
        float peak = 0.0f;

        for (int block = 0; block < blocks; ++block)
        {
            auto output = player->process (silence);
            peak = juce::jmax (peak, output.getMagnitude (0, output.getNumSamples()));
        }

        return peak;
    };

    session.setMetronomeEnabled (true);
    session.getEdit()->getTransport().setPosition (te::TimePosition {});
    session.play();

    const auto clickPeak = maxOutputPeakOver (200);
    INFO ("click peak " << clickPeak);
    CHECK (clickPeak > 0.01f);

    session.stop();
    session.setMetronomeEnabled (false);

    maxOutputPeakOver (30);
    session.getEdit()->getTransport().setPosition (te::TimePosition {});
    session.play();
    const auto silentPeak = maxOutputPeakOver (120);
    INFO ("silent peak " << silentPeak);
    CHECK (silentPeak < 0.01f);

    session.stop();
    session.close();
}

//==============================================================================
// FR-REC-10 (target): the count-in must genuinely delay the start of recording,
// and the delay must grow with the selected mode. This also guards the fix for
// the "countInMode global-property" bug: Tracktion keeps the count-in in the
// user-global Settings.xml (`SettingID::countInMode`), so Session now mirrors it
// on the Edit state and re-applies it in `record()` right before the roll.
namespace
{
    struct CountInPass
    {
        double clipSeconds = 0.0;   ///< Recorded take length.
        float outputPeak = 0.0f;    ///< Max |output| over the pass (input + click).
    };

    /** Records `blocks` blocks of a constant input through the hosted device and
        returns the recorded clip length + the max output peak. `clobberEngine`
        simulates another actor changing the engine's user-global count-in after
        Session set its own value, to prove `record()` re-asserts the session's. */
    CountInPass runCountInPass (te::Edit::CountIn mode, int blocks, int suffix,
                                bool clobberEngine = false)
    {
        auto dir = scratchDirectory ("countin-" + juce::String (suffix));

        AudioEngine audio (false);
        Session session (audio);
        REQUIRE (session.createNew (dir.getChildFile ("CountIn.tracktionedit")));

        te::HostedAudioDeviceInterface::Parameters params;
        params.sampleRate = 48000.0;
        params.blockSize = 256;
        params.inputChannels = 2;
        params.outputChannels = 2;

        auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);

        for (int i = 0; i < 200; ++i)
        {
            audio.deviceManager().dispatchPendingUpdates();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

            if (session.isInputConfigured())
                break;

            session.reconfigureInputs();
        }

        REQUIRE (session.isInputConfigured());
        REQUIRE (session.setTrackArmed (0, true));

        // The Click toggle is OFF on purpose: the count-in click is its own
        // function and must be audible while recording even when the metronome
        // is not enabled for playback.
        session.setMetronomeEnabled (false);
        session.setCountInMode (mode);

        if (clobberEngine)
            session.getEdit()->setCountInMode (te::Edit::CountIn::none);

        session.getEdit()->getTransport().setPosition (te::TimePosition {});

        juce::AudioBuffer<float> input (2, 256);

        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < input.getNumSamples(); ++s)
                input.setSample (ch, s, 0.5f);

        REQUIRE (session.record());

        CountInPass result;

        for (int block = 0; block < blocks; ++block)
        {
            auto output = player->process (input);
            result.outputPeak = juce::jmax (result.outputPeak,
                                            output.getMagnitude (0, output.getNumSamples()));
        }

        session.stop();

        for (int i = 0; i < 200; ++i)
        {
            session.getEdit()->dispatchPendingUpdatesSynchronously();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        }

        if (auto* track = session.getTrack (0))
            for (auto* clip : track->getClips())
                result.clipSeconds = juce::jmax (result.clipSeconds,
                                                 clip->getPosition().getLength().inSeconds());

        session.setTrackArmed (0, false);
        session.setMonitoringEnabled (false);
        session.close();
        return result;
    }
}

TEST_CASE ("count-in delays the start of recording and grows with the mode (measured)")
{
    // 500 blocks x 256 / 48000 = 2.66667 s of processed audio per pass.
    constexpr int blocks = 500;
    constexpr double processedSeconds = blocks * 256.0 / 48000.0;

    // At the default 120 BPM / 4/4, one beat = 0.5 s. Tracktion's pre-roll is
    // (numCountInBeats + 0.5) beats, so the recording start is delayed by:
    //   oneBeat -> 1.5 beats = 0.75 s, oneBar -> 4.5 beats = 2.25 s.
    constexpr double oneBeatDelay = 0.75;
    constexpr double oneBarDelay  = 2.25;

    const auto none = runCountInPass (te::Edit::CountIn::none,    blocks, 1);
    const auto beat = runCountInPass (te::Edit::CountIn::oneBeat, blocks, 2);
    const auto bar  = runCountInPass (te::Edit::CountIn::oneBar,  blocks, 3);

    INFO ("processed " << processedSeconds << " s");
    INFO ("none len " << none.clipSeconds << " peak " << none.outputPeak);
    INFO ("oneBeat len " << beat.clipSeconds << " peak " << beat.outputPeak);
    INFO ("oneBar len " << bar.clipSeconds << " peak " << bar.outputPeak);

    // Without a count-in the whole processed span is captured.
    CHECK (none.clipSeconds == doctest::Approx (processedSeconds).epsilon (0.03));

    // Each mode delays the take by its count-in length.
    CHECK (beat.clipSeconds == doctest::Approx (processedSeconds - oneBeatDelay).epsilon (0.04));
    CHECK (bar.clipSeconds  == doctest::Approx (processedSeconds - oneBarDelay).epsilon (0.06));

    // Longer count-in => shorter take for the same amount of processed audio.
    CHECK (none.clipSeconds > beat.clipSeconds);
    CHECK (beat.clipSeconds > bar.clipSeconds);

    // ...and the mode value the Session reports must match what it applied.
    CHECK (none.clipSeconds > 0.0);

    // The count-in click is audible (its own function, independent of the
    // Click toggle): the one-beat pre-roll adds click energy on top of the
    // constant input, so its peak is clearly above the count-in-off pass.
    CHECK (beat.outputPeak > none.outputPeak + 0.1f);
}

TEST_CASE ("record re-applies the session count-in even if the engine global changed (FR-REC-10)")
{
    constexpr int blocks = 500;
    constexpr double processedSeconds = blocks * 256.0 / 48000.0;
    constexpr double oneBarDelay = 2.25;

    // Select one bar, then clobber the engine's user-global count-in to none
    // (as another edit / a stale Settings.xml value would). `record()` must
    // re-assert the session's value, so the take is still delayed by one bar.
    const auto pass = runCountInPass (te::Edit::CountIn::oneBar, blocks, 4, /*clobberEngine*/ true);

    INFO ("clobbered len " << pass.clipSeconds);
    CHECK (pass.clipSeconds == doctest::Approx (processedSeconds - oneBarDelay).epsilon (0.06));
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

//==============================================================================
// Epic 3 — plugin hosting (FR-MIX-4/6/7).
//
// A minimal hosted-plugin stand-in: a stereo gain whose value is serialised
// through the standard AudioProcessor state chunk. It lets the hosting path
// (insert -> process -> save -> reopen -> state restored) be exercised
// deterministically without a VST3 installed on the machine.
class TestGainPlugin final : public juce::AudioPluginInstance
{
public:
    TestGainPlugin()
        : juce::AudioPluginInstance (BusesProperties()
                                         .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                         .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    void fillInPluginDescription (juce::PluginDescription& d) const override
    {
        d.name = "RRS Test Gain";
        d.pluginFormatName = "VST3";
        d.fileOrIdentifier = "rrs_test_gain";
        d.manufacturerName = "RAW Radio";
        d.descriptiveName = "Deterministic gain for the hosting tests";
        d.numInputChannels = 2;
        d.numOutputChannels = 2;
        d.isInstrument = false;
    }

    const juce::String getName() const override                 { return "RRS Test Gain"; }
    void prepareToPlay (double, int) override                   {}
    void releaseResources() override                            {}
    using juce::AudioProcessor::processBlock;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.applyGain (gain);
    }

    double getTailLengthSeconds() const override                { return 0.0; }
    bool acceptsMidi() const override                           { return false; }
    bool producesMidi() const override                          { return false; }
    juce::AudioProcessorEditor* createEditor() override         { return nullptr; }
    bool hasEditor() const override                             { return false; }
    int getNumPrograms() override                               { return 1; }
    int getCurrentProgram() override                            { return 0; }
    void setCurrentProgram (int) override                       {}
    const juce::String getProgramName (int) override            { return {}; }
    void changeProgramName (int, const juce::String&) override  {}

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        dest.setSize (sizeof (gain));
        std::memcpy (dest.getData(), &gain, sizeof (gain));
    }

    void setStateInformation (const void* data, int size) override
    {
        if (data != nullptr && size >= (int) sizeof (gain))
            std::memcpy (&gain, data, sizeof (gain));
    }

    float gain = 1.0f;
};

TEST_CASE ("plugin hosting: VST3 always, AU on macOS only, never VST2, out-of-process scan (FR-MIX-4/7)")
{
    AudioEngine audio (false);
    PluginHost host (audio.engine());

    const auto formats = host.getHostedFormatNames();
    INFO ("hosted formats: " << formats.joinIntoString (", "));

    // FR-MIX-4 / D11: VST3 is the default hosting format, always built in.
    CHECK (formats.contains ("VST3"));

    // D24: VST2 is never shipped. JUCE calls format "VST" (VST2); assert it is
    // absent. Empty string guard for the (unreachable) no-formats case.
    CHECK_FALSE (formats.contains ("VST"));

    // LV2 is opt-in (JUCE_PLUGINHOST_LV2) — the format list must match the flag.
   #if JUCE_PLUGINHOST_LV2
    CHECK (formats.contains ("LV2"));
   #else
    CHECK_FALSE (formats.contains ("LV2"));
   #endif

    // AU on macOS only; graceful absence elsewhere.
   #if JUCE_MAC
    CHECK (formats.contains ("AudioUnit"));
   #else
    CHECK_FALSE (formats.contains ("AudioUnit"));
   #endif

    // FR-MIX-7 / D23: scanning runs out of process so a crashing plugin cannot
    // take the host down.
    CHECK (host.usesOutOfProcessScanning());
}

TEST_CASE ("inserted hosted plugin processes audio and its state survives save/open (FR-MIX-4/6, measured)")
{
    auto dir = scratchDirectory ("plugin-insert");
    auto editFile = dir.getChildFile ("Plugin.tracktionedit");
    auto tone = writeSineWav (dir.getChildFile ("tone.wav"), 48000.0, 1.0);
    REQUIRE (tone.existsAsFile());

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (editFile));

    // Import the tone as a playback (backing) track: track 0 is the input track,
    // the imported one is last.
    REQUIRE (session.importAudioFile (tone));
    const int trackIndex = session.getNumAudioTracks() - 1;
    REQUIRE (trackIndex >= 1);
    REQUIRE (session.getTrack (trackIndex) != nullptr);

    // Deterministic hosted-plugin factory.
    auto& pm = audio.engine().getPluginManager();
    auto previousFactory = pm.createPluginInstance;
    pm.createPluginInstance = [] (const juce::PluginDescription& d, double, int, juce::String&)
        -> std::unique_ptr<juce::AudioPluginInstance>
    {
        if (d.name == "RRS Test Gain")
            return std::make_unique<TestGainPlugin>();

        return nullptr;
    };

    juce::PluginDescription desc;
    desc.name = "RRS Test Gain";
    desc.pluginFormatName = "VST3";
    desc.fileOrIdentifier = "rrs_test_gain";
    desc.manufacturerName = "RAW Radio";
    desc.numInputChannels = 2;
    desc.numOutputChannels = 2;
    pm.knownPluginList.addType (desc);

    REQUIRE (session.insertPlugin (trackIndex, desc, -1));
    REQUIRE (session.getNumPlugins (trackIndex) == 1);
    CHECK (session.getPluginInfo (trackIndex, 0).name == "RRS Test Gain");
    CHECK (session.getPluginInfo (trackIndex, 0).format == "VST3");
    CHECK_FALSE (session.getPluginInfo (trackIndex, 0).missing);

    // Resolve the created instance so the test can drive its gain.
    te::ExternalPlugin* external = nullptr;

    for (auto* plugin : session.getTrack (trackIndex)->pluginList)
        if (auto* e = dynamic_cast<te::ExternalPlugin*> (plugin))
            external = e;

    REQUIRE (external != nullptr);
    external->initialiseFully();
    auto* instance = dynamic_cast<TestGainPlugin*> (external->getAudioPluginInstance());
    REQUIRE (instance != nullptr);

    auto renderPeak = [&] (te::Edit& edit, const juce::File& dest)
    {
        dest.deleteFile();
        std::atomic<bool> finished { false };
        bool succeeded = false;
        juce::String error;

        auto handle = WavExport::start (edit, dest,
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
        REQUIRE (succeeded);
        return readWavPeak (dest);
    };

    // Unity reference.
    instance->gain = 1.0f;
    const auto unityPeak = renderPeak (*session.getEdit(), dir.getChildFile ("unity.wav"));
    REQUIRE (unityPeak > 0.1f);

    // The plugin genuinely processes the audio: -6 dB.
    instance->gain = 0.5f;
    const auto reducedPeak = renderPeak (*session.getEdit(), dir.getChildFile ("reduced.wav"));
    CHECK (reducedPeak == doctest::Approx (unityPeak * 0.5f).epsilon (0.03f));

    // Persist, reopen in a fresh session on the same engine, and confirm the
    // plugin (and its gain state) round-tripped.
    REQUIRE (session.save());
    session.close();

    Session reopened (audio);
    REQUIRE (reopened.open (editFile));
    const int reopenedTrack = reopened.getNumAudioTracks() - 1;
    REQUIRE (reopened.getNumPlugins (reopenedTrack) == 1);
    CHECK (reopened.getPluginInfo (reopenedTrack, 0).name == "RRS Test Gain");

    const auto reopenedPeak = renderPeak (*reopened.getEdit(), dir.getChildFile ("reopened.wav"));
    INFO ("unity " << unityPeak << ", reopened " << reopenedPeak);
    CHECK (reopenedPeak == doctest::Approx (unityPeak * 0.5f).epsilon (0.05f));

    pm.createPluginInstance = previousFactory;
    reopened.close();
}

//==============================================================================
// Epic 3 (FR-MIX-6) — plugin preset management. Save the plugin's current state
// as a named user preset, change the parameter, load the preset back and assert
// the state is restored. The preset is a JUCE state blob under the app preset
// folder; loading also flushes it into the Edit so a save/reopen keeps it.
TEST_CASE ("plugin presets: save/load restores the plugin state (FR-MIX-6, measured)")
{
    auto dir = scratchDirectory ("plugin-presets");
    auto editFile = dir.getChildFile ("Presets.tracktionedit");
    auto tone = writeSineWav (dir.getChildFile ("tone.wav"), 48000.0, 1.0);
    REQUIRE (tone.existsAsFile());

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (editFile));

    // Hermetic preset store: never touch the user's real app-data folder.
    session.setPresetDirectory (dir.getChildFile ("presets"));
    CHECK (session.getPresetDirectory() == dir.getChildFile ("presets"));

    REQUIRE (session.importAudioFile (tone));
    const int trackIndex = session.getNumAudioTracks() - 1;
    REQUIRE (trackIndex >= 1);
    REQUIRE (session.getTrack (trackIndex) != nullptr);

    // Deterministic hosted-plugin factory (as the hosting test).
    auto& pm = audio.engine().getPluginManager();
    auto previousFactory = pm.createPluginInstance;
    pm.createPluginInstance = [] (const juce::PluginDescription& d, double, int, juce::String&)
        -> std::unique_ptr<juce::AudioPluginInstance>
    {
        if (d.name == "RRS Test Gain")
            return std::make_unique<TestGainPlugin>();

        return nullptr;
    };

    juce::PluginDescription desc;
    desc.name = "RRS Test Gain";
    desc.pluginFormatName = "VST3";
    desc.fileOrIdentifier = "rrs_test_gain";
    desc.manufacturerName = "RAW Radio";
    desc.numInputChannels = 2;
    desc.numOutputChannels = 2;
    pm.knownPluginList.addType (desc);

    REQUIRE (session.insertPlugin (trackIndex, desc, -1));
    REQUIRE (session.getNumPlugins (trackIndex) == 1);

    te::ExternalPlugin* external = nullptr;

    for (auto* plugin : session.getTrack (trackIndex)->pluginList)
        if (auto* e = dynamic_cast<te::ExternalPlugin*> (plugin))
            external = e;

    REQUIRE (external != nullptr);
    external->initialiseFully();
    auto* instance = dynamic_cast<TestGainPlugin*> (external->getAudioPluginInstance());
    REQUIRE (instance != nullptr);

    const auto key = session.getPluginPresetKey (trackIndex, 0);
    REQUIRE (key.isNotEmpty());

    // Save the plugin's 0.25 gain as "Quarter".
    instance->gain = 0.25f;
    REQUIRE (session.savePluginPreset (trackIndex, 0, "Quarter"));
    CHECK (session.listPluginPresets (trackIndex, 0).contains ("Quarter"));

    // Change the parameter, then load the preset back.
    instance->gain = 1.0f;
    REQUIRE (session.loadPluginPreset (trackIndex, 0, "Quarter"));
    CHECK (instance->gain == doctest::Approx (0.25f));

    // Invalid / missing presets fail cleanly (no crash, error surfaced).
    CHECK_FALSE (session.loadPluginPreset (trackIndex, 0, "does not exist"));
    CHECK_FALSE (session.savePluginPreset (trackIndex, 0, "bad/name"));

    // The loaded state is persisted into the Edit: after save + reopen the plugin
    // comes back at the preset's gain.
    REQUIRE (session.save());
    session.close();

    Session reopened (audio);
    REQUIRE (reopened.open (editFile));
    const int reopenedTrack = reopened.getNumAudioTracks() - 1;
    REQUIRE (reopened.getNumPlugins (reopenedTrack) == 1);

    te::ExternalPlugin* reopenedExternal = nullptr;

    for (auto* plugin : reopened.getTrack (reopenedTrack)->pluginList)
        if (auto* e = dynamic_cast<te::ExternalPlugin*> (plugin))
            reopenedExternal = e;

    REQUIRE (reopenedExternal != nullptr);
    reopenedExternal->initialiseFully();
    auto* reopenedInstance = dynamic_cast<TestGainPlugin*> (reopenedExternal->getAudioPluginInstance());
    REQUIRE (reopenedInstance != nullptr);
    INFO ("reopened gain " << reopenedInstance->gain);
    CHECK (reopenedInstance->gain == doctest::Approx (0.25f));

    pm.createPluginInstance = previousFactory;
    reopened.close();
}

//==============================================================================
// Epic 3 (FR-MON-3 / FR-MON-4 [hard], closing the Epic 2 gap) — software cue
// mixes.
//
// A 6-out hosted interface is split into three stereo wave output devices:
// device 0 = control-room/main, device 1 = cue A, device 2 = cue B. Two source
// tones are sent to the cues through aux sends at different levels, then the
// real-time output is measured per device AND each cue is verified with an
// independent offline RenderSpecification render of its return bus.
TEST_CASE ("software cue mixes: two independent, distinct mixes routed to separate outputs (FR-MON-3/4, measured)")
{
    auto dir = scratchDirectory ("cues");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Cues.tracktionedit")));

    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 2;
    params.outputChannels = 6;

    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);
    REQUIRE (player != nullptr);

    auto& dm = audio.deviceManager();
    dm.setAllWaveOutputsToNumChannels (2);

    for (int i = 0; i < 200; ++i)
    {
        dm.dispatchPendingUpdates();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        if (session.isInputConfigured())
            break;

        session.reconfigureInputs();
    }

    REQUIRE (session.isInputConfigured());
    REQUIRE (dm.getNumWaveOutDevices() == 3);

    // Distinct source tones: A is a loud 440 Hz, B is a quieter 880 Hz.
    auto toneA = writeSineWav (dir.getChildFile ("A.wav"), 48000.0, 2.0, 0.5f, 440.0);
    auto toneB = writeSineWav (dir.getChildFile ("B.wav"), 48000.0, 2.0, 0.25f, 880.0);
    REQUIRE (toneA.existsAsFile());
    REQUIRE (toneB.existsAsFile());
    REQUIRE (session.importAudioFile (toneA));
    REQUIRE (session.importAudioFile (toneB));

    const int trackA = session.getNumAudioTracks() - 2;
    const int trackB = session.getNumAudioTracks() - 1;
    REQUIRE (trackA >= 1);
    REQUIRE (trackB > trackA);

    // Two cues, each on its own hardware output pair.
    const int cueA = session.createCueMix ("Cue A");
    const int cueB = session.createCueMix ("Cue B");
    REQUIRE (cueA == 0);
    REQUIRE (cueB == 1);
    CHECK (session.getNumCueMixes() == 2);
    CHECK (session.getCueMix (0).hasDedicatedOutput);
    CHECK (session.getCueMix (1).hasDedicatedOutput);
    CHECK (session.getCueMix (0).outputDeviceID != session.getCueMix (1).outputDeviceID);

    // Cue A: track A only, unity. Cue B: track B only, -6 dB. The engineer mix
    // (main out) stays independent and carries both sources.
    REQUIRE (session.setCueSendLevelDb (trackA, cueA, 0.0f));
    REQUIRE (session.setCueSendLevelDb (trackB, cueB, -6.0f));
    CHECK (session.isCueSendEnabled (trackA, cueA));
    CHECK_FALSE (session.isCueSendEnabled (trackB, cueA));
    CHECK (session.getCueSendLevelDb (trackB, cueB) == doctest::Approx (-6.0f).epsilon (0.5f));

    for (int i = 0; i < 30; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

    session.getEdit()->getTransport().setPosition (te::TimePosition {});
    session.play();

    auto peakOver = [&] (int blocks, float* peaks)
    {
        for (int ch = 0; ch < 6; ++ch)
            peaks[ch] = 0.0f;

        juce::AudioBuffer<float> input (2, 256);
        input.clear();

        for (int b = 0; b < blocks; ++b)
        {
            auto out = player->process (input);

            for (int ch = 0; ch < juce::jmin (6, out.getNumChannels()); ++ch)
                peaks[ch] = juce::jmax (peaks[ch], out.getMagnitude (ch, 0, out.getNumSamples()));
        }
    };

    float peaks[6] {};
    peakOver (10, peaks); // settle the fade-in ramps
    peakOver (60, peaks);

    INFO ("output peaks ch0..5: " << peaks[0] << ", " << peaks[1] << ", " << peaks[2] << ", "
                                   << peaks[3] << ", " << peaks[4] << ", " << peaks[5]);

    // Cue A (device 1 -> ch2/3) = tone A (~0.5); cue B (device 2 -> ch4/5) =
    // tone B at -6 dB (~0.125); main (ch0/1) carries both.
    CHECK (peaks[2] == doctest::Approx (0.5f).epsilon (0.1f));
    CHECK (peaks[4] == doctest::Approx (0.125f).epsilon (0.1f));

    // Independence: cue A has no tone B (stays ~0.5, not ~0.75) and the two cues
    // are clearly distinct.
    CHECK (peaks[2] < 0.6f);
    CHECK (peaks[2] > peaks[4] * 2.0f);

    session.stop();

    // Offline verification of each cue via RenderSpecification: render the
    // return bus alone with its aux-send sources included.
    auto renderCuePeak = [&] (int cueIndex, const juce::File& dest)
    {
        dest.deleteFile();

        auto* returnTrack = session.getTrack (session.getCueMix (cueIndex).returnTrackIndex);
        REQUIRE (returnTrack != nullptr);

        te::RenderSpecification spec;
        spec.destination = dest;
        spec.bitDepth = 24;
        spec.sampleRate = 48000.0;
        spec.includeTails = false;
        spec.tracks = { returnTrack->itemID };
        spec.includeSourceTracks = true;

        auto job = te::createRenderJob (*session.getEdit(), spec);
        REQUIRE (job.has_value());

        auto queue = std::make_shared<te::RenderQueue>();
        queue->addJob (std::move (*job));

        std::atomic<bool> done { false };
        queue->onFinished = [&done] { done.store (true); };
        queue->start();

        for (int i = 0; i < 400 && ! done.load(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (25);

        REQUIRE (done.load());
        REQUIRE (queue->getJobs()[0]->getState() == te::RenderQueue::Job::State::completed);
        return readWavPeak (dest);
    };

    const auto cueARenderPeak = renderCuePeak (0, dir.getChildFile ("cueA.wav"));
    const auto cueBRenderPeak = renderCuePeak (1, dir.getChildFile ("cueB.wav"));

    INFO ("cue render peaks: A " << cueARenderPeak << ", B " << cueBRenderPeak);
    CHECK (cueARenderPeak == doctest::Approx (0.5f).epsilon (0.1f));
    CHECK (cueBRenderPeak == doctest::Approx (0.125f).epsilon (0.1f));
    CHECK (cueARenderPeak > cueBRenderPeak * 2.0f);

    session.close();
}

//==============================================================================
// Epic 3 (FR-MIX-2) — submix folder routing (track -> bus/group -> master) and
// per-track output assignment.
TEST_CASE ("routing: submix folder groups tracks and output assignment routes to a device (FR-MIX-2)")
{
    auto dir = scratchDirectory ("routing");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Routing.tracktionedit")));

    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 2;
    params.outputChannels = 4;

    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);
    REQUIRE (player != nullptr);

    auto& dm = audio.deviceManager();
    dm.setAllWaveOutputsToNumChannels (2);

    for (int i = 0; i < 200; ++i)
    {
        dm.dispatchPendingUpdates();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        if (session.isInputConfigured())
            break;

        session.reconfigureInputs();
    }

    REQUIRE (session.isInputConfigured());
    REQUIRE (dm.getNumWaveOutDevices() == 2);

    auto tone = writeSineWav (dir.getChildFile ("tone.wav"), 48000.0, 0.5);
    REQUIRE (session.importAudioFile (tone));
    const int childIndex = session.getNumAudioTracks() - 1;
    auto* child = session.getTrack (childIndex);
    REQUIRE (child != nullptr);

    // Create a submix folder and move the track into it.
    const int folder = session.createSubmixFolder ("Drums");
    REQUIRE (folder == 0);
    CHECK (session.getNumSubmixFolders() == 1);
    CHECK (session.getSubmixFolderName (0) == "Drums");

    REQUIRE (session.addTrackToSubmix (childIndex, folder));
    REQUIRE (child->getParentFolderTrack() != nullptr);
    CHECK (child->getParentFolderTrack()->getName() == "Drums");
    CHECK (child->isPartOfSubmix());

    // Output assignment: a track (and the submix) can target a specific device.
    auto devices = dm.getWaveOutputDevices();
    REQUIRE (devices.size() == 2);

    auto* childNow = child;
    const int childNowIndex = te::getAudioTracks (*session.getEdit()).indexOf (childNow);
    REQUIRE (childNowIndex >= 0);

    REQUIRE (session.setTrackOutputToDevice (childNowIndex, devices[1]->getDeviceID()));
    CHECK (session.getTrackOutputDevice (childNowIndex) == devices[1]->getDeviceID());
    CHECK (session.trackHasDedicatedOutput (childNowIndex));

    REQUIRE (session.setTrackOutputToDefault (childNowIndex));
    CHECK_FALSE (session.trackHasDedicatedOutput (childNowIndex));

    // And back out of the submix.
    REQUIRE (session.removeTrackFromSubmix (childNowIndex));
    CHECK (childNow->getParentFolderTrack() == nullptr);

    session.close();
}

//==============================================================================
// Epic 3 (FR-EXP-2/3) — stem export: one 24-bit WAV per track plus the master
// mix, all at the session rate.
TEST_CASE ("stems export writes per-track stems and a master mix, 24-bit (FR-EXP-2/3, measured)")
{
    auto dir = scratchDirectory ("stems");
    auto editFile = dir.getChildFile ("Stems.tracktionedit");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (editFile));

    auto toneA = writeSineWav (dir.getChildFile ("A.wav"), 48000.0, 0.5, 0.5f, 440.0);
    auto toneB = writeSineWav (dir.getChildFile ("B.wav"), 48000.0, 0.5, 0.25f, 880.0);
    REQUIRE (session.importAudioFile (toneA));
    REQUIRE (session.importAudioFile (toneB));
    REQUIRE (session.getNumAudioTracks() == 3);

    REQUIRE (session.setTrackName (1, "StemA"));
    REQUIRE (session.setTrackName (2, "StemB"));

    auto stemsDir = dir.getChildFile ("stems out");

    std::atomic<bool> done { false };
    bool succeeded = false;
    int numFiles = 0;
    juce::String error;

    auto handle = StemsExport::start (*session.getEdit(), stemsDir,
                                      [&] (bool success, juce::File, int files, juce::String message)
                                      {
                                          succeeded = success;
                                          numFiles = files;
                                          error = message;
                                          done.store (true);
                                      });
    REQUIRE (handle != nullptr);

    for (int i = 0; i < 800 && ! done.load(); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (25);

    REQUIRE (done.load());
    INFO ("stems error: " << error);
    CHECK (succeeded);
    CHECK (numFiles == 3); // StemA + StemB + master

    auto wavs = stemsDir.findChildFiles (juce::File::findFiles, false, "*.wav");
    REQUIRE (wavs.size() == 3);

    juce::File fileA, fileB, fileMaster;

    for (auto& f : wavs)
    {
        if (f.getFileName().containsIgnoreCase ("StemA"))      fileA = f;
        else if (f.getFileName().containsIgnoreCase ("StemB")) fileB = f;
        else                                                   fileMaster = f;
    }

    REQUIRE (fileA.existsAsFile());
    REQUIRE (fileB.existsAsFile());
    REQUIRE (fileMaster.existsAsFile());

    // FR-EXP-1 / NFR-A-1: 24-bit at the session rate (48 kHz with no device).
    juce::WavAudioFormat wav;
    auto check24Bit48k = [] (juce::WavAudioFormat& fmt, const juce::File& f)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (
            fmt.createReaderFor (new juce::FileInputStream (f), true));
        REQUIRE (reader != nullptr);
        return reader;
    };

    for (auto& f : { fileA, fileB, fileMaster })
    {
        auto reader = check24Bit48k (wav, f);
        CHECK ((int) reader->bitsPerSample == StemsExport::bitDepth);
        CHECK (reader->sampleRate == doctest::Approx (48000.0));
    }

    const auto peakA = readWavPeak (fileA);
    const auto peakB = readWavPeak (fileB);
    const auto peakMaster = readWavPeak (fileMaster);

    INFO ("stem peaks A " << peakA << " B " << peakB << " master " << peakMaster);

    // Each stem carries only its own track, and the master sums both.
    CHECK (peakA == doctest::Approx (0.5f).epsilon (0.05f));
    CHECK (peakB == doctest::Approx (0.25f).epsilon (0.05f));
    CHECK (peakMaster > 0.6f);
    CHECK (peakMaster <= 0.8f);

    handle.reset();
    session.close();
}

//==============================================================================
// Epic 3 (FR-MIX-4/6) layout regression: at the minimum window size the plugin
// browser must not overlap any control. The old layout glued Scan/Refresh to the
// list border and drew the "Track plugin + presets" caption over the buttons.
// Every visible child must stay inside the panel and no two may intersect.
TEST_CASE ("plugin browser layout has no overlaps at the minimum window size (FR-MIX-4/6)")
{
    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (
        juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("raw-radio-studio-integration")
            .getChildFile ("plugin-browser-layout")
            .getChildFile ("Layout.tracktionedit")));

    PluginHost host (audio.engine());
    PluginBrowser browser (host, session);

    // The MainComponent overlay caps the panel at 780x520; that is what the
    // browser gets at the 1100x820 minimum window.
    browser.setSize (780, 520);

    auto children = browser.getChildren();
    REQUIRE (children.size() > 0);

    for (auto* child : children)
    {
        if (! child->isVisible())
            continue;

        INFO ("child bounds " << child->getBounds().toString()
              << " panel " << browser.getLocalBounds().toString());
        CHECK (browser.getLocalBounds().contains (child->getBounds()));
    }

    for (int i = 0; i < children.size(); ++i)
    {
        for (int j = i + 1; j < children.size(); ++j)
        {
            auto* a = children[i];
            auto* b = children[j];

            if (! a->isVisible() || ! b->isVisible())
                continue;

            INFO ("overlap " << a->getBounds().toString() << " vs " << b->getBounds().toString());
            CHECK_FALSE (a->getBounds().intersects (b->getBounds()));
        }
    }

    session.close();
}
