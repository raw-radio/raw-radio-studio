// raw-radio-studio — device-panel layout (Epic 1).
//
// The device panel must always leave usable space for its status line and its
// (multi-line) error label: «fail loudly, not silently» is a hard acceptance
// criterion (FR-MON-5). The layout is a free function so the contract can be
// unit-tested without opening an audio device.

#pragma once

#include <juce_graphics/juce_graphics.h>

namespace rrs
{
    /** Fixed device-panel height. Raised from 170 px so the status + error labels
        always get real room; the error label needs several lines for a
        busy-device (`EBUSY`) message. Kept here so MainComponent and the layout
        unit test agree on the value. */
    inline constexpr int devicePanelHeight = 290;

    /** Pixel bounds for every DevicePanel child. */
    struct DevicePanelLayout
    {
        juce::Rectangle<int> typeLabel, typeBox;
        juce::Rectangle<int> deviceLabel, deviceBox;
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
        l.deviceLabel = row2.removeFromLeft (labelWidth);
        l.deviceBox = row2;

        area.removeFromTop (6);

        auto row3 = area.removeFromTop (rowHeight);
        l.rateLabel = row3.removeFromLeft (labelWidth);
        l.rateBox = row3.removeFromLeft (juce::jmin (140, row3.getWidth() / 2));
        row3.removeFromLeft (8);
        l.bufferLabel = row3.removeFromLeft (80);
        l.bufferBox = row3.removeFromLeft (juce::jmin (140, row3.getWidth()));

        area.removeFromTop (8);

        auto row4 = area.removeFromTop (28);
        l.applyButton = row4.removeFromLeft (90);
        row4.removeFromLeft (8);
        l.rescanButton = row4.removeFromLeft (90);

        area.removeFromTop (6);

        // Status line first, then everything left over for the error label.
        l.statusLabel = area.removeFromTop (juce::jmin (20, area.getHeight()));
        area.removeFromTop (juce::jmin (4, area.getHeight()));
        l.errorLabel = area;

        return l;
    }
}
