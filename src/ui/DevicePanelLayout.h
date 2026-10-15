// raw-radio-studio — device-panel layout (Epic 1).
//
// The device panel must always leave usable space for its status line and its
// (multi-line) error label: «fail loudly, not silently» is a hard acceptance
// criterion (FR-MON-5). It also now offers independent Input and Output device
// selectors, so the fixed height accommodates one extra row. The layout is a
// free function so the contract can be unit-tested without opening an audio
// device.

#pragma once

#include <juce_graphics/juce_graphics.h>

namespace rrs
{
    /** Fixed device-panel height *with* an error shown. Raised from 170 px (then
        290 px for the status/error labels) so the separate Input + Output rows
        AND the status + multi-line error labels always get real room. Kept here
        so the layout unit test agrees on the value. */
    inline constexpr int devicePanelHeight = 320;

    /** Height of the panel when there is no error to show (Epic 8 phase 1a: the
        ~96 px that used to be reserved for an always-present, empty error label
        is reclaimed so the sidebar's Device section only takes the space it
        needs — the error occupies room only while it exists). */
    inline constexpr int devicePanelHeightCompact = 224;

    /** Panel height for the current error state. */
    inline int devicePanelHeightFor (bool hasError) noexcept
    {
        return hasError ? devicePanelHeight : devicePanelHeightCompact;
    }

    /** Pixel bounds for every DevicePanel child. */
    struct DevicePanelLayout
    {
        juce::Rectangle<int> typeLabel, typeBox;
        juce::Rectangle<int> inputLabel, inputBox;
        juce::Rectangle<int> outputLabel, outputBox;
        juce::Rectangle<int> rateLabel, rateBox, bufferLabel, bufferBox;
        juce::Rectangle<int> applyButton, rescanButton;
        juce::Rectangle<int> statusLabel, errorLabel;
    };

    /** Computes the DevicePanel child bounds for a panel of the given size.
        The status label takes one line and — when `hasError` is true — the error
        label gets all remaining vertical space, so it is never collapsed to zero
        height. When `hasError` is false the error label is left empty and the
        caller sizes the panel to `devicePanelHeightCompact` so no space is
        reserved for a message that is not there. */
    inline DevicePanelLayout computeDevicePanelLayout (juce::Rectangle<int> bounds,
                                                       bool hasError = true)
    {
        auto area = bounds.reduced (12);
        area.removeFromTop (20); // title

        constexpr int rowHeight = 24;
        constexpr int labelWidth = 90;

        DevicePanelLayout l;

        auto row1 = area.removeFromTop (rowHeight);
        l.typeLabel = row1.removeFromLeft (labelWidth);
        l.typeBox = row1.removeFromLeft (juce::jmin (160, row1.getWidth() / 2));

        area.removeFromTop (6);

        auto row2 = area.removeFromTop (rowHeight);
        l.inputLabel = row2.removeFromLeft (labelWidth);
        l.inputBox = row2;

        area.removeFromTop (6);

        auto row3 = area.removeFromTop (rowHeight);
        l.outputLabel = row3.removeFromLeft (labelWidth);
        l.outputBox = row3;

        area.removeFromTop (6);

        auto row4 = area.removeFromTop (rowHeight);
        l.rateLabel = row4.removeFromLeft (labelWidth);
        l.rateBox = row4.removeFromLeft (juce::jmin (140, row4.getWidth() / 2));
        row4.removeFromLeft (8);
        l.bufferLabel = row4.removeFromLeft (80);
        l.bufferBox = row4.removeFromLeft (juce::jmin (140, row4.getWidth()));

        area.removeFromTop (8);

        auto row5 = area.removeFromTop (28);
        l.applyButton = row5.removeFromLeft (90);
        row5.removeFromLeft (8);
        l.rescanButton = row5.removeFromLeft (90);

        area.removeFromTop (6);

        // Status line first, then everything left over for the error label — but
        // only while there is an error to show.
        l.statusLabel = area.removeFromTop (juce::jmin (20, area.getHeight()));

        if (hasError)
        {
            area.removeFromTop (juce::jmin (4, area.getHeight()));
            l.errorLabel = area;
        }

        return l;
    }
}
