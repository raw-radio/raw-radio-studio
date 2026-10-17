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
#include "studio/PluginSelection.h"
#include "studio/TimeStretch.h"
#include "studio/WavRepair.h"
#include "ui/BrandFonts.h"
#include "ui/ClipSnap.h"
#include "ui/ClipWaveform.h"
#include "ui/DevicePanelLayout.h"
#include "ui/FaderTaper.h"
#include "ui/IconCache.h"
#include "ui/MeterBallistics.h"
#include "ui/MixerLayout.h"
#include "ui/TrackLaneLayout.h"
#include "ui/SidebarLayout.h"
#include "studio/MeterCache.h"
#include "studio/InputGrouping.h"
#include "ui/InputMenuModel.h"

#include <vector>

#include <cmath>
#include <climits>
#include <limits>
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
    // names used by the transport / action rows, the arrangement edit row
    // (undo/redo/split/delete/duplicate/loop/crossfade/stretch) and the device
    // panel.
    for (const auto* name : { "file-plus", "folder-open", "floppy-disk", "floppy-disk-back",
                              "upload-simple", "download-simple", "x", "gear-six", "info",
                              "record", "headphones", "play", "pause", "stop", "skip-back",
                              "metronome", "timer", "plus", "trash", "waveform",
                              "arrows-clockwise",
                              "arrow-counter-clockwise", "arrow-clockwise", "scissors",
                              "copy", "repeat", "arrows-left-right", "arrows-horizontal" })
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
    SUBCASE ("Auto on a 1-channel device is a single centre channel (RT-5)")
    {
        const auto mono = mappedChannelsFor ({ 0, 1, InputLayout::Auto }, 1);
        REQUIRE (mono.size() == 1);
        CHECK (mono[0].deviceChannel == 0);
        CHECK (mono[0].type == juce::AudioChannelSet::centre);

        // 0 channels (no device yet) also resolves to a single mono channel.
        const auto none = mappedChannelsFor ({ 0, 1, InputLayout::Auto }, 0);
        REQUIRE (none.size() == 1);
        CHECK (none[0].deviceChannel == 0);

        const auto stereo = mappedChannelsFor ({ 0, 2, InputLayout::Auto }, 2);
        REQUIRE (stereo.size() == 2);
        CHECK (stereo[0].deviceChannel == 0);
        CHECK (stereo[1].deviceChannel == 1);
    }

    SUBCASE ("Mono picks any hardware channel as a single centre channel (RT-5)")
    {
        // The 3rd input of a 4-in interface (0-based index 2) on a mono track.
        const auto cfg = mappedChannelsFor ({ 2, 1, InputLayout::Mono }, 4);
        REQUIRE (cfg.size() == 1);
        CHECK (cfg[0].deviceChannel == 2);
        CHECK (cfg[0].type == juce::AudioChannelSet::centre);
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
        REQUIRE (cfg.size() == 1);
        CHECK (cfg[0].deviceChannel == 1);
    }

    SUBCASE ("layout string round-trips for persistence")
    {
        for (auto layout : { InputLayout::None, InputLayout::Auto, InputLayout::Mono,
                             InputLayout::Stereo, InputLayout::MultiChannel })
            CHECK (inputLayoutFromString (inputLayoutToString (layout)) == layout);
    }
}

//==============================================================================
TEST_CASE ("a simulated busy-device error is representable and visible")
{
    // Simulate the exact error the ALSA open path surfaces when PipeWire holds the
    // device; this is what DevicePanel::setError puts in the error label.
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

// BLOCKER 1 regression (revealed under ASan). Signalsmith's default preset uses
// a ~0.12 s window, so its output latency is ~2880 samples at 48 kHz. A clip
// shorter than that used to write the latency fold past the end of the padded
// output buffer (heap-buffer-overflow, silent in Release). The call must stay
// memory-safe and return exactly the requested length.
TEST_CASE ("time-stretch is safe for a clip shorter than the transform's latency (ASan regression)")
{
    constexpr double sampleRate = 48000.0;
    constexpr int shortLength = 512; // << output latency (~2880 @ 48 kHz)

    const auto input = makeSine (2, shortLength, sampleRate, 440.0, 0.5f);
    const auto result = TimeStretch::process (input, sampleRate, 1.0);

    REQUIRE (result.ok);
    CHECK (result.error.isEmpty());
    CHECK (result.audio.getNumSamples() == shortLength);

    bool allFinite = true;

    for (int ch = 0; ch < result.audio.getNumChannels() && allFinite; ++ch)
        for (int i = 0; i < result.audio.getNumSamples(); ++i)
            if (! std::isfinite (result.audio.getSample (ch, i)))
            {
                allFinite = false;
                break;
            }

    CHECK (allFinite);
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

//==============================================================================
// Clip-edge snapping (Epic 4 UX). The core is a pure function so the exact
// behaviour the arrangement relies on — nearest edge within threshold, ties to
// the smaller time, non-positive threshold disables — is pinned here without a
// window, a mouse or an Edit.
TEST_CASE ("clip edge snapping picks the nearest boundary within the threshold")
{
    const std::vector<double> edges { 3.0, 5.0, 10.0 };

    // Genuinely near 5 -> snaps to it.
    {
        const auto r = clipsnap::snapTimeToEdges (4.8, edges, 0.5);
        CHECK (r.snapped);
        CHECK (r.time == doctest::Approx (5.0));
    }

    // Nearest of two in-range edges wins (4.2 is 0.8 from 5.0, 1.2 from 3.0 but
    // with a 1.5 threshold both are in range).
    {
        const auto r = clipsnap::snapTimeToEdges (4.2, edges, 1.5);
        CHECK (r.snapped);
        CHECK (r.time == doctest::Approx (5.0));
    }

    // Outside every threshold: unchanged and flagged as not snapped.
    {
        const auto r = clipsnap::snapTimeToEdges (7.0, edges, 0.5);
        CHECK_FALSE (r.snapped);
        CHECK (r.time == doctest::Approx (7.0));
    }

    // Exactly on the threshold edge is inclusive.
    {
        const auto r = clipsnap::snapTimeToEdges (3.5, edges, 0.5);
        CHECK (r.snapped);
        CHECK (r.time == doctest::Approx (3.0));
    }

    // A non-positive threshold disables snapping.
    CHECK_FALSE (clipsnap::snapTimeToEdges (4.9, edges, 0.0).snapped);
    CHECK_FALSE (clipsnap::snapTimeToEdges (4.9, edges, -1.0).snapped);

    // No edges: candidate is returned unchanged.
    {
        const auto r = clipsnap::snapTimeToEdges (4.9, {}, 1.0);
        CHECK_FALSE (r.snapped);
        CHECK (r.time == doctest::Approx (4.9));
    }

    // Exact tie between 3.25 and 3.75 (both 0.25 from 3.5) resolves to the
    // smaller time, independent of the order the edges were collected in.
    CHECK (clipsnap::snapTimeToEdges (3.5, { 3.75, 3.25 }, 1.0).time == doctest::Approx (3.25));
    CHECK (clipsnap::snapTimeToEdges (3.5, { 3.25, 3.75 }, 1.0).time == doctest::Approx (3.25));

    // Deterministic repeat.
    const auto a = clipsnap::snapTimeToEdges (2.9, edges, 0.5);
    const auto b = clipsnap::snapTimeToEdges (2.9, edges, 0.5);
    CHECK (a.time == doctest::Approx (b.time));
    CHECK (a.snapped == b.snapped);
}

//==============================================================================
// Uniform track-lane geometry (owner bug fix). Two things had to line up:
//   * every lane shares one height (the first lane used to get a fixed 46 px and
//     later lanes were squeezed into the leftover space), and
//   * every clip FILLS its lane (the clip band used to be the lane minus a
//     reserved 20 px name row, so in the default 38 px lane an imported clip
//     collapsed to the 8 px floor while the armed input lane's background filled
//     the lane — a tall block next to a thin strip). The name is now an overlay
//     drawn on top of the clip, so input and imported clips are the same height.
TEST_CASE ("track lanes share one uniform height with clips filling them")
{
    using namespace rrs::track_lane;

    // A roomy arrangement: every lane gets the full 72 px DAW default.
    {
        const auto rects = computeLaneRects ({ 0, 0, 900, 600 }, 3);
        REQUIRE (rects.size() == 3);

        for (const auto& r : rects)
            CHECK (r.getHeight() == preferredHeight);

        // Stacked top-to-bottom, no overlap, inside the area.
        CHECK (rects[0].getY() < rects[1].getY());
        CHECK (rects[1].getY() < rects[2].getY());
        CHECK (rects[0].getBottom() == rects[1].getY());
        CHECK (rects[2].getBottom() <= 600);
    }

    // The exact input-vs-import regression: whatever the track count, the input
    // lane and the imported lane are the same height.
    {
        const auto rects = computeLaneRects ({ 0, 0, 900, 222 }, 3);
        REQUIRE (rects.size() == 3);
        CHECK (rects[0].getHeight() == rects[1].getHeight());
        CHECK (rects[1].getHeight() == rects[2].getHeight());
    }

    // A cramped arrangement no longer compresses lanes below the floor (Epic 8
    // phase 1a): every lane keeps the 56 px minimum and the stack overflows, so
    // the arrangement scrolls instead of squeezing lanes to 1–3 px.
    {
        const auto rects = computeLaneRects ({ 0, 0, 900, 120 }, 4);
        REQUIRE (rects.size() == 4);

        for (const auto& r : rects)
            CHECK (r.getHeight() == minHeight);

        const auto laneHeight = uniformLaneHeight (120, 4);
        CHECK (laneHeight == minHeight);
        CHECK (needsScroll (120, 4, laneHeight));
        CHECK (laneStackHeight (4, laneHeight) > 120);
    }

    // Degenerate inputs are handled without producing lanes.
    CHECK (computeLaneRects ({ 0, 0, 900, 300 }, 0).empty());
    CHECK (computeLaneRects ({ 0, 0, 0, 0 }, 3).empty());

    // The shared height is capped at the preferred DAW default and floored at the
    // scroll minimum (never the old 1 px squeeze).
    CHECK (uniformLaneHeight (10000, 1) == preferredHeight);
    CHECK (uniformLaneHeight (0, 3) == minHeight);

    // The clip fills its lane (uniform `clipInset` all round) and the name row is
    // an overlay INSIDE the clip band, not a reserved strip that would squeeze a
    // cramped clip down to a thin sliver.
    for (const int laneHeight : { preferredHeight, 50, 30, 18 })
    {
        juce::Rectangle<int> nameRow, clipBand;
        splitLane ({ 0, 0, 900, laneHeight }, nameRow, clipBand);

        CHECK (clipBand.getHeight() == laneHeight - 2 * clipInset);
        CHECK (clipBand.getY() == clipInset);
        CHECK (clipBand.getBottom() == laneHeight - clipInset);
        CHECK (nameRow.getY() >= clipBand.getY());
        CHECK (nameRow.getBottom() <= clipBand.getBottom());
    }
}

//==============================================================================
// Epic 8 phase 1a: lane-height presets (56/72/96, floor 56) and the vertical
// scroll model (offset clamp + the y->lane mapping that paint()/hit-tests share).
TEST_CASE ("lane-height presets and vertical scroll geometry (Epic 8 phase 1a)")
{
    using namespace rrs::track_lane;

    // Presets: compact 56 / normal 72 / large 96. Compact is 56, NOT 40 (R-A).
    CHECK (presetHeight (0) == 56);
    CHECK (presetHeight (1) == 72);
    CHECK (presetHeight (2) == 96);
    CHECK (presetCompact == minHeight);
    CHECK (isValidPreset (0));
    CHECK (isValidPreset (2));
    CHECK_FALSE (isValidPreset (3));
    CHECK_FALSE (isValidPreset (-1));

    // A non-normal preset overrides the auto height even in a short area.
    CHECK (uniformLaneHeight (200, 8, 2) == presetLarge);
    CHECK (uniformLaneHeight (200, 8, 0) == presetCompact);

    // Roomy -> full 72; many tracks in a short window -> the 56 floor, then scroll.
    CHECK (uniformLaneHeight (600, 3) == preferredHeight);
    CHECK (uniformLaneHeight (120, 8) == minHeight);

    // Content height + scroll clamping: 0 when it fits, else content - viewport.
    {
        const int laneH = uniformLaneHeight (300, 8);
        const int content = laneStackHeight (8, laneH);

        CHECK (laneH == minHeight);
        CHECK (content == 2 * vMargin + 8 * laneH);
        CHECK (maxScrollOffset (300, 8, laneH) == content - 300);
        CHECK (clampScrollOffset (-5, 300, 8, laneH) == 0);
        CHECK (clampScrollOffset (100000, 300, 8, laneH) == content - 300);
        CHECK_FALSE (needsScroll (10000, 8, preferredHeight));
        CHECK (needsScroll (300, 8, laneH));
    }

    // computeLaneRects applies the scroll offset; laneIndexAtY is its exact
    // inverse for every drawn lane, at any offset (this is what keeps the drawn
    // clip and its click hit-test in agreement).
    {
        const juce::Rectangle<int> area { 0, 0, 900, 300 };
        const int laneH = uniformLaneHeight (area.getHeight(), 8);
        const int maxOff = maxScrollOffset (area.getHeight(), 8, laneH);

        REQUIRE (laneH == minHeight);
        REQUIRE (maxOff > 0);

        for (const int offset : { 0, maxOff / 2, maxOff })
        {
            const auto rects = computeLaneRects (area, 8, offset);
            REQUIRE (rects.size() == 8);

            for (int i = 0; i < 8; ++i)
            {
                CHECK (rects[(size_t) i].getHeight() == laneH);
                const int yInArea = rects[(size_t) i].getCentreY() - area.getY();
                CHECK (laneIndexAtY (yInArea, 8, laneH, offset) == i);
            }
        }

        // The scrolled stack moves up by the offset.
        const auto atTop = computeLaneRects (area, 8, 0);
        const auto scrolled = computeLaneRects (area, 8, 40);
        CHECK (scrolled[0].getY() == atTop[0].getY() - 40);
    }

    // Visible range: from the top the first lane is visible; mid-scroll a later
    // lane becomes the first visible and the earlier lanes drop out.
    {
        const auto top = visibleLaneRange (300, 8, minHeight, 0);
        CHECK (top.first == 0);
        CHECK (top.second > 0);

        const auto mid = visibleLaneRange (300, 8, minHeight, 240);
        CHECK (mid.first > 0);
        CHECK (mid.second > mid.first);
    }

    // Degenerate inputs never crash.
    CHECK (computeLaneRects ({ 0, 0, 900, 300 }, 8, -10).size() == 8);
    CHECK (visibleLaneRange (0, 8, minHeight, 0).first == 0);
    CHECK (visibleLaneRange (0, 8, minHeight, 0).second == 0);
    CHECK (laneIndexAtY (0, 0, minHeight, 0) == -1);
}

//==============================================================================
// Epic 8 phase 1a: sidebar width clamps, section bitmask round-trip and the
// window-width-dependent effective maximum (so the transport row is never
// clipped at the 1100 px minimum window).
TEST_CASE ("sidebar geometry and section-state codec (Epic 8 phase 1a)")
{
    using namespace rrs::sidebar;

    // Width clamp.
    CHECK (clampWidth (-100) == minWidth);
    CHECK (clampWidth (9999) == maxWidth);
    CHECK (clampWidth (300) == 300);

    // Effective maximum is a function of the window width: at the 1100 px minimum
    // the sidebar may only take 360 px (not 420) so the 700 px transport row and
    // its timecode stay fully visible.
    CHECK (effectiveMaxWidth (1100) == 360);
    CHECK (effectiveMaxWidth (10000) == maxWidth);
    CHECK (effectiveMaxWidth (400) == minWidth); // never below the minimum

    // The per-window clamp also respects an explicit effective max.
    CHECK (clampWidth (420, 360) == 360);
    CHECK (clampWidth (200, 360) == minWidth);
    CHECK (clampWidth (300, 360) == 300);

    // encode/decode round-trip.
    for (const bool d : { true, false })
        for (const bool c : { true, false })
            for (const bool s : { true, false })
            {
                Sections in { d, c, s };
                const auto decoded = decodeSections (encodeSections (in));
                CHECK (decoded.device == d);
                CHECK (decoded.channels == c);
                CHECK (decoded.status == s);
            }

    // Defaults: Device + Channels open, Status collapsed.
    const auto def = Sections {};
    CHECK (def.device);
    CHECK (def.channels);
    CHECK_FALSE (def.status);

    // Malformed / short input falls back to the defaults.
    const auto empty = decodeSections ("");
    CHECK (empty.device);
    CHECK (empty.channels);
    CHECK_FALSE (empty.status);
    const auto shortIn = decodeSections ("XY"); // only 2 chars
    CHECK (shortIn.device);
    CHECK (shortIn.channels);
    CHECK_FALSE (shortIn.status);
    const auto junk = decodeSections ("DCcXY"); // extra chars: first 3 used
    CHECK (junk.device);
    CHECK (junk.channels);
    CHECK_FALSE (junk.status);
}

//==============================================================================
// Epic 8 phase 1a: the y -> lane inverse and the scroll bounds at their edges.
TEST_CASE ("laneIndexAtY and scroll bounds at the edges (Epic 8 phase 1a)")
{
    using namespace rrs::track_lane;

    const int numTracks = 4;
    const int laneH = minHeight; // 56
    const int offset = 0;

    // Just above the first lane's top (inside the vMargin band) -> -1.
    CHECK (laneIndexAtY (vMargin - 1, numTracks, laneH, offset) == -1);
    // The first lane's top pixel.
    CHECK (laneIndexAtY (vMargin, numTracks, laneH, offset) == 0);
    // Last pixel of lane 0.
    CHECK (laneIndexAtY (vMargin + laneH - 1, numTracks, laneH, offset) == 0);
    // First pixel of lane 1.
    CHECK (laneIndexAtY (vMargin + laneH, numTracks, laneH, offset) == 1);
    // Below the last lane -> -1.
    CHECK (laneIndexAtY (vMargin + numTracks * laneH, numTracks, laneH, offset) == -1);

    // With an offset the whole stack shifts up; a y above the (shifted) top
    // (y < vMargin - offset) is outside.
    CHECK (laneIndexAtY (vMargin - 21, numTracks, laneH, 20) == -1); // -15 +20 -6 = -1
    CHECK (laneIndexAtY (vMargin + 20, numTracks, laneH, 20) == 0);
    CHECK (laneIndexAtY (vMargin + 20 + laneH, numTracks, laneH, 20) == 1);

    // maxScrollOffset boundaries: at exactly maxOffset the clamp is unchanged;
    // one past it clamps back to maxOffset.
    const int content = laneStackHeight (numTracks, laneH);
    const int areaHeight = content - 30; // guarantees a positive overflow
    const int maxOff = maxScrollOffset (areaHeight, numTracks, laneH);
    CHECK (maxOff == 30);
    CHECK (clampScrollOffset (maxOff, areaHeight, numTracks, laneH) == maxOff);
    CHECK (clampScrollOffset (maxOff + 1, areaHeight, numTracks, laneH) == maxOff);

    // computeLaneRects at the boundaries of the track count.
    const juce::Rectangle<int> area { 0, 0, 900, 600 };

    {
        const auto one = computeLaneRects (area, 1);
        REQUIRE (one.size() == 1);
        CHECK (one[0].getHeight() == preferredHeight);
        CHECK (one[0].getWidth() == area.getWidth() - 2 * hMargin);
    }

    {
        const auto many = computeLaneRects ({ 0, 0, 900, 300 }, 16);
        REQUIRE (many.size() == 16);
        for (const auto& r : many)
            CHECK (r.getHeight() == minHeight);
        CHECK (needsScroll (300, 16, minHeight));
    }
}

//==============================================================================
// Epic 8 (1a follow-up): the collapse/expand handle must stay inside the column
// and never overlap the 4 px resize zone on the very edge, and must sit at the
// TOP (so it can never fall under the status/action rows).
TEST_CASE ("sidebar collapse handle geometry (Epic 8 follow-up)")
{
    using namespace rrs::sidebar;

    for (const int w : { minWidth, 320, 360, maxWidth })
    {
        const auto h = handleRectExpanded (w);
        CHECK (h.getX() >= 0);
        CHECK (h.getRight() <= w - resizeZone); // does not touch the resize zone
        CHECK (h.getY() >= 0);
        CHECK (h.getBottom() <= padding + headerHeight); // inside the first header
    }

    const auto rail = handleRectRail();
    CHECK (rail.getX() >= 0);
    CHECK (rail.getRight() <= railWidth);
    CHECK (rail.getY() == railIconPadding);           // at the TOP of the rail
    CHECK (rail.getBottom() <= railWidth);            // top area, far from the bottom
}

//==============================================================================
// Epic 8 phase 2: clip-waveform binning math (px -> time, offset/loop mapping).
TEST_CASE ("clip waveform binning (Epic 8 phase 2)")
{
    using namespace rrs::clipwave;

    // 2 px per bin; the last (partial) bin still counts.
    CHECK (binCount (0) == 0);
    CHECK (binCount (1) == 1);
    CHECK (binCount (2) == 1);
    CHECK (binCount (3) == 2);
    CHECK (binCount (100) == 50);

    // Bin edges tile the clip's [start, end] range linearly and monotonically.
    const double start = 10.0, end = 14.0;
    const int bins = binCount (8); // 4
    CHECK (bins == 4);
    CHECK (binStartSeconds (start, end, 0, bins) == doctest::Approx (10.0));
    CHECK (binEndSeconds (start, end, 0, bins) == doctest::Approx (11.0));
    CHECK (binStartSeconds (start, end, 3, bins) == doctest::Approx (13.0));
    CHECK (binEndSeconds (start, end, 3, bins) == doctest::Approx (14.0));

    for (int i = 1; i < bins; ++i)
        CHECK (binStartSeconds (start, end, i, bins) ==
               doctest::Approx (binEndSeconds (start, end, i - 1, bins)));

    // Peak from min/max and the silence threshold (~ -60 dBFS).
    CHECK (peakFromMinMax (-0.7f, 0.5f) == doctest::Approx (0.7f));
    CHECK (isSilent (0.0f));
    CHECK (isSilent (0.0005f));
    CHECK_FALSE (isSilent (0.5f));

    // Source-time mapping: linear with the clip's offset (no loop).
    CHECK (sourceTimeForBin (10.0, 10.0, 2.0, 100.0, false) == doctest::Approx (2.0));
    CHECK (sourceTimeForBin (10.0, 13.0, 2.0, 100.0, false) == doctest::Approx (5.0));
    // Clamped to the file length.
    CHECK (sourceTimeForBin (0.0, 1000.0, 0.0, 5.0, false) == doctest::Approx (5.0));

    // Looped clip: wrap per iteration (iteration = fileLength - offset).
    // offset 0, fileLength 4 -> iteration 4.
    CHECK (sourceTimeForBin (0.0, 5.0, 0.0, 4.0, true) == doctest::Approx (1.0));
    CHECK (sourceTimeForBin (0.0, 4.0, 0.0, 4.0, true) == doctest::Approx (0.0));
    CHECK (sourceTimeForBin (0.0, 9.0, 0.0, 4.0, true) == doctest::Approx (1.0));

    // Loop with an offset: iteration = fileLength - offset = 4 from offset 2.
    CHECK (sourceTimeForBin (0.0, 6.0, 2.0, 6.0, true) == doctest::Approx (4.0));
    CHECK (sourceTimeForBin (0.0, 1.0, 2.0, 6.0, true) == doctest::Approx (3.0));

    // Degenerate: offset past the file length, negative timeline, zero length.
    CHECK (sourceTimeForBin (0.0, 0.0, 10.0, 5.0, false) == doctest::Approx (5.0)); // clamped
    CHECK (sourceTimeForBin (0.0, -3.0, 0.0, 10.0, false) == doctest::Approx (0.0)); // clamped >= 0
    CHECK (sourceTimeForBin (0.0, 3.0, 0.0, 0.0, false) == doctest::Approx (3.0));   // unknown length: linear

    // Clipping / silence via min/max (a scalar isSilent alone is not enough).
    CHECK (peakFromMinMax (-1.0f, -1.0f) == doctest::Approx (1.0f));
    CHECK (peakFromMinMax (0.0f, 0.0f) == doctest::Approx (0.0f));
    CHECK (isSilent (peakFromMinMax (0.0f, 0.0f)));
    CHECK_FALSE (isSilent (peakFromMinMax (-1.0f, 1.0f)));
}

//==============================================================================
// WV-1 (owner report: "the wave is not big in amplitude … a −24 dBFS take is a flat
// strip"): the amplitude scale, the view-only peak normalisation and the channel
// band layout. Pure maths — this is what the drawing in MainComponent::drawClip
// divides up its pixels with.
TEST_CASE ("clip waveform amplitude scale, view normalisation and band layout (WV-1)")
{
    using namespace rrs::clipwave;

    // --- WaveformScale::linear is the historic `peak * halfHeight` -------------
    CHECK (displayNorm (0.0f, WaveformScale::linear) == doctest::Approx (0.0f));
    CHECK (displayNorm (0.25f, WaveformScale::linear) == doctest::Approx (0.25f));
    CHECK (displayNorm (1.0f, WaveformScale::linear) == doctest::Approx (1.0f));
    CHECK (displayNorm (1.5f, WaveformScale::linear) == doctest::Approx (1.0f));  // clamped
    CHECK (displayNorm (-0.5f, WaveformScale::linear) == doctest::Approx (0.0f));
    CHECK (displayNorm (std::numeric_limits<float>::quiet_NaN(), WaveformScale::linear) == 0.0f);

    // --- WaveformScale::decibel spreads the quiet end -------------------------
    CHECK (displayNorm (1.0f, WaveformScale::decibel) == doctest::Approx (1.0f));   // 0 dBFS
    CHECK (displayNorm (0.5f, WaveformScale::decibel) == doctest::Approx (0.8997f).epsilon (0.001f)); // −6 dBFS
    CHECK (displayNorm (0.0631f, WaveformScale::decibel) == doctest::Approx (0.6f).epsilon (0.001f)); // −24 dBFS
    CHECK (displayNorm (0.0f, WaveformScale::decibel) == doctest::Approx (0.0f));
    CHECK (displayNorm (-1.0f, WaveformScale::decibel) == doctest::Approx (0.0f));
    // The dB curve reaches exactly 0 at the silence floor (one seamless picture with
    // `isSilent`), i.e. at −60 dBFS.
    CHECK (silencePeak == doctest::Approx (0.001f));
    CHECK (displayNorm (silencePeak, WaveformScale::decibel) == doctest::Approx (0.0f));
    CHECK (displayNorm (silencePeak * 0.5f, WaveformScale::decibel) == doctest::Approx (0.0f)); // clamped

    // The owner's complaint, quantified: on a 40 px lane (halfH = 20) a −24 dBFS take
    // is ~1 px linear (0.063 * 20 = 1.26) but ~12 px on the dB scale.
    const auto halfH = 20.0f;
    CHECK (displayNorm (0.0631f, WaveformScale::linear) * halfH == doctest::Approx (1.262f).epsilon (0.001f));
    CHECK (displayNorm (0.0631f, WaveformScale::decibel) * halfH == doctest::Approx (12.0f).epsilon (0.001f));

    // Monotonic (non-decreasing) on both curves.
    for (const auto scale : { WaveformScale::linear, WaveformScale::decibel })
    {
        float previous = -1.0f;

        for (const auto peak : { 0.0f, 0.001f, 0.01f, 0.1f, 0.25f, 0.5f, 0.9f, 1.0f, 2.0f })
        {
            const auto norm = displayNorm (peak, scale);
            CHECK (norm >= previous);
            CHECK (norm >= 0.0f);
            CHECK (norm <= 1.0f);
            previous = norm;
        }
    }

    // --- View normalisation (display only) ------------------------------------
    // Off, silent, invalid: identity — nothing is invented.
    CHECK (viewGain (0.0316f, false) == doctest::Approx (1.0f));
    CHECK (viewGain (0.0316f, true) == doctest::Approx (31.6456f));   // −30 dB -> full
    CHECK (viewGain (1.0f, true) == doctest::Approx (1.0f));
    CHECK (viewGain (0.0f, true) == doctest::Approx (1.0f));          // silence
    CHECK (viewGain (0.0005f, true) == doctest::Approx (1.0f));       // below the floor
    CHECK (viewGain (-2.0f, true) == doctest::Approx (1.0f));
    CHECK (viewGain (std::numeric_limits<float>::infinity(), true) == doctest::Approx (1.0f));
    CHECK (viewGain (std::numeric_limits<float>::quiet_NaN(), true) == doctest::Approx (1.0f));
    // A clipped/hot float peak scales DOWN (never amplifies), reaching exactly 1.0.
    CHECK (viewGain (1.5f, true) * 1.5f == doctest::Approx (1.0f));
    // A whisper is capped (+40 dB), so the view cannot become pure noise.
    CHECK (viewGain (0.001f, true) == doctest::Approx (maxViewGain));
    CHECK (viewGain (0.0001f, true) == doctest::Approx (1.0f)); // below the floor

    // The gain really makes a quiet clip readable on the dB scale: −40 dB * 40 -> 0 dB.
    CHECK (displayNorm (0.01f * viewGain (0.01f, true), WaveformScale::decibel)
           == doctest::Approx (1.0f));

    // --- Band layout (WV-3) ---------------------------------------------------
    // Mono (or a clip with no channels) is always ONE band.
    CHECK (bandLayout (0, 96).numBands == 1);
    CHECK (bandLayout (1, 96).numBands == 1);
    CHECK (bandLayout (1, 96).bottomChannel == -1);

    // Stereo: L above, R below — when each half is at least `minBandHeightPx`.
    {
        const auto two = bandLayout (2, 2 * minBandHeightPx);
        CHECK (two.numBands == 2);
        CHECK (two.topChannel == 0);
        CHECK (two.bottomChannel == 1);
    }

    // Too short for two readable bands -> overlay into one.
    CHECK (bandLayout (2, 2 * minBandHeightPx - 1).numBands == 1);
    CHECK (bandLayout (2, 12).numBands == 1);
    CHECK (bandLayout (2, 12).bottomChannel == -1);

    // Wider than stereo: still L/R (channels 0/1), matching the 2-channel metering.
    {
        const auto quad = bandLayout (4, 96);
        CHECK (quad.numBands == 2);
        CHECK (quad.topChannel == 0);
        CHECK (quad.bottomChannel == 1);
        CHECK (bandLayout (8, 12).numBands == 1); // short: one overlaid band
    }

    // --- Band extents tile the band exactly, without overlap -------------------
    {
        const auto one = bandLayout (1, 41);
        CHECK (bandExtent (one, 100, 41, 0).y == 100);
        CHECK (bandExtent (one, 100, 41, 0).height == 41);
        CHECK (bandExtent (one, 100, 41, 1).y == 100);   // no second band: same picture
        CHECK (bandExtent (one, 100, 41, 1).height == 41);
    }
    {
        const auto two = bandLayout (2, 41);
        const auto top = bandExtent (two, 100, 41, 0);
        const auto bottom = bandExtent (two, 100, 41, 1);
        CHECK (top.y == 100);
        CHECK (top.height == 20);
        CHECK (bottom.y == 120);
        CHECK (bottom.height == 21);                  // the odd pixel goes to the bottom
        CHECK (top.height + bottom.height == 41);     // no gap, no overlap
    }

    // --- Persisted scale name round-trips -------------------------------------
    CHECK (waveformScaleToString (WaveformScale::linear) == "linear");
    CHECK (waveformScaleToString (WaveformScale::decibel) == "decibel");
    CHECK (waveformScaleFromString ("decibel") == WaveformScale::decibel);
    CHECK (waveformScaleFromString ("linear") == WaveformScale::linear);
    // A broken or missing value falls back to the SHIPPED default (the same one an
    // absent key gives), so a garbled settings file cannot silently change the view.
    CHECK (waveformScaleFromString ("garbage") == defaultWaveformScale);
    CHECK (waveformScaleFromString ("") == defaultWaveformScale);
    CHECK (waveformScaleFromString ("Linear") == defaultWaveformScale); // exact match
}

//==============================================================================
// Epic 8 phase 2: clip-level live-take detection (a pre-existing clip on an
// armed track must NOT be treated as the take being recorded).
TEST_CASE ("clip live-take detection (Epic 8 phase 2)")
{
    using namespace rrs::clipwave;

    const juce::File a { juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("a.wav") };
    const juce::File b { juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("b.wav") };

    CHECK (isLiveTake (true, true, a, a));       // recording, wave, file matches
    CHECK_FALSE (isLiveTake (true, true, a, b)); // a different clip on the same track
    CHECK_FALSE (isLiveTake (false, true, a, a)); // not recording
    CHECK_FALSE (isLiveTake (true, false, a, a)); // MIDI / non-wave
    CHECK_FALSE (isLiveTake (true, true, a, juce::File())); // no active recording
    CHECK_FALSE (isLiveTake (true, true, juce::File(), a)); // clip has no file
}

//==============================================================================
// Epic 8 phase 1b: the lane control band ("gutter") geometry. Every control is
// laid out inside the left `gutterWidth` px of the lane, in a fixed order, so the
// bands line up into one column and clips start at the same x on every lane.
TEST_CASE ("lane control band geometry (Epic 8 phase 1b)")
{
    using namespace rrs::track_lane;

    CHECK (gutterWidth == 112);

    // Per-height sizes (owner R-A presets 56 / 72 / 96).
    CHECK (controlMetrics (56).arm == 18);
    CHECK (controlMetrics (56).ms == 16);
    CHECK (controlMetrics (56).inputW == 24);
    CHECK (controlMetrics (72).arm == 20);
    CHECK (controlMetrics (72).ms == 18);
    CHECK (controlMetrics (72).inputW == 28);
    CHECK (controlMetrics (96).arm == 20);
    CHECK (controlMetrics (96).inputW == 28);
    CHECK (controlMetrics (56).inputVisible);

    const juce::Rectangle<int> lane { 100, 40, 800, 72 };
    const auto r = computeLaneControlRects (lane);

    // The band is the lane's clip band, left `gutterWidth` px wide.
    CHECK (r.band.getX() == lane.getX());
    CHECK (r.band.getWidth() == gutterWidth);
    CHECK (r.band.getY() == lane.getY() + clipInset);
    CHECK (r.band.getHeight() == lane.getHeight() - 2 * clipInset);

    // Every control sits inside the band, in the fixed arm/mute/solo/input order.
    CHECK (r.band.contains (r.arm));
    CHECK (r.band.contains (r.mute));
    CHECK (r.band.contains (r.solo));
    CHECK (r.band.contains (r.input));
    CHECK (r.arm.getX() < r.mute.getX());
    CHECK (r.mute.getX() < r.solo.getX());
    CHECK (r.solo.getX() < r.input.getX());
    CHECK (r.input.getX() < r.meter.getRight());

    // The clip zone starts exactly at the band's right edge.
    CHECK (r.band.getRight() == lane.getX() + gutterWidth);

    // The band follows the lane's x/hMargin from computeLaneRects (single source).
    const auto lanes = computeLaneRects ({ 0, 0, 900, 600 }, 1);
    REQUIRE (lanes.size() == 1);
    CHECK (computeLaneControlRects (lanes[0]).band.getX() == lanes[0].getX());
    CHECK (lanes[0].getX() == hMargin);
}

//==============================================================================
// Epic 8 phase 1b: x -> control hit-test. The gutter and the clip zone must not
// overlap: a point at/right of the band is the clip zone (none).
TEST_CASE ("lane control hit-test maps x to the right control (Epic 8 phase 1b)")
{
    using namespace rrs::track_lane;

    const juce::Rectangle<int> lane { 100, 40, 800, 72 };
    const auto r = computeLaneControlRects (lane);

    const auto centre = [] (juce::Rectangle<int> rect) { return rect.getCentre(); };

    CHECK (controlAt (lane, true, centre (r.arm)) == LaneControl::arm);
    CHECK (controlAt (lane, true, centre (r.mute)) == LaneControl::mute);
    CHECK (controlAt (lane, true, centre (r.solo)) == LaneControl::solo);
    CHECK (controlAt (lane, true, centre (r.input)) == LaneControl::input);
    CHECK (controlAt (lane, true, centre (r.meter)) == LaneControl::meter);

    // Left pad (inside the band, before the arm) is the empty band.
    CHECK (controlAt (lane, true, { lane.getX() + 1, r.band.getCentreY() }) == LaneControl::band);

    // The first clip-zone pixel is NOT the gutter (it is the timeline).
    CHECK (controlAt (lane, true, { lane.getX() + gutterWidth, r.band.getCentreY() }) == LaneControl::none);
    CHECK (controlAt (lane, true, { lane.getX() - 1, r.band.getCentreY() }) == LaneControl::none);

    // On a playback-only track the input chip / meter are inert (they fall back to
    // the empty band), but mute/solo still work.
    CHECK (controlAt (lane, false, centre (r.input)) == LaneControl::band);
    CHECK (controlAt (lane, false, centre (r.meter)) == LaneControl::band);
    CHECK (controlAt (lane, false, centre (r.mute)) == LaneControl::mute);
}

//==============================================================================
// Epic 8 phase 1b: the gutter column scrolls with the lanes — the control band of
// lane i keeps the same x and shifts by exactly the scroll offset.
TEST_CASE ("gutter and lanes scroll in lockstep (Epic 8 phase 1b)")
{
    using namespace rrs::track_lane;

    const juce::Rectangle<int> area { 0, 0, 900, 600 };
    const auto atTop = computeLaneRects (area, 8, 0);
    const auto scrolled = computeLaneRects (area, 8, 40);
    REQUIRE (atTop.size() == 8);
    REQUIRE (scrolled.size() == 8);

    for (int i = 0; i < 8; ++i)
    {
        const auto bandTop = computeLaneControlRects (atTop[(size_t) i]).band;
        const auto bandScrolled = computeLaneControlRects (scrolled[(size_t) i]).band;

        CHECK (bandTop.getX() == bandScrolled.getX());
        CHECK (bandTop.getHeight() == bandScrolled.getHeight());
        CHECK (bandTop.getY() - bandScrolled.getY() == 40);
    }
}

//==============================================================================
// Epic 8 phase 1b (follow-up): the read-and-clear -> peek meter cache used for
// single-owner metering (MainComponent pumps; mixer strips + lane meters peek).
TEST_CASE ("meter cache stores, peeks (silence before store) and clears (Epic 8 phase 1b)")
{
    struct FakeReading { float db = -100.0f; bool clipped = false; };

    rrs::MeterCache<FakeReading> cache;
    CHECK (cache.size() == 0);

    // Nothing stored yet (or out of range) -> silence.
    CHECK (cache.peek (0).db == doctest::Approx (-100.0f));
    CHECK (cache.peek (5).db == doctest::Approx (-100.0f));
    CHECK (cache.peek (-1).db == doctest::Approx (-100.0f));

    FakeReading r;
    r.db = -6.0f;
    r.clipped = true;
    cache.store (2, r);

    CHECK (cache.peek (2).db == doctest::Approx (-6.0f));
    CHECK (cache.peek (2).clipped);
    // Untouched indices stay silent.
    CHECK (cache.peek (0).db == doctest::Approx (-100.0f));
    CHECK (cache.peek (1).db == doctest::Approx (-100.0f));

    // Overwrite + clear (track-set change / session teardown).
    cache.store (2, FakeReading {});
    CHECK (cache.peek (2).db == doctest::Approx (-100.0f));

    cache.clear();
    CHECK (cache.size() == 0);
    CHECK (cache.peek (2).db == doctest::Approx (-100.0f));
}

//==============================================================================
// RT-1: pure input grouping + channel occupancy (one track = one channel or one
// stereo pair; explicit failures, no silent clamp).
namespace
{
    rrs::TrackInputRequest req (int track, rrs::InputLayout layout, int channel = -1)
    {
        return { track, layout, channel };
    }
}

TEST_CASE ("input grouping: one device per channel on a 4-in interface")
{
    using namespace rrs;

    const std::vector<TrackInputRequest> requests {
        req (0, InputLayout::Mono), req (1, InputLayout::Mono),
        req (2, InputLayout::Mono), req (3, InputLayout::Mono) };

    const auto plan = planInputGrouping (requests, 4);
    REQUIRE (plan.ok);
    REQUIRE (plan.assignments.size() == 4);

    for (int i = 0; i < 4; ++i)
    {
        CHECK (plan.assignments[(size_t) i].trackIndex == i);
        CHECK (plan.assignments[(size_t) i].firstChannel == i);
        CHECK (plan.assignments[(size_t) i].layout == InputLayout::Mono);
    }

    // Cannot hand out more tracks than channels (I3): the 3rd mono track fails.
    const std::vector<TrackInputRequest> tooMany {
        req (0, InputLayout::Mono), req (1, InputLayout::Mono), req (2, InputLayout::Mono) };
    const auto over = planInputGrouping (tooMany, 2);
    CHECK_FALSE (over.ok);
    CHECK (over.error == InputGroupingError::NoFreeChannel);
    CHECK (over.errorTrackIndex == 2);
}

TEST_CASE ("input grouping: a stereo pair keeps two channels on one device")
{
    using namespace rrs;

    const std::vector<TrackInputRequest> requests {
        req (0, InputLayout::Stereo), req (1, InputLayout::Mono), req (2, InputLayout::Mono) };

    const auto plan = planInputGrouping (requests, 4);
    REQUIRE (plan.ok);
    REQUIRE (plan.assignments.size() == 3);

    CHECK (plan.assignments[0].layout == InputLayout::Stereo);
    CHECK (plan.assignments[0].firstChannel == 0);
    CHECK (plan.assignments[1].firstChannel == 2);
    CHECK (plan.assignments[2].firstChannel == 3);

    const auto occ = channelOccupancy (plan.assignments);
    CHECK (occ.at (0) == 0);
    CHECK (occ.at (1) == 0); // the pair belongs to the same track
    CHECK (occ.at (2) == 1);
    CHECK (occ.at (3) == 2);
}

TEST_CASE ("input grouping: default is stereo for 2 and mono-per-channel for >=4")
{
    using namespace rrs;

    CHECK (defaultLayoutForDevice (1) == InputLayout::Mono);
    CHECK (defaultLayoutForDevice (2) == InputLayout::Stereo);
    CHECK (defaultLayoutForDevice (3) == InputLayout::Mono);
    CHECK (defaultLayoutForDevice (4) == InputLayout::Mono);
    CHECK (defaultLayoutForDevice (8) == InputLayout::Mono);

    // Auto on a 2-in device -> one stereo pair (I5).
    const std::vector<TrackInputRequest> twoIn { req (0, InputLayout::Auto) };
    const auto stereoPlan = planInputGrouping (twoIn, 2);
    REQUIRE (stereoPlan.ok);
    REQUIRE (stereoPlan.assignments.size() == 1);
    CHECK (stereoPlan.assignments[0].layout == InputLayout::Stereo);
    CHECK (stereoPlan.assignments[0].firstChannel == 0);

    // Auto on a >=4-in device -> one channel per track (I5).
    const std::vector<TrackInputRequest> fourIn {
        req (0, InputLayout::Auto), req (1, InputLayout::Auto), req (2, InputLayout::Auto) };
    const auto monoPlan = planInputGrouping (fourIn, 4);
    REQUIRE (monoPlan.ok);
    for (int i = 0; i < 3; ++i)
    {
        CHECK (monoPlan.assignments[(size_t) i].layout == InputLayout::Mono);
        CHECK (monoPlan.assignments[(size_t) i].firstChannel == i);
    }
}

TEST_CASE ("input grouping: occupancy reports each channel's owning track")
{
    using namespace rrs;

    const std::vector<TrackInputRequest> requests {
        req (0, InputLayout::Stereo), req (1, InputLayout::Mono) };
    const auto plan = planInputGrouping (requests, 4);
    REQUIRE (plan.ok);

    const auto occ = channelOccupancy (plan.assignments);
    CHECK (occ.size() == 3);
    CHECK (occ.at (0) == 0);
    CHECK (occ.at (1) == 0);
    CHECK (occ.at (2) == 1);

    CHECK_FALSE (isChannelFree (occ, 0));
    CHECK_FALSE (isChannelFree (occ, 1));
    CHECK_FALSE (isChannelFree (occ, 2));
    CHECK (isChannelFree (occ, 3));
    CHECK_FALSE (isChannelFree (occ, -1)); // negative never "free"
}

TEST_CASE ("input grouping: a channel off the device is refused, not clamped")
{
    using namespace rrs;

    // Mono explicit channel beyond the device.
    const std::vector<TrackInputRequest> monoOff { req (0, InputLayout::Mono, 4) };
    const auto a = planInputGrouping (monoOff, 4);
    CHECK_FALSE (a.ok);
    CHECK (a.error == InputGroupingError::ChannelOffDevice);
    CHECK (a.errorTrackIndex == 0);

    // Stereo pair running off the device (channels 3 and 4 on a 4-ch device).
    const std::vector<TrackInputRequest> stereoOff { req (0, InputLayout::Stereo, 3) };
    const auto b = planInputGrouping (stereoOff, 4);
    CHECK_FALSE (b.ok);
    CHECK (b.error == InputGroupingError::ChannelOffDevice);
}

TEST_CASE ("input grouping: stereo is refused when a neighbour channel is taken")
{
    using namespace rrs;

    // Track 0 owns channel 1; track 1 wants a stereo pair on (0,1).
    const std::vector<TrackInputRequest> requests {
        req (0, InputLayout::Mono, 1), req (1, InputLayout::Stereo, 0) };
    const auto plan = planInputGrouping (requests, 4);
    CHECK_FALSE (plan.ok);
    CHECK (plan.error == InputGroupingError::OccupiedNeighbour);
    CHECK (plan.errorTrackIndex == 1);

    // The pure switch check agrees: switching a mono-on-1 track to stereo on 0
    // would take channel 1, owned by track 0.
    const std::vector<SpanOwner> other { SpanOwner { 1, 1, 0 } };
    CHECK (wouldConflictOnSwitchToStereo (0, 4, other));       // channel 1 owned by track 0
    CHECK_FALSE (wouldConflictOnSwitchToStereo (2, 4, other)); // 2-3 is free
    CHECK (wouldConflictOnSwitchToStereo (3, 4, other));       // pair runs off a 4-ch device

    // RT-4: an IDENTICAL span shares — a second stereo pair on 0-1 is not a
    // conflict with an existing 0-1 pair.
    const std::vector<SpanOwner> otherPair { SpanOwner { 0, 2, 0 } };
    CHECK_FALSE (wouldConflictOnSwitchToStereo (0, 4, otherPair));

    // A different-length span on the same start IS a conflict (mono 0 vs pair 0-1).
    const std::vector<SpanOwner> otherMono { SpanOwner { 0, 1, 0 } };
    CHECK (wouldConflictOnSwitchToStereo (0, 4, otherMono));
}

//==============================================================================
// RT-6: the span-aware occupancy check — sharing (RT-4) vs a real conflict.
TEST_CASE ("input occupancy: a span is shared only when it is identical (RT-6)")
{
    using namespace rrs;

    // Track 0 owns channel 1 (mono).
    const std::vector<SpanOwner> other { SpanOwner { 1, 1, 0 } };

    // Free channel (0) — no conflict.
    {
        const auto occ = buildSpanOccupancy (other, 4);
        const auto r = checkSpanOccupancy (0, 1, 4, occ);
        CHECK (r.conflict == SpanConflict::None);
    }

    // Same channel, same length (mono 1 vs mono 1) — sharing, not a conflict.
    {
        const auto occ = buildSpanOccupancy (other, 4);
        const auto r = checkSpanOccupancy (1, 1, 4, occ);
        CHECK (r.conflict == SpanConflict::None);
    }

    // Same start, DIFFERENT length (pair 1-2 vs mono 1) — conflict, owner named.
    {
        const auto occ = buildSpanOccupancy (other, 4);
        const auto r = checkSpanOccupancy (1, 2, 4, occ);
        CHECK (r.conflict == SpanConflict::OwnedByOther);
        CHECK (r.conflictChannel == 1);
        CHECK (r.ownerTrackIndex == 0);
    }

    // A pair whose right channel is owned (0-1 over a mono on 1) — conflict on 1.
    {
        const auto occ = buildSpanOccupancy (other, 4);
        const auto r = checkSpanOccupancy (0, 2, 4, occ);
        CHECK (r.conflict == SpanConflict::OwnedByOther);
        CHECK (r.conflictChannel == 1);
    }

    // Off the device — its own outcome, never an owner.
    {
        const auto occ = buildSpanOccupancy (other, 4);
        const auto r = checkSpanOccupancy (3, 2, 4, occ);
        CHECK (r.conflict == SpanConflict::OffDevice);
    }

    // An unassigned track (zero-length span) is never a conflict.
    {
        const auto occ = buildSpanOccupancy (other, 4);
        CHECK (checkSpanOccupancy (0, 0, 4, occ).conflict == SpanConflict::None);
    }

    // An identical stereo pair shares (both channels).
    {
        const std::vector<SpanOwner> pair { SpanOwner { 0, 2, 0 } };
        const auto occ = buildSpanOccupancy (pair, 4);
        CHECK (checkSpanOccupancy (0, 2, 4, occ).conflict == SpanConflict::None);
    }
}

//==============================================================================
// RT-6: the span of a mapping follows its layout (one source used by the menu and
// the conflict check).
TEST_CASE ("input mapping: the span follows the layout (RT-6)")
{
    using namespace rrs;

    CHECK (mappingChannelSpan (InputMapping { 0, 1, InputLayout::None }, 4) == 0);
    CHECK (mappingChannelSpan (InputMapping { 0, 1, InputLayout::Mono }, 4) == 1);
    CHECK (mappingChannelSpan (InputMapping { 0, 2, InputLayout::Stereo }, 4) == 2);
    CHECK (mappingChannelSpan (InputMapping { 1, 3, InputLayout::MultiChannel }, 4) == 3);
    // Auto follows the device default (4 ch -> mono per channel).
    CHECK (mappingChannelSpan (InputMapping { 0, 1, InputLayout::Auto }, 4) == 1);
    CHECK (mappingChannelSpan (InputMapping { 0, 1, InputLayout::Auto }, 2) == 2);
}

//==============================================================================
// RT-8: the Channel menu marks free / shared / used / off-device.
TEST_CASE ("input occupancy: the channel menu marks free, shared, used and off-device (RT-8)")
{
    using namespace rrs;

    // Track 0 (mono) owns channel 1; we build the menu for a MONO track.
    const std::vector<SpanOwner> other { SpanOwner { 1, 1, 0 } };
    const auto monoMenu = channelMenuModel (1, 4, other);
    REQUIRE (monoMenu.size() == 4);

    CHECK (monoMenu[0].availability == ChannelAvailability::Free);
    CHECK (monoMenu[0].ownerTrackIndex == -1);
    CHECK (monoMenu[1].availability == ChannelAvailability::Shared); // identical span
    CHECK (monoMenu[1].ownerTrackIndex == 0);
    CHECK (monoMenu[2].availability == ChannelAvailability::Free);
    CHECK (monoMenu[3].availability == ChannelAvailability::Free);

    // The same occupancy seen by a STEREO track: channel 1 is now a different span
    // (used), and the last channel cannot start a pair (off-device).
    const auto stereoMenu = channelMenuModel (2, 4, other);
    REQUIRE (stereoMenu.size() == 4);
    CHECK (stereoMenu[1].availability == ChannelAvailability::Used);
    CHECK (stereoMenu[1].ownerTrackIndex == 0);
    CHECK (stereoMenu[3].availability == ChannelAvailability::OffDevice);

    // A stereo pair owner is shared only by an identical stereo pair.
    const std::vector<SpanOwner> pair { SpanOwner { 0, 2, 0 } };
    const auto pairMenu = channelMenuModel (2, 4, pair);
    CHECK (pairMenu[0].availability == ChannelAvailability::Shared);
    CHECK (pairMenu[1].availability == ChannelAvailability::Used);
    CHECK (pairMenu[2].availability == ChannelAvailability::Free);
    CHECK (pairMenu[3].availability == ChannelAvailability::OffDevice);
}

//==============================================================================
// RT-6/RT-8: when a span is BOTH off-device and overlapping an owner, the
// off-device outcome wins (RT-2b: an off-device pin is a valid, reported state,
// never a "taken" clash).
TEST_CASE ("input occupancy: off-device wins over an owner (RT-6)")
{
    using namespace rrs;

    // Owner: mono on channel 3 (the last channel of a 4-ch device).
    const std::vector<SpanOwner> other { SpanOwner { 3, 1, 0 } };
    const auto occ = buildSpanOccupancy (other, 4);

    // A stereo pair at 3 runs off the device AND touches the owner: off-device.
    const auto check = checkSpanOccupancy (3, 2, 4, occ);
    CHECK (check.conflict == SpanConflict::OffDevice);
    CHECK (check.ownerTrackIndex == -1); // off-device blames no owner

    // ... and the Channel menu says so: channel 3 (its pair runs off-device) and
    // channel 2 (its pair would take the owner's channel 3) differ, while 1 is free.
    const auto menu = channelMenuModel (2, 4, other);
    CHECK (menu[1].availability == ChannelAvailability::Free);
    CHECK (menu[2].availability == ChannelAvailability::Used);       // pair 2-3 takes ch 3
    CHECK (menu[3].availability == ChannelAvailability::OffDevice);  // pair 3-4 off-device
}

//==============================================================================
// B1 regression: the menu id decoder must never confuse a Layout id (200..203)
// with a channel (100..199). This is the unit test whose absence let the bug ship.
TEST_CASE ("input menu: ids decode to channels vs layouts and never cross over (B1)")
{
    using namespace rrs::input_menu;

    // Channels: 100 + channel, up to (but not including) the layout base.
    for (int channel = 0; channel < 8; ++channel)
    {
        const auto sel = decodeResult (channelItemBase + channel);
        CHECK (sel.kind == MenuSelection::Kind::Channel);
        CHECK (sel.channel == channel);
    }

    // The last addressable channel and the first layout id do not overlap.
    CHECK (decodeResult (channelItemBase + 99).kind == MenuSelection::Kind::Channel);
    CHECK (decodeResult (channelItemBase + 99).channel == 99);

    // Layouts: each decodes to a Layout, never to a channel.
    const std::pair<int, InputLayout> layouts[] {
        { layoutAuto, InputLayout::Auto }, { layoutMono, InputLayout::Mono },
        { layoutStereo, InputLayout::Stereo }, { layoutMulti, InputLayout::MultiChannel } };

    for (const auto& [id, layout] : layouts)
    {
        const auto sel = decodeResult (id);
        CHECK (sel.kind == MenuSelection::Kind::Layout);
        CHECK (sel.layout == layout);
        CHECK (sel.channel == -1);                 // the B1 symptom: 201 -> channel 101
        CHECK_FALSE (sel.kind == MenuSelection::Kind::Channel);
    }

    // The exact ids from the bug report are layouts, not channels 101/102/103.
    CHECK (decodeResult (201).layout == InputLayout::Mono);
    CHECK (decodeResult (202).layout == InputLayout::Stereo);
    CHECK (decodeResult (203).layout == InputLayout::MultiChannel);

    // Dismissed / unknown ids decode to nothing.
    CHECK_FALSE (decodeResult (0).isValid());
    CHECK_FALSE (decodeResult (-1).isValid());
    CHECK_FALSE (decodeResult (204).isValid());
    CHECK_FALSE (decodeResult (99).isValid());
}

//==============================================================================
// B1: applying a menu result changes the LAYOUT for a layout id and the CHANNEL
// for a channel id — the same code path both popups use.
TEST_CASE ("input menu: applying a result changes layout vs channel correctly (B1)")
{
    using namespace rrs;
    using namespace rrs::input_menu;

    const int numChannels = 4;

    // A layout id must never touch the channel.
    {
        InputMapping m { 1, 1, InputLayout::Mono };
        CHECK (applySelection (decodeResult (layoutStereo), m, numChannels));
        CHECK (m.layout == InputLayout::Stereo);
        CHECK (m.firstChannel == 1);       // unchanged (the B1 bug wrote 102)
        CHECK (m.numChannels == 2);
    }

    const std::pair<int, InputLayout> otherLayouts[] {
        { layoutAuto, InputLayout::Auto }, { layoutMono, InputLayout::Mono },
        { layoutMulti, InputLayout::MultiChannel } };

    for (const auto& [id, layout] : otherLayouts)
    {
        InputMapping m { 1, 1, InputLayout::Mono };
        CHECK (applySelection (decodeResult (id), m, numChannels));
        CHECK (m.layout == layout);
        CHECK (m.firstChannel == 1);
    }

    // A channel id changes the channel (and assigns an unassigned track).
    {
        InputMapping m { 0, 1, InputLayout::None };
        CHECK (applySelection (decodeResult (channelItemBase + 3), m, numChannels));
        CHECK (m.firstChannel == 3);
        CHECK (m.layout == InputLayout::Mono);   // RT-4: picking assigns
        CHECK (m.numChannels == 1);
    }

    // A channel id keeps an already-set layout.
    {
        InputMapping m { 0, 2, InputLayout::Stereo };
        CHECK (applySelection (decodeResult (channelItemBase + 1), m, numChannels));
        CHECK (m.firstChannel == 1);
        CHECK (m.layout == InputLayout::Stereo);
        CHECK (m.numChannels == 2);
    }

    // Multi takes every remaining channel from `firstChannel`.
    {
        InputMapping m { 0, 1, InputLayout::Mono };
        CHECK (applySelection (decodeResult (layoutMulti), m, numChannels));
        CHECK (m.layout == InputLayout::MultiChannel);
        CHECK (m.numChannels == numChannels);
    }

    // Nothing decodes -> nothing changes.
    {
        InputMapping m { 2, 1, InputLayout::Mono };
        const auto before = m;
        CHECK_FALSE (applySelection (decodeResult (0), m, numChannels));
        CHECK (m == before);
    }
}

//==============================================================================
// RT-6: `probeLayoutAvailability` must not offer an unavailable layout and must
// not hide an available one.
TEST_CASE ("input menu: layout availability probe reflects the refusal rule (RT-6)")
{
    using namespace rrs;
    using namespace rrs::input_menu;

    const int numChannels = 4;
    const InputMapping mono { 1, 1, InputLayout::Mono };

    // Only Stereo is refused (its pair would take a channel owned by another
    // track); everything else stays available.
    const auto stereoBlocked = probeLayoutAvailability (
        mono, numChannels,
        [] (const InputMapping& probe) { return probe.layout != InputLayout::Stereo; });

    CHECK (stereoBlocked.autoLayout);
    CHECK (stereoBlocked.mono);
    CHECK_FALSE (stereoBlocked.stereo);
    CHECK (stereoBlocked.multi);

    // The probe passes the RIGHT mapping for each layout (Auto keeps the channel,
    // Stereo is a 2-channel pair, Multi spans to the device end).
    std::vector<InputMapping> seen;

    juce::ignoreUnused (probeLayoutAvailability (
        mono, numChannels,
        [&seen] (const InputMapping& probe) { seen.push_back (probe); return true; }));

    REQUIRE (seen.size() == 4u);
    CHECK (seen[0].layout == InputLayout::Auto);
    CHECK (seen[0].firstChannel == 1);
    CHECK (seen[1].layout == InputLayout::Mono);
    CHECK (seen[1].numChannels == 1);
    CHECK (seen[2].layout == InputLayout::Stereo);
    CHECK (seen[2].numChannels == 2);
    CHECK (seen[3].layout == InputLayout::MultiChannel);
    CHECK (seen[3].numChannels == numChannels - 1);
}

//==============================================================================
// M3 invariant: the planner and the standalone span occupancy give the SAME
// verdict (free / shared / taken / off-device) for the same situation — the two
// "sharing" implementations cannot silently drift.
TEST_CASE ("input grouping: the planner agrees with checkSpanOccupancy (M3 invariant)")
{
    using namespace rrs;

    // A pinned mono on channel 1 (track 0) and a pinned pair 0-1 (track 1).
    const auto plannerVerdict = [ ] (int first, int need,
                                     const std::vector<TrackInputRequest>& priorRequests,
                                     int deviceChannels)
    {
        std::vector<TrackInputRequest> requests = priorRequests;
        requests.push_back (TrackInputRequest { 99, need == 2 ? InputLayout::Stereo : InputLayout::Mono,
                                                first, need });
        const auto plan = planInputLayout (requests, deviceChannels);

        for (const auto& t : plan.tracks)
            if (t.trackIndex == 99)
                return t.error;

        return InputGroupingError::InvalidTrackIndex;
    };

    const std::vector<TrackInputRequest> prior { TrackInputRequest { 0, InputLayout::Mono, 1, 0 } };
    const std::vector<SpanOwner> occ { SpanOwner { 1, 1, 0 } };
    const auto occupancy = buildSpanOccupancy (occ, 4);

    // Free: channel 0 (mono).
    CHECK (plannerVerdict (0, 1, prior, 4) == InputGroupingError::None);
    CHECK (checkSpanOccupancy (0, 1, 4, occupancy).conflict == SpanConflict::None);

    // Shared: channel 1 (identical mono span).
    CHECK (plannerVerdict (1, 1, prior, 4) == InputGroupingError::None);
    CHECK (checkSpanOccupancy (1, 1, 4, occupancy).conflict == SpanConflict::None);

    // Taken: channel 1 as a pair (different length).
    CHECK (plannerVerdict (1, 2, prior, 4) == InputGroupingError::OccupiedNeighbour);
    CHECK (checkSpanOccupancy (1, 2, 4, occupancy).conflict == SpanConflict::OwnedByOther);

    // Off-device: a pair at channel 3.
    CHECK (plannerVerdict (3, 2, prior, 4) == InputGroupingError::ChannelOffDevice);
    CHECK (checkSpanOccupancy (3, 2, 4, occupancy).conflict == SpanConflict::OffDevice);
}

//==============================================================================
// RT-7: the input-device change signature is a PURE comparison of two halves —
// the wave-device count AND the active input-channel count. The second half is
// what catches a card that keeps the same number of WaveInputDevices but exposes
// a different number of active channels (the owner's 2 -> 8 -> 2 scenario); the
// integration tests would still pass with the device count alone, so this unit
// test is what pins the channel-count half.
TEST_CASE ("input signature: both halves of the device change are compared (RT-7)")
{
    // Nothing changed: no re-apply.
    CHECK_FALSE (inputSignatureChanged (1, 2, 1, 2));
    CHECK_FALSE (inputSignatureChanged (4, 32, 4, 32));

    // Only the DEVICE COUNT changed (a different card with the same channel count).
    CHECK (inputSignatureChanged (2, 2, 1, 2));

    // Only the ACTIVE CHANNEL COUNT changed — the same device count (the owner's
    // 2 -> 8 -> 2 switch). This is the half the integration harness alone does not
    // isolate, because switching inputChannels there also rebuilds the device list.
    CHECK (inputSignatureChanged (1, 8, 1, 2));
    CHECK (inputSignatureChanged (1, 2, 1, 8));

    // Unknown signature (session closed / never applied) — either half being the
    // `-1` sentinel counts as a change.
    CHECK (inputSignatureChanged (1, 2, -1, -1));
    CHECK (inputSignatureChanged (1, 2, 1, -1));
    CHECK (inputSignatureChanged (1, 2, -1, 2));
    CHECK (inputSignatureChanged (0, 0, -1, -1));
}

TEST_CASE ("input grouping: deterministic — the result does not depend on insertion order")
{
    using namespace rrs;

    const std::vector<TrackInputRequest> forward {
        req (0, InputLayout::Stereo), req (1, InputLayout::Mono), req (2, InputLayout::Stereo) };
    const std::vector<TrackInputRequest> reversed {
        req (2, InputLayout::Stereo), req (1, InputLayout::Mono), req (0, InputLayout::Stereo) };

    const auto a = planInputGrouping (forward, 6);
    const auto b = planInputGrouping (reversed, 6);
    REQUIRE (a.ok);
    REQUIRE (b.ok);
    REQUIRE (a.assignments.size() == b.assignments.size());

    for (size_t i = 0; i < a.assignments.size(); ++i)
        CHECK (a.assignments[i] == b.assignments[i]);

    // RT-4: an IDENTICAL span may be shared — two mono tracks on channel 0 are
    // both valid, in either order (no "last wins").
    const std::vector<TrackInputRequest> shareA { req (1, InputLayout::Mono, 0), req (0, InputLayout::Mono, 0) };
    const std::vector<TrackInputRequest> shareB { req (0, InputLayout::Mono, 0), req (1, InputLayout::Mono, 0) };
    const auto sa = planInputGrouping (shareA, 4);
    const auto sb = planInputGrouping (shareB, 4);
    CHECK (sa.ok);
    CHECK (sb.ok);
    REQUIRE (sa.assignments.size() == 2);
    CHECK (sa.assignments[(size_t) 0] == sb.assignments[(size_t) 0]);
    CHECK (sa.assignments[(size_t) 1] == sb.assignments[(size_t) 1]);
    for (const auto& shared : sa.assignments)
        CHECK (shared.firstChannel == 0);

    // A DIFFERENT overlapping span is still refused (deterministic; owner named).
    const std::vector<TrackInputRequest> clashA { req (1, InputLayout::Stereo, 0), req (0, InputLayout::Mono, 0) };
    const std::vector<TrackInputRequest> clashB { req (0, InputLayout::Mono, 0), req (1, InputLayout::Stereo, 0) };
    const auto pa = planInputGrouping (clashA, 4);
    const auto pb = planInputGrouping (clashB, 4);
    CHECK_FALSE (pa.ok);
    CHECK_FALSE (pb.ok);
    CHECK (pa.error == InputGroupingError::OccupiedNeighbour);
    CHECK (pb.error == InputGroupingError::OccupiedNeighbour);
    CHECK (pa.errorTrackIndex == 1);
    CHECK (pb.errorTrackIndex == 1);
    CHECK (pa.assignments.empty());
    CHECK (pb.assignments.empty());
}

//==============================================================================
// RT-4: an unassigned (`InputLayout::None`) track is a valid state — it consumes
// no channel and is not an error, but it is also not placed.
TEST_CASE ("input grouping: an unassigned track is valid and not placed (RT-4)")
{
    using namespace rrs;

    const std::vector<TrackInputRequest> reqs {
        req (0, InputLayout::Mono, 0),
        req (1, InputLayout::None),
        req (2, InputLayout::Mono, 1) };

    const auto plan = planInputLayout (reqs, 2);
    REQUIRE (plan.tracks.size() == 3);

    CHECK (plan.tracks[0].placed);
    CHECK (plan.tracks[0].firstChannel == 0);

    CHECK_FALSE (plan.tracks[1].placed);
    CHECK (plan.tracks[1].layout == InputLayout::None);
    CHECK (plan.tracks[1].error == InputGroupingError::None); // valid, no error

    CHECK (plan.tracks[2].placed);
    CHECK (plan.tracks[2].firstChannel == 1);

    // planInputGrouping ignores the unassigned track.
    const auto grouping = planInputGrouping (reqs, 2);
    CHECK (grouping.ok);
    CHECK (grouping.assignments.size() == 2);
}

//==============================================================================
// RT-1 review regressions.
TEST_CASE ("input grouping: a non-positive device channel count is NoDevice, deterministically")
{
    using namespace rrs;

    // No device + no requests.
    const std::vector<TrackInputRequest> none;
    const auto empty = planInputGrouping (none, 0);
    CHECK_FALSE (empty.ok);
    CHECK (empty.error == InputGroupingError::NoDevice);
    CHECK (empty.errorTrackIndex == -1);
    CHECK (empty.assignments.empty());

    CHECK_FALSE (planInputGrouping (none, -1).ok);
    CHECK (planInputGrouping (none, -1).error == InputGroupingError::NoDevice);

    // No device + requests: still NoDevice (not NoFreeChannel), and errorTrackIndex
    // follows the canonical (sorted) order regardless of the input order (High-2).
    const std::vector<TrackInputRequest> a { req (9, InputLayout::Mono) };
    const std::vector<TrackInputRequest> b { req (9, InputLayout::Mono), req (3, InputLayout::Mono) };
    const std::vector<TrackInputRequest> c { req (3, InputLayout::Mono), req (9, InputLayout::Mono) };

    CHECK (planInputGrouping (a, 0).error == InputGroupingError::NoDevice);
    CHECK (planInputGrouping (a, 0).errorTrackIndex == 9);
    CHECK (planInputGrouping (b, 0).errorTrackIndex == 3);
    CHECK (planInputGrouping (c, 0).errorTrackIndex == 3);
}

TEST_CASE ("input grouping: 1-channel and 3-channel devices plan")
{
    using namespace rrs;

    // 1 channel -> Mono (I5); a second track is refused (device full).
    const std::vector<TrackInputRequest> two { req (0, InputLayout::Auto), req (1, InputLayout::Auto) };
    const auto one = planInputGrouping (two, 1);
    CHECK_FALSE (one.ok);
    CHECK (one.error == InputGroupingError::NoFreeChannel);
    CHECK (one.errorTrackIndex == 1);
    CHECK (one.assignments.empty());

    // 3 channels -> Auto resolves to Mono per channel (I5): 3 tracks fit, the 4th fails.
    const std::vector<TrackInputRequest> four {
        req (0, InputLayout::Auto), req (1, InputLayout::Auto), req (2, InputLayout::Auto), req (3, InputLayout::Auto) };
    const auto three = planInputGrouping (four, 3);
    CHECK_FALSE (three.ok);
    CHECK (three.error == InputGroupingError::NoFreeChannel);
    CHECK (three.errorTrackIndex == 3);
    CHECK (three.assignments.empty());

    // A stereo pair + a mono on 3 channels is fine.
    const std::vector<TrackInputRequest> pairAndMono { req (0, InputLayout::Stereo), req (1, InputLayout::Mono) };
    const auto pm = planInputGrouping (pairAndMono, 3);
    REQUIRE (pm.ok);
    REQUIRE (pm.assignments.size() == 2);
    CHECK (pm.assignments[0].firstChannel == 0);
    CHECK (pm.assignments[0].layout == InputLayout::Stereo);
    CHECK (pm.assignments[1].firstChannel == 2);
    CHECK (pm.assignments[1].layout == InputLayout::Mono);
}

TEST_CASE ("input grouping: a negative track index is refused (B1)")
{
    using namespace rrs;

    const std::vector<TrackInputRequest> dup { req (-1, InputLayout::Mono), req (-1, InputLayout::Mono) };
    const auto plan = planInputGrouping (dup, 4);
    CHECK_FALSE (plan.ok);
    CHECK (plan.error == InputGroupingError::InvalidTrackIndex);
    CHECK (plan.assignments.empty()); // nothing placed on the two default indices

    // Validation happens before any placement, so a later bad index still poisons the plan.
    const std::vector<TrackInputRequest> mixed { req (0, InputLayout::Mono), req (-2, InputLayout::Mono) };
    const auto m = planInputGrouping (mixed, 4);
    CHECK_FALSE (m.ok);
    CHECK (m.error == InputGroupingError::InvalidTrackIndex);
    CHECK (m.errorTrackIndex == -2);
    CHECK (m.assignments.empty());
}

TEST_CASE ("input grouping: planLayoutFor and channelsPerLayout")
{
    using namespace rrs;

    // Auto delegates to defaultLayoutForDevice (InputMapping.h): 2 -> Stereo, else Mono.
    CHECK (planLayoutFor (InputLayout::Auto, 1) == InputLayout::Mono);
    CHECK (planLayoutFor (InputLayout::Auto, 2) == InputLayout::Stereo);
    CHECK (planLayoutFor (InputLayout::Auto, 3) == InputLayout::Mono);
    CHECK (planLayoutFor (InputLayout::Auto, 8) == InputLayout::Mono);

    // RT-1 does not support N-channel groups: MultiChannel plans as a stereo pair.
    CHECK (planLayoutFor (InputLayout::MultiChannel, 8) == InputLayout::Stereo);

    // Explicit layouts pass through.
    CHECK (planLayoutFor (InputLayout::Mono, 8) == InputLayout::Mono);
    CHECK (planLayoutFor (InputLayout::Stereo, 8) == InputLayout::Stereo);

    // channelsPerLayout takes a RESOLVED layout only: Stereo -> 2, everything else -> 1.
    CHECK (channelsPerLayout (InputLayout::Stereo) == 2);
    CHECK (channelsPerLayout (InputLayout::Mono) == 1);
    CHECK (channelsPerLayout (InputLayout::Auto) == 1);
    CHECK (channelsPerLayout (InputLayout::MultiChannel) == 1);
}

TEST_CASE ("input grouping: on failure the plan carries no assignments (High-1)")
{
    using namespace rrs;

    // First stereo pair fits on 4 channels, second does not -> the whole plan is
    // empty (no half-placed assignments).
    const std::vector<TrackInputRequest> twoP { req (0, InputLayout::Stereo), req (1, InputLayout::Stereo) };
    const auto p = planInputGrouping (twoP, 2);
    CHECK_FALSE (p.ok);
    CHECK (p.error == InputGroupingError::NoFreeChannel);
    CHECK (p.assignments.empty());

    // A good placement followed by an off-device explicit channel is also empty.
    const std::vector<TrackInputRequest> mixed { req (0, InputLayout::Mono, 0), req (1, InputLayout::Mono, 4) };
    const auto m = planInputGrouping (mixed, 4);
    CHECK_FALSE (m.ok);
    CHECK (m.error == InputGroupingError::ChannelOffDevice);
    CHECK (m.assignments.empty());
}

TEST_CASE ("input grouping: requestedChannel near INT_MAX is off-device, not overflow (B3)")
{
    using namespace rrs;

    const std::vector<TrackInputRequest> maxCh { req (0, InputLayout::Mono, INT_MAX) };
    const auto a = planInputGrouping (maxCh, 4);
    CHECK_FALSE (a.ok);
    CHECK (a.error == InputGroupingError::ChannelOffDevice); // not OccupiedNeighbour
    CHECK (a.errorTrackIndex == 0);

    const std::vector<TrackInputRequest> maxMono { req (0, InputLayout::Mono, INT_MAX - 1) };
    CHECK (planInputGrouping (maxMono, 4).error == InputGroupingError::ChannelOffDevice);

    const std::vector<TrackInputRequest> maxStereo { req (0, InputLayout::Stereo, INT_MAX) };
    CHECK (planInputGrouping (maxStereo, 4).error == InputGroupingError::ChannelOffDevice);
}

TEST_CASE ("input grouping: two stereo pairs on 4 channels")
{
    using namespace rrs;

    const std::vector<TrackInputRequest> requests { req (0, InputLayout::Stereo), req (1, InputLayout::Stereo) };
    const auto plan = planInputGrouping (requests, 4);
    REQUIRE (plan.ok);
    REQUIRE (plan.assignments.size() == 2);
    CHECK (plan.assignments[0].firstChannel == 0);
    CHECK (plan.assignments[1].firstChannel == 2);

    const auto occ = channelOccupancy (plan.assignments);
    CHECK (occ.at (0) == 0);
    CHECK (occ.at (1) == 0);
    CHECK (occ.at (2) == 1);
    CHECK (occ.at (3) == 1);
}

TEST_CASE ("input grouping: an empty request list on a working device is ok and empty")
{
    using namespace rrs;

    const std::vector<TrackInputRequest> none;
    const auto a = planInputGrouping (none, 4);
    CHECK (a.ok);
    CHECK (a.error == InputGroupingError::None);
    CHECK (a.assignments.empty());

    CHECK (planInputGrouping (none, 2).ok);
}



//==============================================================================
// Epic 8 phase 1b: control metrics bucket boundaries (the auto height 56..72
// must bucket correctly) and the input-visible degradation rule.
TEST_CASE ("lane control metrics boundaries (Epic 8 phase 1b)")
{
    using namespace rrs::track_lane;

    // input-visible rule: < 44 hides the chip/meter.
    CHECK_FALSE (controlMetrics (43).inputVisible);
    CHECK (controlMetrics (44).inputVisible);
    CHECK (controlMetrics (56).inputVisible);

    // compact (< 64) / normal (64..83) / large (>= 84).
    CHECK (controlMetrics (63).arm == 18);
    CHECK (controlMetrics (63).ms == 16);
    CHECK (controlMetrics (64).arm == 20);
    CHECK (controlMetrics (64).ms == 18);
    CHECK (controlMetrics (83).arm == 20);
    CHECK (controlMetrics (83).inputW == 28);
    CHECK (controlMetrics (84).arm == 20);
    CHECK (controlMetrics (84).inputW == 28);
}

//==============================================================================
// Epic 8 phase 1b: points in the gaps between controls are the empty band, and
// the mini meter is nested vertically inside the band.
TEST_CASE ("lane control gaps and meter geometry (Epic 8 phase 1b)")
{
    using namespace rrs::track_lane;

    const juce::Rectangle<int> lane { 100, 40, 800, 72 };
    const auto r = computeLaneControlRects (lane);
    const int cy = r.band.getCentreY();

    // The inter-control gaps fall back to the empty band (no accidental control).
    CHECK (controlAt (lane, true, { r.arm.getRight() + 1, cy }) == LaneControl::band);  // arm|mute gap
    CHECK (controlAt (lane, true, { r.mute.getRight() + 1, cy }) == LaneControl::band); // mute|solo gap
    CHECK (controlAt (lane, true, { r.solo.getRight() + 1, cy }) == LaneControl::band); // solo|input gap

    // The meter is a tall thin bar nested in the band.
    CHECK (r.meter.getY() >= r.band.getY());
    CHECK (r.meter.getBottom() <= r.band.getBottom());
    CHECK (r.meter.getHeight() >= 6);
    CHECK (r.meter.getWidth() == controlMetrics (lane.getHeight()).meterW);
}

//==============================================================================
// Epic 8 phase 1b (HIGH): the clip zone starts EXACTLY on the gutter's right
// edge, and the timeline map's 0 s lands there too — as a function of
// hMargin / gutterWidth / laneInset (no reliance on "they happen to be 8").
TEST_CASE ("clip zone starts on the gutter right edge (Epic 8 phase 1b)")
{
    using namespace rrs::track_lane;

    const juce::Rectangle<int> area { 40, 0, 900, 400 };

    CHECK (hMargin == laneInset);

    // The gutter's right edge (== clip-zone left) for the layout's lanes.
    const auto lanes = computeLaneRects (area, 1);
    REQUIRE (lanes.size() == 1);
    CHECK (computeLaneControlRects (lanes[0]).band.getRight() == laneContentX (area));
    CHECK (laneContentX (area) == area.getX() + hMargin + gutterWidth);

    // The map area is the lane area minus the gutter; its inset lands 0 s exactly
    // on the gutter's right edge.
    CHECK (timelineAreaFor (area).getX() == area.getX() + gutterWidth);
    CHECK (timelineAreaFor (area).getX() + laneInset == laneContentX (area));
}

//==============================================================================
// Epic 8 phase 1a: the device panel only reserves space for an error while one
// is shown (the empty ~96 px error label is reclaimed in the sidebar).
TEST_CASE ("device panel reclaims the empty error space (Epic 8 phase 1a)")
{
    CHECK (devicePanelHeightFor (false) < devicePanelHeightFor (true));

    // At the compact height the status label is still laid out (not collapsed).
    const auto compact = computeDevicePanelLayout ({ 0, 0, 900, devicePanelHeightCompact }, false);
    CHECK (compact.statusLabel.getHeight() > 0);

    const auto noError = computeDevicePanelLayout ({ 0, 0, 900, devicePanelHeightCompact }, false);
    CHECK (noError.statusLabel.getHeight() > 0);
    CHECK (noError.errorLabel.isEmpty());
    CHECK (noError.inputBox.getBottom() <= noError.statusLabel.getY());

    // With an error the full panel keeps a multi-line error label.
    const auto withError = computeDevicePanelLayout ({ 0, 0, 900, devicePanelHeight }, true);
    CHECK (withError.errorLabel.getHeight() >= 80);
    CHECK (withError.errorLabel.getY() >= withError.statusLabel.getBottom());
}

//==============================================================================
// Insert-plugin eligibility (owner bug fix). Instruments/panners are not audio
// effects: DLSMusicDevice (isInstrument, category "Synth") and HRTFPanner
// (category "Panner") must be rejected from the track insert, while ordinary
// effects pass.
TEST_CASE ("insert-plugin filter rejects instruments and non-effects")
{
    using namespace rrs::plugin_selection;

    // DLSMusicDevice: JUCE sets isInstrument and category "Synth".
    CHECK (isInstrumentOrNonEffect (true, "Synth"));
    CHECK_FALSE (insertRejectionReason (true, "Synth", 0, 4).isEmpty());

    // HRTFPanner: an AU effect-typed panner (category "Panner").
    CHECK (isNonEffectCategory ("Panner"));
    CHECK_FALSE (insertRejectionReason (false, "Panner", 2, 2).isEmpty());

    // Generators / mixers / MIDI effects are non-effects too.
    CHECK (isNonEffectCategory ("Generator"));
    CHECK (isNonEffectCategory ("Mixer"));
    CHECK (isNonEffectCategory ("MidiEffects"));

    // An ordinary stereo effect passes, including one with no reported category.
    CHECK (insertRejectionReason (false, "Fx", 2, 2).isEmpty());
    CHECK (insertRejectionReason (false, "", 2, 2).isEmpty());
    CHECK (insertRejectionReason (false, "Effect", 1, 1).isEmpty());

    // An unknown category is not treated as a non-effect (no false rejects).
    CHECK_FALSE (isNonEffectCategory (""));

    // A positive asymmetric layout (1-in/2-out) is flagged; unknown 0/0 is not.
    CHECK (hasIncompatibleInsertLayout (1, 2));
    CHECK (hasIncompatibleInsertLayout (0, 2));
    CHECK_FALSE (hasIncompatibleInsertLayout (2, 2));
    CHECK_FALSE (hasIncompatibleInsertLayout (0, 0));
    CHECK_FALSE (hasIncompatibleInsertLayout (1, 1));
}

// Epic 5 (FR-MIDI-3): the MIDI track's *instrument slot* is the mirror of the
// effect insert. Instruments (synths/samplers) pass; audio effects are rejected
// so an instrument track can never host an effect in the instrument slot and the
// two slots stay mutually exclusive.
TEST_CASE ("instrument-slot filter accepts instruments and rejects effects")
{
    using namespace rrs::plugin_selection;

    // A VST3 synth: isInstrument set, category "Synth".
    CHECK (isInstrumentPlugin (true, "Synth"));
    CHECK (instrumentRejectionReason (true, "Synth").isEmpty());

    // A sampler reported by category only.
    CHECK (isInstrumentPlugin (false, "Instrument"));
    CHECK (instrumentRejectionReason (false, "Instrument").isEmpty());

    // Generators are instrument-like (they create sound).
    CHECK (isInstrumentPlugin (false, "Generator"));

    // Audio effects must not land in the instrument slot.
    CHECK_FALSE (isInstrumentPlugin (false, "Fx"));
    CHECK_FALSE (isInstrumentPlugin (false, "Effect"));
    CHECK_FALSE (isInstrumentPlugin (false, ""));
    CHECK_FALSE (instrumentRejectionReason (false, "Effect").isEmpty());

    // Panners/mixers are neither instruments nor valid effects.
    CHECK_FALSE (isInstrumentPlugin (false, "Panner"));
    CHECK_FALSE (isInstrumentPlugin (false, "Mixer"));
}

//==============================================================================
// Epic 7 (FR-REC-8): crash-safe WAV header repair.
//
// These exercise `rrs::wavrepair::repairTruncatedHeader` on real 24-bit WAVs:
// the "crash before the first Tracktion flush" state is a valid header whose
// RIFF and `data` sizes still read 0 while the sample payload is already on disk.
namespace
{
    juce::File studioUnitScratchDir (const juce::String& name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("raw-radio-studio-unit")
                       .getChildFile (name);
        dir.deleteRecursively();
        dir.createDirectory();
        return dir;
    }

    /** Writes a valid 24-bit WAV with `numChannels` channels and returns its raw
        bytes. */
    juce::MemoryBlock makeValidWavBytes (const juce::File& file, double sampleRate, double seconds,
                                         int numChannels = 2)
    {
        file.deleteFile();

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);

        auto writer = wav.createWriterFor (stream,
                                           juce::AudioFormatWriterOptions{}
                                               .withSampleRate (sampleRate)
                                               .withNumChannels (numChannels)
                                               .withBitsPerSample (24));

        juce::MemoryBlock bytes;

        if (writer == nullptr)
            return bytes;

        const auto numSamples = (int) (sampleRate * seconds);
        juce::AudioBuffer<float> buffer (numChannels, numSamples);

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                buffer.setSample (ch, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                  * 440.0 * (double) i / sampleRate));

        writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
        writer.reset();

        file.loadFileAsData (bytes);
        return bytes;
    }

    /** Reads the `blockAlign` field from a WAV's `fmt ` chunk (robust to any
        chunks before `fmt `). */
    int readWavBlockAlign (const juce::File& file)
    {
        juce::MemoryBlock mb;
        file.loadFileAsData (mb);

        const auto n = (int) mb.getSize();
        const auto* d = (const unsigned char*) mb.getData();

        for (int i = 12; i + 8 <= n; ++i)
            if (d[i] == 'f' && d[i + 1] == 'm' && d[i + 2] == 't' && d[i + 3] == ' ')
                // fmt: "fmt "(4) + size(4) + audioFormat(2) + numChannels(2)
                //      + sampleRate(4) + byteRate(4) + blockAlign(2).
                if (i + 8 + 14 <= n)
                    return (int) d[i + 8 + 12] | ((int) d[i + 8 + 13] << 8);

        return -1;
    }

    /** Patches the RIFF + `data` chunk sizes to 0 (the mid-crash state) and
        writes the result to `file`. */
    bool writeZeroLengthHeaderWav (const juce::File& file, const juce::MemoryBlock& validBytes)
    {
        juce::MemoryBlock mb (validBytes);
        auto* d = (unsigned char*) mb.getData();
        const auto n = (int) mb.getSize();

        for (int i = 12; i + 8 <= n; ++i)
        {
            if (d[i] == 'd' && d[i + 1] == 'a' && d[i + 2] == 't' && d[i + 3] == 'a')
            {
                d[i + 4] = d[i + 5] = d[i + 6] = d[i + 7] = 0; // data size = 0

                const auto headerOnly = (juce::uint32) (i + 8) - 8; // RIFF size w/o payload
                d[4] = (unsigned char) (headerOnly & 0xff);
                d[5] = (unsigned char) ((headerOnly >> 8) & 0xff);
                d[6] = (unsigned char) ((headerOnly >> 16) & 0xff);
                d[7] = (unsigned char) ((headerOnly >> 24) & 0xff);

                return file.replaceWithData (mb.getData(), mb.getSize());
            }
        }

        return false;
    }

    juce::int64 readWavLengthInSamples (const juce::File& file)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (
            wav.createReaderFor (new juce::FileInputStream (file), true));

        return reader != nullptr ? reader->lengthInSamples : -1;
    }
}

TEST_CASE ("WavRepair rebuilds a zero-length header so a crashed take becomes readable")
{
    using namespace rrs::wavrepair;

    auto dir = studioUnitScratchDir ("wavrepair");
    const auto seed = dir.getChildFile ("seed.wav");
    const auto bytes = makeValidWavBytes (seed, 48000.0, 0.25);
    REQUIRE (! bytes.isEmpty());

    const auto broken = dir.getChildFile ("Crash Session_Input 1_Take_1.wav");
    REQUIRE (writeZeroLengthHeaderWav (broken, bytes));

    // The crash state really is unreadable...
    CHECK (readWavLengthInSamples (broken) == 0);

    // ...until the header is rebuilt from the on-disk payload.
    const auto result = repairTruncatedHeader (broken);
    INFO ("repair detail: " << result.detail);
    CHECK (result.wasRepaired());
    CHECK (result.dataBytes > 0);
    CHECK (result.sampleFrames > 0);

    const auto repairedLen = readWavLengthInSamples (broken);
    CHECK (repairedLen > 0);

    // Re-running is idempotent: the header now matches the payload.
    const auto again = repairTruncatedHeader (broken);
    CHECK (again.status == wavrepair::Result::Status::consistent);
}

TEST_CASE ("WavRepair rebuilds a zero-length header on a 1-channel (mono) take (RT-5)")
{
    using namespace rrs::wavrepair;

    auto dir = studioUnitScratchDir ("wavrepair-mono");
    const auto seed = dir.getChildFile ("seed-mono.wav");
    const auto bytes = makeValidWavBytes (seed, 48000.0, 0.25, 1); // MONO
    REQUIRE (! bytes.isEmpty());

    const auto broken = dir.getChildFile ("Crash Session_Input 1_Take_1.wav");
    REQUIRE (writeZeroLengthHeaderWav (broken, bytes));

    // The crash state really is unreadable...
    CHECK (readWavLengthInSamples (broken) == 0);

    // ...until the header is rebuilt from the 1-channel payload.
    const auto result = repairTruncatedHeader (broken);
    INFO ("repair detail: " << result.detail);
    CHECK (result.wasRepaired());
    CHECK (result.dataBytes > 0);
    CHECK (result.sampleFrames > 0);

    // It reads back as a TRUE mono WAV: one channel, and the frame count matches
    // the repaired payload with the mono block-align (3 bytes for 24-bit mono).
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (
        wav.createReaderFor (new juce::FileInputStream (broken), true));
    REQUIRE (reader != nullptr);
    CHECK (reader->numChannels == 1);
    CHECK (reader->lengthInSamples == result.sampleFrames);
    CHECK (readWavBlockAlign (broken) == 3); // 24-bit mono

    // Idempotent.
    CHECK (repairTruncatedHeader (broken).status == wavrepair::Result::Status::consistent);
}

TEST_CASE ("WavRepair leaves a valid WAV and a non-WAV untouched")
{
    using namespace rrs::wavrepair;

    auto dir = studioUnitScratchDir ("wavrepair-valid");
    const auto valid = dir.getChildFile ("valid.wav");
    REQUIRE (! makeValidWavBytes (valid, 48000.0, 0.1).isEmpty());

    const auto before = valid.getSize();
    const auto result = repairTruncatedHeader (valid);
    CHECK (result.status == wavrepair::Result::Status::consistent);
    CHECK_FALSE (result.wasRepaired());
    CHECK (valid.getSize() == before);

    const auto notWav = dir.getChildFile ("notes.txt");
    notWav.replaceWithText ("this is not a wav file at all\n");
    const auto other = repairTruncatedHeader (notWav);
    CHECK (other.status == wavrepair::Result::Status::notRiff);
    CHECK_FALSE (other.wasRepaired());
    CHECK (notWav.loadFileAsString().startsWith ("this is not"));
}


