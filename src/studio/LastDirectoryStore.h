// raw-radio-studio — last-used file-dialog directory store.
//
// Every save/open/export file chooser used to open at a fixed location (the
// Projects/ folder for sessions, the current session's folder for exports), so
// the engineer had to re-navigate to their working folder on every operation.
// This store remembers the last directory the user actually chose, per
// operation kind, in the app settings (`juce::PropertiesFile` at
// `<app data>/raw-radio-studio/raw-radio-studio.settings`), and the UI uses
// it as the chooser's initial directory. The keys are `lastDir.projects`,
// `lastDir.import`, `lastDir.export`, and `lastDir.exportStems`.
//
// Operation kinds (each gets its own key so browsing for one never clobbers
// another):
//
//   * projects      — New / Open / Save As of `.tracktionedit` session files.
//                     Initially the Projects directory
//                     (`<app data>/raw-radio-studio/Projects/`), then the last
//                     directory a project was created/opened/saved in.
//   * importAudio   — "Import audio file" backing-track chooser.
//   * exportFile    — Export WAV and Export region WAV (file choosers).
//   * exportStems   — Export stems (folder chooser); kept separate because a
//                     stems batch lands in a directory, not a single file.
//
// Pure logic: this class does no dialogs and touches the filesystem only to
// verify that a remembered directory still exists. It is unit-tested against an
// in-memory Storage, so the remember/fallback contract is covered without a UI.

#pragma once

#include <juce_core/juce_core.h>

namespace rrs
{
    class LastDirectoryStore
    {
    public:
        /** The persisted key namespace for each file-dialog operation. */
        enum class Kind
        {
            projects,
            importAudio,
            exportFile,
            exportStems
        };

        /** Minimal key/value persistence seam. The application adapts a
            `juce::PropertiesFile`; tests use an in-memory map. Keeping this tiny
            interface means the remember/fallback logic is testable without any
            file I/O. */
        class Storage
        {
        public:
            virtual ~Storage() = default;

            virtual juce::String getValue (const juce::String& key) const = 0;
            virtual void setValue (const juce::String& key, const juce::String& value) = 0;
            /** Flushes pending values to disk (a no-op for the in-memory test). */
            virtual void save() = 0;
        };

        explicit LastDirectoryStore (Storage& storageToUse);

        /** Settings key backing `kind` (e.g. "lastDir.projects"). */
        static juce::String keyFor (Kind kind);

        /** The remembered directory for `kind`, or `fallback` when nothing was
            remembered or the remembered directory no longer exists. */
        juce::File getDirectory (Kind kind, const juce::File& fallback) const;

        /** Remembers the parent directory of a chosen file (for file choosers). */
        void rememberFile (Kind kind, const juce::File& chosenFile);

        /** Remembers an explicitly chosen directory (for folder choosers). */
        void rememberDirectory (Kind kind, const juce::File& chosenDirectory);

    private:
        Storage& storage;

        JUCE_DECLARE_NON_COPYABLE (LastDirectoryStore)
    };
}
