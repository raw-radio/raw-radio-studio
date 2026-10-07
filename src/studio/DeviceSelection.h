// raw-radio-studio — input/output device selection resolution (Epic 1).
//
// Pure, header-only helper so the "which device names does an apply actually
// open?" decision can be unit-tested without an audio device.
//
// Fixes the device-selection bug where the panel offered a single list and the
// selected *input* device name was copied into `outputDeviceName`. On any device
// whose input and output names differ (a USB mic, or the Mac built-in mic vs
// speakers) that injected a non-existent output name and the whole setup failed,
// tearing down the working device (playback stopped). Full-duplex devices whose
// names happened to match (Bluetooth headsets) worked by luck.
//
// The resolver keeps the two sides independent and treats an empty request as
// "leave the current device on this side untouched":
//
//   * selecting an input-only device (a mic) keeps the existing output device;
//   * selecting an output-only device keeps the existing input device;
//   * the input name is NEVER copied into the output (or vice versa).

#pragma once

#include <juce_core/juce_core.h>

namespace rrs
{
    /** The effective input/output device names an apply should open. */
    struct DeviceSelection
    {
        juce::String input;
        juce::String output;
    };

    /** Resolves the effective device-name pair for an apply.

        @param requestedInput   user-selected input name ("" = keep current)
        @param requestedOutput  user-selected output name ("" = keep current)
        @param currentInput     the input device currently in use (may be "")
        @param currentOutput    the output device currently in use (may be "")
    */
    inline DeviceSelection resolveDeviceNames (const juce::String& requestedInput,
                                               const juce::String& requestedOutput,
                                               const juce::String& currentInput,
                                               const juce::String& currentOutput)
    {
        DeviceSelection selection;
        selection.input  = requestedInput.isNotEmpty()  ? requestedInput  : currentInput;
        selection.output = requestedOutput.isNotEmpty() ? requestedOutput : currentOutput;
        return selection;
    }
}
