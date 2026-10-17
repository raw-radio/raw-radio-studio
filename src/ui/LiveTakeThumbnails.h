// raw-radio-studio — cached live-recording thumbnails (LW-2 / review M1).
//
// The engine keeps ONE `RecordingThumbnailManager::Thumbnail` per file that is
// currently being written, and it is only kept alive by the `Ptr`s its users hold
// (`Thumbnail::~Thumbnail` removes it from the manager's pool). Tracktion creates
// the UI's Ptr for a take through `getThumbnailFor()`, which either returns the
// existing object or *news one up* — and a `Thumbnail` that loses its last Ptr is
// destroyed, so re-fetching the same file every frame would create/destroy a
// thumbnail (and its AudioThumbnail) continuously.
//
// This cache holds one Ptr PER TAKE FILE, so:
//   * drawing N tracks that are recording at once re-runs `getThumbnailFor` at most
//     once per file per take (not once per frame per track — a single-member cache
//     had to re-fetch whenever the drawn track changed),
//   * the object a frame draws is the same one the next frame draws (pointer
//     stability), and
//   * `clear()` releases everything the moment recording stops (so nothing lingers
//     once the takes are finalised and the real clips' SmartThumbnails take over).
//
// Message thread only (the engine feeds the thumbnails from the audio thread; this
// class never touches them). Kept free of MainComponent so it is unit-testable.

#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <map>

namespace rrs
{
    class LiveTakeThumbnails
    {
    public:
        /** The thumbnail for a take file, created on first use and then reused.
            Returns nullptr for an empty File (never a throw). */
        tracktion::RecordingThumbnailManager::Thumbnail* get (tracktion::Engine& engine,
                                                             const juce::File& file)
        {
            if (file == juce::File())
                return nullptr;

            const auto found = thumbnails.find (file);

            if (found != thumbnails.end())
                return found->second.get();

            return thumbnails
                .emplace (file, engine.getRecordingThumbnailManager().getThumbnailFor (file))
                .first->second.get();
        }

        /** Drops every cached take. Called when recording stops (and when the
            session changes), so a finished take's thumbnail cannot linger. */
        void clear()
        {
            thumbnails.clear();
        }

        /** How many take files are cached right now (never more than the number of
            files currently being drawn as live takes). */
        int size() const noexcept
        {
            return (int) thumbnails.size();
        }

    private:
        std::map<juce::File, tracktion::RecordingThumbnailManager::Thumbnail::Ptr> thumbnails;
    };
}
