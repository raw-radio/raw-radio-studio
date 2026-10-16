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
#include "ui/BrandButton.h"
#include "ui/BrandColours.h"
#include "ui/PluginBrowser.h"
#include "ui/TrackLaneLayout.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <set>
#include <thread>
#include <vector>

namespace te = tracktion;
using namespace rrs;

//==============================================================================
// Epic 7 (NFR-P-5) RT-safety allocation probe.
//
// Global `operator new`/`delete` hooks so a test can assert the audio-callback
// path performs no allocations. They are inert until a test "arms" the probe, so
// the rest of the suite (and the engine's own construction/teardown) is
// unaffected. Only the non-aligned forms are replaced; any aligned allocation
// falls through to the default and is simply not counted — our callback path
// never performs one.
namespace rrs_rt_probe
{
    std::atomic<bool> armed { false };
    std::atomic<long> allocations { 0 };
}

// Every *non-aligned* form of the allocation operators is routed through
// `std::malloc`/`std::free`. This must stay complete: if a throwing form is
// replaced but the matching *nothrow* form is left to the default
// implementation, a buffer allocated by that default form is later released by
// our `operator delete` (-> `std::free`), which AddressSanitizer flags as an
// `alloc-dealloc-mismatch (operator new vs free)`. libstdc++ hits exactly this
// through `std::stable_sort`: its `_Temporary_buffer` allocates via
// `get_temporary_buffer` -> `::operator new(size, std::nothrow)` but frees via
// `__return_temporary_buffer` -> `::operator delete(ptr, size)`. `stable_sort`
// is reached from `juce::ValueTree::sort` (juce_ElementComparator.h) which
// Tracktion calls in `TrackList::sortTracksByType` (see the ASan triage in
// docs/EPIC7_GAPS.md). Keeping all non-aligned forms allocator-consistent means
// the ASan gate can keep `alloc_dealloc_mismatch` enabled.
//
// Aligned forms (`operator new(size_t, align_val_t)` and friends) are *not*
// replaced: JUCE/Tracktion do not over-align on this path, and leaving both the
// aligned allocation and the aligned deallocation to the default keeps them
// consistent with each other. Placement forms are never replaced (illegal).
void* operator new (std::size_t size)
{
    if (rrs_rt_probe::armed.load (std::memory_order_relaxed))
        rrs_rt_probe::allocations.fetch_add (1, std::memory_order_relaxed);

    if (auto* p = std::malloc (size != 0 ? size : 1))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size) { return ::operator new (size); }

void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    if (rrs_rt_probe::armed.load (std::memory_order_relaxed))
        rrs_rt_probe::allocations.fetch_add (1, std::memory_order_relaxed);

    return std::malloc (size != 0 ? size : 1);
}

void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    return ::operator new (size, std::nothrow);
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { std::free (p); }

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

    /** RMS level of a rendered WAV (across all channels), or -1 on failure. A
        clipped peak hides level differences, so mixes are compared by RMS rather
        than peak. */
    double readWavRms (const juce::File& file)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (
            wav.createReaderFor (new juce::FileInputStream (file), true));

        if (reader == nullptr || reader->lengthInSamples <= 0)
            return -1.0;

        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);

        if (! reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true))
            return -1.0;

        return buffer.getRMSLevel (0, 0, buffer.getNumSamples());
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

    /** Rewrites the RIFF + `data` chunk sizes of `file` to 0, reproducing the WAV
        a crash leaves before Tracktion's first ~6 s flush (header claims 0
        samples although the sample payload is already on disk). */
    bool zeroLengthWavHeader (const juce::File& file)
    {
        juce::MemoryBlock mb;

        if (! file.loadFileAsData (mb))
            return false;

        auto* d = (unsigned char*) mb.getData();
        const auto n = (int) mb.getSize();

        for (int i = 12; i + 8 <= n; ++i)
        {
            if (d[i] == 'd' && d[i + 1] == 'a' && d[i + 2] == 't' && d[i + 3] == 'a')
            {
                d[i + 4] = d[i + 5] = d[i + 6] = d[i + 7] = 0;

                const auto headerOnly = (juce::uint32) (i + 8) - 8;
                d[4] = (unsigned char) (headerOnly & 0xff);
                d[5] = (unsigned char) ((headerOnly >> 8) & 0xff);
                d[6] = (unsigned char) ((headerOnly >> 16) & 0xff);
                d[7] = (unsigned char) ((headerOnly >> 24) & 0xff);

                return file.replaceWithData (mb.getData(), mb.getSize());
            }
        }

        return false;
    }

    //==========================================================================
    // Engine-quiescence helpers for the live-input tests.
    //
    // Tracktion rebuilds its MIDI/wave device lists *asynchronously on the
    // message thread*: `DeviceManager::handleAsyncUpdate()` and
    // `DeviceManager::applyNewMidiDeviceList()` both call
    // `clearAllContextDevices()` + `reloadAllContextDevices()`, which tear down
    // and recreate every `InputDeviceInstance` of every active playback context
    // (tracktion_DeviceManager.cpp:829/841 and :487/496). The headless tests
    // configure routing and then roll the transport within microseconds, so an
    // async rebuild that had not drained can land in the middle of a take — a
    // device-list rebuild stops the transport recording and discards the take
    // (measured: "MIDI clips: 0") — or before the wave input is wired into the
    // graph, leaving the input track without its live-monitor node (measured:
    // the count-in pass rendered the click with no monitored input).
    //
    // These helpers make a test wait until that message-thread work has settled
    // (bounded, with a clear failure on timeout) instead of racing it. They only
    // *wait for a condition*; they never lengthen a fixed sleep or weaken an
    // assertion.
    struct DeviceChangeCounter final : juce::ChangeListener
    {
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            lastChangeMs.store (juce::Time::getMillisecondCounter());
        }

        std::atomic<juce::uint32> lastChangeMs { 0 };
    };

    /** Pumps the message thread (dispatching the device manager's pending work)
        until neither the Tracktion `DeviceManager` nor the underlying
        `juce::AudioDeviceManager` has broadcast a change for `quietMs`, or
        `deadlineMs` elapses. Returns false if it never went quiet.

        The default `quietMs` is deliberately longer than the Session's 10 Hz
        housekeeping timer (`Session::timerCallback`, `startTimerHz (10)`): that
        timer can itself re-apply the input routing and call
        `edit->restartPlayback()` (Session.cpp:4765-4774), which would also tear
        down an in-flight take. Requiring a full timer period with no change
        means that work has already run and stopped changing before we roll. */
    bool waitForDeviceManagerIdle (AudioEngine& audio, int quietMs = 150, int deadlineMs = 3000)
    {
        DeviceChangeCounter counter;
        auto& dm = audio.deviceManager();
        auto& juceDm = dm.deviceManager;

        dm.addChangeListener (&counter);
        juceDm.addChangeListener (&counter);

        const auto startMs = juce::Time::getMillisecondCounter();
        counter.lastChangeMs.store (startMs);

        bool quiet = false;

        while (juce::Time::getMillisecondCounter() - startMs < (juce::uint32) deadlineMs)
        {
            dm.dispatchPendingUpdates();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

            if (juce::Time::getMillisecondCounter() - counter.lastChangeMs.load() >= (juce::uint32) quietMs)
            {
                quiet = true;
                break;
            }
        }

        juceDm.removeChangeListener (&counter);
        dm.removeChangeListener (&counter);
        return quiet;
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
// Arrangement clip height (owner bug fix): an input track's clip and an imported
// "minus" clip must render at the same height, and every clip must fill its
// lane. In the default window a lane is only ~38 px tall, so the old reserved
// 20 px name row left the clip an 8 px sliver while the armed input lane's
// background filled the whole lane — the "input track is taller than the
// imported clip" report. e7054e9 made the LANES uniform but left this reserved
// row in place, so the clips still disagreed. This headless diagnostic builds a
// real session (input track + imported track), prints the lane and clip heights,
// and pins them equal. It fails if a clip no longer fills its lane.
TEST_CASE ("input and imported clips share one height and fill their lane")
{
    auto dir = scratchDirectory ("clip-height");
    auto wavFile = writeSineWav (dir.getChildFile ("minus.wav"), 48000.0, 0.5);
    REQUIRE (wavFile.existsAsFile());

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Clips.tracktionedit")));
    REQUIRE (session.getNumAudioTracks() == 1);

    // A recorded take on the armed input track (track 0), so both tracks carry a
    // clip and the comparison is clip-vs-clip, not clip-vs-lane.
    {
        auto* edit = session.getEdit();
        REQUIRE (edit != nullptr);

        auto tracks = te::getAudioTracks (*edit);
        REQUIRE (tracks.size() == 1);

        const te::ClipPosition position { { te::TimePosition(), te::TimeDuration::fromSeconds (0.5) }, {} };
        REQUIRE (tracks[0]->insertWaveClip ("Take", wavFile, position, false) != nullptr);
    }

    // An imported "minus" backing track (track 1).
    REQUIRE (session.importAudioFile (wavFile));
    REQUIRE (session.getNumAudioTracks() == 2);

    // Two representative arrangement heights: a roomy 72 px default and the
    // cramped default window (~38 px lanes, where the bug was visible).
    for (const int areaHeight : { 600, 88 })
    {
        const juce::Rectangle<int> area { 0, 0, 900, areaHeight };
        const auto lanes = track_lane::computeLaneRects (area, session.getNumAudioTracks());
        REQUIRE (lanes.size() == 2);

        int laneHeights[2] {}, clipHeights[2] {};

        for (int i = 0; i < 2; ++i)
        {
            const auto clips = session.getClips (i);
            REQUIRE (clips.size() == 1);

            juce::Rectangle<int> nameRow, band;
            track_lane::splitLane (lanes[(size_t) i], nameRow, band);

            // The drawn clip uses the band's y/height (x comes from the timeline
            // map, which never changes the height).
            laneHeights[i] = lanes[(size_t) i].getHeight();
            clipHeights[i] = band.getHeight();

            std::cout << "[clip-height] area " << areaHeight
                      << " track " << i << ": lane " << laneHeights[i]
                      << " px, clip " << clipHeights[i] << " px\n";
        }

        CHECK (laneHeights[0] == laneHeights[1]);
        CHECK (clipHeights[0] == clipHeights[1]);
        CHECK (clipHeights[0] == laneHeights[0] - 2 * track_lane::clipInset);
    }

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
// RT-2: a channel that is not on the device must NOT resolve to a clamped
// substitute device — it is reported as unavailable (and surfaced as an error),
// instead of silently duplicating the last device.
TEST_CASE ("input mapping: no device is resolved when the channel is off the map (RT-2)")
{
    auto dir = scratchDirectory ("input-offmap");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("OffMap.tracktionedit")));

    // A real hosted device with exactly 2 input channels so `resolveInputDeviceFor`
    // has a device list to search.
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

    REQUIRE (audio.deviceManager().getNumWaveInDevices() > 0);
    REQUIRE (! session.getInputTrackIndices().isEmpty());

    const int track = session.getInputTrackIndices()[0];

    // Channel 0 is carried by the 2-channel device.
    REQUIRE (session.setTrackInputMapping (track, InputMapping { 0, 1, InputLayout::Mono }));
    CHECK (session.isTrackInputChannelAvailable (track));

    // Channel 9 is off the device: no substitute device (RT-2) — reported as
    // unavailable regardless of the transient device-list state.
    REQUIRE (session.setTrackInputMapping (track, InputMapping { 9, 1, InputLayout::Mono }));
    CHECK_FALSE (session.isTrackInputChannelAvailable (track));

    // The failure is also surfaced to the user (RT-2): retry the reconfigure
    // while the wave-device list settles, then expect a clear error.
    for (int i = 0; i < 100 && session.getLastError().isEmpty(); ++i)
    {
        session.reconfigureInputs();
        audio.deviceManager().dispatchPendingUpdates();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }

    CHECK (session.getLastError().isNotEmpty());

    session.close();
}

//==============================================================================
// RT-4: extra tracks on a full device are created UNASSIGNED (valid "No input"),
// not with an off-device channel; and an explicitly pinned off-device channel
// still reports a concrete RT-2b problem (the two states stay distinct).
TEST_CASE ("input mapping: extra tracks are unassigned; an off-device pin reports a problem (RT-4/RT-2b)")
{
    auto dir = scratchDirectory ("input-problem");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Problem.tracktionedit")));

    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 2;   // exactly two hardware inputs
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

    REQUIRE (audio.deviceManager().getNumWaveInDevices() > 0);
    REQUIRE (! session.getInputTrackIndices().isEmpty());

    auto settle = [&]
    {
        for (int i = 0; i < 50; ++i)
        {
            audio.deviceManager().dispatchPendingUpdates();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        }
    };

    // Track 0 maps channel 0 -> fine.
    CHECK (session.getTrackInputProblem (session.getInputTrackIndices()[0]).isEmpty());

    // 2nd track -> channel 1 (still on the device).
    REQUIRE (session.addAudioTrack() >= 0);
    settle();

    // 3rd track -> no free channel -> UNASSIGNED (valid), not an error. The
    // mapping is written synchronously at creation, so this needs no settle.
    const int third = session.addAudioTrack();
    REQUIRE (third >= 0);
    CHECK (session.getTrackInputMapping (third).layout == InputLayout::None);
    settle();

    const auto idxs = session.getInputTrackIndices();
    REQUIRE (idxs.size() >= 3);

    CHECK (session.getTrackInputMapping (idxs[2]).layout == InputLayout::None);
    CHECK (session.getTrackInputProblem (idxs[2]).isEmpty());          // valid, no error
    CHECK (session.isTrackInputChannelAvailable (idxs[2]));            // no "problem"
    CHECK (session.getAppliedInputDeviceIndex (idxs[2]) == -1);        // no device
    CHECK (session.getTrackName (idxs[2]).startsWith ("Track "));      // honest name
    CHECK_FALSE (session.setTrackArmed (idxs[2], true));
    CHECK (session.getLastError().contains ("No input assigned"));

    for (auto i : idxs)
        CHECK (session.isTrackInputChannelAvailable (i) == session.getTrackInputProblem (i).isEmpty());

    // RT-2b is preserved: a track PINNED to an off-device channel is INVALID and
    // reports the concrete reason (distinct from the unassigned state above).
    REQUIRE (session.setTrackInputMapping (idxs[2], InputMapping { 7, 1, InputLayout::Mono }));
    settle();

    const auto problem = session.getTrackInputProblem (idxs[2]);
    CHECK (problem.isNotEmpty());
    CHECK (problem.contains ("Input channel"));
    CHECK_FALSE (session.isTrackInputChannelAvailable (idxs[2]));
    CHECK_FALSE (session.setTrackArmed (idxs[2], true));
    CHECK (session.getLastError().contains ("Input channel"));

    session.close();
}

//==============================================================================
// RT-2b #1: the problem accounts for the mapping's SPAN, not just its first
// channel — a stereo pair that would run off the device (or silently degrade to
// centred mono) is flagged.
TEST_CASE ("input problem: a stereo pair at/over the device edge is flagged (RT-2b #1)")
{
    auto dir = scratchDirectory ("input-span");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Span.tracktionedit")));

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
        if (session.isInputConfigured()) break;
        session.reconfigureInputs();
    }

    REQUIRE (audio.deviceManager().getNumWaveInDevices() > 0);
    const int track = session.getInputTrackIndices()[0];

    // Stereo on channel 1 of a 2-channel device needs channels 1-2 -> off device.
    REQUIRE (session.setTrackInputMapping (track, InputMapping { 1, 2, InputLayout::Stereo }));
    const auto problem = session.getTrackInputProblem (track);
    CHECK (problem.isNotEmpty());
    CHECK (problem.contains ("Input channels 2-3"));   // 1-based, spans to the missing channel
    CHECK_FALSE (session.isTrackInputChannelAvailable (track));

    // Stereo on channel 0 fits -> cleared (success path).
    REQUIRE (session.setTrackInputMapping (track, InputMapping { 0, 2, InputLayout::Stereo }));
    CHECK (session.getTrackInputProblem (track).isEmpty());
    CHECK (session.isTrackInputChannelAvailable (track));

    // Mono on the last channel (channel 1) still fits (a single channel).
    REQUIRE (session.setTrackInputMapping (track, InputMapping { 1, 1, InputLayout::Mono }));
    CHECK (session.getTrackInputProblem (track).isEmpty());

    session.close();
}

//==============================================================================
// RT-2b #2: stereo on a 1-channel device has no room for the pair.
TEST_CASE ("input problem: stereo on a 1-channel device is flagged (RT-2b #2)")
{
    auto dir = scratchDirectory ("input-1ch");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("OneCh.tracktionedit")));

    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 1;
    params.outputChannels = 2;
    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);

    for (int i = 0; i < 200; ++i)
    {
        audio.deviceManager().dispatchPendingUpdates();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        if (session.isInputConfigured()) break;
        session.reconfigureInputs();
    }

    REQUIRE (audio.deviceManager().getNumWaveInDevices() > 0);
    const int track = session.getInputTrackIndices()[0];

    // Default (Auto on a 1-channel device) is Mono -> fine.
    CHECK (session.getTrackInputProblem (track).isEmpty());

    // An explicit stereo pair cannot fit one channel.
    REQUIRE (session.setTrackInputMapping (track, InputMapping { 0, 2, InputLayout::Stereo }));
    CHECK (session.getTrackInputProblem (track).isNotEmpty());

    session.close();
}

//==============================================================================
// RT-2b #2/#4/#6/#7: single authoritative predicate + guard rails.
TEST_CASE ("input problem: single predicate and guards (RT-2b)")
{
    auto dir = scratchDirectory ("input-guards");
    auto wav = writeSineWav (dir.getChildFile ("take.wav"), 48000.0, 1.0);
    REQUIRE (wav.existsAsFile());

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Guards.tracktionedit")));

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
        if (session.isInputConfigured()) break;
        session.reconfigureInputs();
    }

    REQUIRE (audio.deviceManager().getNumWaveInDevices() > 0);
    const int inputTrack = session.getInputTrackIndices()[0];

    // A playback-only (imported) track is never an input track -> no problem.
    REQUIRE (session.importAudioFile (wav));
    int playbackTrack = -1;
    for (int i = 0; i < session.getNumAudioTracks(); ++i)
        if (! session.getInputTrackIndices().contains (i))
            playbackTrack = i;
    REQUIRE (playbackTrack >= 0);
    CHECK (session.getTrackInputProblem (playbackTrack).isEmpty());
    CHECK (session.isTrackInputChannelAvailable (playbackTrack));

    // #4/#7: no device open -> channels == 0 -> problem empty AND available true
    // (the two predicates stay in agreement).
    audio.deviceManager().deviceManager.closeAudioDevice();
    for (int i = 0; i < 20; ++i)
        audio.deviceManager().dispatchPendingUpdates();

    if (audio.getNumActiveInputChannels() == 0)
    {
        CHECK (session.getTrackInputProblem (inputTrack).isEmpty());
        CHECK (session.isTrackInputChannelAvailable (inputTrack));
    }

    CHECK (session.isTrackInputChannelAvailable (inputTrack)
               == session.getTrackInputProblem (inputTrack).isEmpty());
    CHECK (session.isTrackInputChannelAvailable (playbackTrack)
               == session.getTrackInputProblem (playbackTrack).isEmpty());

    session.close();
}


//==============================================================================
// RT-3: grouping is derived from the tracks' layouts with ONE device per track.
// The regression this guards: the old `setTarget(..., moveToTrack=true)` dropped
// every other track's target, so adding a track made the previous ones lose their
// input ("last wins").
TEST_CASE ("input routing: one device per span; adding a track keeps the others (RT-3/RT-4)")
{
    auto dir = scratchDirectory ("input-rt3");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Rt3.tracktionedit")));

    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 2;   // one device per hardware channel is expected
    params.outputChannels = 2;
    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);

    auto settle = [&]
    {
        for (int i = 0; i < 200; ++i)
        {
            audio.deviceManager().dispatchPendingUpdates();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

            if (session.isInputConfigured())
                break;

            session.reconfigureInputs();
        }
    };

    settle();
    REQUIRE (! session.getInputTrackIndices().isEmpty());

    // A second input track.
    REQUIRE (session.addAudioTrack() >= 0);
    settle();

    const auto idx = session.getInputTrackIndices();
    REQUIRE (idx.size() >= 2);

    // Both tracks are armable (each owns a device) and use DIFFERENT channels.
    CHECK (session.setTrackArmed (idx[0], true));
    CHECK (session.setTrackArmed (idx[1], true));
    CHECK (session.getTrackInputMapping (idx[0]).firstChannel
               != session.getTrackInputMapping (idx[1]).firstChannel);

    // ... on DIFFERENT WaveInputDevices (RT-3: 1 track = 1 device).
    CHECK (session.getAppliedInputDeviceIndex (idx[0]) >= 0);
    CHECK (session.getAppliedInputDeviceIndex (idx[1]) >= 0);
    CHECK (session.getAppliedInputDeviceIndex (idx[0])
               != session.getAppliedInputDeviceIndex (idx[1]));

    // Adding a third track (off a 2-channel device) must NOT drop the first two
    // tracks' targets (the "last wins" regression).
    REQUIRE (session.addAudioTrack() >= 0);
    settle();

    CHECK (session.setTrackArmed (idx[0], true));
    CHECK (session.setTrackArmed (idx[1], true));

    const auto idx3 = session.getInputTrackIndices();
    REQUIRE (idx3.size() >= 3);
    // RT-4: the 3rd track (no free channel) is UNASSIGNED — valid, not an error.
    CHECK (session.getTrackInputMapping (idx3[2]).layout == InputLayout::None);
    CHECK (session.getTrackInputProblem (idx3[2]).isEmpty());
    CHECK_FALSE (session.setTrackArmed (idx3[2], true));
    CHECK (session.getLastError().contains ("No input assigned"));

    // Review #1 smoke: swap the two mono tracks' channels so each track's device
    // changes; the stale target must be dropped (no crash, both still placed).
    REQUIRE (session.setTrackInputMapping (idx[0], InputMapping { 1, 1, InputLayout::Mono }));
    REQUIRE (session.setTrackInputMapping (idx[1], InputMapping { 0, 1, InputLayout::Mono }));
    settle();
    CHECK (session.getAppliedInputDeviceIndex (idx[0]) >= 0);
    CHECK (session.getAppliedInputDeviceIndex (idx[1]) >= 0);
    CHECK (session.setTrackArmed (idx[0], true));
    CHECK (session.setTrackArmed (idx[1], true));

    session.close();
}

//==============================================================================
// RT-3 B1/B2: a stereo pair on a 2-channel device must NOT be clobbered by a
// mono track pinned to its right channel, and the pinning track must report the
// occupancy per-track (the timer must not re-resolve the device by
// channel-contains and overwrite the stereo config).
TEST_CASE ("input routing: occupancy is per-track and a stereo device is not clobbered (RT-3 B1/B2)")
{
    auto dir = scratchDirectory ("input-rt3-b1");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Rt3b1.tracktionedit")));

    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 2;
    params.outputChannels = 2;
    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);

    auto settle = [&]
    {
        for (int i = 0; i < 200; ++i)
        {
            audio.deviceManager().dispatchPendingUpdates();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

            if (session.isInputConfigured())
                break;

            session.reconfigureInputs();
        }
    };

    settle();
    REQUIRE (! session.getInputTrackIndices().isEmpty());
    const int a = session.getInputTrackIndices()[0];

    // Track A: a stereo pair on channels 0-1.
    REQUIRE (session.setTrackInputMapping (a, InputMapping { 0, 2, InputLayout::Stereo }));
    settle();
    CHECK (session.getTrackInputProblem (a).isEmpty());
    const int aDevice = session.getAppliedInputDeviceIndex (a);
    CHECK (aDevice >= 0);

    // Track B: mono pinned to channel 1 (A's right channel) -> refused.
    REQUIRE (session.addAudioTrack() >= 0);
    settle();
    const auto idxs = session.getInputTrackIndices();
    REQUIRE (idxs.size() >= 2);
    const int b = idxs[1];
    REQUIRE (session.setTrackInputMapping (b, InputMapping { 1, 1, InputLayout::Mono }));
    settle();

    const auto bProblem = session.getTrackInputProblem (b);
    CHECK (bProblem.isNotEmpty());
    CHECK (bProblem.contains ("already used"));
    CHECK_FALSE (session.isTrackInputChannelAvailable (b));
    CHECK (session.getAppliedInputDeviceIndex (b) == -1);

    // B1: A's device assignment is untouched; A stays available and armable.
    CHECK (session.getAppliedInputDeviceIndex (a) == aDevice);
    CHECK (session.getTrackInputProblem (a).isEmpty());
    CHECK (session.setTrackArmed (a, true));

    // Re-running the configure/reconfigure pass (as the timer would) must not
    // clobber A either (the applied layout, not a channel-contains resolver).
    session.reconfigureInputs();
    settle();
    CHECK (session.getAppliedInputDeviceIndex (a) == aDevice);
    CHECK (session.getTrackInputProblem (a).isEmpty());
    CHECK (session.getTrackInputProblem (b).contains ("already used"));

    session.close();
}

//==============================================================================
// RT-4: tracks may SHARE an identical span (same channel/pair): several tracks on
// one channel is valid (their device config is identical) — unlike a different
// overlapping span, which is still refused.
TEST_CASE ("input routing: tracks may share the same channel (RT-4)")
{
    auto dir = scratchDirectory ("input-share");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Share.tracktionedit")));

    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 2;
    params.outputChannels = 2;
    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);

    auto settle = [&]
    {
        for (int i = 0; i < 200; ++i)
        {
            audio.deviceManager().dispatchPendingUpdates();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

            if (session.isInputConfigured())
                break;

            session.reconfigureInputs();
        }
    };

    settle();
    REQUIRE (! session.getInputTrackIndices().isEmpty());
    const auto i0 = session.getInputTrackIndices()[0];
    REQUIRE (session.setTrackInputMapping (i0, InputMapping { 0, 1, InputLayout::Mono }));
    settle();

    REQUIRE (session.addAudioTrack() >= 0);
    settle();
    const auto i1 = session.getInputTrackIndices()[1];
    // Point the 2nd track at the SAME channel 0 (identical span) -> shared.
    REQUIRE (session.setTrackInputMapping (i1, InputMapping { 0, 1, InputLayout::Mono }));
    settle();

    // Both valid, on one shared device, both armable.
    CHECK (session.getTrackInputProblem (i0).isEmpty());
    CHECK (session.getTrackInputProblem (i1).isEmpty());
    CHECK (session.getAppliedInputDeviceIndex (i0) >= 0);
    CHECK (session.getAppliedInputDeviceIndex (i0) == session.getAppliedInputDeviceIndex (i1));
    CHECK (session.setTrackArmed (i0, true));
    CHECK (session.setTrackArmed (i1, true));
    CHECK (session.isAnyTrackArmed());

    session.close();
}

//==============================================================================
// RT-4 M3: adding a track must consider existing SPANS, not just first channels —
// with a Stereo 0-1 track the next track starts at channel 2 (no collision).
TEST_CASE ("input routing: a new track skips an existing stereo span (RT-4 M3)")
{
    auto dir = scratchDirectory ("input-span-pick");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("SpanPick.tracktionedit")));

    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 4;
    params.outputChannels = 2;
    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);

    auto settle = [&]
    {
        for (int i = 0; i < 200; ++i)
        {
            audio.deviceManager().dispatchPendingUpdates();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

            if (session.isInputConfigured())
                break;

            session.reconfigureInputs();
        }
    };

    settle();
    REQUIRE (! session.getInputTrackIndices().isEmpty());
    const int t0 = session.getInputTrackIndices()[0];
    REQUIRE (session.setTrackInputMapping (t0, InputMapping { 0, 2, InputLayout::Stereo }));
    settle();

    // New track: must skip channels 0 and 1 (owned by the stereo span).
    REQUIRE (session.addAudioTrack() >= 0);
    settle();

    const auto idxs = session.getInputTrackIndices();
    REQUIRE (idxs.size() >= 2);
    CHECK (session.getTrackInputMapping (idxs[1]).firstChannel >= 2);
    CHECK (session.getTrackInputProblem (idxs[1]).isEmpty()); // no conflict at birth
    CHECK (session.isTrackInputChannelAvailable (idxs[1]));

    session.close();
}

//==============================================================================
// RT-4: an unassigned ("No input") track survives save/open (round-trip) and can
// be assigned later, in the same session.
TEST_CASE ("input mapping: an unassigned track survives save/open and can be assigned later (RT-4)")
{
    auto dir = scratchDirectory ("input-unassigned");

    // Device-free (mappings persist on their own; see the FR-REC-3 pattern).
    AudioEngine audio (false);
    Session session (audio);
    auto file = dir.getChildFile ("Unassigned.tracktionedit");
    REQUIRE (session.createNew (file));

    REQUIRE (session.addAudioTrack() >= 0);
    auto idxs = session.getInputTrackIndices();
    REQUIRE (idxs.size() >= 2);

    // Make the 2nd track unassigned (valid "No input").
    const int unassigned = idxs[1];
    REQUIRE (session.setTrackInputMapping (unassigned, InputMapping { 0, 1, InputLayout::None }));
    CHECK (session.getTrackInputMapping (unassigned).layout == InputLayout::None);
    CHECK (session.getTrackInputProblem (unassigned).isEmpty());

    REQUIRE (session.save());

    // Reopen in a fresh session: the unassigned state round-trips.
    {
        Session reopened (audio);
        REQUIRE (reopened.open (file));

        const auto ridx = reopened.getInputTrackIndices();
        REQUIRE (ridx.size() >= 2);
        CHECK (reopened.getTrackInputMapping (ridx[1]).layout == InputLayout::None);
        CHECK (reopened.getTrackInputProblem (ridx[1]).isEmpty());

        // Assign later: it becomes a normal, valid track (no device -> no problem).
        REQUIRE (reopened.setTrackInputMapping (ridx[1], InputMapping { 1, 1, InputLayout::Mono }));
        CHECK (reopened.getTrackInputMapping (ridx[1]).layout == InputLayout::Mono);
        CHECK (reopened.getTrackInputProblem (ridx[1]).isEmpty());

        reopened.close();
    }

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

// BLOCKER 2 regression: `setClipLoop(..., 0)` must be accepted and actually turn
// looping off — for a natively loopable source and for the timeline fallback
// (clips with no loop metadata). Previously `numLoops < 1` was rejected, so the
// toggle could never disable a loop, and the fallback kept accumulating copies.
TEST_CASE ("Epic 4 loop toggle: numLoops 0 disables native and fallback loops")
{
    auto dir = scratchDirectory ("loop-toggle");
    auto wavFile = writeSineWav (dir.getChildFile ("take.wav"), 48000.0, 2.0);
    REQUIRE (wavFile.existsAsFile());

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("LoopToggle.tracktionedit")));
    REQUIRE (session.importAudioFile (wavFile));

    const int track = 1; // the imported take's track
    const auto baseline = session.getClips (track);
    REQUIRE (baseline.size() == 1);
    const int ci = baseline[0].clipIndex;
    const auto originalLength = baseline[0].lengthSeconds;
    CHECK_FALSE (baseline[0].isLooping);

    // Loop x3 (native when the source carries loop metadata, otherwise two
    // butt-joined copies on the timeline).
    REQUIRE (session.setClipLoop (track, ci, 3));
    {
        Session::ClipInfo looped;
        REQUIRE (session.getClipInfo (track, ci, looped));
        CHECK (looped.isLooping); // the UI toggle relies on this to offer "off"
    }

    const bool fallback = (session.getClips (track).size() == 3);

    {
        Session::ClipInfo looped;
        REQUIRE (session.getClipInfo (track, ci, looped));

        if (fallback)
            CHECK (looped.lengthSeconds == doctest::Approx (originalLength).epsilon (0.02));
        else
            CHECK (looped.lengthSeconds == doctest::Approx (originalLength * 3.0).epsilon (0.02));
    }

    // Loop off — accepted and restoring exactly the original clip.
    REQUIRE (session.setClipLoop (track, ci, 0));

    {
        Session::ClipInfo off;
        REQUIRE (session.getClipInfo (track, ci, off));
        CHECK_FALSE (off.isLooping);
        CHECK (off.lengthSeconds == doctest::Approx (originalLength).epsilon (0.02));
    }

    CHECK (session.getClips (track).size() == 1);

    // Toggling off when already off is a harmless, idempotent no-op.
    CHECK (session.setClipLoop (track, ci, 0));
    CHECK (session.getClips (track).size() == 1);

    session.close();
}

// BLOCKER 2 regression, fallback branch: when the source carries no loop
// metadata the engine cannot loop natively, so `setClipLoop` butt-joins copies
// on the timeline. Turning the loop off must remove exactly those copies (it
// previously did nothing at all, so copies accumulated).
TEST_CASE ("Epic 4 fallback loop: setClipLoop(0) removes the generated copies")
{
    auto dir = scratchDirectory ("loop-fallback");
    auto wavFile = writeSineWav (dir.getChildFile ("take.wav"), 48000.0, 2.0);
    REQUIRE (wavFile.existsAsFile());

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("LoopFallback.tracktionedit")));
    REQUIRE (session.importAudioFile (wavFile));

    const int track = 1;
    const auto baseline = session.getClips (track);
    REQUIRE (baseline.size() == 1);
    const int ci = baseline[0].clipIndex;
    const auto originalLength = baseline[0].lengthSeconds;

    // Force the fallback branch: mark the clip one-shot so `canLoop()` is false
    // (a normal imported clip is loopable via the engine's tempo defaults).
    {
        auto* audioClip = dynamic_cast<te::AudioClipBase*> (
            te::getAudioTracks (*session.getEdit())[track]->getClips()[ci]);
        REQUIRE (audioClip != nullptr);
        audioClip->getLoopInfo().state.setProperty (te::IDs::oneShot, true, nullptr);
        REQUIRE_FALSE (audioClip->canLoop());
    }

    REQUIRE (session.setClipLoop (track, ci, 3));

    {
        const auto clips = session.getClips (track);
        REQUIRE (clips.size() == 3); // original + two butt-joined copies
        CHECK (clips[0].isLooping);  // reported as looping so the UI can turn it off
        CHECK (clips[0].lengthSeconds == doctest::Approx (originalLength).epsilon (0.02));
    }

    // Re-applying the loop is exact, not cumulative.
    REQUIRE (session.setClipLoop (track, ci, 3));
    CHECK (session.getClips (track).size() == 3);

    // Loop off removes every generated copy and restores the single original.
    REQUIRE (session.setClipLoop (track, ci, 0));

    {
        const auto clips = session.getClips (track);
        REQUIRE (clips.size() == 1);
        CHECK_FALSE (clips[0].isLooping);
        CHECK (clips[0].lengthSeconds == doctest::Approx (originalLength).epsilon (0.02));
    }

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
// Epic 4 (FR-ED-5): time-stretch a clip to a target duration through the pinned
// free library. The clip is replaced by one referencing a new rendered WAV; the
// original source file stays on disk. Undo restores the original clip.
TEST_CASE ("Epic 4 time-stretch: a clip renders to a target duration (FR-ED-5)")
{
    auto dir = scratchDirectory ("clip-stretch");
    constexpr double sampleRate = 48000.0;

    auto wavFile = writeSineWav (dir.getChildFile ("tone.wav"), sampleRate, 1.0, 0.5f, 440.0);
    REQUIRE (wavFile.existsAsFile());

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Stretch.tracktionedit")));
    REQUIRE (session.importAudioFile (wavFile));

    const int track = 1;
    auto clips = session.getClips (track);
    REQUIRE (clips.size() == 1);
    const int ci = clips[0].clipIndex;
    CHECK (clips[0].lengthSeconds == doctest::Approx (1.0).epsilon (0.02));

    // Stretch to 0.5 s.
    REQUIRE (session.stretchClipToDuration (track, ci, 0.5));

    auto after = session.getClips (track);
    REQUIRE (after.size() == 1);
    CHECK (after[0].lengthSeconds == doctest::Approx (0.5).epsilon (0.02));
    CHECK (after[0].startSeconds == doctest::Approx (0.0).epsilon (0.01));

    auto* stretchedClip = dynamic_cast<te::WaveAudioClip*> (session.getTrack (track)->getClips()[0]);
    REQUIRE (stretchedClip != nullptr);

    const auto stretchedFile = stretchedClip->getOriginalFile();
    CHECK (stretchedFile.existsAsFile());
    CHECK (stretchedFile.getFullPathName().contains ("Processed"));
    CHECK (stretchedFile != wavFile);

    // The original source is preserved.
    CHECK (wavFile.existsAsFile());

    // Undo restores the original clip and file.
    REQUIRE (session.undo());
    auto restored = session.getClips (track);
    REQUIRE (restored.size() == 1);
    CHECK (restored[0].lengthSeconds == doctest::Approx (1.0).epsilon (0.02));

    auto* restoredClip = dynamic_cast<te::WaveAudioClip*> (session.getTrack (track)->getClips()[0]);
    REQUIRE (restoredClip != nullptr);
    CHECK (restoredClip->getOriginalFile() == wavFile);

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

        // Wait (bounded) until the constant input is genuinely monitored at the
        // output before rolling. This test's whole premise is "the count-in click
        // adds energy on top of the *constant input*": if the engine's async
        // device-list rebuild (see waitForDeviceManagerIdle) has not wired the
        // input track's live node yet, the pass renders the click with no input
        // and the peak comparison silently compares two different things. The
        // probe makes the premise hold deterministically — and, because it now
        // asserts the input is live, it is stricter than before, not weaker.
        auto monitoredInputIsAudible = [&]
        {
            for (int i = 0; i < 300; ++i)
            {
                const auto probe = player->process (input);

                if (probe.getMagnitude (0, probe.getNumSamples()) > 0.4f)
                    return true;

                audio.deviceManager().dispatchPendingUpdates();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            }

            return false;
        };

        REQUIRE_MESSAGE (monitoredInputIsAudible(),
                         "the hosted device's constant input never reached the output "
                         "(live monitoring was not wired before recording)");

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

//==============================================================================
// BUG B (owner report): the Arm/Monitor toggle chips above the timeline drew two
// stacked outlines at once — the green Monitor/on border *plus* the orange
// accent keyboard-focus ring — which read as the Arm and Monitor states being on
// together. Every chip must resolve to exactly ONE border, in its own state
// colour, and focus must not introduce a second, differently-coloured outline.
TEST_CASE ("toggle chips resolve to one state-coloured outline, never stacked (BUG B)")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    BrandButton monitor ("Monitor", BrandButton::Style::Chip);
    monitor.setOnColours (brand::success.withAlpha (0.25f), brand::success, brand::success);
    monitor.setSize (72, 36);

    // --- Off -----------------------------------------------------------------
    monitor.setToggleState (false, juce::dontSendNotification);

    const auto off = monitor.getAppearance (false, false, false);
    CHECK (off.borderColour == brand::border);
    CHECK (off.textColour == brand::textSecondary);
    CHECK (off.borderColour != brand::accent);

    // Focus may not paint the accent orange on a Monitor chip: that is exactly
    // the "Arm + Monitor both outlined" report.
    const auto offFocused = monitor.getAppearance (false, false, true);
    CHECK (offFocused.borderColour != brand::accent);
    CHECK (offFocused.borderColour == off.borderColour);
    CHECK (offFocused.borderThickness > off.borderThickness);

    // --- On ------------------------------------------------------------------
    monitor.setToggleState (true, juce::dontSendNotification);

    const auto on = monitor.getAppearance (false, false, false);
    CHECK (on.borderColour == brand::success);
    CHECK (on.textColour == brand::success);

    // Focus thickens the SAME green border; it does not add a second outline.
    const auto onFocused = monitor.getAppearance (false, false, true);
    CHECK (onFocused.borderColour == brand::success);
    CHECK (onFocused.borderColour != brand::accent);
    CHECK (onFocused.borderThickness > on.borderThickness);

    // --- Arm must stay visually distinct from Monitor ------------------------
    BrandButton arm ("Arm", BrandButton::Style::Chip);
    arm.setOnColours (brand::accentMuted, brand::accent, brand::accent);
    arm.setSize (72, 36);
    arm.setToggleState (true, juce::dontSendNotification);

    const auto armOn = arm.getAppearance (false, false, false);
    CHECK (armOn.borderColour == brand::accent);
    CHECK (armOn.borderColour != on.borderColour); // orange (Arm) vs green (Monitor)

    // --- A disabled button is inert and never stacks -------------------------
    monitor.setEnabled (false);
    const auto disabled = monitor.getAppearance (false, false, true);
    CHECK (disabled.borderColour == brand::border);
    CHECK (disabled.textColour == brand::textDisabled);
    CHECK_FALSE (disabled.useGradient);
}


//==============================================================================
// Owner bug: deleting an imported "minus" clip left a track behind that could
// not be removed and blocked adding a new minus. The Session side of the fix is
// that an empty track is a first-class removable track and importing again
// afterwards works; the UI now lets the empty lane be clicked to select the
// track so Remove track targets it (see MainComponent).
TEST_CASE ("deleting a track's last clip keeps the track removable and import works again")
{
    auto dir = scratchDirectory ("remove-empty-track");
    auto wav = writeSineWav (dir.getChildFile ("minus.wav"), 48000.0, 0.25);
    REQUIRE (wav.existsAsFile());

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("RemoveEmpty.tracktionedit")));
    REQUIRE (session.getNumAudioTracks() == 1); // the input track

    REQUIRE (session.importAudioFile (wav));
    REQUIRE (session.getNumAudioTracks() == 2);
    const int minusTrack = session.getNumAudioTracks() - 1;
    REQUIRE (session.getClips (minusTrack).size() == 1);

    // Delete the only clip: the track remains (empty), and is still removable.
    const auto clipIndex = session.getClips (minusTrack)[0].clipIndex;
    REQUIRE (session.deleteClip (minusTrack, clipIndex));
    CHECK (session.getClips (minusTrack).empty());
    REQUIRE (session.getNumAudioTracks() == 2);

    REQUIRE (session.removeAudioTrack (minusTrack));
    CHECK (session.getNumAudioTracks() == 1);

    // Importing a fresh backing track after the cleanup still succeeds.
    REQUIRE (session.importAudioFile (wav));
    CHECK (session.getNumAudioTracks() == 2);
    CHECK (session.getClips (session.getNumAudioTracks() - 1).size() == 1);

    session.close();
}

//==============================================================================
// Owner bug: inserting an instrument (DLSMusicDevice) or a panner (HRTFPanner)
// as an insert effect killed the track. Effects-only: those are rejected before
// the plugin joins the chain, the track's audio is untouched, and ordinary
// effects still insert.
TEST_CASE ("insert-plugin rejects instruments/non-effects and leaves the track audio intact")
{
    auto dir = scratchDirectory ("insert-filter");
    auto tone = writeSineWav (dir.getChildFile ("tone.wav"), 48000.0, 0.5);
    REQUIRE (tone.existsAsFile());

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("InsertFilter.tracktionedit")));
    REQUIRE (session.importAudioFile (tone));
    const int trackIndex = session.getNumAudioTracks() - 1;
    REQUIRE (trackIndex >= 1);

    auto renderPeak = [&] (const juce::File& dest)
    {
        dest.deleteFile();
        std::atomic<bool> finished { false };
        bool succeeded = false;
        juce::String error;

        auto handle = WavExport::start (*session.getEdit(), dest,
                                        [&] (bool ok, juce::File, juce::String message)
                                        {
                                            succeeded = ok;
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

    const auto before = renderPeak (dir.getChildFile ("before.wav"));
    REQUIRE (before > 0.1f);

    // DLSMusicDevice-like instrument: JUCE sets isInstrument and category "Synth".
    juce::PluginDescription instrument;
    instrument.name = "DLSMusicDevice";
    instrument.pluginFormatName = "AudioUnit";
    instrument.fileOrIdentifier = "AudioUnit:Synths/aumu,dls ,appl";
    instrument.isInstrument = true;
    instrument.category = "Synth";
    instrument.numInputChannels = 0;
    instrument.numOutputChannels = 4;

    session.clearLastError();
    CHECK_FALSE (session.insertPlugin (trackIndex, instrument, -1));
    CHECK (session.getLastError().isNotEmpty());
    CHECK (session.getNumPlugins (trackIndex) == 0);

    // HRTFPanner-like panner (an AU effect-typed plugin that is not an insert
    // effect and mangles the chain).
    juce::PluginDescription panner;
    panner.name = "HRTFPanner";
    panner.pluginFormatName = "AudioUnit";
    panner.fileOrIdentifier = "AudioUnit:Panners/aupn,hrtf,appl";
    panner.isInstrument = false;
    panner.category = "Panner";
    panner.numInputChannels = 2;
    panner.numOutputChannels = 2;

    session.clearLastError();
    CHECK_FALSE (session.insertPlugin (trackIndex, panner, -1));
    CHECK (session.getLastError().isNotEmpty());
    CHECK (session.getNumPlugins (trackIndex) == 0);

    // A positive asymmetric layout (1-in / 2-out) is rejected too.
    juce::PluginDescription asymmetric;
    asymmetric.name = "MonoToStereoThing";
    asymmetric.pluginFormatName = "VST3";
    asymmetric.fileOrIdentifier = "rrs_asymmetric";
    asymmetric.category = "Effect";
    asymmetric.numInputChannels = 1;
    asymmetric.numOutputChannels = 2;

    session.clearLastError();
    CHECK_FALSE (session.insertPlugin (trackIndex, asymmetric, -1));
    CHECK (session.getLastError().isNotEmpty());
    CHECK (session.getNumPlugins (trackIndex) == 0);

    // The rejected inserts never touched the track: its clip survives and it
    // still renders the same audio.
    CHECK (session.getClips (trackIndex).size() == 1);
    const auto after = renderPeak (dir.getChildFile ("after.wav"));
    CHECK (after == doctest::Approx (before).epsilon (0.02));

    session.close();
}

//==============================================================================
// Epic 5 — MIDI + virtual instruments (FR-MIDI-1..4).
//
// No MIDI hardware is opened: MIDI capture is exercised with the *hosted* audio
// device's synthetic "MIDI Input", which the engine routes exactly like a
// physical controller. Verification is otherwise headless and offline.
namespace
{
    /** Renders `session`'s edit to `dest` and waits for completion. Returns the
        render's success flag; `error` receives the engine message. */
    bool renderSession (Session& session, const juce::File& dest, juce::String& error)
    {
        std::atomic<bool> finished { false };
        bool succeeded = false;

        auto handle = WavExport::start (*session.getEdit(), dest,
                                        [&] (bool success, juce::File, juce::String message)
                                        {
                                            succeeded = success;
                                            error = message;
                                            finished.store (true);
                                        });

        if (handle == nullptr)
            return false;

        for (int i = 0; i < 800 && ! finished.load(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        handle.reset();
        return finished.load() && succeeded;
    }
}

// FR-MIDI-3: an instrument can be hosted on a MIDI track's instrument slot, and
// the effect insert still rejects instruments (the two slots stay distinct).
TEST_CASE ("Epic 5: instrument slot hosts a built-in instrument on a MIDI track")
{
    auto dir = scratchDirectory ("epic5-instrument");
    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Instr.tracktionedit")));

    const auto midiTrack = session.addMidiTrack ("Synth");
    REQUIRE (midiTrack >= 0);
    CHECK (session.isMidiTrack (midiTrack));
    CHECK (session.getNumMidiTracks() == 1);
    CHECK_FALSE (session.trackHasInstrument (midiTrack));

    REQUIRE (session.insertBuiltInInstrument (midiTrack, "4osc"));
    CHECK (session.trackHasInstrument (midiTrack));

    // A MIDI track is not a wave-input track: impossible to assign it a
    // hardware input (it records MIDI, not audio).
    CHECK_FALSE (session.setTrackInputMapping (midiTrack, InputMapping {}));

    // The effect insert chain still rejects an instrument description.
    juce::PluginDescription instrument;
    instrument.name = "Some Synth";
    instrument.pluginFormatName = "VST3";
    instrument.fileOrIdentifier = "rrs_synth";
    instrument.isInstrument = true;
    instrument.category = "Synth";
    instrument.numInputChannels = 0;
    instrument.numOutputChannels = 2;

    session.clearLastError();
    CHECK_FALSE (session.insertPlugin (midiTrack, instrument, -1));
    CHECK (session.getLastError().isNotEmpty());

    // And the instrument slot rejects an audio effect.
    juce::PluginDescription effect;
    effect.name = "Some Reverb";
    effect.pluginFormatName = "VST3";
    effect.fileOrIdentifier = "rrs_reverb";
    effect.isInstrument = false;
    effect.category = "Fx";
    effect.numInputChannels = 2;
    effect.numOutputChannels = 2;

    session.clearLastError();
    CHECK_FALSE (session.insertInstrumentPlugin (midiTrack, effect));
    CHECK (session.getLastError().isNotEmpty());

    session.close();
}

// FR-MIDI-2: piano-roll edits change the clip's note data (add / move / resize /
// delete) and are undoable, measured through the Session API the piano roll uses.
TEST_CASE ("Epic 5: piano-roll note edits change MIDI note data (undoable)")
{
    auto dir = scratchDirectory ("epic5-pianoroll");
    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Roll.tracktionedit")));

    const auto midiTrack = session.addMidiTrack ("Roll");
    REQUIRE (midiTrack >= 0);

    const auto clipIndex = session.createMidiClip (midiTrack, 0.0, 2.0);
    REQUIRE (clipIndex >= 0);

    Session::MidiClipInfo clipInfo;
    REQUIRE (session.getMidiClipInfo (midiTrack, clipIndex, clipInfo));
    CHECK (clipInfo.lengthSeconds == doctest::Approx (2.0).epsilon (0.01));
    CHECK (clipInfo.numNotes == 0);

    // Add: C4 (60) at beat 0, length 1 beat.
    const auto noteIndex = session.addMidiNote (midiTrack, clipIndex, 60, 0.0, 1.0, 100);
    REQUIRE (noteIndex >= 0);

    auto notes = session.getMidiNotes (midiTrack, clipIndex);
    REQUIRE (notes.size() == 1);
    CHECK (notes[0].noteNumber == 60);
    CHECK (notes[0].startBeats == doctest::Approx (0.0).epsilon (0.001));
    CHECK (notes[0].lengthBeats == doctest::Approx (1.0).epsilon (0.001));
    CHECK (notes[0].velocity == 100);

    // Move: pitch up to E4 (64) and start at beat 0.5.
    REQUIRE (session.moveMidiNote (midiTrack, clipIndex, 0, 64, 0.5));
    notes = session.getMidiNotes (midiTrack, clipIndex);
    REQUIRE (notes.size() == 1);
    CHECK (notes[0].noteNumber == 64);
    CHECK (notes[0].startBeats == doctest::Approx (0.5).epsilon (0.001));

    // Resize: 1.5 beats long.
    REQUIRE (session.resizeMidiNote (midiTrack, clipIndex, 0, 1.5));
    notes = session.getMidiNotes (midiTrack, clipIndex);
    REQUIRE (notes.size() == 1);
    CHECK (notes[0].lengthBeats == doctest::Approx (1.5).epsilon (0.001));

    // Undo the resize restores the previous length.
    REQUIRE (session.undo());
    notes = session.getMidiNotes (midiTrack, clipIndex);
    REQUIRE (notes.size() == 1);
    CHECK (notes[0].lengthBeats == doctest::Approx (1.0).epsilon (0.001));

    // Delete removes it.
    REQUIRE (session.removeMidiNote (midiTrack, clipIndex, 0));
    CHECK (session.getMidiNotes (midiTrack, clipIndex).empty());

    session.close();
}

// FR-MIDI-1 / FR-MIDI-4: record MIDI from a (synthetic) controller into a clip
// on the armed MIDI track. The hosted audio device's "MIDI Input" behaves
// exactly like a hardware controller; messages are injected block by block.
TEST_CASE ("Epic 5: MIDI input records into a clip on the armed track")
{
    auto dir = scratchDirectory ("epic5-midi-record");
    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Rec.tracktionedit")));

    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 2;
    params.outputChannels = 2;

    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);

    // The engine populates its MIDI device list asynchronously (DeviceManager
    // timer). Create a virtual source (which also exercises the virtual-MIDI
    // path) and pump until the list settles; the hosted audio device's synthetic
    // "MIDI Input" is then available and behaves exactly like a controller.
    REQUIRE (audio.deviceManager().createVirtualMidiDevice ("RRS Test MIDI").wasOk());

    for (int i = 0; i < 400; ++i)
    {
        audio.deviceManager().dispatchPendingUpdates();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        if (session.getMidiInputDeviceNames().contains ("MIDI Input"))
            break;
    }

    const auto midiTrack = session.addMidiTrack ("Rec");
    REQUIRE (midiTrack >= 0);
    REQUIRE (session.insertBuiltInInstrument (midiTrack, "4osc"));

    // The hosted audio device exposes a synthetic MIDI input.
    const auto midiNames = session.getMidiInputDeviceNames();
    INFO ("MIDI inputs: [" << midiNames.joinIntoString (", ") << "]");
    REQUIRE (midiNames.contains ("MIDI Input"));

    REQUIRE (session.setSelectedMidiInput ("MIDI Input"));
    REQUIRE (session.armMidiTrack (midiTrack, true));
    CHECK (session.isTrackArmed (midiTrack));

    // Let the message-thread work that arming and device-list setup queued drain
    // *before* rolling. `armMidiTrack` -> `InputDeviceInstance::setTarget` ->
    // `handleDestinationChange` posts an async `updateRecordingStatus`
    // (tracktion_InputDevice.cpp:247); if it runs while the take is rolling it
    // stops the transport recording and the take is discarded (the observed
    // "MIDI clips: 0, notes: 0"). The same applies to a pending device-list
    // rebuild (see waitForDeviceManagerIdle): it calls
    // `clearAllContextDevices()`, which *discards* in-flight recordings
    // (tracktion_MidiInputDevice.cpp:1102). Waiting for the engine to go quiet —
    // and for the routing to still hold afterwards — removes the race.
    REQUIRE_MESSAGE (waitForDeviceManagerIdle (audio),
                     "the engine never settled before the MIDI take");
    REQUIRE (session.isTrackArmed (midiTrack));

    session.setPositionSeconds (0.0);
    REQUIRE (session.record());
    CHECK (session.isRecording());

    // Inject a held note across half a second of audio blocks.
    const int totalFrames = 24000; // 0.5 s @ 48 kHz
    int played = 0;

    while (played < totalFrames)
    {
        const int n = juce::jmin (params.blockSize, totalFrames - played);

        juce::AudioBuffer<float> input (params.inputChannels, n);
        input.clear();

        juce::MidiBuffer midi;

        if (played == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);

        if (played + n >= totalFrames)
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), n - 1);

        player->process (input, midi);

        if ((played % 4096) == 0)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);

        played += n;
    }

    session.stop();

    for (int i = 0; i < 60; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);

    int totalNotes = 0;

    for (const auto& clip : session.getMidiClips (midiTrack))
        totalNotes += (int) session.getMidiNotes (midiTrack, clip.clipIndex).size();

    INFO ("MIDI clips: " << (int) session.getMidiClips (midiTrack).size()
                         << ", notes: " << totalNotes);
    CHECK_FALSE (session.getMidiClips (midiTrack).empty());
    CHECK (totalNotes >= 1);

    session.close();
}

// FR-MIDI-3 + FR-EXP-1: a MIDI track rendered through the hosted instrument is
// non-silent, and MIDI + audio coexist and export correctly in one session.
TEST_CASE ("Epic 5: MIDI + audio coexist and export correctly through an instrument")
{
    auto dir = scratchDirectory ("epic5-export");

    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Mix.tracktionedit")));

    // Audio track: a 440 Hz sine.
    auto wavFile = writeSineWav (dir.getChildFile ("tone.wav"), 48000.0, 1.0, 0.5f, 440.0);
    REQUIRE (wavFile.existsAsFile());
    REQUIRE (session.importAudioFile (wavFile));

    // MIDI track: the built-in synth playing two notes.
    const auto midiTrack = session.addMidiTrack ("Synth");
    REQUIRE (midiTrack >= 0);
    REQUIRE (session.insertBuiltInInstrument (midiTrack, "4osc"));

    const auto clipIndex = session.createMidiClip (midiTrack, 0.0, 1.0);
    REQUIRE (clipIndex >= 0);
    REQUIRE (session.addMidiNote (midiTrack, clipIndex, 60, 0.0, 0.5, 127) >= 0);
    REQUIRE (session.addMidiNote (midiTrack, clipIndex, 67, 0.5, 0.5, 127) >= 0);

    // Render the combined session.
    auto mixed = dir.getChildFile ("mixed.wav");
    juce::String error;
    REQUIRE (renderSession (session, mixed, error));
    INFO ("render error: " << error);

    const auto mixedPeak = readWavPeak (mixed);
    CHECK (mixedPeak > 0.05f);

    // Mute every audio track: the render must still be non-silent (the MIDI
    // track through its instrument) but measurably quieter than the combined mix
    // (compared by RMS: the mix peaks/clips, so peak hides the difference), so
    // both sources are genuinely in the export.
    for (int i = 0; i < session.getNumAudioTracks(); ++i)
        if (i != midiTrack)
            REQUIRE (session.setTrackMute (i, true));

    auto midiOnly = dir.getChildFile ("midi_only.wav");
    REQUIRE (renderSession (session, midiOnly, error));

    const auto mixedRms = readWavRms (mixed);
    const auto midiOnlyRms = readWavRms (midiOnly);
    INFO ("mixed peak: " << mixedPeak << ", RMS: " << mixedRms
                         << ", MIDI-only RMS: " << midiOnlyRms);
    CHECK (midiOnlyRms > 0.001);        // the MIDI track renders through the instrument
    CHECK (midiOnlyRms < mixedRms);     // removing the audio source reduced the level

    session.close();
}

// Epic 5 instrument-insert defect: hosting an instrument on an INPUT track must
// NOT convert it to MIDI (the old behaviour detached the hardware input, turned
// MonitorMode::off and disabled the wave device, so the mic went dead and the
// Monitor button could not revive it). A separate MIDI track is auto-created for
// the instrument; the input track's marker, mapping, device and monitoring stay
// completely intact (measured through the live hosted device).
TEST_CASE ("Epic 5: instrument insert on an input track spawns a MIDI track and preserves monitoring")
{
    auto dir = scratchDirectory ("epic5-input-instrument");
    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("InputInstr.tracktionedit")));

    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 2;
    params.outputChannels = 2;

    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);
    REQUIRE (player != nullptr);

    auto& dm = audio.deviceManager();

    for (int i = 0; i < 300; ++i)
    {
        dm.dispatchPendingUpdates();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        if (session.isInputConfigured())
            break;

        session.reconfigureInputs();
    }

    REQUIRE (session.isInputConfigured());
    REQUIRE (session.getInputTrackIndices().contains (0));
    REQUIRE (session.isMonitoringEnabled());

    auto* waveInBefore = dm.getDefaultWaveInDevice();
    REQUIRE (waveInBefore != nullptr);
    REQUIRE (waveInBefore->isEnabled());

    const auto mappingBefore = session.getTrackInputMapping (0);
    const int tracksBefore = session.getNumAudioTracks();

    REQUIRE (session.insertBuiltInInstrument (0, "4osc"));

    // The instrument landed on a new MIDI track, not on the input track.
    CHECK (session.lastInstrumentInsertCreatedTrack());
    const auto midiTrack = session.getLastInstrumentInsertTrack();
    REQUIRE (midiTrack == tracksBefore);
    CHECK (session.getNumAudioTracks() == tracksBefore + 1);
    CHECK (session.isMidiTrack (midiTrack));
    CHECK (session.trackHasInstrument (midiTrack));

    // The input track is untouched: still an input track with the same mapping,
    // never converted to MIDI and with no restore marker.
    CHECK (session.getInputTrackIndices().contains (0));
    CHECK_FALSE (session.isMidiTrack (0));
    CHECK_FALSE (session.trackWasConvertedFromInput (0));
    REQUIRE (session.getTrack (0) != nullptr);
    CHECK (session.getTrack (0)->state.hasProperty (juce::Identifier ("rrsInputTrack")));

    const auto mappingAfter = session.getTrackInputMapping (0);
    CHECK (mappingAfter.firstChannel == mappingBefore.firstChannel);
    CHECK (mappingAfter.numChannels == mappingBefore.numChannels);

    // Monitoring and the wave device are still live — the measured regression.
    CHECK (session.isInputConfigured());
    CHECK (session.isMonitoringEnabled());
    dm.dispatchPendingUpdates();
    auto* waveInAfter = dm.getDefaultWaveInDevice();
    REQUIRE (waveInAfter != nullptr);
    CHECK (waveInAfter->isEnabled());
    CHECK (waveInAfter->getMonitorMode() != te::InputDevice::MonitorMode::off);

    // A second instrument on the same MIDI track is still rejected (one per track).
    session.clearLastError();
    CHECK_FALSE (session.insertBuiltInInstrument (midiTrack, "4osc"));
    CHECK (session.getLastError().isNotEmpty());

    session.close();
}

// Epic 5 instrument-insert review follow-up: when plugin creation fails there
// must be no empty auto-created MIDI track left behind. An unknown built-in type
// makes `createNewPlugin` return null *after* the target track was resolved.
TEST_CASE ("Epic 5: a failed instrument create leaves no empty MIDI track")
{
    auto dir = scratchDirectory ("epic5-insert-failure");
    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("InsertFail.tracktionedit")));

    REQUIRE (session.getInputTrackIndices().contains (0));
    const auto tracksBefore = session.getNumAudioTracks();

    session.clearLastError();
    CHECK_FALSE (session.insertBuiltInInstrument (0, "not-a-real-instrument"));
    CHECK (session.getLastError().isNotEmpty());

    // The auto-created MIDI track is gone again and the input track is intact.
    CHECK (session.getNumAudioTracks() == tracksBefore);
    CHECK (session.getInputTrackIndices().contains (0));
    CHECK_FALSE (session.lastInstrumentInsertCreatedTrack());
    CHECK (session.getLastInstrumentInsertTrack() < 0);

    session.close();
}

// The plugin browser's Track combo follows the timeline selection instead of
// defaulting to track 0 (the mic input) — the root cause that made an instrument
// insert naturally hit the input track. `selectedTrackProvider` is what
// MainComponent wires to the timeline's selected track.
TEST_CASE ("Epic 5: plugin browser target track follows the timeline selection")
{
    auto dir = scratchDirectory ("epic5-browser-target");
    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("BrowserTarget.tracktionedit")));

    // Track 0 = input, track 1 = a MIDI track.
    REQUIRE (session.getInputTrackIndices().contains (0));
    const auto midiTrack = session.addMidiTrack ("Browser Sync");
    REQUIRE (midiTrack == 1);

    PluginHost host (audio.engine());
    PluginBrowser browser (host, session);
    browser.setSize (780, 520);
    CHECK (browser.getTargetTrackIndex() >= 0);

    // Timeline selection is authoritative on refresh (not track 0 by default).
    int selected = midiTrack;
    browser.selectedTrackProvider = [&selected] { return selected; };
    browser.refresh();
    CHECK (browser.getTargetTrackIndex() == midiTrack);

    selected = 0;
    browser.refresh();
    CHECK (browser.getTargetTrackIndex() == 0);

    // Explicit sync (what MainComponent does when the lane is clicked) also works.
    browser.setTargetTrack (midiTrack);
    CHECK (browser.getTargetTrackIndex() == midiTrack);

    session.close();
}

// A track converted from an input track remembers how to restore itself: removing
// the hosted instrument returns the input marker, mapping and monitoring, so the
// mic recovers (the fix for the currently-broken session).
TEST_CASE ("Epic 5: removing an instrument from a converted track restores the input")
{
    auto dir = scratchDirectory ("epic5-restore-input");
    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Restore.tracktionedit")));

    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 2;
    params.outputChannels = 2;

    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);
    auto& dm = audio.deviceManager();

    for (int i = 0; i < 300; ++i)
    {
        dm.dispatchPendingUpdates();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        if (session.isInputConfigured())
            break;

        session.reconfigureInputs();
    }

    REQUIRE (session.isInputConfigured());
    REQUIRE (session.getInputTrackIndices().contains (0));
    const auto mappingBefore = session.getTrackInputMapping (0);

    // Convert the input track and host an instrument on it (the legacy/fallback
    // path). The conversion records a restore marker with the original mapping.
    REQUIRE (session.convertTrackToMidi (0));
    CHECK (session.trackWasConvertedFromInput (0));
    CHECK (session.isMidiTrack (0));
    CHECK_FALSE (session.getInputTrackIndices().contains (0));

    REQUIRE (session.insertBuiltInInstrument (0, "4osc"));
    CHECK (session.trackHasInstrument (0));
    // Inserting on an already-MIDI track does not spawn another track.
    CHECK_FALSE (session.lastInstrumentInsertCreatedTrack());

    // Remove the instrument: the track returns to its input state and monitoring
    // comes back.
    REQUIRE (session.removeInstrument (0));
    CHECK_FALSE (session.trackHasInstrument (0));
    CHECK_FALSE (session.isMidiTrack (0));
    CHECK_FALSE (session.trackWasConvertedFromInput (0));
    CHECK (session.getInputTrackIndices().contains (0));

    const auto mappingAfter = session.getTrackInputMapping (0);
    CHECK (mappingAfter.firstChannel == mappingBefore.firstChannel);
    CHECK (mappingAfter.numChannels == mappingBefore.numChannels);

    dm.dispatchPendingUpdates();
    CHECK (session.isMonitoringEnabled());

    session.close();
}

// Epic 5 requirement 6 / session recovery: a session whose first track is a MIDI
// track and that carries no input track (no input marker) must not open with a
// dead mic. `configureTracks` appends a fresh input track ("Input 1") and keeps
// monitoring enabled.
TEST_CASE ("Epic 5: opening a session with no input track recovers a fresh Input 1")
{
    auto dir = scratchDirectory ("epic5-input-recovery");
    auto editFile = dir.getChildFile ("NoInput.tracktionedit");

    AudioEngine audio (false);

    // Build the at-risk state: track 0 is a MIDI track and no input track/marker
    // exists (what a legacy or hand-edited project can carry).
    {
        Session session (audio);
        REQUIRE (session.createNew (editFile));
        REQUIRE (session.getInputTrackIndices().contains (0));

        REQUIRE (session.convertTrackToMidi (0));
        CHECK (session.isMidiTrack (0));
        CHECK_FALSE (session.getInputTrackIndices().contains (0));
        session.close();
    }

    Session reopened (audio);
    REQUIRE (reopened.open (editFile));

    // The MIDI track is preserved; the missing input path is recovered.
    REQUIRE (reopened.isMidiTrack (0));
    CHECK (reopened.getNumAudioTracks() == 2);

    const auto inputs = reopened.getInputTrackIndices();
    REQUIRE (inputs.size() == 1);
    const auto recoveryInput = inputs[0];
    CHECK (recoveryInput == reopened.getNumAudioTracks() - 1);
    CHECK (reopened.getTrackName (recoveryInput) == "Input 1");
    CHECK_FALSE (reopened.isMidiTrack (recoveryInput));

    // Monitoring is re-established on the recovered input once a device exists.
    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 2;
    params.outputChannels = 2;

    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);
    auto& dm = audio.deviceManager();

    for (int i = 0; i < 300; ++i)
    {
        dm.dispatchPendingUpdates();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        if (reopened.isInputConfigured())
            break;

        reopened.reconfigureInputs();
    }

    REQUIRE (reopened.isInputConfigured());
    CHECK (reopened.isMonitoringEnabled());

    dm.dispatchPendingUpdates();
    auto* waveIn = dm.getDefaultWaveInDevice();
    REQUIRE (waveIn != nullptr);
    CHECK (waveIn->isEnabled());
    CHECK (waveIn->getMonitorMode() != te::InputDevice::MonitorMode::off);

    reopened.close();
}

// Epic 5 QA BUG 6: piano-roll note edits are clamped to the clip bounds — a note
// asked to run or move past the clip end is pulled back so it still sounds.
TEST_CASE ("Epic 5: piano-roll note edits are clamped to the clip bounds")
{
    auto dir = scratchDirectory ("epic5-clamp");
    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Clamp.tracktionedit")));

    const auto midiTrack = session.addMidiTrack ("Clamp");
    REQUIRE (midiTrack >= 0);

    const auto clipIndex = session.createMidiClip (midiTrack, 0.0, 2.0);
    REQUIRE (clipIndex >= 0);

    Session::MidiClipInfo info;
    REQUIRE (session.getMidiClipInfo (midiTrack, clipIndex, info));
    const auto beatsPerSecond =
        session.getEdit()->tempoSequence.getBeatsPerSecondAt (te::TimePosition::fromSeconds (0.0));
    const auto clipBeats = info.lengthSeconds * beatsPerSecond;
    REQUIRE (clipBeats > 1.0);

    // A note asked to run far past the clip end is shortened to fit.
    const auto noteIndex = session.addMidiNote (midiTrack, clipIndex, 60, clipBeats - 0.5, 100.0, 100);
    REQUIRE (noteIndex >= 0);

    auto notes = session.getMidiNotes (midiTrack, clipIndex);
    REQUIRE (notes.size() == 1);
    CHECK (notes[0].startBeats + notes[0].lengthBeats <= clipBeats + 0.01);

    // Moving it past the end keeps it inside the clip.
    REQUIRE (session.moveMidiNote (midiTrack, clipIndex, 0, 62, 1000.0));
    notes = session.getMidiNotes (midiTrack, clipIndex);
    REQUIRE (notes.size() == 1);
    CHECK (notes[0].startBeats + notes[0].lengthBeats <= clipBeats + 0.01);

    // Resizing it past the end is clamped too.
    REQUIRE (session.resizeMidiNote (midiTrack, clipIndex, 0, 100.0));
    notes = session.getMidiNotes (midiTrack, clipIndex);
    REQUIRE (notes.size() == 1);
    CHECK (notes[0].startBeats + notes[0].lengthBeats <= clipBeats + 0.01);

    session.close();
}

// Epic 5 QA BUG 4: switching the selected MIDI input clears the previous device's
// routing, so an armed MIDI track does not keep listening to the old controller.
TEST_CASE ("Epic 5: switching MIDI input clears the previous device routing")
{
    auto dir = scratchDirectory ("epic5-midi-switch");
    AudioEngine audio (false);
    Session session (audio);
    REQUIRE (session.createNew (dir.getChildFile ("Switch.tracktionedit")));

    te::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = 48000.0;
    params.blockSize = 256;
    params.inputChannels = 2;
    params.outputChannels = 2;

    auto player = std::make_unique<te::test_utilities::EnginePlayer> (audio.engine(), params);

    REQUIRE (audio.deviceManager().createVirtualMidiDevice ("RRS Switch MIDI").wasOk());

    for (int i = 0; i < 400; ++i)
    {
        audio.deviceManager().dispatchPendingUpdates();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        if (session.getMidiInputDeviceNames().contains ("MIDI Input"))
            break;
    }

    const auto midiTrack = session.addMidiTrack ("Switch");
    REQUIRE (midiTrack >= 0);
    REQUIRE (session.insertBuiltInInstrument (midiTrack, "4osc"));

    REQUIRE (session.getMidiInputDeviceNames().contains ("MIDI Input"));
    REQUIRE (session.setSelectedMidiInput ("MIDI Input"));
    REQUIRE (session.armMidiTrack (midiTrack, true));
    CHECK (session.isTrackArmed (midiTrack));

    // Clearing the selection must drop the old device's target + record-enable.
    REQUIRE (session.setSelectedMidiInput ({}) );
    CHECK_FALSE (session.isTrackArmed (midiTrack));

    session.close();
}

//==============================================================================
// Epic 7 (FR-REC-8 / FR-PRJ-3): end-to-end crash recovery.
//
// Simulates a crash mid-record — the startup sentinel is left behind, an autosave
// `.tmp_` survives, and the take captured moments before the crash has a WAV
// header that was never flushed (declares 0 samples) although its payload is on
// disk. Recovery must rebuild that take and import it as a usable clip.
TEST_CASE ("crash recovery rebuilds and imports an orphaned take end-to-end")
{
    const auto sentinel = paths::lockFile();
    const bool sentinelExistedBefore = sentinel.existsAsFile();
    const auto sentinelContentBefore = sentinelExistedBefore ? sentinel.loadFileAsString() : juce::String();

    auto dir = scratchDirectory ("crash-recovery-e2e");
    const auto editFile = dir.getChildFile ("Crash Session.tracktionedit");

    // A real session on disk (one default "Input 1" track).
    {
        AudioEngine audio (false);
        Session session (audio);
        REQUIRE (session.createNew (editFile));
        REQUIRE (editFile.existsAsFile());
    } // <- destructor cleans its own sentinel/temp; we rebuild the crash state below

    // Reconstruct the artifacts a hard crash would leave behind.
    paths::appDataDirectory().createDirectory();
    REQUIRE (sentinel.replaceWithText ("raw-radio-studio crash sentinel\n"));
    REQUIRE (editFile.copyFileTo (paths::tempEditFileFor (editFile)));

    // The take from moments before the crash: valid audio, but a header that
    // still declares 0 samples (no Tracktion flush yet). Named as the engine
    // names takes (`<session>_<track>_Take_<n>.wav`); not referenced by the edit.
    const auto recordingsDir = paths::recordingsDirectoryFor (editFile);
    REQUIRE (recordingsDir.createDirectory());
    const auto orphan = recordingsDir.getChildFile ("Crash Session_Input 1_Take_1.wav");
    REQUIRE (writeSineWav (orphan, 48000.0, 0.5).existsAsFile());
    REQUIRE (zeroLengthWavHeader (orphan));

    // Detection flags the interrupted session and the recoverable take.
    const auto info = Session::detectRecovery (editFile);
    CHECK (info.interrupted());
    CHECK (info.hasTempEdit);
    CHECK (info.candidateRecordings.size() == 1);

    // Restart -> apply the autosave -> reopen -> import the orphan take.
    {
        AudioEngine audio (false);
        Session session (audio);
        REQUIRE (session.applyTempEditRecovery (info));
        REQUIRE (session.open (editFile));

        const auto imported = session.importOrphanedRecordings();
        const auto report = session.getLastRecordingRecovery();
        INFO ("report: " << report.summary());
        CHECK (imported == 1);
        CHECK (report.repaired == 1);
        CHECK (report.skippedFiles.isEmpty());

        auto* track = session.getTrack (0);
        REQUIRE (track != nullptr);
        REQUIRE (track->getClips().size() == 1);

        auto* wave = dynamic_cast<te::WaveAudioClip*> (track->getClips()[0]);
        REQUIRE (wave != nullptr);
        CHECK (wave->getOriginalFile() == orphan);
        CHECK (wave->getName().startsWith ("Recovered:"));
        CHECK (wave->getPosition().getLength().inSeconds() > 0.4);

        session.close();
    }

    if (sentinelExistedBefore)
        sentinel.replaceWithText (sentinelContentBefore);
    else
        sentinel.deleteFile();
}

//==============================================================================
// Epic 7 (FR-PRJ-3): a *clean* shutdown must leave no recovery artifacts behind,
// otherwise the next launch shows a spurious "unsaved version found" prompt for a
// session the user deliberately closed.
TEST_CASE ("a clean Session shutdown leaves no parasitic recovery prompt")
{
    const auto sentinel = paths::lockFile();
    const bool sentinelExistedBefore = sentinel.existsAsFile();
    const auto sentinelContentBefore = sentinelExistedBefore ? sentinel.loadFileAsString() : juce::String();

    auto dir = scratchDirectory ("clean-exit");
    const auto editFile = dir.getChildFile ("Clean Session.tracktionedit");

    {
        AudioEngine audio (false);
        Session session (audio);
        REQUIRE (session.createNew (editFile));

        // While the session is live: sentinel present + an autosave temp version.
        CHECK (sentinel.existsAsFile());

        {
            te::EditFileOperations ops (*session.getEdit());
            REQUIRE (ops.saveTempVersion (true));
        }

        CHECK (paths::tempEditFileFor (editFile).existsAsFile());
    } // <- clean shutdown (destructor)

    CHECK_FALSE (paths::tempEditFileFor (editFile).existsAsFile());
    CHECK_FALSE (sentinel.existsAsFile());

    const auto info = Session::detectRecovery (editFile);
    CHECK_FALSE (info.hasTempEdit);
    CHECK_FALSE (info.uncleanShutdown);
    CHECK_FALSE (info.interrupted());
    CHECK_FALSE (info.hasRecoverables());

    if (sentinelExistedBefore)
        sentinel.replaceWithText (sentinelContentBefore);
}

//==============================================================================
// Epic 7 (FR-REC-8): recovered takes are routed back to the track named in their
// file name instead of all being dumped on track 0.
TEST_CASE ("recovered takes are routed to the track named in their file name")
{
    const auto sentinel = paths::lockFile();
    const bool sentinelExistedBefore = sentinel.existsAsFile();
    const auto sentinelContentBefore = sentinelExistedBefore ? sentinel.loadFileAsString() : juce::String();

    auto dir = scratchDirectory ("recovery-routing");
    const auto editFile = dir.getChildFile ("Routing Session.tracktionedit");

    {
        AudioEngine audio (false);
        Session session (audio);
        REQUIRE (session.createNew (editFile));
        REQUIRE (session.setTrackName (0, "Input 1"));
        session.getEdit()->ensureNumberOfAudioTracks (2);
        REQUIRE (session.setTrackName (1, "Guitar"));
        REQUIRE (session.getNumAudioTracks() == 2);

        const auto recordingsDir = paths::recordingsDirectoryFor (editFile);
        REQUIRE (recordingsDir.createDirectory());

        const auto take1 = recordingsDir.getChildFile ("Routing Session_Input 1_Take_1.wav");
        const auto take2 = recordingsDir.getChildFile ("Routing Session_Guitar_Take_1.wav");
        REQUIRE (writeSineWav (take1, 48000.0, 0.2, 0.5f, 220.0).existsAsFile());
        REQUIRE (writeSineWav (take2, 48000.0, 0.2, 0.5f, 880.0).existsAsFile());

        const auto imported = session.importOrphanedRecordings();
        INFO ("report: " << session.getLastRecordingRecovery().summary());
        CHECK (imported == 2);

        auto* inputTrack = session.getTrack (0);
        auto* guitarTrack = session.getTrack (1);
        REQUIRE (inputTrack != nullptr);
        REQUIRE (guitarTrack != nullptr);
        REQUIRE (inputTrack->getClips().size() == 1);
        REQUIRE (guitarTrack->getClips().size() == 1);

        auto* inputClip = dynamic_cast<te::WaveAudioClip*> (inputTrack->getClips()[0]);
        auto* guitarClip = dynamic_cast<te::WaveAudioClip*> (guitarTrack->getClips()[0]);
        REQUIRE (inputClip != nullptr);
        REQUIRE (guitarClip != nullptr);
        CHECK (inputClip->getOriginalFile() == take1);
        CHECK (guitarClip->getOriginalFile() == take2);

        session.close();
    }

    if (sentinelExistedBefore)
        sentinel.replaceWithText (sentinelContentBefore);
    else
        sentinel.deleteFile();
}

//==============================================================================
// Epic 7 (NFR-P-5): the only audio-thread function we own must be allocation-free.
// A global `operator new` probe counts allocations strictly inside the callback.
TEST_CASE ("RT-safety: the input-callback level meter performs no allocations")
{
    AudioEngine audio (false);
    Session session (audio);

    auto dir = scratchDirectory ("rt-safety");
    REQUIRE (session.createNew (dir.getChildFile ("RT.tracktionedit")));

    static float left[512];
    static float right[512];

    for (int i = 0; i < 512; ++i)
    {
        left[i]  = 0.25f;
        right[i] = -0.25f;
    }

    float* channels[2] = { left, right };
    const auto view = choc::buffer::createChannelArrayView<float> (channels, 2, 512);

    // Warm up once (any one-time lazy setup), then arm the probe.
    session.acceptInputBuffer (view);

    rrs_rt_probe::allocations.store (0, std::memory_order_relaxed);
    rrs_rt_probe::armed.store (true, std::memory_order_relaxed);

    for (int i = 0; i < 100000; ++i)
        session.acceptInputBuffer (view);

    rrs_rt_probe::armed.store (false, std::memory_order_relaxed);

    INFO ("allocations on the audio-callback path: " << rrs_rt_probe::allocations.load());
    CHECK (rrs_rt_probe::allocations.load() == 0);

    // The meter really consumed the block (proves we measured the right path).
    CHECK (session.getInputLevels().peak[0].load() > 0.2f);
}


