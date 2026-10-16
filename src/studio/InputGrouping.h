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
        channel, or -1 to let the planner pick the first free run. `needOverride`
        (>0) forces the span (used for a MultiChannel track's `numChannels`);
        0 means "derive it from the resolved layout". `trackIndex` MUST be >= 0. */
    struct TrackInputRequest
    {
        int trackIndex = -1;
        InputLayout layout = InputLayout::Auto;
        int requestedChannel = -1;
        int needOverride = 0;
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

    /** One track's result in a per-track layout plan (see `planInputLayout`). */
    struct TrackLayoutResult
    {
        int trackIndex = -1;
        int firstChannel = 0;
        int need = 1;                            ///< resolved span
        InputLayout layout = InputLayout::Mono;  ///< resolved Mono/Stereo
        bool placed = false;
        InputGroupingError error = InputGroupingError::None; ///< set when !placed (None for placed)
        int ownerTrackIndex = -1;                ///< for occupancy errors: the track that owns the channel
        int conflictChannel = -1;                ///< the offending channel (0-based), or the requested start
    };

    /** A per-track layout plan: `tracks` is in canonical (`trackIndex`) order; a
        track that could not be placed carries an explicit `error` (no silent
        clamp). Session reads `tracks`; the occupancy it applies is derived from
        the placed track's `firstChannel`/`need`. */
    struct InputLayoutPlan
    {
        std::vector<TrackLayoutResult> tracks;
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

    /** Per-track layout plan: places each request in canonical (`trackIndex`)
        order, keeping a channel-occupancy map. A track that cannot be placed
        gets an explicit `error` (with `ownerTrackIndex`/`conflictChannel`) while
        the others are still placed — unlike the all-or-nothing
        `planInputGrouping`. This is the single rule both `configureTracks` and
        the per-track "input problem" read (RT-3 M1/M2).

        Occupancy (RT-4 refresh): tracks may **share an identical span** (same
        `firstChannel` + same `need`) — several tracks on one channel/pair is
        valid (the device config is identical). A *different* span overlapping an
        owned channel is still refused. */
    inline InputLayoutPlan planInputLayout (std::span<const TrackInputRequest> requests,
                                            int deviceChannels)
    {
        InputLayoutPlan plan;

        std::vector<TrackInputRequest> ordered (requests.begin(), requests.end());
        std::stable_sort (ordered.begin(), ordered.end(),
                          [] (const TrackInputRequest& a, const TrackInputRequest& b)
                          { return a.trackIndex < b.trackIndex; });

        const bool hasDevice = deviceChannels > 0;

        // For each channel: the span that owns it (so identical spans can share).
        struct Span { int first; int need; int repTrack; };
        std::vector<std::optional<Span>> owner (hasDevice ? (size_t) deviceChannels : 0);

        auto isFreeAt = [&owner, hasDevice] (int channel) noexcept
        {
            return hasDevice && channel >= 0 && channel < (int) owner.size()
                && ! owner[(size_t) channel].has_value();
        };

        for (const auto& request : ordered)
        {
            TrackLayoutResult r;
            r.trackIndex = request.trackIndex;
            r.layout = planLayoutFor (request.layout, deviceChannels);
            r.need = request.needOverride > 0 ? request.needOverride
                                              : channelsPerLayout (r.layout);
            r.conflictChannel = request.requestedChannel;

            if (request.trackIndex < 0)
            {
                r.error = InputGroupingError::InvalidTrackIndex;
            }
            else if (request.layout == InputLayout::None)
            {
                // Unassigned (RT-4): valid, consumes no channel, is not an error.
                r.need = 0;
                r.placed = false;
                r.error = InputGroupingError::None;
            }
            else if (! hasDevice)
            {
                r.error = InputGroupingError::NoDevice;
            }
            else if (request.requestedChannel >= 0)
            {
                const int first = request.requestedChannel;

                if (! channelSpanFits (first, r.need, deviceChannels))
                {
                    r.error = InputGroupingError::ChannelOffDevice;
                }
                else
                {
                    for (int i = 0; i < r.need; ++i)
                    {
                        const auto& slot = owner[(size_t) (first + i)];

                        if (! slot.has_value())
                            continue;

                        // Same span (same start + same length): sharing is allowed.
                        if (slot->first == first && slot->need == r.need)
                            continue;

                        r.error = (r.need == 2 || slot->need == 2) ? InputGroupingError::OccupiedNeighbour
                                                                  : InputGroupingError::ChannelOccupied;
                        r.ownerTrackIndex = slot->repTrack;
                        r.conflictChannel = first + i;
                        break;
                    }

                    if (r.error == InputGroupingError::None)
                    {
                        for (int i = 0; i < r.need; ++i)
                            owner[(size_t) (first + i)] = Span { first, r.need, request.trackIndex };

                        r.firstChannel = first;
                        r.placed = true;
                    }
                }
            }
            else
            {
                int chosen = -1;

                // Overflow-safe span bound (same helper as everywhere else).
                for (int start = 0; channelSpanFits (start, r.need, deviceChannels); ++start)
                {
                    bool free = true;

                    for (int i = 0; i < r.need; ++i)
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
                {
                    r.error = InputGroupingError::NoFreeChannel;
                }
                else
                {
                    for (int i = 0; i < r.need; ++i)
                        owner[(size_t) (chosen + i)] = Span { chosen, r.need, request.trackIndex };

                    r.firstChannel = chosen;
                    r.placed = true;
                }
            }

            plan.tracks.push_back (r);
        }

        return plan;
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
        const auto layout = planInputLayout (requests, deviceChannels);

        // No device at all is its own error, even with no requests (High-2).
        if (deviceChannels <= 0)
        {
            plan.ok = false;
            plan.error = InputGroupingError::NoDevice;
            plan.errorTrackIndex = layout.tracks.empty() ? -1 : layout.tracks.front().trackIndex;
            return plan;
        }

        for (const auto& t : layout.tracks)
        {
            // Unassigned tracks (RT-4) are not part of the grouping and are not
            // an error — skip them.
            if (! t.placed && t.error == InputGroupingError::None && t.layout == InputLayout::None)
                continue;

            if (! t.placed)
            {
                plan.ok = false;
                plan.error = t.error;
                plan.errorTrackIndex = t.trackIndex;
                plan.assignments.clear(); // High-1: never expose a half-placed plan
                return plan;
            }

            plan.assignments.push_back ({ t.trackIndex, t.firstChannel,
                                          t.layout == InputLayout::Stereo ? InputLayout::Stereo
                                                                          : InputLayout::Mono });
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
