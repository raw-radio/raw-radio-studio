// raw-radio-studio — the shared Input menu builder (RT-8).
//
// Both the arrangement gutter popup (MainComponent::showLaneInputMenu) and the
// mixer-strip popup (MixerPanel::showInputMenu) build their Channel/Layout
// submenus from this ONE place, read the occupancy from the ONE source
// (`Session::getInputChannelMenu`), decode the result with
// `InputMenuModel.h:decodeResult` and apply it with `applySelection`. The two
// menus therefore cannot diverge (the B1 bug was exactly a hand-rolled
// channel/layout id test that mistook a layout id for a channel).
//
// Only the `juce::PopupMenu` construction lives here; the pure ids/decoder/model
// are in `InputMenuModel.h` (unit-testable without JUCE-GUI).

#pragma once

#include <JuceHeader.h>

#include <functional>
#include <vector>

#include "studio/InputGrouping.h"
#include "ui/InputMenuModel.h"

namespace rrs::input_menu
{
    /** Builds the Channel submenu: one item per real device channel, ticking the
        current one and marking channels owned by another track.

        `trackNameFor` resolves the owning track index to a display name (the
        labels read `Input 2 — used by "Input 1"` / `Input 2 — shared with "Input 1"`). */
    inline juce::PopupMenu buildChannelMenu (const std::vector<ChannelMenuItem>& items,
                                             int currentChannel,
                                             bool assigned,
                                             const std::function<juce::String (int)>& trackNameFor)
    {
        juce::PopupMenu menu;

        for (const auto& item : items)
        {
            const bool isCurrent = assigned && item.channel == currentChannel;
            const auto ownerName = item.ownerTrackIndex >= 0 ? trackNameFor (item.ownerTrackIndex)
                                                             : juce::String();
            auto label = "Input " + juce::String (item.channel + 1);
            bool enabled = true;

            switch (item.availability)
            {
                case ChannelAvailability::Free:
                    break;

                case ChannelAvailability::Shared:
                    // RT-4: an identical span may be shared — selectable.
                    label += "  \u2014 shared with \"" + ownerName + "\"";
                    break;

                case ChannelAvailability::Used:
                    label += "  \u2014 used by \"" + ownerName + "\"";
                    enabled = false;
                    break;

                case ChannelAvailability::OffDevice:
                    // RT-2b keeps an off-device channel selectable so it becomes a
                    // visible "invalid" state with a concrete reason — never
                    // disabled, only hinted.
                    label += "  \u2014 needs more channels";
                    break;
            }

            menu.addItem (channelItemBase + item.channel, label, enabled, isCurrent);
        }

        return menu;
    }

    /** Builds the Layout submenu. A disabled layout is labelled with `blockedHint`
        (RT-6) so the reason is visible without opening a dialog. */
    inline juce::PopupMenu buildLayoutMenu (InputLayout current, const LayoutAvailability& available,
                                            const juce::String& blockedHint)
    {
        juce::PopupMenu menu;

        auto add = [&menu, &blockedHint] (int id, const juce::String& name, bool enabled, bool ticked)
        {
            auto label = name;

            if (! enabled && blockedHint.isNotEmpty())
                label += "  \u2014 " + blockedHint;

            menu.addItem (id, label, enabled, ticked);
        };

        add (layoutAuto,   "Auto",          available.autoLayout, current == InputLayout::Auto);
        add (layoutMono,   "Mono",          available.mono,       current == InputLayout::Mono);
        add (layoutStereo, "Stereo",        available.stereo,     current == InputLayout::Stereo);
        add (layoutMulti,  "Multi-channel", available.multi,      current == InputLayout::MultiChannel);

        return menu;
    }
}
