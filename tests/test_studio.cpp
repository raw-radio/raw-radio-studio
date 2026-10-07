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
#include "studio/AppPaths.h"
#include "ui/DevicePanelLayout.h"

#include <cmath>
#include <memory>

using namespace rrs;

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
   #else
    // Non-Linux backends have no plugin-fallback hazard; the policy is a no-op.
    CHECK (isAcceptableInputDeviceName ("anything on this platform"));
   #endif
}

//==============================================================================
TEST_CASE ("device panel always gives the status + error labels real height")
{
    // The panel must be tall enough for a multi-line device error (FR-MON-5).
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
