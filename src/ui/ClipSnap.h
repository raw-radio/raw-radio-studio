// raw-radio-studio — deterministic clip-edge snapping (Epic 4 UX).
//
// During a clip move/trim the dragged edge is pulled onto nearby clip
// boundaries (and the playhead / whole-second grid) so takes line up. This
// header holds the pure, side-effect-free core of that so it can be unit-tested
// without a window, an Edit or a mouse: given a candidate time, a list of edge
// times and a threshold, return the snapped time.
//
// The behaviour is intentionally simple and reproducible:
//   * an edge snaps when |edge - candidate| <= threshold (threshold is passed
//     in seconds, converted from pixels by the caller so the pull distance is
//     constant on screen regardless of zoom);
//   * the nearest edge wins;
//   * exact ties resolve to the smaller time, so the result never depends on
//     the order the edges happened to be collected in.
//
// Header-only, no JUCE dependency: safe to include from the UI and from tests.

#pragma once

#include <cmath>
#include <vector>

namespace rrs::clipsnap
{
    struct SnapResult
    {
        double time = 0.0;   ///< Snapped time, or `candidateSeconds` if not snapped.
        bool snapped = false;///< True when an edge was within the threshold.
    };

    /** Pure snap: returns the edge nearest to `candidateSeconds` when it lies
        within `thresholdSeconds`, otherwise the candidate unchanged. A
        non-positive threshold disables snapping. Ties resolve to the smaller
        edge time (deterministic). */
    inline SnapResult snapTimeToEdges (double candidateSeconds,
                                       const std::vector<double>& edges,
                                       double thresholdSeconds) noexcept
    {
        SnapResult result { candidateSeconds, false };

        if (! (thresholdSeconds > 0.0))
            return result;

        bool found = false;
        double bestDelta = 0.0;

        for (const double edge : edges)
        {
            const double delta = std::abs (edge - candidateSeconds);

            if (delta > thresholdSeconds)
                continue;

            // Strictly closer wins; on a tie the smaller edge wins so the outcome
            // is independent of collection order. The epsilon avoids a raw
            // floating-point `==` (and treats sub-picosecond differences as a tie).
            constexpr double tieEpsilon = 1.0e-12;

            if (! found || delta < bestDelta - tieEpsilon
                || (std::abs (delta - bestDelta) <= tieEpsilon && edge < result.time))
            {
                found = true;
                bestDelta = delta;
                result.time = edge;
            }
        }

        result.snapped = found;
        return result;
    }
}
