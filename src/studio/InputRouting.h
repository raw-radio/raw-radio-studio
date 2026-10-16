// raw-radio-studio — input channel routing (Epic 1) + Tracktion binding for the
// device-agnostic per-track mapping (Epic 2, FR-REC-3).
//
// The pure mapping logic lives in `InputMapping.h` (Tracktion-free, unit
// tested). This header turns a mapping into a Tracktion `ChannelConfiguration`
// for the live device, and keeps the Epic 1 one-argument convenience wrapper so
// existing call sites are unaffected.
//
// The configuration's channel count is the count of the recorded file (Tracktion
// `WaveInputDevice::startRecording` sizes the writer from `getChannels()`), so a
// Mono mapping produces a single channel via `mappedChannelsFor` (RT-5).

#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "InputMapping.h"

#include <vector>

namespace rrs
{
    /** Builds the wave-input channel configuration for an explicit mapping. */
    inline tracktion::ChannelConfiguration
        inputChannelConfigurationForMapping (const InputMapping& mapping, int deviceChannelCount)
    {
        std::vector<tracktion::ChannelIndex> channels;

        for (const auto& mapped : mappedChannelsFor (mapping, deviceChannelCount))
            channels.push_back (tracktion::ChannelIndex (mapped.deviceChannel, mapped.type));

        return tracktion::ChannelConfiguration (std::move (channels));
    }

    /** Epic 1 convenience wrapper: routes the whole device for the given active
        channel count. 1 (or 0) channel -> a single mono channel (RT-5);
        >=2 -> stereo L/R. */
    inline tracktion::ChannelConfiguration inputChannelConfigurationFor (int numActiveInputChannels)
    {
        return inputChannelConfigurationForMapping (
            InputMapping { 0, 1, numActiveInputChannels <= 1 ? InputLayout::Mono : InputLayout::Stereo },
            numActiveInputChannels);
    }
}
