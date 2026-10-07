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
    /** Fixed device-panel height. Raised from 170 px (then 290 px for the
        status/error labels) so the separate Input + Output rows AND the status +
        multi-line error labels always get real room. Kept here so MainComponent
        and the layout unit test agree on the value. */
    inline constexpr int devicePanelHeight = 320;

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
        The status label takes one line and the error label gets all remaining
        vertical space, so it is never collapsed to zero height. */
    inline DevicePanelLayout computeDevicePanelLayout (juce::Rectangle<int> bounds)
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

        // Status line first, then everything left over for the error label.
        l.statusLabel = area.removeFromTop (juce::jmin (20, area.getHeight()));
        area.removeFromTop (juce::jmin (4, area.getHeight()));
        l.errorLabel = area;

        return l;
    }
}
