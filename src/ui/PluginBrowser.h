// raw-radio-studio — plugin browser (Epic 3, FR-MIX-4/6/7).
//
// A themed panel (shown as an in-window overlay by MainComponent) that lets the
// engineer scan for plugins, search the known-plugin list, insert a hosted
// plugin onto a track, and open/remove the plugins already on it. All actions
// are message-thread edits of the Edit; scanning runs out of process on a
// worker thread (PluginHost).
//
// UI-only; never touches the audio thread.

#pragma once

#include <JuceHeader.h>

#include <functional>

#include "studio/PluginHost.h"
#include "studio/Session.h"
#include "ui/BrandButton.h"

namespace rrs
{
    class PluginBrowser final : public juce::Component,
                                private juce::ListBoxModel,
                                private juce::Timer
    {
    public:
        PluginBrowser (PluginHost&, Session&);
        ~PluginBrowser() override;

        /** Called when the panel's Close button is pressed. */
        std::function<void()> onClose;

        /** Re-reads the session (tracks/plugins) — call when the panel is shown. */
        void refresh();

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        //==============================================================================
        // Known-plugin list model.
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool rowIsSelected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

        void timerCallback() override;

        void refreshTrackList();
        void refreshKnownList();
        void refreshTrackPlugins();
        void updateStatus();

        void insertSelected();
        void openSelectedTrackPlugin();
        void removeSelectedTrackPlugin();

        int selectedTrackIndex() const;

        PluginHost& host;
        Session& session;

        juce::ComboBox trackBox;
        juce::TextEditor search;
        juce::ListBox knownList { "known", this };
        juce::ComboBox trackPluginBox;
        juce::Label statusLabel;

        BrandButton scanButton { "Scan", BrandButton::Style::Primary };
        BrandButton refreshButton { "Refresh" };
        BrandButton insertButton { "Insert" };
        BrandButton openButton { "Open" };
        BrandButton removeButton { "Remove" };
        BrandButton closeButton { "Close" };

        juce::Array<juce::PluginDescription> known;
        bool scanning = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginBrowser)
    };
}
