// raw-radio-studio — input channel routing (Epic 1).
//
// Pure, header-only helper that decides how the active audio input's hardware
// channels map onto the track's (always two-channel) input.
//
// Bug it fixes: the input device was unconditionally configured as a stereo
// pair (device ch 0 -> L, device ch 1 -> R). On a 1-channel input (a Fifine USB
// mic, the Mac built-in mic — both surfaced as "Mono") hardware channel 1 does
// not exist, so the right side stayed silent and monitoring was hard-left.
//
// A mono source is therefore mapped to BOTH the left and right input channels
// (device ch 0 -> L and device ch 0 -> R), which centres it while keeping the
// destination stereo. Stereo (or wider) sources keep the plain L/R mapping, so
// they are neither doubled nor widened.
//
// See Tracktion `WaveInputDeviceInstance::copyIncomingDataIntoBuffer`, which
// fills each destination channel from `ChannelIndex::indexInDevice`; two entries
// pointing at device channel 0 therefore yield L == R.

#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <vector>

namespace rrs
{
    /** Builds the wave-input channel configuration for the given number of
        active hardware input channels.

        @param numActiveInputChannels  channels on the open device; 0 means "no
                                       device open yet" and is treated as mono so
                                       the default is centred, never hard-left.
    */
    inline tracktion::ChannelConfiguration inputChannelConfigurationFor (int numActiveInputChannels)
    {
        // One (or no) active channel: a mono source — duplicate device ch 0 to L
        // and R so monitoring and recording are centred.
        if (numActiveInputChannels <= 1)
        {
            std::vector<tracktion::ChannelIndex> channels;
            channels.push_back (tracktion::ChannelIndex (0, juce::AudioChannelSet::left));
            channels.push_back (tracktion::ChannelIndex (0, juce::AudioChannelSet::right));
            return tracktion::ChannelConfiguration (std::move (channels));
        }

        // Stereo or wider: keep the first two hardware channels as L/R (unchanged).
        return tracktion::ChannelConfiguration::stereo();
    }
}
