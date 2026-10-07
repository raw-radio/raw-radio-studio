// raw-radio-studio — application paths (Epic 1).
//
// Central place for the on-disk locations this app owns. Keeping them in one
// header avoids scattering `getSpecialLocation` calls through the codebase and
// makes the crash-recovery layout explicit:
//
//   <app data>/raw-radio-studio/
//       Projects/                  — .tracktionedit session files
//       settings.json              — app settings (autosave interval, last session)
//       session.lock               — unclean-shutdown sentinel (removed on clean exit)
//       Projects/Recordings/       — recorded takes (crash-safe, incremental WAV)

#pragma once

#include <juce_core/juce_core.h>

namespace rrs::paths
{
    /** Per-user application data directory for this app. */
    inline juce::File appDataDirectory()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("raw-radio-studio");
    }

    /** Directory that holds `.tracktionedit` session files. */
    inline juce::File projectsDirectory()
    {
        return appDataDirectory().getChildFile ("Projects");
    }

    /** Default session file used when the user has not chosen one. */
    inline juce::File defaultEditFile()
    {
        return projectsDirectory().getChildFile ("Untitled Session.tracktionedit");
    }

    /** App settings file (JSON via juce::PropertiesFile). */
    inline juce::File settingsFile()
    {
        return appDataDirectory().getChildFile ("settings.json");
    }

    /** Sentinel written at startup and removed on a clean shutdown. */
    inline juce::File lockFile()
    {
        return appDataDirectory().getChildFile ("session.lock");
    }

    /** Recordings directory for a session file (sibling `Recordings/` folder). */
    inline juce::File recordingsDirectoryFor (const juce::File& editFile)
    {
        return editFile.getParentDirectory().getChildFile ("Recordings");
    }

    /** Path of the autosaved temp version of a session file (`.tmp_<name>` sibling).
        Mirrors tracktion::EditFileOperations::getTempVersionOfEditFile so recovery
        can be reasoned about (and tested) without the engine. */
    inline juce::File tempEditFileFor (const juce::File& editFile)
    {
        return editFile != juce::File()
                   ? editFile.getSiblingFile (".tmp_" + editFile.getFileNameWithoutExtension())
                   : juce::File();
    }
}
