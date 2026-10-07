// raw-radio-studio — device-agnostic per-track input mapping (Epic 2, FR-REC-3).
//
// Pure, Tracktion-free description of how a track's input device channels map
// onto the track's own input bus. Kept free of Tracktion types so it can be
// unit-tested without the engine; `InputRouting.h` turns a mapping into a
// Tracktion `ChannelConfiguration` for the live device.
//
// A track's mapping is: starting hardware channel + channel count + layout.
// Layouts:
//   * Auto         1-ch device -> centred mono; >=2-ch -> stereo L/R (Epic 1).
//   * Mono         device ch `firstChannel` duplicated to L+R (centred).
//   * Stereo       device ch `firstChannel` -> L, `firstChannel+1` -> R.
//   * MultiChannel device ch `firstChannel`..`firstChannel+N-1` -> N channels.
//
// The mono "duplicate to L and R" case is the Epic 1 hard-left fix: a 1-channel
// source is centred rather than landing only on the left.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace rrs
{
    /** How a track maps its input device's hardware channels onto its own bus. */
    enum class InputLayout
    {
        Auto,          ///< 1-ch device -> centred mono; >=2-ch -> stereo L/R.
        Mono,          ///< Device channel `firstChannel` duplicated to L+R (centred).
        Stereo,        ///< Device ch `firstChannel` -> L, `firstChannel+1` -> R.
        MultiChannel   ///< Device ch `firstChannel`..`firstChannel+N-1` -> N discrete.
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

    /** Resolves `Auto` to a concrete layout for a device with
        `deviceChannelCount` hardware inputs. */
    inline InputLayout resolveInputLayout (InputLayout layout, int deviceChannelCount) noexcept
    {
        if (layout != InputLayout::Auto)
            return layout;

        return deviceChannelCount <= 1 ? InputLayout::Mono : InputLayout::Stereo;
    }

    /** Builds the destination channel list for an explicit mapping.

        @param mapping            the track's input assignment.
        @param deviceChannelCount channels on the input device (0 = "none open
                                  yet", treated as mono so the default is
                                  centred, never hard-left). */
    inline std::vector<MappedChannel> mappedChannelsFor (const InputMapping& mapping, int deviceChannelCount)
    {
        const auto available = juce::jmax (1, deviceChannelCount);
        const auto first = juce::jlimit (0, available - 1, mapping.firstChannel);
        const auto layout = resolveInputLayout (mapping.layout, deviceChannelCount);

        auto centredMono = [first]
        {
            return std::vector<MappedChannel> {
                { first, juce::AudioChannelSet::left },
                { first, juce::AudioChannelSet::right } };
        };

        switch (layout)
        {
            case InputLayout::Mono:
                return centredMono();

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

                if (n == 1)
                    return centredMono();

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

    /** Stable string form of a layout (for persistence and UI). */
    inline juce::String inputLayoutToString (InputLayout layout)
    {
        switch (layout)
        {
            case InputLayout::Mono:         return "mono";
            case InputLayout::Stereo:       return "stereo";
            case InputLayout::MultiChannel: return "multi";
            case InputLayout::Auto:
            default:                        return "auto";
        }
    }

    inline InputLayout inputLayoutFromString (const juce::String& text)
    {
        if (text == "mono")   return InputLayout::Mono;
        if (text == "stereo") return InputLayout::Stereo;
        if (text == "multi")  return InputLayout::MultiChannel;
        return InputLayout::Auto;
    }
}
