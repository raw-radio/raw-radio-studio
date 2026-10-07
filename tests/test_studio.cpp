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

TEST_CASE ("ALSA hardware name policy rejects the plugin layer")
{
    CHECK (isAlsaHardwareDeviceName ("hw:0,0"));
    CHECK (isAlsaHardwareDeviceName ("HDA Intel PCH, ALC295 Analog (hw:0,0)"));
    CHECK_FALSE (isAlsaHardwareDeviceName ("default"));
    CHECK_FALSE (isAlsaHardwareDeviceName ("pulse"));
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
