// raw-radio-studio — last-used file-dialog directory store (see header).

#include "LastDirectoryStore.h"

namespace rrs
{
    LastDirectoryStore::LastDirectoryStore (Storage& storageToUse)
        : storage (storageToUse)
    {
    }

    juce::String LastDirectoryStore::keyFor (Kind kind)
    {
        switch (kind)
        {
            case Kind::projects:    return "lastDir.projects";
            case Kind::importAudio: return "lastDir.import";
            case Kind::exportFile:  return "lastDir.export";
            case Kind::exportStems: return "lastDir.exportStems";
        }

        jassertfalse;
        return "lastDir.unknown";
    }

    juce::File LastDirectoryStore::getDirectory (Kind kind, const juce::File& fallback) const
    {
        const auto stored = storage.getValue (keyFor (kind));

        if (stored.isNotEmpty())
        {
            const juce::File remembered (stored);

            // Guard against a remembered folder that has since been moved or
            // deleted: fall back rather than open a chooser at a missing path.
            if (remembered.isDirectory())
                return remembered;
        }

        return fallback;
    }

    void LastDirectoryStore::rememberFile (Kind kind, const juce::File& chosenFile)
    {
        if (chosenFile != juce::File())
            rememberDirectory (kind, chosenFile.getParentDirectory());
    }

    void LastDirectoryStore::rememberDirectory (Kind kind, const juce::File& chosenDirectory)
    {
        if (chosenDirectory == juce::File())
            return;

        storage.setValue (keyFor (kind), chosenDirectory.getFullPathName());
        storage.save();
    }
}
