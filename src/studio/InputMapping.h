// raw-radio-studio — device-agnostic per-track input mapping (Epic 2, FR-REC-3).
//
// Pure, Tracktion-free description of how a track's input device channels map
// onto the track's own input bus. Kept free of Tracktion types so it can be
// unit-tested without the engine; `InputRouting.h` turns a mapping into a
// Tracktion `ChannelConfiguration` for the live device.
//
// A track's mapping is: starting hardware channel + channel count + layout.
// Layouts:
//   * None         no input assigned yet (RT-4): a valid, unassigned state.
//   * Auto         1-ch device -> mono; >=2-ch -> stereo L/R (Epic 1).
//   * Mono         device ch `firstChannel` as ONE (centre) channel.
//   * Stereo       device ch `firstChannel` -> L, `firstChannel+1` -> R.
//   * MultiChannel device ch `firstChannel`..`firstChannel+N-1` -> N channels.
//
// RT-5: a Mono track maps to a SINGLE channel (typed `centre`, the Tracktion
// canonical for `ChannelConfiguration::mono()`), so the recorded take is a true
// 1-channel WAV. The centring on playback/monitor is done by the engine's channel
// conversion (`ChannelMap::conversion` in EditNodeBuilder / the
// ChannelRemappingNode), which upmixes mono -> stereo; the `centre` type is
// chosen for Tracktion-canonical consistency, not as the centring mechanism
// itself. (Before RT-5 Mono duplicated the source to L+R, making every take a
// centred 2-channel file — the old Epic 1 hard-left workaround.)

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace rrs
{
    /** How a track maps its input device's hardware channels onto its own bus. */
    enum class InputLayout
    {
        Auto,          ///< 1-ch device -> mono; >=2-ch -> stereo L/R.
        Mono,          ///< Device channel `firstChannel` as one centre channel (RT-5).
        Stereo,        ///< Device ch `firstChannel` -> L, `firstChannel+1` -> R.
        MultiChannel,  ///< Device ch `firstChannel`..`firstChannel+N-1` -> N discrete.
        None           ///< No input assigned (RT-4): valid; assign later. No channel,
                       ///< no device, no problem — the track is just not fed yet.
    };

    /** A track's input assignment. */
    struct InputMapping
    {
        int firstChannel = 0;   ///< 0-based hardware channel within the device.
        int numChannels = 1;    ///< 1..N (used by MultiChannel; ignored otherwise).
        InputLayout layout = InputLayout::Auto;

        bool operator== (const InputMapping& other) const noexcept
        {
            return firstChannel == other.firstChannel
                && numChannels == other.numChannels
                && layout == other.layout;
        }

        bool operator!= (const InputMapping& other) const noexcept { return ! operator== (other); }
    };

    /** One destination channel: which hardware channel feeds it and what type
        (L/R/discrete) it is on the track bus. */
    struct MappedChannel
    {
        int deviceChannel = 0;
        juce::AudioChannelSet::ChannelType type = juce::AudioChannelSet::left;
    };

    /** The default layout for a device with `deviceChannels` hardware inputs
        (owner-agreed I5): 1 -> Mono, **2 -> Stereo** (the whole pair),
        **>= 3 -> Mono per channel**. The 3-channel case has no clean all-stereo
        default, so mono-per-channel is the conservative completion between
        "2 -> stereo" and ">= 4 -> mono".

        SINGLE SOURCE OF TRUTH for the `Auto` default: both `resolveInputLayout`
        (here) and `InputGrouping::planLayoutFor` delegate to this rule. */
    inline InputLayout defaultLayoutForDevice (int deviceChannels) noexcept
    {
        if (deviceChannels <= 1)
            return InputLayout::Mono;

        if (deviceChannels == 2)
            return InputLayout::Stereo;

        return InputLayout::Mono;
    }

    /** Resolves `Auto` to a concrete layout for a device with
        `deviceChannelCount` hardware inputs. */
    inline InputLayout resolveInputLayout (InputLayout layout, int deviceChannelCount) noexcept
    {
        if (layout != InputLayout::Auto)
            return layout;

        return defaultLayoutForDevice (deviceChannelCount);
    }

    /** Builds the destination channel list for an explicit mapping.

        @param mapping            the track's input assignment.
        @param deviceChannelCount channels on the input device (0 = "none open
                                  yet", treated as mono so the default is a single
                                  channel, never hard-left). */
    inline std::vector<MappedChannel> mappedChannelsFor (const InputMapping& mapping, int deviceChannelCount)
    {
        const auto available = juce::jmax (1, deviceChannelCount);
        const auto first = juce::jlimit (0, available - 1, mapping.firstChannel);
        const auto layout = resolveInputLayout (mapping.layout, deviceChannelCount);

        // RT-5: ONE channel typed `centre` — the recorded file is a TRUE mono
        // (1-channel) WAV. The engine's channel conversion (not this type) upmixes
        // it to L+R on playback/monitor; `centre` keeps it Tracktion-canonical
        // (`ChannelConfiguration::mono()` uses `AudioChannelSet::centre`).
        auto monoChannel = [first]
        {
            return std::vector<MappedChannel> {
                { first, juce::AudioChannelSet::centre } };
        };

        // RT-5 leftover: still used for a Stereo request on a device too small for
        // a pair (unreachable via configureTracks — the planner flags it first).
        auto centredMono = [first]
        {
            return std::vector<MappedChannel> {
                { first, juce::AudioChannelSet::left },
                { first, juce::AudioChannelSet::right } };
        };

        switch (layout)
        {
            case InputLayout::None:
                // Unassigned: no destination channels at all (never configured).
                return {};

            case InputLayout::Mono:
                return monoChannel();

            case InputLayout::Stereo:
                if (available >= 2 && first + 1 < available)
                    return { { first,     juce::AudioChannelSet::left },
                             { first + 1, juce::AudioChannelSet::right } };

                return centredMono();

            case InputLayout::MultiChannel:
            case InputLayout::Auto:
            default:
            {
                const auto n = juce::jlimit (1, available - first, juce::jmax (1, mapping.numChannels));

                // RT-5: a 1-channel layout records a 1-channel file too.
                if (n == 1)
                    return monoChannel();

                if (n == 2)
                    return { { first,     juce::AudioChannelSet::left },
                             { first + 1, juce::AudioChannelSet::right } };

                const auto set = juce::AudioChannelSet::discreteChannels (n);
                const auto types = set.getChannelTypes();
                std::vector<MappedChannel> channels;
                channels.reserve ((size_t) n);

                for (int i = 0; i < n; ++i)
                    channels.push_back ({ first + i, types[i] });

                return channels;
            }
        }
    }

    /** The number of hardware channels a mapping occupies — its **span** (RT-1/
        RT-6). This is the span Session uses for its occupancy rules (the RT-6
        write guard and the RT-8 Channel menu, both through `InputGrouping.h`):

          * `None`         -> 0 (unassigned: nothing is occupied),
          * `Mono`         -> 1,
          * `Stereo`       -> 2 (a pair),
          * `MultiChannel` -> `numChannels` (>= 1),
          * `Auto`         -> the device default (`defaultLayoutForDevice`).

        NOTE: the planner (`InputGrouping.h`) derives the same span through
        `planLayoutFor` + `channelsPerLayout` + the request's `needOverride` (a
        MultiChannel track's `numChannels`); the two are kept in sync by the
        "planner agrees with mappingChannelSpan" invariant test rather than by
        sharing this function, because the planner must stay Tracktion-free of
        `InputMapping`'s `numChannels` for the multi case.

        Overflow-safe / total: an unknown device (0 channels) still yields a
        concrete 1..N span, never 0 for an assigned layout. */
    inline int mappingChannelSpan (const InputMapping& mapping, int deviceChannelCount) noexcept
    {
        switch (mapping.layout)
        {
            case InputLayout::None:
                return 0;

            case InputLayout::Mono:
                return 1;

            case InputLayout::Stereo:
                return 2;

            case InputLayout::MultiChannel:
                return juce::jmax (1, mapping.numChannels);

            case InputLayout::Auto:
            default:
                return defaultLayoutForDevice (deviceChannelCount) == InputLayout::Stereo ? 2 : 1;
        }
    }

    /** Stable string form of a layout (for persistence and UI). */
    inline juce::String inputLayoutToString (InputLayout layout)
    {
        switch (layout)
        {
            case InputLayout::None:         return "none";
            case InputLayout::Mono:         return "mono";
            case InputLayout::Stereo:       return "stereo";
            case InputLayout::MultiChannel: return "multi";
            case InputLayout::Auto:
            default:                        return "auto";
        }
    }

    inline InputLayout inputLayoutFromString (const juce::String& text)
    {
        if (text == "none")   return InputLayout::None;
        if (text == "mono")   return InputLayout::Mono;
        if (text == "stereo") return InputLayout::Stereo;
        if (text == "multi")  return InputLayout::MultiChannel;
        return InputLayout::Auto;
    }
}
