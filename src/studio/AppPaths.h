// raw-radio-studio — application paths (Epic 1).
//
// Central place for the on-disk locations this app owns. Keeping them in one
// header avoids scattering `getSpecialLocation` calls through the codebase and
// makes the crash-recovery layout explicit:
//
//   <app data>/raw-radio-studio/
//       Projects/                  — .tracktionedit session files
//       session.lock               — interrupted-session sentinel: written when
//                                    a session becomes active, removed on a
//                                    clean shutdown and by Session::close()
//                                    (a deliberately-closed project). Its
//                                    presence at startup is the authoritative
//                                    "previous run was interrupted" signal (see
//                                    MainComponent's startup recovery).
//       Projects/Recordings/       — recorded takes (crash-safe, incremental WAV)
//
// App settings (autosave interval, last session, last-used file-dialog
// directories — see LastDirectoryStore.h) are not handled here: they are stored
// by a juce::PropertiesFile owned by MainComponent (see loadSettings()).

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

    /** Sentinel written at startup and removed on a clean shutdown (or by
        Session::close()). */
    inline juce::File lockFile()
    {
        return appDataDirectory().getChildFile ("session.lock");
    }

    /** Recordings directory for a session file (sibling `Recordings/` folder). */
    inline juce::File recordingsDirectoryFor (const juce::File& editFile)
    {
        return editFile.getParentDirectory().getChildFile ("Recordings");
    }

    /** User plugin presets (FR-MIX-6): one subfolder per hosted plugin. */
    inline juce::File presetsDirectory()
    {
        return appDataDirectory().getChildFile ("Presets");
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
