// raw-radio-studio — multitrack mixer UI (Epic 2, FR-MIX-1/3).
//
// A compact, brand-styled channel-strip mixer: per-track record-arm + fader +
// pan + mute + solo and a peak/clip meter, plus a master fader/pan/mute/meter.
// Drawn from scratch (like the arrangement lanes) so it stays on the RAW Radio
// design tokens and does not depend on LookAndFeel slider internals.
//
// UI-only: it reads meter values and mixer state from the Session and writes
// changes back. Never touches the audio thread.

#pragma once

#include <JuceHeader.h>

#include <vector>

#include "MeterBallistics.h"
#include "studio/Session.h"

namespace rrs
{
    class MixerPanel final : public juce::Component,
                             private juce::ChangeListener,
                             private juce::Timer
    {
    public:
        /** Fixed panel height so MainComponent can reserve the space. */
        static constexpr int preferredHeight = 220;

        /** Height of the always-visible mixer header. Collapsed manually, the
            mixer keeps this 22 px strip so it is never more than one click away
            (Epic 8 phase 1a — distinct from the auto-hide-by-height fallback). */
        static constexpr int headerHeight = 22;

        explicit MixerPanel (Session&);
        ~MixerPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        /** True while the mixer is manually collapsed to its header strip. */
        bool isCollapsed() const noexcept { return collapsed; }

        /** Manual collapse/expand; repaints and notifies onCollapsedChanged so the
            host can re-layout (22 px header vs. the full 220 px panel) and persist
            the state. */
        void setCollapsed (bool shouldBeCollapsed);

        /** Fired when the mixer is collapsed/expanded from the UI (header click). */
        std::function<void()> onCollapsedChanged;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;

        struct StripControls
        {
            juce::Rectangle<int> strip;
            juce::Rectangle<int> name;
            juce::Rectangle<int> arm;   ///< Track: record-arm chip (FR-REC-2). Unused on master.
            juce::Rectangle<int> input; ///< Track: input-mapping chip (FR-REC-3). Unused on master.
            juce::Rectangle<int> trim;  ///< Track: record trim (FR-REC-4). Input tracks only.
            juce::Rectangle<int> meter;
            juce::Rectangle<int> level; ///< Dedicated gain readout row ABOVE the fader (BUG A).
            juce::Rectangle<int> fader;
            juce::Rectangle<int> pan;
            juce::Rectangle<int> mute;
            juce::Rectangle<int> solo;
            bool isMaster = false;
            bool isInput = false; ///< Track: record/input track (arm + input chips shown).
            int trackIndex = -1;
        };

        struct MeterVisual
        {
            MeterBallistics ballistics; ///< smoothed dBFS level + peak hold.
            bool clipped = false;
            int clipHold = 0;
        };

        void rebuildStrips();
        void updateMeters();
        void drawStrip (juce::Graphics&, const StripControls&, const MeterVisual&, float gainDb,
                        float pan, bool muted, bool soloed, bool armed);
        void drawMeter (juce::Graphics&, juce::Rectangle<int>, const MeterVisual&);

        /** The full-width header strip at the top of the panel (always the same
            rectangle whether collapsed or expanded; clicking it toggles). */
        juce::Rectangle<int> headerBounds() const;

        /** Opens the per-track input-assignment popup (channel + layout, FR-REC-3). */
        void showInputMenu (int trackIndex);

        /** dBFS -> 0..1 for the level bars only. Faders use the documented
            taper in `FaderTaper.h` (linear-in-dB for gain, an S-curve for the
            record trim), not this meter scale. */
        static float meterNormalised (float db) noexcept;

        Session& session;
        std::vector<StripControls> strips;
        std::vector<MeterVisual> meters;
        int stripCount = -1;

        enum class DragTarget { None, Fader, Pan, Trim };
        DragTarget dragTarget = DragTarget::None;
        int dragIndex = -1;

        /** True when a trim drag applied a live gain change that has not yet been
            persisted. The session file is written once on mouse-up instead of on
            every mouse-move. */
        bool trimDragDirty = false;

        /** Timestamp of the previous meter update, for dt-based ballistics. */
        double lastMeterMs = 0.0;

        /** Manually collapsed to the header strip (Epic 8 phase 1a). */
        bool collapsed = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPanel)
    };
}
