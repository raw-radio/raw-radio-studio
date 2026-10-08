// raw-radio-studio — user plugin presets (FR-MIX-6).

#include "PluginPresets.h"

#include "AppPaths.h"

namespace rrs
{
    namespace
    {
        /** Keeps [A-Za-z0-9_-]; everything else collapses to '_'. Used for the
            plugin folder key so a plugin name can never escape the store root. */
        juce::String sanitiseToken (const juce::String& text)
        {
            juce::String out;

            for (auto c : text)
                out << (juce::CharacterFunctions::isLetterOrDigit (c) || c == '_' ? c : '_');

            out = out.trimCharactersAtStart ("_").trimCharactersAtEnd ("_");

            if (out.length() > 48)
                out = out.substring (0, 48);

            return out.isNotEmpty() ? out : juce::String ("plugin");
        }

        /** Characters that may never appear in a preset name/file. Beyond the
            path separators/control characters (which break POSIX and Windows
            paths alike) this includes the characters Windows forbids in a file
            name: * ? " < > | :. Validation *rejects* them and sanitiseFileName
            replaces them, so the accepted set and the written file name can
            never diverge (a name that validates always maps to a writable file,
            on every platform). */
        bool isIllegalFileNameChar (juce::juce_wchar c)
        {
            return c < 32 || c == 127
                || c == '/' || c == '\\' || c == ':'
                || c == '*' || c == '?' || c == '"'
                || c == '<' || c == '>' || c == '|';
        }

        /** Preset *file* name: spaces and most punctuation are allowed for a
            readable name, but path separators, control characters and the
            Windows-reserved punctuation are replaced with '_'. */
        juce::String sanitiseFileName (const juce::String& name)
        {
            juce::String out;

            for (auto c : name.trim())
                out << (isIllegalFileNameChar (c) ? juce::juce_wchar ('_') : c);

            return out.trim();
        }
    }

    PluginPresets::PluginPresets (juce::File rootDirectory)
        : root (rootDirectory != juce::File() ? std::move (rootDirectory)
                                              : paths::presetsDirectory())
    {
    }

    void PluginPresets::setRootDirectory (const juce::File& rootDirectory)
    {
        const juce::ScopedLock sl (lock);
        root = rootDirectory != juce::File() ? rootDirectory : paths::presetsDirectory();
    }

    juce::File PluginPresets::getRootDirectory() const
    {
        const juce::ScopedLock sl (lock);
        return root;
    }

    juce::String PluginPresets::keyFor (const juce::String& pluginName,
                                        const juce::String& format,
                                        const juce::String& fileOrIdentifier)
    {
        const auto identity = pluginName + "|" + format + "|" + fileOrIdentifier;
        const auto h = static_cast<juce::uint64> (identity.hashCode64());
        const auto hash = juce::String::toHexString (h).paddedLeft ('0', 16).substring (0, 8).toLowerCase();
        return sanitiseToken (pluginName) + "-" + hash;
    }

    bool PluginPresets::isValidPresetName (const juce::String& name)
    {
        const auto trimmed = name.trim();

        if (trimmed.isEmpty())
            return false;

        for (auto c : trimmed)
            if (isIllegalFileNameChar (c))
                return false;

        return true;
    }

    juce::File PluginPresets::folderForKey (const juce::String& key) const
    {
        return root.getChildFile (key);
    }

    juce::File PluginPresets::presetFileFor (const juce::String& key, const juce::String& name) const
    {
        return folderForKey (key).getChildFile (sanitiseFileName (name) + extension);
    }

    juce::StringArray PluginPresets::listPresets (const juce::String& key) const
    {
        const juce::ScopedLock sl (lock);

        juce::StringArray names;

        if (key.isEmpty())
            return names;

        const auto folder = folderForKey (key);

        if (! folder.isDirectory())
            return names;

        for (const auto& file : folder.findChildFiles (juce::File::findFiles, false,
                                                       juce::String ("*") + extension))
            names.add (file.getFileNameWithoutExtension());

        names.sort (true);
        return names;
    }

    bool PluginPresets::savePreset (const juce::String& key, const juce::String& name,
                                    const juce::MemoryBlock& state) const
    {
        if (key.isEmpty() || ! isValidPresetName (name))
            return false;

        const juce::ScopedLock sl (lock);

        const auto folder = folderForKey (key);

        if (! folder.createDirectory())
            return false;

        const auto file = folder.getChildFile (sanitiseFileName (name) + extension);

        // Atomic replace: a crashed write can never leave a half-written preset.
        return file.replaceWithData (state.getData(), state.getSize());
    }

    bool PluginPresets::loadPreset (const juce::String& key, const juce::String& name,
                                    juce::MemoryBlock& stateOut) const
    {
        if (key.isEmpty() || ! isValidPresetName (name))
            return false;

        const juce::ScopedLock sl (lock);

        const auto file = folderForKey (key).getChildFile (sanitiseFileName (name) + extension);

        if (! file.existsAsFile())
            return false;

        stateOut.setSize (0);
        return file.loadFileAsData (stateOut);
    }

    bool PluginPresets::deletePreset (const juce::String& key, const juce::String& name) const
    {
        if (key.isEmpty() || ! isValidPresetName (name))
            return false;

        const juce::ScopedLock sl (lock);

        return folderForKey (key).getChildFile (sanitiseFileName (name) + extension).deleteFile();
    }
}
