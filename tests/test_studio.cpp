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
#include "studio/LastDirectoryStore.h"
#include "studio/PluginPresets.h"
#include "studio/TimeStretch.h"
#include "ui/BrandFonts.h"
#include "ui/DevicePanelLayout.h"
#include "ui/FaderTaper.h"
#include "ui/IconCache.h"
#include "ui/MeterBallistics.h"
#include "ui/MixerLayout.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
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

    /** Counts *fully opaque* pixels whose colour does not match `tint`.

        Guards the tinting contract: an untinted SVG element (JUCE does not
        resolve the `currentColor` Phosphor ships, nor a missing fill, so such
        elements render black) stays black under `Drawable::replaceColour
        (white -> tint)` and shows as a dark "hole" on the dark theme.
        Partial-alpha antialiased edges are skipped (the alpha threshold),
        because their premultiplied colour is not a reliable signal; every pixel
        the icon actually paints solid must carry the tint. */
    int countOpaquePixelsOffTint (const juce::Image& image, juce::Colour tint,
                                  int tolerance = 16, int alphaThreshold = 250)
    {
        int count = 0;
        const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);

        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
            {
                const auto pixel = data.getPixelColour (x, y);

                if (pixel.getAlpha() < alphaThreshold)
                    continue;

                if (std::abs ((int) pixel.getRed() - (int) tint.getRed()) > tolerance
                    || std::abs ((int) pixel.getGreen() - (int) tint.getGreen()) > tolerance
                    || std::abs ((int) pixel.getBlue() - (int) tint.getBlue()) > tolerance)
                    ++count;
            }

        return count;
    }
}

TEST_CASE ("brand icon cache parses and tints the vendored Phosphor SVGs")
{
    auto& cache = IconCache::getInstance();

    // A vendored icon parses, tints and rasterises at 2x the requested size.
    const auto icon = cache.getIconImage ("file-plus", juce::Colours::red, 16);
    CHECK (icon.isValid());
    CHECK (icon.getWidth() == 32);
    CHECK (icon.getHeight() == 32);

    // ...and actually contains drawn (tinted) pixels, not an empty raster.
    CHECK (countNonTransparentPixels (icon) > 0);

    // Tinting contract: every opaque pixel is the requested tint. Phosphor
    // regular icons ship `fill="currentColor"` (which JUCE cannot resolve) with
    // no per-element fill, so an unnormalised element would render black and be
    // left black by `replaceColour (white -> tint)` — a dark dot on the theme.
    CHECK (countOpaquePixelsOffTint (icon, juce::Colours::red) == 0);

    // Regression: a complex multi-subpath glyph (gear-six) and a ring+dot glyph
    // (record) must be fully tinted too — render larger so there are plenty of
    // fully-opaque pixels to inspect. This is the Phosphor equivalent of the old
    // Material `radio_button_checked` bare-`<circle>` guard.
    for (const auto* name : { "gear-six", "record" })
    {
        const auto complexIcon = cache.getIconImage (name, juce::Colours::white, 48);
        REQUIRE (complexIcon.isValid());
        CHECK (countNonTransparentPixels (complexIcon) > 0);
        CHECK (countOpaquePixelsOffTint (complexIcon, juce::Colours::white) == 0);
    }

    // Every vendored resource must resolve — guards against a renamed file
    // silently yielding a blank button. These are the Phosphor Icons (regular)
    // names used by the transport / action rows and the device panel.
    for (const auto* name : { "file-plus", "folder-open", "floppy-disk", "floppy-disk-back",
                              "upload-simple", "download-simple", "x", "gear-six", "info",
                              "record", "headphones", "play", "pause", "stop", "skip-back",
                              "metronome", "timer", "plus", "trash", "waveform",
                              "arrows-clockwise" })
    {
        const auto rendered = cache.getIconImage (name, juce::Colours::white, 16);
        CHECK (rendered.isValid());
        CHECK (countNonTransparentPixels (rendered) > 0);
        CHECK (countOpaquePixelsOffTint (rendered, juce::Colours::white) == 0);
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
// Owner request: the input / mixer level bars must use a sensible dB scale so a
// normal mic level does not read "above half". The shared display scale is
// -60..0 dBFS, linear in dB, with 0 dBFS at full scale — so half the bar is
// exactly -30 dBFS (the old -60..+6 scale put moderate levels high and left 0
// dBFS short of the top).
TEST_CASE ("meter display scale is -60..0 dBFS with -30 dBFS at half")
{
    const auto normaliseMeterDb = [] (float db) { return MeterBallistics::normaliseMeterDb (db); };

    CHECK (normaliseMeterDb (-60.0f) == doctest::Approx (0.0f));
    CHECK (normaliseMeterDb (-30.0f) == doctest::Approx (0.5f));
    CHECK (normaliseMeterDb (0.0f)   == doctest::Approx (1.0f));

    // A typical well-set mic level (-30..-18 dBFS) sits around the middle to
    // three-quarters, never pinned at the top.
    CHECK (normaliseMeterDb (-18.0f) < 0.75f);
    CHECK (normaliseMeterDb (-18.0f) > 0.5f);

    // Out-of-range values clamp at both ends (silence -> empty, >0 dBFS -> full).
    CHECK (normaliseMeterDb (-100.0f) == doctest::Approx (0.0f));
    CHECK (normaliseMeterDb (6.0f)    == doctest::Approx (1.0f));

    // Monotonic in dB (a louder signal never draws a shorter bar).
    float previous = normaliseMeterDb (-60.0f);

    for (int db = -59; db <= 0; ++db)
    {
        const auto current = normaliseMeterDb ((float) db);
        CHECK (current >= previous);
        previous = current;
    }

    // The scale endpoints are exposed for the meters to share; faders use a
    // separate taper (see FaderTaper.h) and must not be tied to this.
    CHECK (MeterBallistics::meterFloorDb == -60.0f);
    CHECK (MeterBallistics::meterCeilDb == 0.0f);
}

//==============================================================================
// Fader taper (owner request): the level travel maps linearly to decibels,
// while the record trim is shaped by a tanh S-curve. The owner found the level
// useful range compressed into the top of a -60..+6 travel, so it was narrowed
// to -40..+6 (unity ~0.870, linear-in-dB); separately, the trim dropped away
// from +24 dB too fast, so it now holds closer to the top through a documented,
// tunable bend (FaderTaper.h). 0 dB lands where the documented map puts it, the
// mapping is exactly invertible, and the bottom of a level fader is a hard mute.
TEST_CASE ("fader taper: linear level law and a non-linear S-curve trim")
{
    using rrs::fader::level;
    using rrs::fader::inputTrim;
    using rrs::fader::inputTrimBend;

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

        // Equal travel steps are equal dB steps: the level taper is linear in dB.
        // (The shape was never linear in amplitude — only the range changed.)
        CHECK ((half - quarter) == doctest::Approx (threeQtr - half).epsilon (1.0e-4f));
        CHECK ((threeQtr - half) == doctest::Approx (top - threeQtr).epsilon (1.0e-4f));
    }

    SUBCASE ("record trim: an S-curve holds the gain closer to the maximum")
    {
        // Linear would be +12 dB at 0.75 and +19.2 dB at 0.9; the S-curve must
        // sit higher (closer to +24) so lowering from the top feels gentler.
        CHECK (inputTrim.posToDb (0.75f) > 12.0f);
        CHECK (inputTrim.posToDb (0.75f) == doctest::Approx (16.84f).epsilon (1.0e-3f));
        CHECK (inputTrim.posToDb (0.9f) > 19.2f);
        CHECK (inputTrim.posToDb (0.9f) == doctest::Approx (22.10f).epsilon (1.0e-3f));

        // Still strictly inside the +24 dB rail, and closer to it than linear.
        CHECK (inputTrim.posToDb (0.95f) < 24.0f);
        CHECK (inputTrim.posToDb (0.95f) > -24.0f + 48.0f * 0.95f);

        // Symmetric about unity: the curve is odd, so ±offsets mirror exactly.
        for (const float d : { 0.1f, 0.25f, 0.4f, 0.49f })
            CHECK (inputTrim.posToDb (0.5f + d)
                   == doctest::Approx (-inputTrim.posToDb (0.5f - d)).epsilon (1.0e-4f));

        // Monotonic across the whole travel (never doubles back).
        float previous = inputTrim.posToDb (0.0f);
        for (int i = 1; i <= 20; ++i)
        {
            const auto current = inputTrim.posToDb ((float) i / 20.0f);
            CHECK (current > previous);
            previous = current;
        }

        // The documented bend is the trim's, not the level faders'.
        CHECK (inputTrimBend > 0.0f);
        CHECK (level.bend == 0.0f);
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
// BUG A (owner report): on the input channel strips the level readout was drawn
// at the top of the fader travel, so the thumb covered it once the fader was
// raised. The strip must reserve a dedicated readout row above the travel; the
// thumb at the top of its throw must never intersect that row — at any window
// size.
TEST_CASE ("mixer strip reserves a level-readout row the fader thumb can never cover (BUG A)")
{
    using namespace rrs::mixer_layout;

    // `inner` is the strip column left between the control rows and the top of
    // the strip. Cover the real minimum-window strip (short) through a tall one.
    for (const int innerHeight : { 40, 56, 72, 86, 120, 200 })
    {
        juce::Rectangle<int> inner (10, 100, 80, innerHeight);
        juce::Rectangle<int> levelRow, travel;
        splitLevelAndFader (inner, levelRow, travel);

        INFO ("inner height " << innerHeight);
        REQUIRE (levelRow.getHeight() == levelReadoutHeight);
        REQUIRE_FALSE (levelRow.isEmpty());
        REQUIRE_FALSE (travel.isEmpty());

        // The readout sits strictly above the travel and is tall enough for the
        // 10 pt mono readout.
        CHECK (travel.getY() >= levelRow.getBottom());
        CHECK (levelRow.getHeight() >= 12);

        // No fader position — including the top (1.0) and out-of-range values —
        // may place the 18x6 thumb inside the readout row.
        for (const float t : { -1.0f, 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 2.0f })
        {
            const auto thumbY = faderThumbY (travel, t);
            const juce::Rectangle<float> thumb ((float) travel.getCentreX() - 9.0f, thumbY - 3.0f,
                                                18.0f, 6.0f);

            INFO ("taper pos " << t << " thumb " << thumb.toString());
            CHECK_FALSE (thumb.toNearestInt().intersects (levelRow));

            // The thumb also stays inside the fader travel itself (never spills
            // into the pan row below or the readout above).
            CHECK (thumb.getY() >= (float) travel.getY());
            CHECK (thumb.getBottom() <= (float) travel.getBottom());
        }
    }
}

//==============================================================================
// FR-ED-5: offline time-stretch / pitch-shift through the pinned MIT library.
// These tests prove the acceptance measurement directly on the DSP: the length
// changes to the requested target, the audio is still there (not silence), the
// pitch is preserved by a pure time-stretch, and an independent pitch-shift
// moves it.
namespace
{
    juce::AudioBuffer<float> makeSine (int numChannels, int numSamples,
                                       double sampleRate, double frequency,
                                       float amplitude = 0.5f)
    {
        juce::AudioBuffer<float> buffer (numChannels, numSamples);

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                buffer.setSample (ch, i,
                                  amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                * frequency * (double) i / sampleRate));

        return buffer;
    }

    float bufferRms (const juce::AudioBuffer<float>& buffer)
    {
        return buffer.getRMSLevel (0, 0, buffer.getNumSamples());
    }

    /** Rough dominant frequency from zero crossings on channel 0, measured over
        the middle 80% (edges carry transform ramps). */
    double estimateFrequency (const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        const auto n = buffer.getNumSamples();
        const auto start = n / 10;
        const auto end = n - n / 10;

        if (end <= start + 1)
            return 0.0;

        int crossings = 0;

        for (int i = start + 1; i < end; ++i)
        {
            const auto a = buffer.getSample (0, i - 1);
            const auto b = buffer.getSample (0, i);

            if ((a <= 0.0f && b > 0.0f) || (a >= 0.0f && b < 0.0f))
                ++crossings;
        }

        const auto seconds = (double) (end - start) / sampleRate;
        return (double) crossings / 2.0 / seconds;
    }
}

TEST_CASE ("time-stretch changes duration while preserving the signal (FR-ED-5)")
{
    constexpr double sampleRate = 48000.0;
    constexpr double frequency = 440.0;
    constexpr int inputLength = 48000; // 1.0 s

    const auto input = makeSine (2, inputLength, sampleRate, frequency, 0.5f);

    SUBCASE ("2x stretch to an exact target duration")
    {
        const auto result = TimeStretch::stretchToDuration (input, sampleRate, 2.0);

        REQUIRE (result.ok);
        CHECK (result.error.isEmpty());
        CHECK (result.sampleRate == doctest::Approx (sampleRate));
        CHECK (result.audio.getNumSamples() == 96000);
        CHECK (result.getDurationSeconds() == doctest::Approx (2.0).epsilon (1.0e-6));

        // The audio survived: RMS is in the right ballpark and the tone is not
        // doubled/halved in frequency (a pure time-stretch keeps pitch).
        const auto rms = bufferRms (result.audio);
        INFO ("stretched RMS = " << rms);
        CHECK (rms > 0.25f);
        CHECK (rms < 0.7f);

        const auto measured = estimateFrequency (result.audio, sampleRate);
        INFO ("measured frequency = " << measured);
        CHECK (measured == doctest::Approx (frequency).epsilon (0.05));
    }

    SUBCASE ("0.5x compress to an exact target duration")
    {
        const auto result = TimeStretch::stretchToDuration (input, sampleRate, 0.5);

        REQUIRE (result.ok);
        CHECK (result.audio.getNumSamples() == 24000);
        CHECK (result.getDurationSeconds() == doctest::Approx (0.5).epsilon (1.0e-6));
        CHECK (bufferRms (result.audio) > 0.25f);
    }

    SUBCASE ("pitch-shift is independent of duration")
    {
        // +12 semitones with a 1.0x time factor: same length, double the pitch.
        const auto result = TimeStretch::process (input, sampleRate, 1.0, 12.0);

        REQUIRE (result.ok);
        CHECK (result.audio.getNumSamples() == inputLength);

        const auto measured = estimateFrequency (result.audio, sampleRate);
        INFO ("pitch-shifted frequency = " << measured);
        CHECK (measured == doctest::Approx (2.0 * frequency).epsilon (0.05));
    }
}

TEST_CASE ("time-stretch rejects invalid requests instead of producing garbage")
{
    const auto input = makeSine (2, 4800, 48000.0, 440.0);

    CHECK_FALSE (TimeStretch::process (input, 48000.0, 0.0).ok);
    CHECK_FALSE (TimeStretch::process (input, 48000.0, -1.0).ok);
    CHECK_FALSE (TimeStretch::process (input, 48000.0, 100.0).ok); // far outside bounds
    CHECK_FALSE (TimeStretch::stretchToDuration (input, 48000.0, 0.0).ok);
    CHECK_FALSE (TimeStretch::stretchToDuration ({}, 48000.0, 1.0).ok);

    // A valid request still succeeds.
    CHECK (TimeStretch::process (input, 48000.0, 1.0).ok);
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

//==============================================================================
// FR-MIX-6: the user-preset store maps a plugin to a stable, filesystem-safe
// folder key, validates names, and round-trips a state blob (save/list/load/
// delete). Pure filesystem logic — no engine.
TEST_CASE ("plugin presets store round-trips state and rejects unsafe names (FR-MIX-6)")
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("raw-radio-studio-preset-test");
    root.deleteRecursively();

    PluginPresets presets (root);
    CHECK (presets.getRootDirectory() == root);

    // A key is stable and safe (never contains a path separator).
    const auto key = PluginPresets::keyFor ("My/Fancy Plugin!", "VST3", "/p/plugin.vst3");
    CHECK (key.isNotEmpty());
    CHECK_FALSE (key.containsChar ('/'));
    CHECK_FALSE (key.containsChar ('\\'));
    CHECK (key == PluginPresets::keyFor ("My/Fancy Plugin!", "VST3", "/p/plugin.vst3"));
    CHECK (key != PluginPresets::keyFor ("Other Plugin", "VST3", "/p/plugin.vst3"));

    // Name validation.
    CHECK (PluginPresets::isValidPresetName ("Warm 80s"));
    CHECK_FALSE (PluginPresets::isValidPresetName (""));
    CHECK_FALSE (PluginPresets::isValidPresetName ("   "));
    CHECK_FALSE (PluginPresets::isValidPresetName ("a/b"));
    CHECK_FALSE (PluginPresets::isValidPresetName ("a\\b"));

    // Windows-reserved punctuation must be rejected too (the file store would
    // otherwise fail to write such a name on Windows).
    for (auto* bad : { "a*b", "a?b", "a\"b", "a<b", "a>b", "a|b", "a:b" })
        CHECK_FALSE (PluginPresets::isValidPresetName (bad));

    // Round-trip a state blob.
    const char payload[] = "preset-state-bytes";
    juce::MemoryBlock state;
    state.append (payload, sizeof (payload));

    CHECK (presets.savePreset (key, "Warm 80s", state));
    CHECK (presets.listPresets (key).contains ("Warm 80s"));

    juce::MemoryBlock loaded;
    CHECK (presets.loadPreset (key, "Warm 80s", loaded));
    CHECK (loaded.getSize() == state.getSize());
    CHECK (std::memcmp (loaded.getData(), state.getData(), state.getSize()) == 0);

    // Unknown preset / unsafe save fail cleanly.
    CHECK_FALSE (presets.loadPreset (key, "nope", loaded));
    CHECK_FALSE (presets.savePreset (key, "bad/name", state));

    CHECK (presets.deletePreset (key, "Warm 80s"));
    CHECK_FALSE (presets.listPresets (key).contains ("Warm 80s"));

    root.deleteRecursively();
}

//==============================================================================
// Last-used file-dialog directory store: the path under test is the one the UI
// depends on — the remembered directory is returned as the chooser default and
// updated after a choice, per operation kind, with a fallback when nothing is
// remembered (or the remembered folder no longer exists). Pure logic against an
// in-memory storage, so no dialog is involved.
namespace
{
    class FakeDirectoryStorage final : public LastDirectoryStore::Storage
    {
    public:
        juce::String getValue (const juce::String& key) const override
        {
            return values.getValue (key, {});
        }

        void setValue (const juce::String& key, const juce::String& value) override
        {
            values.set (key, value);
        }

        void save() override { ++saveCount; }

        juce::StringPairArray values;
        int saveCount = 0;
    };
}

TEST_CASE ("last-used file-dialog directory is remembered per operation (UI)")
{
    FakeDirectoryStorage storage;
    LastDirectoryStore store (storage);

    const auto tempRoot = juce::File::getSpecialLocation (juce::File::tempDirectory);
    const auto projects = tempRoot.getChildFile ("rrs-lastdir-projects");
    const auto exports  = tempRoot.getChildFile ("rrs-lastdir-exports");
    projects.createDirectory();
    exports.createDirectory();

    // Nothing remembered yet: the caller's fallback is used.
    CHECK (store.getDirectory (LastDirectoryStore::Kind::projects, projects) == projects);

    // A file choice remembers its parent directory, under the projects key.
    const auto projectFile = projects.getChildFile ("Song.tracktionedit");
    store.rememberFile (LastDirectoryStore::Kind::projects, projectFile);
    CHECK (storage.values.getValue (LastDirectoryStore::keyFor (LastDirectoryStore::Kind::projects), {})
           == projects.getFullPathName());
    CHECK (storage.saveCount == 1);

    // Operations are independent: another kind still falls back.
    CHECK (store.getDirectory (LastDirectoryStore::Kind::exportFile, exports) == exports);

    // The remembered directory wins over the fallback on the next chooser.
    CHECK (store.getDirectory (LastDirectoryStore::Kind::projects, exports) == projects);

    // A folder chooser (stems) records the directory directly.
    store.rememberDirectory (LastDirectoryStore::Kind::exportStems, exports);
    CHECK (store.getDirectory (LastDirectoryStore::Kind::exportStems, projects) == exports);

    // A stale remembered directory (deleted since) falls back rather than
    // opening a chooser at a missing path.
    projects.deleteRecursively();
    CHECK (store.getDirectory (LastDirectoryStore::Kind::projects, exports) == exports);

    // An empty file (dialog cancelled) is ignored: no write, no save.
    const auto savesBefore = storage.saveCount;
    store.rememberFile (LastDirectoryStore::Kind::projects, juce::File());
    CHECK (storage.saveCount == savesBefore);

    exports.deleteRecursively();
}
