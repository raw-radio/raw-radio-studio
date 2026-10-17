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

    //==========================================================================
    // Span occupancy: ONE source of truth for "who owns a channel" (RT-4/RT-6/RT-8).
    //
    // A channel is owned by a *span* (a starting channel + a length), not by a
    // bare track index: RT-4 allows several tracks to **share an identical span**
    // (same start + same length) on one device, while a *different* span that
    // overlaps an owned channel is refused. Recording the span (not just the
    // track) is therefore required to tell sharing from a real conflict.
    //
    // These primitives are the SINGLE implementation of that rule: the planner
    // (`planInputLayout`), the Session write guard (RT-6) and the Channel menu
    // (RT-8) all decide through `checkSpanOccupancy`, so they cannot disagree.

    /** One track's occupied span. `need` is the channel count (>= 1). */
    struct SpanOwner
    {
        int firstChannel = 0;
        int need = 1;
        int trackIndex = -1;
    };

    /** `channel -> owning span` (empty optional = free). Channels outside the
        device are simply not represented. Used by `checkSpanOccupancy` and the
        Channel menu (RT-8). */
    inline std::vector<std::optional<SpanOwner>> buildSpanOccupancy (std::span<const SpanOwner> spans,
                                                                    int deviceChannels)
    {
        std::vector<std::optional<SpanOwner>> owner ((size_t) juce::jmax (0, deviceChannels));

        for (const auto& span : spans)
        {
            if (span.need < 1 || ! channelSpanFits (span.firstChannel, span.need, deviceChannels))
                continue;

            for (int i = 0; i < span.need; ++i)
                owner[(size_t) (span.firstChannel + i)] = span;
        }

        return owner;
    }

    /** Why a proposed span cannot be placed (RT-6). */
    enum class SpanConflict
    {
        None,           ///< free, or shared with an identical span (RT-4)
        OffDevice,      ///< the span runs off the device
        OwnedByOther    ///< an overlapping channel is owned by a DIFFERENT span
    };

    /** The outcome of `checkSpanOccupancy`. */
    struct SpanCheckResult
    {
        SpanConflict conflict = SpanConflict::None;
        int conflictChannel = -1;   ///< the offending channel (0-based)
        int ownerTrackIndex = -1;   ///< the owner track for `OwnedByOther`
        int ownerNeed = 0;          ///< the owner span's length for `OwnedByOther`
    };

    /** Checks whether a span `first .. first + need - 1` may be placed against an
        occupancy built by `buildSpanOccupancy`.

        RT-4 sharing: a channel owned by an **identical** span (same
        `firstChannel` + same `need`) is shared, not a conflict. Any other owner —
        including a different-length span starting on the same channel — is a
        conflict, reported with the offending channel, its owner and the owner's
        span length (I6; the caller distinguishes a "neighbour" conflict). */
    inline SpanCheckResult checkSpanOccupancy (int first, int need, int deviceChannels,
                                               const std::vector<std::optional<SpanOwner>>& occupancy)
    {
        SpanCheckResult result;

        if (need < 1)
            return result; // a zero-length span occupies nothing (unassigned)

        if (! channelSpanFits (first, need, deviceChannels))
        {
            result.conflict = SpanConflict::OffDevice;
            result.conflictChannel = first;
            return result;
        }

        for (int i = 0; i < need; ++i)
        {
            const auto& slot = occupancy[(size_t) (first + i)];

            if (! slot.has_value())
                continue;

            // Same span (start + length): sharing (RT-4) — never a conflict.
            if (slot->firstChannel == first && slot->need == need)
                continue;

            result.conflict = SpanConflict::OwnedByOther;
            result.conflictChannel = first + i;
            result.ownerTrackIndex = slot->trackIndex;
            result.ownerNeed = slot->need;
            return result;
        }

        return result;
    }

    /** Would switching to a stereo pair on `(firstChannel, firstChannel + 1)`
        conflict with `otherSpans` (the spans of the OTHER tracks)? A thin wrapper
        over `checkSpanOccupancy` so the stereo switch (I6, RT-6) shares the one
        rule — identical spans still share (RT-4). */
    inline bool wouldConflictOnSwitchToStereo (int firstChannel, int deviceChannels,
                                               std::span<const SpanOwner> otherSpans)
    {
        const auto occupancy = buildSpanOccupancy (otherSpans, deviceChannels);
        return checkSpanOccupancy (firstChannel, 2, deviceChannels, occupancy).conflict
               != SpanConflict::None;
    }

    /** Per-track layout plan: places each request in canonical (`trackIndex`)
        order, keeping a span occupancy. A track that cannot be placed
        gets an explicit `error` (with `ownerTrackIndex`/`conflictChannel`) while
        the others are still placed — unlike the all-or-nothing
        `planInputGrouping`. This is the single rule both `configureTracks` and
        the per-track "input problem" read (RT-3 M1/M2).

        Occupancy (RT-4): tracks may **share an identical span** (same
        `firstChannel` + same `need`). The placement decision is
        `checkSpanOccupancy` itself — the same primitive the Session write guard
        (RT-6) and the Channel menu (RT-8) use, so all three agree by construction. */
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
        std::vector<std::optional<SpanOwner>> owner (hasDevice ? (size_t) deviceChannels : 0);

        auto place = [&owner, hasDevice] (int first, int need, int trackIndex) noexcept
        {
            if (! hasDevice)
                return;

            for (int i = 0; i < need; ++i)
                if (first + i >= 0 && first + i < (int) owner.size())
                    owner[(size_t) (first + i)] = SpanOwner { first, need, trackIndex };
        };

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

                // ONE rule: the same span-occupancy check the write guard (RT-6)
                // and the Channel menu (RT-8) use. `ownerNeed` picks the wording
                // (a pair touching a neighbour vs a mono channel clash).
                const auto check = checkSpanOccupancy (first, r.need, deviceChannels, owner);

                if (check.conflict == SpanConflict::OffDevice)
                {
                    r.error = InputGroupingError::ChannelOffDevice;
                }
                else if (check.conflict == SpanConflict::OwnedByOther)
                {
                    r.error = (r.need == 2 || check.ownerNeed == 2)
                                  ? InputGroupingError::OccupiedNeighbour
                                  : InputGroupingError::ChannelOccupied;
                    r.ownerTrackIndex = check.ownerTrackIndex;
                    r.conflictChannel = check.conflictChannel;
                }
                else
                {
                    place (first, r.need, request.trackIndex);
                    r.firstChannel = first;
                    r.placed = true;
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
                    place (chosen, r.need, request.trackIndex);
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

    //==========================================================================
    // RT-8 — the Channel menu model.

    /** How a hardware channel appears in the Channel menu (RT-8 / I4). */
    enum class ChannelAvailability
    {
        Free,       ///< no owner — take it.
        Shared,     ///< owned by an identical span (RT-4) — take it (shares the device).
        Used,       ///< owned by a different span — DISABLED in the menu, labelled
                    ///< with the owner.
        OffDevice   ///< the proposed span would not fit (e.g. a pair at the last
                    ///< channel). Kept SELECTABLE in the menu (RT-2b deliberately
                    ///< allows pinning an invalid channel so the reason is shown),
                    ///< only hinted in the label.
    };

    /** One Channel-menu row. `ownerTrackIndex` is set for `Shared` / `Used`. */
    struct ChannelMenuItem
    {
        int channel = 0;
        ChannelAvailability availability = ChannelAvailability::Free;
        int ownerTrackIndex = -1;
    };

    /** Builds the Channel-menu model for a track that would take a span of
        `proposedNeed` channels starting at each listed channel. `otherSpans` are
        the spans of the OTHER input tracks.

        Every real device channel `0 .. deviceChannels - 1` is listed (I4). The mark
        is the SAME decision the write guard uses (`checkSpanOccupancy`), so the menu
        and `Session::canSetTrackInputMapping` can never disagree:
          * `Shared`  — the span starting here is IDENTICAL to an owner's span
                        (RT-4 sharing — selectable),
          * `Used`    — the span would overlap a DIFFERENT span (disabled; owner set),
          * `OffDevice` — the span does not fit the device (disabled; RT-2b keeps
                        it selectable at write time, the menu only hints),
          * `Free`    — no owner overlaps. */
    inline std::vector<ChannelMenuItem> channelMenuModel (int proposedNeed, int deviceChannels,
                                                          std::span<const SpanOwner> otherSpans)
    {
        const int need = juce::jmax (1, proposedNeed);
        const auto occupancy = buildSpanOccupancy (otherSpans, deviceChannels);

        std::vector<ChannelMenuItem> items;
        items.reserve ((size_t) juce::jmax (0, deviceChannels));

        for (int channel = 0; channel < deviceChannels; ++channel)
        {
            ChannelMenuItem item;
            item.channel = channel;

            const auto check = checkSpanOccupancy (channel, need, deviceChannels, occupancy);

            switch (check.conflict)
            {
                case SpanConflict::OffDevice:
                    item.availability = ChannelAvailability::OffDevice;
                    break;

                case SpanConflict::OwnedByOther:
                    item.availability = ChannelAvailability::Used;
                    item.ownerTrackIndex = check.ownerTrackIndex;
                    break;

                case SpanConflict::None:
                default:
                {
                    // No conflict: shared only when this exact span is an owner's
                    // own span (identical start + length), else free.
                    const auto& slot = occupancy[(size_t) channel];

                    if (slot.has_value() && slot->firstChannel == channel && slot->need == need)
                    {
                        item.availability = ChannelAvailability::Shared;
                        item.ownerTrackIndex = slot->trackIndex;
                    }
                    else
                    {
                        item.availability = ChannelAvailability::Free;
                    }
                    break;
                }
            }

            items.push_back (item);
        }

        return items;
    }
}
