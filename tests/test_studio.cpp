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
#include "ui/IconCache.h"

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

TEST_CASE ("brand icon cache parses and tints the vendored Lucide SVGs")
{
    auto& cache = IconCache::getInstance();

    // A vendored icon parses, tints and rasterises at 2x the requested size.
    const auto icon = cache.getIconImage ("file-plus", juce::Colours::red, 16);
    CHECK (icon.isValid());
    CHECK (icon.getWidth() == 32);
    CHECK (icon.getHeight() == 32);

    // ...and actually contains drawn (tinted) pixels, not an empty raster.
    CHECK (countNonTransparentPixels (icon) > 0);

    // Every vendored resource must resolve — guards against a renamed file
    // silently yielding a blank button.
    for (const auto* name : { "file-plus", "folder-open", "save", "save-all", "file-input",
                              "file-output", "x", "settings", "info", "circle-dot", "headphones",
                              "circle", "play", "pause", "square", "refresh-cw" })
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
