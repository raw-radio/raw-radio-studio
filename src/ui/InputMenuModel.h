// raw-radio-studio — the PURE Input-menu model + result decoder (RT-8, B1).
//
// Split out of `InputMenu.h` (which needs `juce::PopupMenu`) so the id decoder and
// the mapping update can be unit-tested without pulling JUCE-GUI into the unit
// test: this header only depends on `InputMapping.h` / `InputGrouping.h` and
// `<functional>`.
//
// ONE decoder (`decodeResult`) + ONE apply (`applySelection`) are used by both the
// gutter popup and the mixer popup — the B1 bug was a hand-rolled
// `id >= channelItemBase` test in each popup that mistook a LAYOUT id (200..203)
// for a channel, silently writing `firstChannel = 101..103` and breaking the track.

#pragma once

#include <functional>

#include "studio/InputGrouping.h"
#include "studio/InputMapping.h"

namespace rrs::input_menu
{
    // Stable item ids. Channel items are `channelItemBase + channel` (100..199);
    // layout items are the four `layout*` constants (200..203). The two ranges are
    // DISJOINT — that is what `decodeResult` relies on.
    constexpr int channelItemBase = 100;
    constexpr int layoutAuto      = 200;
    constexpr int layoutMono      = 201;
    constexpr int layoutStereo    = 202;
    constexpr int layoutMulti     = 203;

    // B1: the two id families MUST stay disjoint — a channel id (100..199) can
    // never reach the layout range. Enforced at compile time so a future edit that
    // moves the layout base cannot silently reintroduce the bug.
    static_assert (channelItemBase + 99 < layoutAuto, "channel/layout id ranges overlap");
    static_assert (layoutAuto < layoutMono && layoutMono < layoutStereo && layoutStereo < layoutMulti,
                   "layout ids must be contiguous and ordered");

    /** Which layouts may be selected for a track right now. `false` = the switch
        is refused (RT-6: its span would overlap another track's channel), so the
        item is greyed out with a reason instead of silently failing. */
    struct LayoutAvailability
    {
        bool autoLayout = true;
        bool mono = true;
        bool stereo = true;
        bool multi = true;
    };

    /** The layout a layout-item `id` selects (`Auto` for anything else). */
    inline InputLayout layoutForItem (int id) noexcept
    {
        switch (id)
        {
            case layoutMono:   return InputLayout::Mono;
            case layoutStereo: return InputLayout::Stereo;
            case layoutMulti:  return InputLayout::MultiChannel;
            case layoutAuto:
            default:           return InputLayout::Auto;
        }
    }

    /** The meaning of a menu result id — the ONE decoder both menus use.
        B1 regression: the channel and layout id families are DISJOINT
        (`channelItemBase .. layoutAuto - 1` vs `layoutAuto .. layoutMulti`), so a
        layout id can never be mistaken for a channel (which would write
        `firstChannel = 101..103` and silently break the track's input). */
    struct MenuSelection
    {
        enum class Kind { None, Channel, Layout };

        Kind kind = Kind::None;
        int channel = -1;                          ///< valid when kind == Channel
        InputLayout layout = InputLayout::Auto;    ///< valid when kind == Layout

        bool isValid() const noexcept { return kind != Kind::None; }
    };

    /** Decodes a raw `PopupMenu` result id into a selection. Returns a `None`
        selection for 0 (dismissed) or any unknown id. Layout ids are checked
        FIRST (they are numerically above the channel base). */
    inline MenuSelection decodeResult (int id) noexcept
    {
        MenuSelection selection;

        if (id >= layoutAuto && id <= layoutMulti)
        {
            selection.kind = MenuSelection::Kind::Layout;
            selection.layout = layoutForItem (id);
            return selection;
        }

        // Bounded channel range: MUST stay below `layoutAuto` (the B1 bug).
        if (id >= channelItemBase && id < layoutAuto)
        {
            selection.kind = MenuSelection::Kind::Channel;
            selection.channel = id - channelItemBase;
            return selection;
        }

        return selection;
    }

    /** Applies a decoded selection to a mapping, keeping the channel count coherent
        with the layout (Multi = every remaining channel, Stereo = 2, else 1).
        Returns false when the selection decodes to nothing (nothing is changed).

        The ONE place that turns a menu result into a mapping — both popups call it,
        so a "Layout" click can never write a channel instead (B1). Pure: no
        Session, so it is unit-tested directly. */
    inline bool applySelection (const MenuSelection& selection, InputMapping& mapping, int numChannels)
    {
        switch (selection.kind)
        {
            case MenuSelection::Kind::Channel:
                mapping.firstChannel = selection.channel;

                // RT-4: picking a channel ASSIGNS an unassigned track (otherwise the
                // layout would stay `None` and the channel is dropped).
                if (mapping.layout == InputLayout::None)
                    mapping.layout = InputLayout::Mono;
                break;

            case MenuSelection::Kind::Layout:
                mapping.layout = selection.layout;
                break;

            case MenuSelection::Kind::None:
            default:
                return false;
        }

        if (mapping.layout == InputLayout::MultiChannel)
            mapping.numChannels = juce::jmax (1, numChannels - mapping.firstChannel);
        else if (mapping.layout == InputLayout::Stereo)
            mapping.numChannels = 2;
        else
            mapping.numChannels = 1;

        return true;
    }

    /** Probes the four layouts against `canSet` (RT-6) and reports which may be
        selected for the track right now. `canSet` is the caller's
        `Session::canSetTrackInputMapping` for the track; both the gutter and the
        mixer menu use this so they can never disagree about what is allowed. */
    inline LayoutAvailability probeLayoutAvailability (
        const InputMapping& mapping, int numChannels,
        const std::function<bool (const InputMapping&)>& canSet)
    {
        LayoutAvailability available;
        auto probe = mapping;

        probe.layout = InputLayout::Auto;
        available.autoLayout = canSet (probe);

        probe.layout = InputLayout::Mono;
        probe.numChannels = 1;
        available.mono = canSet (probe);

        probe.layout = InputLayout::Stereo;
        probe.numChannels = 2;
        available.stereo = canSet (probe);

        probe.layout = InputLayout::MultiChannel;
        probe.numChannels = juce::jmax (1, numChannels - probe.firstChannel);
        available.multi = canSet (probe);

        return available;
    }
}
