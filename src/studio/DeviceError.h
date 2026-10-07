// raw-radio-studio — audio-device error classification (Epic 1).
//
// Pure, header-only helpers so they can be unit-tested without an audio device
// or a running Tracktion Engine. They implement the "fail loudly, not silently"
// policy from the spec (FR-MON-5, NFR-IO-4):
//
//   * a missing device and a busy device get distinct, actionable messages;
//   * on Linux the ALSA backend must open raw hardware PCMs directly, never the
//     PipeWire/PulseAudio plugin path held by another daemon. JUCE reports ALSA
//     devices by their human-readable names (e.g. "HDA Intel PCH, ALC295 Analog",
//     "USB Audio Device, USB Audio") and only injects the "Default ALSA …" and
//     "Pulseaudio …" pseudo-devices for the plugin layer, so we exclude exactly
//     those rather than looking for an "hw:" id (which never appears in a name).

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

    /** True for the plugin/pseudo devices JUCE injects into the ALSA device list
        ("Default ALSA Input/Output" and "Pulseaudio input/output"). These open
        the `default`/`pulse` plugin PCMs, i.e. route through PipeWire/PulseAudio,
        and are excluded so we open a raw hardware PCM directly (NFR-IO-4).

        Every other ALSA name JUCE reports is a real hardware PCM. */
    inline bool isAlsaPluginPseudoDeviceName (const juce::String& deviceName)
    {
        return deviceName.startsWithIgnoreCase ("Default ALSA")
            || deviceName.startsWithIgnoreCase ("Pulseaudio");
    }

    /** Whether a device name is acceptable for direct recording.
        On Linux we exclude only JUCE's injected plugin pseudo-devices so we never
        silently fall back to the PipeWire/PulseAudio path. JUCE's ALSA backend
        reports real hardware with human-readable names, never as `hw:N,M`, so a
        `hw:` substring test would reject every real device. */
    inline bool isAcceptableInputDeviceName (const juce::String& deviceName)
    {
       #if JUCE_LINUX
        return deviceName.isNotEmpty() && ! isAlsaPluginPseudoDeviceName (deviceName);
       #else
        juce::ignoreUnused (deviceName);
        return true;
       #endif
    }
}
