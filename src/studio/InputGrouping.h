// raw-radio-studio — Tracktion-free input grouping + channel occupancy (RT-1).
//
// Pure planning layer for the "one track = one WaveInputDevice/channel or one
// stereo pair" invariant (owner-agreed I1/I2/I3/I5/I6). Given a list of
// per-track input requests and a device's channel count, it derives:
//   * a concrete layout per track (Mono = one channel, Stereo = a pair),
//   * `channel -> owning track` occupancy,
//   * explicit failures instead of a silent clamp.
//
// Depends on `InputMapping.h` (which pulls `<juce_audio_basics>` for the
// `InputLayout` enum and `defaultLayoutForDevice`), but NOT on Tracktion or
// JUCE-GUI — so it is unit-tested instantly.
//
// NOT real-time safe: it allocates (`std::vector`/`std::map`) — call it from the
// message thread only. RT-3 wires the result into `Session::configureTracks`.

#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <span>
#include <vector>

#include "InputMapping.h"

namespace rrs
{
    /** Why a grouping plan failed — never a silent clamp (I2/I3). */
    enum class InputGroupingError
    {
        None,               ///< ok
        NoDevice,           ///< the device has no input channels (none open / unknown)
        NoFreeChannel,      ///< the device has no free channel/pair left (I3)
        ChannelOccupied,    ///< an explicit Mono channel is owned by another track
        OccupiedNeighbour,  ///< a stereo pair would take a channel owned by another track (I6)
        ChannelOffDevice,   ///< a channel index is outside [0, deviceChannels) (I2)
        InvalidTrackIndex   ///< a request carried trackIndex < 0 (B1)
    };

    /** One track's input request. `layout` may be `Auto` (resolved by the device
        default, see `InputMapping.h`); `requestedChannel` is an explicit hardware
        channel, or -1 to let the planner pick the first free run. `trackIndex`
        MUST be >= 0. */
    struct TrackInputRequest
    {
        int trackIndex = -1;
        InputLayout layout = InputLayout::Auto;
        int requestedChannel = -1;
    };

    /** A planned assignment: `firstChannel` (and `firstChannel + 1` for stereo). */
    struct GroupAssignment
    {
        int trackIndex = -1;
        int firstChannel = 0;
        InputLayout layout = InputLayout::Mono; // Mono or Stereo only

        bool operator== (const GroupAssignment& other) const noexcept
        {
            return trackIndex == other.trackIndex
                && firstChannel == other.firstChannel
                && layout == other.layout;
        }
    };

    /** The plan. When `ok == false`, `assignments` is **empty** (nothing is
        half-placed — High-1); read `assignments` only when `ok == true`. */
    struct InputGroupPlan
    {
        bool ok = true;
        InputGroupingError error = InputGroupingError::None;
        int errorTrackIndex = -1;   ///< the offending track, or -1 (no track concept)
        std::vector<GroupAssignment> assignments;
    };

    /** Resolves a request's layout for planning: `Auto` -> device default
        (`InputMapping.h`, single source of truth); `MultiChannel` is planned as a
        stereo pair — **RT-1 does not support N-channel groups**, only Mono/Stereo. */
    inline InputLayout planLayoutFor (InputLayout requested, int deviceChannels) noexcept
    {
        if (requested == InputLayout::Auto)
            return defaultLayoutForDevice (deviceChannels);

        if (requested == InputLayout::MultiChannel)
            return InputLayout::Stereo;

        return requested;
    }

    /** Hardware channels a planned layout occupies.

        Contract: call only with a resolved `Mono`/`Stereo` layout (i.e. the output
        of `planLayoutFor`). Any other value returns 1. */
    inline int channelsPerLayout (InputLayout layout) noexcept
    {
        return layout == InputLayout::Stereo ? 2 : 1;
    }

    /** True when a run of `need` consecutive channels starting at `first` lies
        inside [0, deviceChannels). Overflow-safe for any `first`/`need` (the
        comparison never computes `first + need`). Single source of the check —
        used by the planner, the mono→stereo switch test and by Session's
        "channel off the device" predicate. */
    inline bool channelSpanFits (int first, int need, int deviceChannels) noexcept
    {
        return first >= 0 && need >= 1 && deviceChannels >= need
            && first <= deviceChannels - need;
    }

    /** True when `channel` is unowned (see `channelOccupancy`). */
    inline bool isChannelFree (const std::map<int, int>& occupancy, int channel) noexcept
    {
        return channel >= 0 && occupancy.find (channel) == occupancy.end();
    }

    /** Plans the grouping for `requests` on a device with `deviceChannels`
        inputs.

        Deterministic: requests are processed in `trackIndex` order, so the result
        never depends on the input order and no track ever "wins" by arriving last.
        Failures are explicit (see `InputGroupingError`) — the plan never clamps a
        channel onto the device. On failure the plan is empty (see
        `InputGroupPlan`).

        For RT-4 ("place only a new track over the existing occupancy"): pin the
        existing tracks with an explicit `requestedChannel` and pass the new one
        last (by order of `trackIndex`), or pre-seed occupancy via those pins. */
    inline InputGroupPlan planInputGrouping (std::span<const TrackInputRequest> requests,
                                             int deviceChannels)
    {
        InputGroupPlan plan;

        // Canonical order up front (also used by the no-device branch: High-2).
        std::vector<TrackInputRequest> ordered (requests.begin(), requests.end());
        std::stable_sort (ordered.begin(), ordered.end(),
                          [] (const TrackInputRequest& a, const TrackInputRequest& b)
                          { return a.trackIndex < b.trackIndex; });

        auto fail = [&plan] (InputGroupingError error, int trackIndex)
        {
            plan.ok = false;
            plan.error = error;
            plan.errorTrackIndex = trackIndex;
            plan.assignments.clear(); // High-1: never expose a half-placed plan
            return plan;
        };

        if (deviceChannels <= 0)
        {
            plan.ok = false;
            plan.error = InputGroupingError::NoDevice;
            plan.errorTrackIndex = ordered.empty() ? -1 : ordered.front().trackIndex;
            return plan;
        }

        // B1: a track index is required up front — never rely on a `-1` sentinel
        // meaning "free channel".
        for (const auto& request : ordered)
            if (request.trackIndex < 0)
                return fail (InputGroupingError::InvalidTrackIndex, request.trackIndex);

        std::vector<std::optional<int>> owner ((size_t) deviceChannels); // B1: real emptiness

        auto isFreeAt = [&owner] (int channel) noexcept
        {
            return channel >= 0 && channel < (int) owner.size()
                && ! owner[(size_t) channel].has_value();
        };

        for (const auto& request : ordered)
        {
            const auto layout = planLayoutFor (request.layout, deviceChannels);
            const int need = channelsPerLayout (layout);

            auto place = [&] (int first)
            {
                for (int i = 0; i < need; ++i)
                    owner[(size_t) (first + i)] = request.trackIndex;

                plan.assignments.push_back ({ request.trackIndex, first, layout });
            };

            if (request.requestedChannel >= 0)
            {
                const int first = request.requestedChannel;

                // B3: overflow-safe span check (shared helper).
                if (! channelSpanFits (first, need, deviceChannels))
                    return fail (InputGroupingError::ChannelOffDevice, request.trackIndex);

                for (int i = 0; i < need; ++i)
                    if (! isFreeAt (first + i))
                        return fail (layout == InputLayout::Stereo
                                         ? InputGroupingError::OccupiedNeighbour
                                         : InputGroupingError::ChannelOccupied, // High-3
                                     request.trackIndex);

                place (first);
                continue;
            }

            int chosen = -1;

            for (int start = 0; start + need <= deviceChannels; ++start)
            {
                bool free = true;

                for (int i = 0; i < need; ++i)
                    if (! isFreeAt (start + i))
                    {
                        free = false;
                        break;
                    }

                if (free)
                {
                    chosen = start;
                    break;
                }
            }

            if (chosen < 0)
                return fail (InputGroupingError::NoFreeChannel, request.trackIndex);

            place (chosen);
        }

        return plan;
    }

    /** `channel -> owning track index`, from a plan's assignments (both channels
        of a stereo pair map to the same track). */
    inline std::map<int, int> channelOccupancy (std::span<const GroupAssignment> assignments)
    {
        std::map<int, int> occupancy;

        for (const auto& assignment : assignments)
        {
            const int need = channelsPerLayout (assignment.layout);

            for (int i = 0; i < need; ++i)
                occupancy[assignment.firstChannel + i] = assignment.trackIndex;
        }

        return occupancy;
    }

    /** Would switching `trackIndex` (currently owning `firstChannel`) to a stereo
        pair on `(firstChannel, firstChannel + 1)` conflict? True when the pair
        runs off the device, or the right channel is owned by a different track
        (I6 — refuse rather than overwrite). */
    inline bool wouldConflictOnSwitchToStereo (int trackIndex, int firstChannel, int deviceChannels,
                                               const std::map<int, int>& occupancy) noexcept
    {
        // B3: overflow-safe span check (shared helper); a stereo pair needs 2.
        if (! channelSpanFits (firstChannel, 2, deviceChannels))
            return true;

        const auto owner = occupancy.find (firstChannel + 1);
        return owner != occupancy.end() && owner->second != trackIndex;
    }
}
