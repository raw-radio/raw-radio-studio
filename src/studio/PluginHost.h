// raw-radio-studio — plugin hosting support (Epic 3, FR-MIX-4/6/7).
//
// Owns the *scan* side of plugin hosting: enumerating the formats JUCE was
// built with (VST3 always; LV2 opt-in; AU on macOS — never VST2, decision
// D24), scanning their default search paths, and exposing the resulting
// known-plugin list to the browser UI.
//
// Scanning is **out-of-process** (FR-MIX-7 / NFR-REL-3): Tracktion's
// `PluginManager` installs a `PluginScanHelpers::CustomScanner` on its
// `knownPluginList`, and JUCE's `PluginDirectoryScanner` drives it. For VST3
// and LADSPA each probe is performed by a child process, so a crashing or
// hung plugin cannot take the host down. This class only runs the scan on a
// background thread; it never touches the audio thread.
//
// Hosting itself is in-process (decision D23): inserting a plugin into a track
// is handled by `Session` (the plugin becomes part of the Edit and therefore
// serialises with the project).

#pragma once

#include <JuceHeader.h>

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
#include <functional>

namespace rrs
{
    class PluginHost final : private juce::Thread
    {
    public:
        explicit PluginHost (tracktion::Engine&);
        ~PluginHost() override;

        //==============================================================================
        using CompletionCallback = std::function<void (int numFound, juce::String error)>;

        /** True while a scan is running. */
        bool isScanning() const noexcept            { return isThreadRunning(); }

        /** Overall scan progress in [0, 1], or -1 when no scan is running. */
        float getProgress() const noexcept          { return progress.load(); }

        /** The file/identifier currently being probed (for the status line). */
        juce::String getCurrentPluginName() const;

        /** Starts a scan of every hosted format's default search paths.
            `rescanExisting` also re-probes plugins already in the list.
            A no-op while a scan is already running. The callback fires on the
            message thread with the number of *newly* discovered plugins. */
        void startScan (bool rescanExisting, CompletionCallback = {});

        /** Asks a running scan to stop at the next file. Returns immediately;
            poll isScanning() for completion. */
        void cancelScan();

        //==============================================================================
        /** The known plugins, optionally filtered by a case-insensitive search
            across name / manufacturer / format, sorted by name.

            When `effectsOnly` is true (the default) instruments, generators,
            panners and other non-effect plugins are excluded: the browser feeds
            a track's insert chain, which accepts audio effects only (see
            studio/PluginSelection.h). Pass false for a raw inventory. */
        juce::Array<juce::PluginDescription> getKnownPlugins (const juce::String& searchText = {},
                                                              bool effectsOnly = true) const;

        int getNumKnownPlugins() const;

        //==============================================================================
        /** True when the engine will scan in a child process (FR-MIX-7). */
        bool usesOutOfProcessScanning() const;

        /** The format names this build hosts (e.g. "VST3", "AudioUnit"). */
        juce::StringArray getHostedFormatNames() const;

    private:
        void run() override;

        tracktion::Engine& engine;

        std::atomic<float> progress { -1.0f };
        bool rescan = false;
        CompletionCallback completion;
        mutable juce::CriticalSection nameLock;
        juce::String currentName;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginHost)
    };
}
