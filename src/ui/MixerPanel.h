// raw-radio-studio — multitrack mixer UI (Epic 2, FR-MIX-1/3).
//
// A compact, brand-styled channel-strip mixer: per-track fader + pan + mute +
// solo and a peak/clip meter, plus a master fader/meter. Drawn from scratch
// (like the arrangement lanes) so it stays on the RAW Radio design tokens and
// does not depend on LookAndFeel slider internals.
//
// UI-only: it reads meter values and mixer state from the Session and writes
// changes back. Never touches the audio thread.

#pragma once

#include <JuceHeader.h>

#include <vector>

#include "studio/Session.h"

namespace rrs
{
    class MixerPanel final : public juce::Component,
                             private juce::ChangeListener,
                             private juce::Timer
    {
    public:
        /** Fixed panel height so MainComponent can reserve the space. */
        static constexpr int preferredHeight = 196;

        explicit MixerPanel (Session&);
        ~MixerPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;

        struct StripControls
        {
            juce::Rectangle<int> strip;
            juce::Rectangle<int> name;
            juce::Rectangle<int> meter;
            juce::Rectangle<int> fader;
            juce::Rectangle<int> pan;
            juce::Rectangle<int> mute;
            juce::Rectangle<int> solo;
            bool isMaster = false;
            int trackIndex = -1;
        };

        struct MeterVisual
        {
            float level = 0.0f;      ///< 0..1 normalised (from peak dBFS).
            float hold = 0.0f;
            int holdCountdown = 0;
            bool clipped = false;
            int clipHold = 0;
        };

        void rebuildStrips();
        void updateMeters();
        void drawStrip (juce::Graphics&, const StripControls&, const MeterVisual&, float gainDb,
                        float pan, bool muted, bool soloed);
        void drawMeter (juce::Graphics&, juce::Rectangle<int>, const MeterVisual&);

        static float normaliseDb (float db) noexcept;

        Session& session;
        std::vector<StripControls> strips;
        std::vector<MeterVisual> meters;
        int stripCount = -1;

        enum class DragTarget { None, Fader, Pan };
        DragTarget dragTarget = DragTarget::None;
        int dragIndex = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPanel)
    };
}
