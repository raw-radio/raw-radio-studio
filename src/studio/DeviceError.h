// raw-radio-studio — audio-device error classification (Epic 1).
//
// Pure, header-only helpers so they can be unit-tested without an audio device
// or a running Tracktion Engine. They implement the "fail loudly, not silently"
// policy from the spec (FR-MON-5, NFR-IO-4):
//
//   * a missing device and a busy device get distinct, actionable messages;
//   * on Linux the ALSA backend must open raw hardware PCMs directly, never the
//     PipeWire/PulseAudio plugin path held by another daemon. JUCE enumerates
//     *every* ALSA PCM hint (except `default:`/`sysdefault:`/`plughw:`/`null`),
//     so plugin-backed and software-routed PCMs (`pipewire`, `jack`, `dmix`,
//     `dsnoop`) also appear — reported by their human-readable descriptor, never
//     by their `hw:N,M` id. We reject all of them so the direct-hardware
//     auto-selection can never silently take the plugin path (NFR-IO-4).

#pragma once

#include <juce_core/juce_core.h>

namespace rrs
{
    /** Classified audio-device failure, ready to show in the UI. */
    struct DeviceErrorInfo
    {
        bool isBusy    = false;   ///< EBUSY-style failure (device held by another client).
        bool isMissing = false;   ///< Device does not exist / was unplugged.
        juce::String raw;         ///< The original error string (kept for the build log).
        juce::String userMessage; ///< Actionable, human-readable message.
    };

    /** Classifies a raw backend error (JUCE / ALSA / CoreAudio) into a UI message.
        Never returns an empty message for a non-empty input. */
    inline DeviceErrorInfo classifyDeviceError (const juce::String& rawError)
    {
        DeviceErrorInfo info;
        info.raw = rawError;

        const auto lower = rawError.toLowerCase();

        const auto mentionsBusy = lower.contains ("busy")
                               || lower.contains ("ebusy")
                               || lower.contains ("in use")
                               || lower.contains ("being used")
                               || lower.contains ("exclusive");

        const auto mentionsMissing = lower.contains ("not found")
                                  || lower.contains ("no such")
                                  || lower.contains ("doesn't exist")
                                  || lower.contains ("does not exist")
                                  || lower.contains ("no device")
                                  || lower.contains ("unavailable")
                                  || lower.contains ("disconnected");

        if (mentionsBusy)
        {
            info.isBusy = true;
            info.userMessage =
                "The audio device is busy and could not be opened.\n\n"
                "On Linux this usually means PipeWire/PulseAudio (or another client) is "
                "holding the ALSA device. Stop the service or free the device, then retry.\n\n"
                "raw-radio-studio does NOT fall back to another backend by design.";
        }
        else if (mentionsMissing)
        {
            info.isMissing = true;
            info.userMessage =
                "The selected audio device could not be found. It may have been "
                "unplugged or is not present on this machine.";
        }
        else
        {
            info.userMessage = rawError.isNotEmpty() ? rawError
                                                      : juce::String ("Failed to open the audio device.");
        }

        return info;
    }

    /** True for ALSA PCM devices that are NOT raw hardware and must never be
        used for direct recording (NFR-IO-4).

        JUCE's ALSA backend enumerates *every* PCM hint (`snd_device_name_hint`)
        except `default:`/`sysdefault:`/`plughw:`/`null`, so plugin-backed and
        software-routed PCMs appear next to real hardware. The `id` -> reported
        name mapping on a stock Ubuntu 24.04 (PipeWire) box looks like:

          * `default`  -> "Default ALSA Output" / "Default ALSA Input" (JUCE-injected)
          * `pulse`    -> "Pulseaudio output" / "Pulseaudio input"    (JUCE-injected)
          * `pipewire` -> "PipeWire Sound Server"
          * `jack`     -> "JACK Audio Connection Kit"
          * `dmix:`    -> "<card>; Direct sample mixing device"
          * `dsnoop:`  -> "<card>; Direct sample snooping device"

        All of these route through PipeWire/PulseAudio/a plugin layer, so they are
        rejected from the "direct hardware" auto-selection. The match is
        case-insensitive on the human-readable descriptor because JUCE reports ALSA
        devices by `DESC`, never by their `hw:N,M` id. Matching is deliberately
        conservative: rejecting a marginal real device is preferable to silently
        taking the plugin path. */
    inline bool isAlsaPluginPseudoDeviceName (const juce::String& deviceName)
    {
        // JUCE-injected pseudo-devices.
        if (deviceName.startsWithIgnoreCase ("Default ALSA")
            || deviceName.startsWithIgnoreCase ("Pulseaudio"))
            return true;

        // Plugin-server bridge PCMs (PipeWire / PulseAudio / JACK).
        if (deviceName.containsIgnoreCase ("pipewire")
            || deviceName.containsIgnoreCase ("pulseaudio")
            || deviceName.containsIgnoreCase ("jack")
            || deviceName.containsIgnoreCase ("sound server"))
            return true;

        // Software-mixing PCMs. ALSA describes these by their role ("Direct
        // sample mixing/snooping device"); some configurations also surface the
        // raw `dmix`/`dsnoop` id when the description is empty.
        if (deviceName.containsIgnoreCase ("dmix")
            || deviceName.containsIgnoreCase ("dsnoop")
            || deviceName.containsIgnoreCase ("direct sample mixing")
            || deviceName.containsIgnoreCase ("direct sample snooping"))
            return true;

        return false;
    }

    /** Whether a device name is acceptable for direct recording.
        On Linux we exclude plugin-backed/routed PCMs (see
        isAlsaPluginPseudoDeviceName) so we never silently fall back to the
        PipeWire/PulseAudio path. Real hardware PCMs (`hw:`, `front:`,
        `surround:`, analog card names) stay usable. JUCE's ALSA backend reports
        real hardware with human-readable names, never as `hw:N,M`, so a `hw:`
        substring test would reject every real device. */
    inline bool isAcceptableInputDeviceName (const juce::String& deviceName)
    {
       #if JUCE_LINUX
        return deviceName.isNotEmpty() && ! isAlsaPluginPseudoDeviceName (deviceName);
       #else
        juce::ignoreUnused (deviceName);
        return true;
       #endif
    }

    /** Whether a device name is acceptable as a direct playback output.
        Same "no silent plugin/PipeWire fallback" policy as the input side
        (NFR-IO-4): JUCE-injected `Default ALSA Output` / `Pulseaudio output`
        and the `pipewire`/`jack`/`dmix` PCMs are excluded; real hardware and
        every device on non-Linux backends stay usable. */
    inline bool isAcceptableOutputDeviceName (const juce::String& deviceName)
    {
       #if JUCE_LINUX
        return deviceName.isNotEmpty() && ! isAlsaPluginPseudoDeviceName (deviceName);
       #else
        juce::ignoreUnused (deviceName);
        return true;
       #endif
    }
}
