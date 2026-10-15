// raw-radio-studio — read-and-clear → peek meter cache (Epic 8 phase 1b follow-up).
//
// The engine's LevelMeasurer is read-and-clear: exactly one consumer may pull a
// reading per frame. To let several UI widgets (the mixer strips and the lane
// mini-meters) show the same level, a single owner pumps the meters and stores
// each reading here; the other consumers peek it without clearing.
//
// Pure, header-only, value-typed — so the cache semantics (store / peek /
// out-of-range silence / clear) are unit-testable without an engine or a window.
// Never touched from the audio thread.

#pragma once

#include <cstddef>
#include <vector>

namespace rrs
{
    template <typename Reading>
    class MeterCache
    {
    public:
        /** Stores the latest reading for `index` (grows on demand). */
        void store (int index, Reading reading)
        {
            if (index < 0)
                return;

            const auto i = (std::size_t) index;

            if (i >= values.size())
                values.resize (i + 1);

            values[i] = reading;
        }

        /** The latest stored reading for `index`, or a default-constructed
            Reading (silence) when nothing was stored yet / out of range. */
        Reading peek (int index) const
        {
            if (index < 0 || (std::size_t) index >= values.size())
                return Reading {};

            return values[(std::size_t) index];
        }

        /** Drops every cached reading (called when the track set changes / the
            session is torn down, so a stale frame can never be peeked). */
        void clear() { values.clear(); }

        int size() const noexcept { return (int) values.size(); }

    private:
        std::vector<Reading> values;
    };
}
