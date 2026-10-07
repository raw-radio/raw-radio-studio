// raw-radio-studio — audio-device error classification (Epic 1).
//
// Pure, header-only helpers so they can be unit-tested without an audio device
// or a running Tracktion Engine. They implement the "fail loudly, not silently"
// policy from the spec (FR-MON-5, NFR-IO-4):
//
//   * a missing device and a busy device get distinct, actionable messages;
//   * on Linux the ALSA backend must open `hw` devices directly — a name that is
//     not a raw hardware PCM is rejected so we never silently fall back to the
//     "default"/dmix/plugin path held by PipeWire.

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

    /** True if a device name denotes an ALSA raw hardware PCM (e.g. "hw:0,0"). */
    inline bool isAlsaHardwareDeviceName (const juce::String& deviceName)
    {
        return deviceName.containsIgnoreCase ("hw:");
    }

    /** Whether a device name is acceptable for direct recording.
        On Linux we require ALSA `hw` (no silent fallback to the plugin layer). */
    inline bool isAcceptableInputDeviceName (const juce::String& deviceName)
    {
       #if JUCE_LINUX
        return isAlsaHardwareDeviceName (deviceName);
       #else
        juce::ignoreUnused (deviceName);
        return true;
       #endif
    }
}
