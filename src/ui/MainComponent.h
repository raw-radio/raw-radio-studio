// raw-radio-studio — main window content (Epic 1).
//
// Minimal tracking-first UI per STUDIO_SPEC §6.4:
//   * audio-device / input panel,
//   * transport bar (arm, record, play, stop),
//   * input level meter (peak + RMS),
//   * single stereo track lane,
//   * session (new/open/save/save-as), WAV export, settings, About.

#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>

#include "studio/AudioEngine.h"
#include "studio/LastDirectoryStore.h"
#include "studio/PluginHost.h"
#include "studio/Session.h"
#include "studio/StemsExport.h"
#include "studio/WavExport.h"
#include "ui/BrandButton.h"
#include "ui/DevicePanel.h"
#include "ui/InputMeter.h"
#include "ui/MeterBallistics.h"
#include "ui/MixerPanel.h"
#include "ui/PianoRollPanel.h"
#include "ui/PluginBrowser.h"
#include "ui/RoutingPanel.h"
#include "ui/SidebarPanel.h"
#include "ui/Timeline.h"
#include "ui/TrackLaneLayout.h"

#include <vector>

namespace rrs
{
    class MainComponent final : public juce::Component,
                                private juce::ChangeListener,
                                private juce::ScrollBar::Listener,
                                private juce::Timer
    {
    public:
        MainComponent();
        ~MainComponent() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool keyPressed (const juce::KeyPress&) override;
        bool keyStateChanged (bool isKeyDown) override;

        /** Hover cursor feedback over the arrangement: resize cursors on a clip's
            trim edges and corner-resize cursors on its fade handles, so the
            editable zones are discoverable before the click. */
        juce::MouseCursor getMouseCursor() override;

        /** Quit request from the window close button or Cmd+Q. Runs the same
            unsaved-changes guard as New/Open/Close: nothing to lose quits
            immediately, otherwise a Save/Discard/Cancel prompt is shown and the
            app quits only on Save (after a successful write) or Discard. Re-entrant
            calls while the prompt is open are ignored (no double prompts). */
        void requestQuit();

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
        void timerCallback() override;

        void buildTransportUi();
        void refreshTransportUi();
        void showStatus (const juce::String&, bool isError = false);

        void newSession();
        void openSession();
        void closeSession();
        void saveSession();
        void saveSessionAs();
        void importAudioFile();
        void exportSession();
        void exportRegion();
        void exportStems();
        void showAbout();
        void showSettings();

        // --- Epic 5 (MIDI) ---
        /** Opens the piano roll on the selected MIDI clip, or the first MIDI clip
            of the selected/first MIDI track; creates a clip (and a MIDI track if
            needed) when there is none. */
        void openPianoRoll();
        /** Re-populates the MIDI input device combo from the Session. */
        void refreshMidiInputs();
        /** Track the Arm button operates on: the selected track when it is an
            input or MIDI track, otherwise the first input track (Epic 1
            single-track flow), then the first MIDI track, finally track 0. This
            keeps Arm from erroring when a playback-only backing track is
            selected. */
        int armTargetTrack() const;

        /** Starts an offline WAV render of `range` to `file` and manages the
            busy/metronome state shared by full-session and region export
            (FR-EXP-1/2). Caller must have checked the edit exists and is not
            already rendering. */
        void beginWavExport (const juce::File& file, WavExport::RenderRange range,
                             const juce::String& statusText);

        /** Shows `panel` centred (hiding the other overlay first) or hides it
            when already visible. */
        void toggleOverlay (juce::Component& panel);

        /** Shared unsaved-changes guard for New/Open/Close/Quit. Runs `onProceed`
            immediately when there is nothing to lose, otherwise shows a
            Save/Discard/Cancel prompt. A failed Save keeps the session (and does
            not run `onProceed`); Discard and a successful Save proceed; Cancel
            does nothing. `onCancel` (optional) runs whenever the operation is
            aborted — the Cancel button *or* a failed Save — so a caller such as
            the quit path can release a re-entrancy guard. */
        void confirmUnsavedChanges (const juce::String& title, std::function<void()> onProceed,
                                    std::function<void()> onCancel = {});

        void loadSettings();
        void saveSettings();

        /** Reads the persisted arrangement-UX UI state from the settings file and
            applies it to the sidebar, the mixer and the lane-height preset
            (Epic 8 phase 1a). */
        void applyLoadedUiState();
        /** Writes the current sidebar/mixer/preset UI state to the settings file. */
        void persistUiState();

        /** Clamps/hides the lane scrollbar to the current content and viewport. */
        void updateLaneScrollBar();
        /** Sets the global lane-height preset (0 compact / 1 normal / 2 large),
            resets the scroll, re-lays the lanes and persists the choice. */
        void setTrackHeightPreset (int preset);
        /** Opens the compact lane-height dropdown from the edit row (owner R-B). */
        void showHeightPresetMenu();
        /** Summary line for the sidebar «Channels» section. */
        juce::String channelsSummaryText() const;
        /** Multi-line body for the sidebar «Status» section. */
        juce::String statusText() const;

        // --- Epic 8 phase 1b (in-lane controls) ---
        /** The metering pump: OWNER of the engine's read-and-clear level meters.
            Runs every ~30 Hz tick (independent of the mixer's visibility /
            collapsed state) and publishes per-track + master readings for the
            mixer strips and the lane mini-meters (which peek them). */
        void pumpMeters (double dtSeconds);
        /** Draws one lane's control band (arm / mute / solo / input / meter). */
        void drawLaneControls (juce::Graphics&, juce::Rectangle<int> lane, bool isInput,
                               bool armEnabled, bool armed, bool recording, bool muted,
                               bool soloed, int meterIndex);
        void drawMiniMeter (juce::Graphics&, juce::Rectangle<int>, int meterIndex);
        /** The control under `p` for lane `trackIndex` (none when `p` is in the
            clip zone / outside the gutter). */
        track_lane::LaneControl laneControlAt (int trackIndex, juce::Point<int> p) const;
        /** Opens the per-track input-assignment popup (same menu as MixerPanel). */
        void showLaneInputMenu (int trackIndex);
        /** Handles a gutter click (arm/mute/solo/input, or select-track on the
            empty band). */
        void handleLaneControl (int trackIndex, track_lane::LaneControl);
        /** Arms/disarms the track under the cursor (hotkey `A`). */
        void toggleHoveredTrackArm();

        void runStartupRecovery();
        void onSessionOpened();

        /** Lays out the action row for `area` (top-left anchor), wrapping to a
            second line when the window is too narrow. Records the group-divider
            rectangles for paint() and returns the total height consumed. */
        int layoutActionRow (juce::Rectangle<int> area);

        /** Recomputes the per-track lane rectangles inside `trackLaneArea`.
            One rect per engine audio track (up to the available height), in
            engine order, so lane clicks map straight onto track indices. */
        void rebuildTrackLaneRects();

        //==========================================================================
        // Arrangement clip editing (Epic 4 — FR-ED-1/2/3/4/5/6)
        //
        // The arrangement draws clips as rectangles inside the lanes and turns
        // mouse drags into edit operations. A drag only previews (repaint); the
        // edit is committed on mouse-up as a single undoable Session call.
        enum class ClipDragMode { none, move, trimStart, trimEnd, fadeIn, fadeOut };

        /** Snap pull distance, in pixels, converted to seconds at the current
            zoom so the feel is constant on screen. Snap is bypassed by holding
            Alt during a drag (see mouseDrag). */
        static constexpr int snapThresholdPx = 6;

        void selectClip (int trackIndex, int clipIndex);
        /** Selects a track via its lane header, clearing any clip selection, so
            Remove track can target a track that currently has no clips (owner
            bug fix). `trackIndex` out of range clears the selection. */
        void selectTrack (int trackIndex);
        Session::ClipInfo clipInfoFor (int trackIndex, int clipIndex) const;
        juce::Rectangle<int> clipRectFor (int trackIndex, const Session::ClipInfo&) const;
        /** Returns the topmost clip at `position` in `lane`, or -1. */
        int hitTestClip (int trackIndex, juce::Point<int> position) const;
        /** Which edit zone `position` falls in inside `info`'s clip rectangle
            (trim edges, fade corners or the body). Shared by mouseDown and the
            hover cursor so a click and the cursor can never disagree. */
        ClipDragMode zoneForPoint (int trackIndex, const Session::ClipInfo&,
                                   juce::Point<int> position) const;
        /** Mouse cursor for a drag zone (normal for the move/none zones). */
        static juce::MouseCursor cursorForMode (ClipDragMode) noexcept;
        bool selectedClipExists() const;
        Session::ClipInfo selectedClipInfo() const;
        void commitClipDrag();
        void refreshEditButtons();

        /** Edge times a dragged clip can snap to: every other clip's start/end
            (across all tracks), the playhead and the whole-second grid.
            Excludes the clip being dragged so it cannot snap to itself. */
        std::vector<double> snapEdgesFor (int excludeTrack, int excludeClip) const;
        /** Snap pull distance in seconds for the current arrangement width. */
        double snapThresholdSeconds() const;

        void splitSelectedAtPlayhead();
        void deleteSelectedClip();
        void duplicateSelectedClip();
        void loopSelectedClip();
        void crossfadeSelectedClip();
        void stretchSelectedClip();
        void undoEdit();
        void redoEdit();

        static juce::String formatTime (double seconds);

        //==============================================================================
        AudioEngine audio;
        Session session { audio };
        DevicePanel devicePanel { audio };
        InputMeter inputMeter { session.getInputLevels(),
                                [this] { return audio.getNumActiveInputChannels(); } };
        MixerPanel mixerPanel { session };

        /** Epic 8 phase 1a: the device panel moves from a full-width top region
            into a left collapsible sidebar with Device / Channels / Status
            accordion sections. Owns (and lays out) the DevicePanel. */
        SidebarPanel sidebarPanel { devicePanel };

        /** Epic 3: plugin hosting + routing/cue-mix overlays. */
        PluginHost pluginHost { audio.engine() };
        PluginBrowser pluginBrowser { pluginHost, session };
        RoutingPanel routingPanel { session };

        /** Epic 5: piano-roll MIDI editor overlay. */
        PianoRollPanel pianoRoll { session };

        /** Clickable ruler + playhead above the arrangement lanes (owner request:
            position anywhere in the track). */
        Timeline timeline { session };

        BrandButton newButton { "New" }, openButton { "Open" };
        BrandButton closeButton { "Close" };
        BrandButton saveButton { "Save" }, saveAsButton { "Save As" };
        BrandButton importButton { "Import" };
        BrandButton normaliseButton { "Normalize" };
        BrandButton exportButton { "Export WAV", BrandButton::Style::Primary };
        BrandButton exportRegionButton { "Export region" };
        BrandButton clearRegionButton { "Clear region" };
        BrandButton stemsButton { "Export stems" };
        BrandButton pluginsButton { "Plugins" };
        BrandButton routingButton { "Routing" };
        BrandButton addTrackButton { "Add track" }, removeTrackButton { "Remove track" };
        BrandButton midiTrackButton { "MIDI track" };
        BrandButton pianoRollButton { "Piano roll" };
        BrandButton settingsButton { "Settings" }, aboutButton { "About" };

        /** Epic 4 arrangement edit actions (second action row). */
        BrandButton undoButton { "Undo" }, redoButton { "Redo" };
        BrandButton splitClipButton { "Split" }, deleteClipButton { "Delete" };
        BrandButton duplicateClipButton { "Duplicate" }, loopClipButton { "Loop" };
        BrandButton crossfadeButton { "Crossfade" }, stretchClipButton { "Stretch" };

        /** Epic 8 phase 1a (owner R-B): compact lane-height switcher at the right
            edge of the edit row — an icon-only button opening a 56/72/96 dropdown. */
        BrandButton heightPresetButton { "Track height", BrandButton::Style::Secondary };

        BrandButton armButton { "Arm", BrandButton::Style::Chip };
        BrandButton recordButton { "Record", BrandButton::Style::Record };
        BrandButton playButton { "Play", BrandButton::Style::Transport };
        BrandButton stopButton { "Stop", BrandButton::Style::Transport };
        BrandButton goToStartButton { "Start", BrandButton::Style::Transport };
        BrandButton monitorButton { "Monitor", BrandButton::Style::Chip };
        BrandButton metronomeButton { "Click", BrandButton::Style::Chip };
        juce::Label countInLabel;
        juce::ComboBox countInBox;

        /** Epic 5: MIDI input device selection routed to the armed MIDI track. */
        juce::Label midiInputLabel;
        juce::ComboBox midiInputBox;

        juce::Label titleLabel, transportLabel, statusLabel;

        /** Shows the current region-selection start/end (FR-EXP-2). */
        juce::Label selectionLabel;
        juce::TooltipWindow tooltipWindow { this, 700 };

        std::vector<juce::Rectangle<int>> actionDividers;

        std::unique_ptr<juce::PropertiesFile> settings;

        /** Adapter exposing the app settings file to the last-directory store. */
        struct SettingsStorage final : LastDirectoryStore::Storage
        {
            explicit SettingsStorage (juce::PropertiesFile& fileToUse) : file (fileToUse) {}

            juce::String getValue (const juce::String& key) const override { return file.getValue (key); }
            void setValue (const juce::String& key, const juce::String& value) override
            {
                file.setValue (key, value);
            }
            void save() override { file.saveIfNeeded(); }

            juce::PropertiesFile& file;
        };

        /** Remembers the last directory the user chose for each file-dialog
            operation kind (see studio/LastDirectoryStore.h). Created alongside
            `settings` in loadSettings(). */
        std::unique_ptr<SettingsStorage> settingsStorage;
        std::unique_ptr<LastDirectoryStore> lastDirectories;

        std::shared_ptr<WavExport::Handle> exportHandle;
        bool exportInProgress = false;
        std::shared_ptr<StemsExport::Handle> stemsHandle;
        bool stemsInProgress = false;

        /** True while the quit confirmation dialog is open, so a second Cmd+Q /
            window-close event cannot stack a second prompt. */
        bool quitConfirmPending = false;
        /** Metronome state saved when an export starts, restored when it ends so
            the click can never leak into the rendered WAV (FR-EXP-1). */
        bool exportMetronomeWasEnabled = false;

        /** Live transport/monitoring state saved before an offline render and
            restored when it ends. The offline renderer frees the playback context
            and never rebuilds it, which silences monitoring/playback until restart
            (see Session::captureTransportForOfflineRender). Separate snapshots so a
            WAV export and a stems batch can never clobber each other's state. */
        Session::OfflineRenderTransportState wavExportTransportState;
        Session::OfflineRenderTransportState stemsExportTransportState;

        juce::String statusMessage;
        bool statusIsError = false;

        /** Counts 30 Hz timer ticks so the heavier transport/status chrome is
            refreshed ~10 Hz while the playhead is repainted every tick. */
        int uiRefreshTicks = 0;

        juce::Rectangle<int> trackLaneArea;

        /** Epic 8 phase 1b: the x<->time map for clips / playhead / region /
            ruler starts at `trackLaneArea.getX() + gutterWidth` — the gutter
            (control column) occupies the left of every lane. */
        juce::Rectangle<int> trackTimelineArea;

        /** Hover state for the drawn lane controls (JUCE sends no mouseEnter for
            sub-rectangles, so MainComponent tracks them in mouseMove). */
        int hoverTrackIndex = -1;
        track_lane::LaneControl hoverControl = track_lane::LaneControl::none;

        /** Per-lane mini-meter ballistics + clip-hold counters (message thread). */
        std::vector<MeterBallistics> laneMeters;
        std::vector<int> laneMeterClipHold;
        double lastLaneMeterMs = 0.0;

        /** Cached `Session::getInputTrackIndices()` (refreshed in
            refreshTransportUi) so hover/lane hit-tests don't allocate an Array on
            every mouse move. */
        juce::Array<int> inputTrackIndexCache;

        /** Key codes currently held, so an auto-repeat cannot flip a transport /
            arm hotkey repeatedly. Pruned in keyStateChanged(). */
        juce::Array<int> heldHotkeyCodes;

        /** Lane rectangles in `trackLaneArea`, engine-track order (FR-REC-2:
            click a lane to arm/disarm it). Computed with `laneScrollOffsetY` and
            `trackHeightPreset` so paint() and every hit-test share one mapping. */
        std::vector<juce::Rectangle<int>> trackLaneRects;

        /** Epic 8 phase 1a — vertical lane scrolling. `scrollOffsetY` shifts the
            lane stack up (see ui/TrackLaneLayout.h); a single ScrollBar on the
            right edge of the arrangement drives it. */
        std::unique_ptr<juce::ScrollBar> laneScrollBar;
        int laneScrollOffsetY = 0;
        /** Re-entrancy guard while updateLaneScrollBar() moves the scrollbar. */
        bool updatingLaneScrollBar = false;

        /** Global lane-height preset: 0 compact (56) / 1 normal (72) / 2 large (96). */
        int trackHeightPreset = 1;

        //--- Epic 4 clip-edit selection + drag state ---
        int selectedTrackIndex = -1;
        int selectedClipIndex = -1;

        ClipDragMode clipDragMode = ClipDragMode::none;
        int dragTrackIndex = -1;
        int dragClipIndex = -1;
        double dragMouseDownSeconds = 0.0;
        double dragOriginalStart = 0.0, dragOriginalEnd = 0.0;
        double dragOriginalFadeIn = 0.0, dragOriginalFadeOut = 0.0;
        double dragPreviewStart = 0.0, dragPreviewEnd = 0.0;
        double dragPreviewFadeIn = 0.0, dragPreviewFadeOut = 0.0;
        bool clipDragActive = false;

        /** Live snap state for the current drag: where the dragged edge will
            land (seconds) and whether the indicator should be drawn. */
        bool snapActive = false;
        double snapTime = 0.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
