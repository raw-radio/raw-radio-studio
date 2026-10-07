// raw-radio-studio — routing + software cue mixes panel (Epic 3, FR-MIX-2 /
// FR-MON-3/4).
//
// A themed in-window overlay (shown by MainComponent) that exposes:
//   * per-track output assignment (which hardware output pair a track plays to),
//   * submix folders (track -> bus/group -> master),
//   * software cue mixes: add/remove a cue, assign its output pair, and set the
//     selected track's send level/enable into the selected cue.
//
// All actions are message-thread edits of the Edit. UI-only; never touches the
// audio thread.

#pragma once

#include <JuceHeader.h>

#include <functional>

#include "studio/Session.h"
#include "ui/BrandButton.h"

namespace rrs
{
    class RoutingPanel final : public juce::Component,
                               private juce::Timer
    {
    public:
        explicit RoutingPanel (Session&);
        ~RoutingPanel() override;

        std::function<void()> onClose;

        /** Re-reads the session (tracks/submixes/cues) — call when shown. */
        void refresh();

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override;

        void rebuildTrackList();
        void rebuildOutputList (juce::ComboBox&, const juce::String& currentID);
        void rebuildSubmixList();
        void rebuildCueList();
        void refreshControlsFromSelection();
        void updateStatus();

        int selectedTrackIndex() const;
        int selectedCueIndex() const;

        void applyTrackOutput();
        void applyCueOutput();
        void applySend();

        Session& session;
        bool updating = false;

        juce::ComboBox trackBox;
        juce::ComboBox outputBox;

        juce::ComboBox submixBox;
        BrandButton addSubmixButton { "New bus" };
        BrandButton assignSubmixButton { "Assign" };
        BrandButton unassignSubmixButton { "Unassign" };

        juce::ComboBox cueBox;
        BrandButton addCueButton { "Add cue", BrandButton::Style::Primary };
        BrandButton removeCueButton { "Remove cue" };
        juce::ComboBox cueOutputBox;

        BrandButton sendToggle { "Send", BrandButton::Style::Chip };
        juce::Slider sendSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::Label sendLabel;

        juce::Label statusLabel;
        BrandButton closeButton { "Close" };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoutingPanel)
    };
}
