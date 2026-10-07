// raw-radio-studio — Epic 1 unit tests (doctest).
//
// Scope: pure/testable logic only. Hardware-dependent behaviour (recording,
// monitoring) is verified manually — see the Epic 1 report.
//
//   * audio-device error classification (FR-MON-5 / NFR-IO-4),
//   * ALSA `hw` device-name policy (NFR-IO-4),
//   * 24-bit WAV writer round-trip (FR-EXP-1 / NFR-A-1).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <JuceHeader.h>

#include "studio/DeviceError.h"
#include "studio/DeviceSelection.h"
#include "studio/AppPaths.h"
#include "studio/InputMapping.h"
#include "ui/BrandFonts.h"
#include "ui/DevicePanelLayout.h"
#include "ui/FaderTaper.h"
#include "ui/IconCache.h"
#include "ui/MeterBallistics.h"

#include <cmath>
#include <memory>

using namespace rrs;

//==============================================================================
namespace
{
    int countNonTransparentPixels (const juce::Image& image)
    {
        int count = 0;
        const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);

        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                if (data.getPixelColour (x, y).getAlpha() > 0)
                    ++count;

        return count;
    }
}

TEST_CASE ("brand icon cache parses and tints the vendored Material SVGs")
{
    auto& cache = IconCache::getInstance();

    // A vendored icon parses, tints and rasterises at 2x the requested size.
    const auto icon = cache.getIconImage ("note_add", juce::Colours::red, 16);
    CHECK (icon.isValid());
    CHECK (icon.getWidth() == 32);
    CHECK (icon.getHeight() == 32);

    // ...and actually contains drawn (tinted) pixels, not an empty raster.
    CHECK (countNonTransparentPixels (icon) > 0);

    // Every vendored resource must resolve — guards against a renamed file
    // silently yielding a blank button. These are the Material Icons (Outlined)
    // names now used by the transport / action rows.
    for (const auto* name : { "note_add", "folder_open", "save", "save_as", "upload_file",
                              "download", "close", "settings", "info", "radio_button_checked",
                              "headphones", "fiber_manual_record", "play_arrow", "pause", "stop",
                              "skip_previous", "timer", "add", "delete", "auto_fix_high",
                              "refresh" })
    {
        const auto rendered = cache.getIconImage (name, juce::Colours::white, 16);
        CHECK (rendered.isValid());
        CHECK (countNonTransparentPixels (rendered) > 0);
    }

    // Unknown names are handled gracefully (no crash, null image).
    CHECK_FALSE (cache.getIconImage ("not-a-real-icon", juce::Colours::white, 16).isValid());
}

//==============================================================================
TEST_CASE ("brand fonts register the vendored Inter + JetBrains Mono typefaces")
{
    using namespace rrs::brand;

    struct Case
    {
        FontFamily family;
        FontWeight weight;
        const char* familySubstring;
    };

    const Case cases[] =
    {
        { FontFamily::Inter,         FontWeight::Regular,  "Inter" },
        { FontFamily::Inter,         FontWeight::Medium,   "Inter" },
        { FontFamily::Inter,         FontWeight::SemiBold, "Inter" },
        { FontFamily::Inter,         FontWeight::Bold,     "Inter" },
        { FontFamily::JetBrainsMono, FontWeight::Regular,  "JetBrains Mono" },
        { FontFamily::JetBrainsMono, FontWeight::Medium,   "JetBrains Mono" },
    };

    for (const auto& c : cases)
    {
        const auto typeface = getTypeface (c.family, c.weight);
        REQUIRE (typeface != nullptr);
        CHECK (typeface->getName().containsIgnoreCase (c.familySubstring));

        // The Font built for a role must actually resolve to the vendored face
        // (not silently fall back to the default sans).
        const auto resolved = font (c.family, c.weight, 14.0f).getTypefacePtr();
        REQUIRE (resolved != nullptr);
        CHECK (resolved->getName().containsIgnoreCase (c.familySubstring));
    }
}

//==============================================================================
TEST_CASE ("device error: busy / EBUSY is classified and actionable")
{
    const auto info = classifyDeviceError ("ALSA: Cannot open device: Device or resource busy (EBUSY)");

    CHECK (info.isBusy);
    CHECK_FALSE (info.isMissing);
    CHECK (info.userMessage.isNotEmpty());
    CHECK (info.userMessage.containsIgnoreCase ("busy"));
    CHECK (info.raw.isNotEmpty());
}

TEST_CASE ("device error: missing device is classified")
{
    const auto info = classifyDeviceError ("No such device: hw:9,0");

    CHECK (info.isMissing);
    CHECK_FALSE (info.isBusy);
    CHECK (info.userMessage.isNotEmpty());
}

TEST_CASE ("device error: unknown failure keeps the raw message")
{
    constexpr const char* raw = "Some unexpected backend failure";

    const auto info = classifyDeviceError (raw);

    CHECK_FALSE (info.isBusy);
    CHECK_FALSE (info.isMissing);
    CHECK (info.userMessage == juce::String (raw));
}

TEST_CASE ("ALSA device policy excludes plugin/routed PCMs and keeps raw hardware")
{
    // JUCE's ALSA backend reports real hardware by human-readable name, never as
    // an "hw:N,M" id. This includes the analog card itself plus the `hw:` /
    // `front:` / `surround:` PCM variants; all must be accepted (NFR-IO-4).
    CHECK_FALSE (isAlsaPluginPseudoDeviceName ("HDA Intel PCH, ALC295 Analog"));
    CHECK_FALSE (isAlsaPluginPseudoDeviceName ("USB Audio Device, USB Audio"));
    CHECK_FALSE (isAlsaPluginPseudoDeviceName ("Focusrite Scarlett 4i4 4th Gen, USB Audio"));
    CHECK_FALSE (isAlsaPluginPseudoDeviceName ("HDA Intel PCH, ALC295 Analog {hw:0,0}"));
    CHECK_FALSE (isAlsaPluginPseudoDeviceName ("HDA Intel PCH, ALC295 Analog; Front output"));
    CHECK_FALSE (isAlsaPluginPseudoDeviceName ("HDA Intel PCH, ALC295 Analog; Surround output"));
    CHECK_FALSE (isAlsaPluginPseudoDeviceName (
        "HDA Intel PCH, ALC295 Analog; Direct hardware device without any conversions"));

    // JUCE-injected plugin pseudo-devices route through PipeWire/PulseAudio and
    // must be rejected so there is no silent fallback.
    CHECK (isAlsaPluginPseudoDeviceName ("Default ALSA Input"));
    CHECK (isAlsaPluginPseudoDeviceName ("Default ALSA Output"));
    CHECK (isAlsaPluginPseudoDeviceName ("Pulseaudio input"));
    CHECK (isAlsaPluginPseudoDeviceName ("Pulseaudio output"));

    // Plugin-server bridge PCMs present on a stock Ubuntu 24.04 (PipeWire) box.
    // Before the fix these passed the denylist and could be auto-selected.
    CHECK (isAlsaPluginPseudoDeviceName ("PipeWire Sound Server"));
    CHECK (isAlsaPluginPseudoDeviceName ("JACK Audio Connection Kit"));
    CHECK (isAlsaPluginPseudoDeviceName ("PulseAudio Sound Server"));

    // Software-mixing PCMs (ALSA `dmix`/`dsnoop`): described by their role,
    // sometimes surfaced by their raw id.
    CHECK (isAlsaPluginPseudoDeviceName (
        "HDA Intel PCH, ALC295 Analog; Direct sample mixing device"));
    CHECK (isAlsaPluginPseudoDeviceName (
        "HDA Intel PCH, ALC295 Analog; Direct sample snooping device"));
    CHECK (isAlsaPluginPseudoDeviceName ("dmix"));
    CHECK (isAlsaPluginPseudoDeviceName ("dsnoop"));

   #if JUCE_LINUX
    // Raw hardware stays selectable for direct recording...
    CHECK (isAcceptableInputDeviceName ("HDA Intel PCH, ALC295 Analog"));
    CHECK (isAcceptableInputDeviceName ("USB Audio Device, USB Audio"));
    CHECK (isAcceptableInputDeviceName (
        "HDA Intel PCH, ALC295 Analog; Direct hardware device without any conversions"));

    // ...but plugin/routed PCMs are rejected from the direct auto-selection, so
    // `enforceDirectAlsaOnStartup()` can never silently pick PipeWire/JACK.
    CHECK_FALSE (isAcceptableInputDeviceName ("Default ALSA Input"));
    CHECK_FALSE (isAcceptableInputDeviceName ("Pulseaudio output"));
    CHECK_FALSE (isAcceptableInputDeviceName ("PipeWire Sound Server"));
    CHECK_FALSE (isAcceptableInputDeviceName ("JACK Audio Connection Kit"));
    CHECK_FALSE (isAcceptableInputDeviceName (
        "HDA Intel PCH, ALC295 Analog; Direct sample mixing device"));
    CHECK_FALSE (isAcceptableInputDeviceName (""));

    // The output side follows the same "no silent plugin fallback" policy.
    CHECK (isAcceptableOutputDeviceName ("HDA Intel PCH, ALC295 Analog"));
    CHECK (isAcceptableOutputDeviceName ("USB Audio Device, USB Audio"));
    CHECK_FALSE (isAcceptableOutputDeviceName ("Default ALSA Output"));
    CHECK_FALSE (isAcceptableOutputDeviceName ("Pulseaudio output"));
    CHECK_FALSE (isAcceptableOutputDeviceName ("PipeWire Sound Server"));
    CHECK_FALSE (isAcceptableOutputDeviceName (""));
   #else
    // Non-Linux backends have no plugin-fallback hazard; the policy is a no-op.
    CHECK (isAcceptableInputDeviceName ("anything on this platform"));
    CHECK (isAcceptableOutputDeviceName ("anything on this platform"));
   #endif
}

//==============================================================================
TEST_CASE ("device panel always gives the status + error labels real height")
{
    // The panel must be tall enough for a multi-line device error (FR-MON-5) AND
    // for the separate input/output device selectors.
    const auto layout = computeDevicePanelLayout ({ 0, 0, 900, devicePanelHeight });

    CHECK (layout.statusLabel.getHeight() > 0);
    CHECK (layout.errorLabel.getHeight() >= 80); // ~4-5 lines at the default font
    CHECK (layout.errorLabel.getY() >= layout.statusLabel.getBottom());
    CHECK (layout.errorLabel.getBottom() <= devicePanelHeight);

    // Regression guard: at the old 170 px height the remaining area was exhausted
    // before the labels, leaving the error label with far less room.
    const auto cramped = computeDevicePanelLayout ({ 0, 0, 900, 170 });
    CHECK (cramped.errorLabel.getHeight() < layout.errorLabel.getHeight());
}

TEST_CASE ("device panel lays out independent input and output selectors")
{
    const auto layout = computeDevicePanelLayout ({ 0, 0, 900, devicePanelHeight });

    // Both selectors exist, are real, and are stacked input-above-output in
    // separate rows (the bug was a single "Device" list).
    CHECK (layout.inputBox.getHeight() > 0);
    CHECK (layout.outputBox.getHeight() > 0);
    CHECK (layout.inputBox.getY() < layout.outputBox.getY());
    CHECK (layout.inputBox.getBottom() <= layout.outputBox.getY());

    CHECK (layout.inputLabel.getWidth() > 0);
    CHECK (layout.outputLabel.getWidth() > 0);

    // ...and the output row is above the rate/buffer row.
    CHECK (layout.outputBox.getBottom() <= layout.rateBox.getY());
}

//==============================================================================
TEST_CASE ("device selection keeps input and output independent")
{
    const juce::String macMic     = "Микрофон MacBook Pro";
    const juce::String macSpeakers = "Динамики MacBook Pro";
    const juce::String usbMic      = "fifine Microphone";

    SUBCASE ("selecting an input-only device keeps the current output")
    {
        const auto sel = resolveDeviceNames (usbMic, {}, macMic, macSpeakers);

        CHECK (sel.input == usbMic);
        // Regression: the input name must NEVER be copied into the output.
        CHECK (sel.output == macSpeakers);
        CHECK (sel.output != usbMic);
    }

    SUBCASE ("selecting an output-only device keeps the current input")
    {
        const auto sel = resolveDeviceNames ({}, "External DAC", macMic, macSpeakers);

        CHECK (sel.input == macMic);
        CHECK (sel.output == "External DAC");
    }

    SUBCASE ("selecting both sets both")
    {
        const auto sel = resolveDeviceNames (usbMic, "External DAC", macMic, macSpeakers);

        CHECK (sel.input == usbMic);
        CHECK (sel.output == "External DAC");
    }

    SUBCASE ("selecting neither keeps the current pair")
    {
        const auto sel = resolveDeviceNames ({}, {}, macMic, macSpeakers);

        CHECK (sel.input == macMic);
        CHECK (sel.output == macSpeakers);
    }

    SUBCASE ("a matching full-duplex name is still allowed on both sides")
    {
        // Bluetooth headsets expose the same name for in and out; selecting it
        // on both sides is legitimate and must not be treated as a mis-copy.
        const juce::String bt = "Bluetooth Headset";
        const auto sel = resolveDeviceNames (bt, bt, macMic, macSpeakers);

        CHECK (sel.input == bt);
        CHECK (sel.output == bt);
    }

    SUBCASE ("a stale current name is passed through unchanged")
    {
        // The resolver has no view of the enumerated device list: it is a pure
        // "empty request keeps current" pass-through. If the current input is
        // stale (unplugged / renamed), it is returned verbatim; validating it
        // against the live list is the caller's job (AudioEngine::
        // applyDeviceSetup), which leaves the working device running instead of
        // tearing it down (FR-MON-5).
        const juce::String stale = "Unplugged USB Interface";

        const auto sel = resolveDeviceNames ({}, {}, stale, macSpeakers);

        CHECK (sel.input == stale);
        CHECK (sel.output == macSpeakers);
    }
}

//==============================================================================
// FR-REC-3 (Epic 2): a track can select any input device channel and a
// mono/stereo/N layout. The pure resolver must produce the right destination
// map for arbitrary channel counts and starting channels.
TEST_CASE ("input mapping resolves arbitrary channel counts and layouts")
{
    SUBCASE ("Auto keeps the Epic 1 behaviour (mono centred, stereo L/R)")
    {
        const auto mono = mappedChannelsFor ({ 0, 1, InputLayout::Auto }, 1);
        REQUIRE (mono.size() == 2);
        CHECK (mono[0].deviceChannel == 0);
        CHECK (mono[1].deviceChannel == 0); // duplicated to both L and R
        CHECK (mono[0].type == juce::AudioChannelSet::left);
        CHECK (mono[1].type == juce::AudioChannelSet::right);

        // 0 channels (no device yet) also defaults to centred, never hard-left.
        const auto none = mappedChannelsFor ({ 0, 1, InputLayout::Auto }, 0);
        CHECK (none[0].deviceChannel == 0);
        CHECK (none[1].deviceChannel == 0);

        const auto stereo = mappedChannelsFor ({ 0, 2, InputLayout::Auto }, 2);
        REQUIRE (stereo.size() == 2);
        CHECK (stereo[0].deviceChannel == 0);
        CHECK (stereo[1].deviceChannel == 1);
    }

    SUBCASE ("Mono picks any hardware channel and centres it")
    {
        // The 3rd input of a 4-in interface (0-based index 2) on a mono track.
        const auto cfg = mappedChannelsFor ({ 2, 1, InputLayout::Mono }, 4);
        REQUIRE (cfg.size() == 2);
        CHECK (cfg[0].deviceChannel == 2);
        CHECK (cfg[1].deviceChannel == 2);
        CHECK (cfg[0].type == juce::AudioChannelSet::left);
        CHECK (cfg[1].type == juce::AudioChannelSet::right);
    }

    SUBCASE ("Stereo picks any adjacent hardware pair")
    {
        const auto cfg = mappedChannelsFor ({ 2, 2, InputLayout::Stereo }, 4);
        REQUIRE (cfg.size() == 2);
        CHECK (cfg[0].deviceChannel == 2);
        CHECK (cfg[1].deviceChannel == 3);
    }

    SUBCASE ("Stereo on a 1-channel device falls back to centred mono")
    {
        const auto cfg = mappedChannelsFor ({ 0, 2, InputLayout::Stereo }, 1);
        REQUIRE (cfg.size() == 2);
        CHECK (cfg[0].deviceChannel == 0);
        CHECK (cfg[1].deviceChannel == 0);
    }

    SUBCASE ("MultiChannel maps N discrete channels (4-in acceptance path)")
    {
        const auto cfg = mappedChannelsFor ({ 0, 4, InputLayout::MultiChannel }, 4);
        REQUIRE (cfg.size() == 4);
        CHECK (cfg[0].deviceChannel == 0);
        CHECK (cfg[1].deviceChannel == 1);
        CHECK (cfg[2].deviceChannel == 2);
        CHECK (cfg[3].deviceChannel == 3);

        // N is clamped to what the device actually has from `firstChannel` on.
        const auto clamped = mappedChannelsFor ({ 1, 8, InputLayout::MultiChannel }, 4);
        CHECK (clamped.size() == 3);
    }

    SUBCASE ("an out-of-range firstChannel is clamped, never UB")
    {
        const auto cfg = mappedChannelsFor ({ 9, 1, InputLayout::Mono }, 2);
        REQUIRE (cfg.size() == 2);
        CHECK (cfg[0].deviceChannel == 1);
        CHECK (cfg[1].deviceChannel == 1);
    }

    SUBCASE ("layout string round-trips for persistence")
    {
        for (auto layout : { InputLayout::Auto, InputLayout::Mono, InputLayout::Stereo, InputLayout::MultiChannel })
            CHECK (inputLayoutFromString (inputLayoutToString (layout)) == layout);
    }
}

//==============================================================================
TEST_CASE ("a simulated busy-device error is representable and visible")
{
    // Simulate the exact error the ALSA open path surfaces when PipeWire holds the
    // device; this is what DevicePanel::showError puts in the error label.
    const auto info = classifyDeviceError ("ALSA: cannot open device: Device or resource busy (EBUSY)");

    CHECK (info.isBusy);
    CHECK (info.userMessage.isNotEmpty());

    // The message is multi-line (paragraphs) and the error label reserved at the
    // real panel height is tall enough for at least four text lines, so the whole
    // actionable message is visible rather than clipped to zero height.
    const auto lineCount = juce::StringArray::fromLines (info.userMessage).size();
    CHECK (lineCount >= 3);

    const auto layout = computeDevicePanelLayout ({ 0, 0, 900, devicePanelHeight });
    CHECK (layout.errorLabel.getHeight() >= lineCount * 16);
}

//==============================================================================
TEST_CASE ("session recovery paths are derived deterministically")
{
    const juce::File editFile ("/tmp/My Session.tracktionedit");

    CHECK (paths::tempEditFileFor (editFile) == juce::File ("/tmp/.tmp_My Session"));
    CHECK (paths::recordingsDirectoryFor (editFile) == juce::File ("/tmp/Recordings"));
    CHECK (paths::tempEditFileFor ({}) == juce::File());
}

//==============================================================================
// VU meter ballistics (Epic 2 GUI retest): rise fast to a new peak, then fall
// smoothly at a bounded dB/s. The old meters multiplied by a fixed per-frame
// factor with no dt and the mixer reset its state on every change message, so
// they flickered/jittered while the engineer changed input/master volume.
TEST_CASE ("meter ballistics rise fast and release smoothly without per-block flicker")
{
    MeterBallistics meter;
    meter.reset();

    constexpr float dt = 1.0f / 30.0f;

    // Fast attack to a steady -6 dBFS.
    for (int i = 0; i < 20; ++i)
        meter.update (-6.0f, dt);

    CHECK (meter.getDb() == doctest::Approx (-6.0f).epsilon (0.05f));
    CHECK (meter.getHoldDb() == doctest::Approx (-6.0f).epsilon (0.05f));

    // Silence: the value must be monotonic non-increasing (never jumps *up*
    // between blocks — the flicker) and each step is bounded by the release rate.
    float previous = meter.getDb();

    for (int i = 0; i < 45; ++i) // 1.5 s
    {
        meter.update (MeterBallistics::floorDb, dt);
        const auto value = meter.getDb();

        CHECK (value <= previous + 1.0e-4f);
        CHECK (previous - value <= MeterBallistics::releaseDbPerSec * dt + 1.0e-3f);

        previous = value;
    }

    // ~1.5 s at 24 dB/s => ~36 dB of fall (within the 20-40 dB/s window).
    const auto fallenDb = -6.0f - meter.getDb();
    INFO ("fallen " << fallenDb << " dB in 1.5 s");
    CHECK (fallenDb > 30.0f);
    CHECK (fallenDb < 40.0f);
}

TEST_CASE ("meter ballistics are frame-rate independent (same duration, same result)")
{
    MeterBallistics slow, fast;
    slow.reset();
    fast.reset();

    for (int i = 0; i < 30; ++i)  // 1 s at 30 Hz
        slow.update (-6.0f, 1.0f / 30.0f);

    for (int i = 0; i < 60; ++i)  // 1 s at 60 Hz
        fast.update (-6.0f, 1.0f / 60.0f);

    CHECK (slow.getDb() == doctest::Approx (fast.getDb()).epsilon (0.1f));
}

//==============================================================================
// Fader taper (owner request): the travel position maps linearly to decibels.
// The owner found the useful range compressed into the top of a -60..+6 travel;
// narrowing the level range to -40..+6 puts unity at ~0.870 and gives the lower
// travel usable resolution. 0 dB lands where the linear map puts it
// (documented), the mapping is exactly invertible, and the bottom of a level
// fader is a hard mute detent.
TEST_CASE ("fader taper is linear in dB with a documented unity position")
{
    using rrs::fader::level;
    using rrs::fader::inputTrim;

    SUBCASE ("level fader: equal position steps are equal dB steps")
    {
        const auto quarter  = level.posToDb (0.25f);
        const auto half     = level.posToDb (0.50f);
        const auto threeQtr = level.posToDb (0.75f);
        const auto top      = level.posToDb (1.00f);

        // -40 .. +6 over 46 dB of travel, so each 0.25 is 11.5 dB.
        CHECK (quarter  == doctest::Approx (-28.5f));
        CHECK (half     == doctest::Approx (-17.0f));
        CHECK (threeQtr == doctest::Approx (-5.5f));
        CHECK (top      == doctest::Approx (6.0f));

        // Equal travel steps are equal dB steps: the taper is linear in dB.
        // (The shape was never linear in amplitude — only the range changed.)
        CHECK ((half - quarter) == doctest::Approx (threeQtr - half).epsilon (1.0e-4f));
        CHECK ((threeQtr - half) == doctest::Approx (top - threeQtr).epsilon (1.0e-4f));
    }

    SUBCASE ("0 dB position is documented and exactly reproducible")
    {
        // (0 - -40) / (6 - -40) = 40/46 ~= 0.870.
        CHECK (level.unityPos() == doctest::Approx (40.0f / 46.0f));
        CHECK (level.posToDb (level.unityPos()) == doctest::Approx (0.0f).epsilon (1.0e-4f));

        // The owner's reported points must no longer feel like a cliff: 0.7 is
        // a usable -7.8 dB (not the old -46.2 dB) and 0.1 is -35.4 dB.
        CHECK (level.posToDb (0.7f) == doctest::Approx (-7.8f).epsilon (1.0e-4f));
        CHECK (level.posToDb (0.1f) == doctest::Approx (-35.4f).epsilon (1.0e-4f));

        // The record trim is symmetric: unity is dead centre.
        CHECK (inputTrim.unityPos() == doctest::Approx (0.5f));
        CHECK (inputTrim.posToDb (0.5f) == doctest::Approx (0.0f));
        CHECK (inputTrim.posToDb (0.0f) == doctest::Approx (-24.0f));
        CHECK (inputTrim.posToDb (1.0f) == doctest::Approx (24.0f));
    }

    SUBCASE ("dB -> position is the inverse of position -> dB")
    {
        for (const float p : { 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f })
        {
            const auto db = level.posToDb (p);
            CHECK (level.dbToPos (db) == doctest::Approx (p).epsilon (1.0e-4f));

            const auto trimDb = inputTrim.posToDb (p);
            CHECK (inputTrim.dbToPos (trimDb) == doctest::Approx (p).epsilon (1.0e-4f));
        }
    }

    SUBCASE ("level fader bottoms out to a mute detent; the trim does not")
    {
        const auto mute = level.posToDb (0.0f);
        CHECK (std::isinf (mute));
        CHECK (mute < 0.0f);

        // Anything at/below the floor (including -inf) reads as the bottom and
        // is reported as silent, so a mute state round-trips through the UI.
        CHECK (level.dbToPos (mute) == doctest::Approx (0.0f));
        CHECK (level.dbToPos (-100.0f) == doctest::Approx (0.0f));
        CHECK (level.isSilentDb (mute));
        CHECK (level.isSilentDb (-100.0f));
        CHECK_FALSE (level.isSilentDb (-39.0f));   // just above the -40 dB floor
        CHECK_FALSE (level.isSilentDb (0.0f));

        // The record trim is a gain control, never a mute.
        CHECK_FALSE (inputTrim.isSilentDb (inputTrim.posToDb (0.0f)));
        CHECK_FALSE (std::isinf (inputTrim.posToDb (0.0f)));
    }

    SUBCASE ("out-of-range input is clamped, never UB")
    {
        CHECK (level.posToDb (-1.0f) < 0.0f);           // clamps to p=0 -> mute
        CHECK (level.posToDb (5.0f) == doctest::Approx (6.0f));
        CHECK (level.dbToPos (-1000.0f) == doctest::Approx (0.0f));
        CHECK (level.dbToPos (1000.0f) == doctest::Approx (1.0f));
    }
}

//==============================================================================
TEST_CASE ("24-bit WAV export round-trips with the correct format")
{
    const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("raw-radio-studio-tests");
    directory.createDirectory();

    auto file = directory.getChildFile ("export24.wav");
    file.deleteFile();

    constexpr double sampleRate = 48000.0;
    constexpr int numChannels = 2;
    constexpr int numSamples = 512;
    constexpr int expectedBitDepth = 24; // FR-EXP-1 / NFR-A-1

    juce::AudioBuffer<float> source (numChannels, numSamples);

    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            source.setSample (ch, i, 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi
                                                      * (float) i * ((float) (ch + 1) / 64.0f)));

    juce::WavAudioFormat wav;

    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);

        auto writer = wav.createWriterFor (stream,
                                           juce::AudioFormatWriterOptions{}
                                               .withSampleRate (sampleRate)
                                               .withNumChannels (numChannels)
                                               .withBitsPerSample (expectedBitDepth));

        REQUIRE (writer != nullptr);
        CHECK (writer->writeFromAudioSampleBuffer (source, 0, numSamples));
    }

    REQUIRE (file.existsAsFile());

    std::unique_ptr<juce::AudioFormatReader> reader (
        wav.createReaderFor (new juce::FileInputStream (file), true));

    REQUIRE (reader != nullptr);
    CHECK (reader->bitsPerSample == expectedBitDepth);
    CHECK (reader->numChannels == (unsigned int) numChannels);
    CHECK (reader->sampleRate == doctest::Approx (sampleRate));
    CHECK (reader->lengthInSamples == numSamples);

    juce::AudioBuffer<float> readBack (numChannels, numSamples);
    REQUIRE (reader->read (&readBack, 0, numSamples, 0, true, true));

    // 24-bit quantisation is much finer than 1e-3; the file must play back
    // identically for all practical purposes.
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            CHECK (readBack.getSample (ch, i) == doctest::Approx (source.getSample (ch, i)).epsilon (1.0e-3f));

    file.deleteFile();
}
