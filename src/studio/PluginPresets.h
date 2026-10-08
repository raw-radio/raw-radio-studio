// raw-radio-studio — user plugin presets (FR-MIX-6).
//
// A preset is the raw plugin-state blob produced by
// `juce::AudioPluginInstance::getStateInformation` and consumed by
// `setStateInformation`. Each hosted plugin gets its own folder keyed by
// name/format/identifier, and each preset is one file inside it:
//
//   <root>/<plugin-key>/<preset-name>.rrspreset
//
// `Session` wraps this store with the JUCE state IO (getStateInformation /
// setStateInformation) so the browser UI only deals in names. All filesystem
// access is on the message thread — never the audio thread (NRT rule).

#pragma once

#include <juce_core/juce_core.h>

namespace rrs
{
    class PluginPresets
    {
    public:
        /** File extension for a stored preset. */
        static constexpr const char* extension = ".rrspreset";

        /** `rootDirectory` empty selects the app store (`paths::presetsDirectory()`),
            which is what the UI uses; tests pass a scratch folder. */
        explicit PluginPresets (juce::File rootDirectory = {});

        void setRootDirectory (const juce::File& rootDirectory);
        juce::File getRootDirectory() const;

        //==============================================================================
        /** Stable, filesystem-safe folder key for a hosted plugin, derived from
            its name/format/identifier. Never empty for a valid description. */
        static juce::String keyFor (const juce::String& pluginName,
                                    const juce::String& format,
                                    const juce::String& fileOrIdentifier);

        /** A preset name is valid when it is non-empty after trimming and
            contains no path separators or control characters. */
        static bool isValidPresetName (const juce::String& name);

        /** Existing preset names for `key`, sorted case-insensitively. Empty
            for an unknown key. */
        juce::StringArray listPresets (const juce::String& key) const;

        /** Writes `state` as a named preset (atomic replace). Fails on an
            invalid key/name or an unwritable folder. */
        bool savePreset (const juce::String& key, const juce::String& name,
                         const juce::MemoryBlock& state) const;

        /** Reads a named preset's state into `stateOut`. */
        bool loadPreset (const juce::String& key, const juce::String& name,
                         juce::MemoryBlock& stateOut) const;

        bool deletePreset (const juce::String& key, const juce::String& name) const;

        /** Resolves the preset file for a key/name (for tests / diagnostics). */
        juce::File presetFileFor (const juce::String& key, const juce::String& name) const;

    private:
        juce::File folderForKey (const juce::String& key) const;

        juce::File root;
        mutable juce::CriticalSection lock;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginPresets)
    };
}
