// raw-radio-studio — left collapsible sidebar (Epic 8 phase 1a).
//
// Hosts the device panel (moved here from its own full-width top region) plus a
// «Channels» summary and a «Status» read-out as three accordion sections
// (Device / Channels / Status). The column width is resizable (drag its right
// edge) and its collapsed/width/section state is remembered by the host.
//
// Drawn from scratch in paint() with the RAW Radio brand tokens (no new
// dependencies or assets). UI-only; never touches the audio thread.

#pragma once

#include <JuceHeader.h>

#include <functional>

#include "ui/DevicePanel.h"
#include "ui/SidebarLayout.h"

namespace rrs
{
    class SidebarPanel final : public juce::Component
    {
    public:
        explicit SidebarPanel (DevicePanel& deviceToHost);

        void paint (juce::Graphics&) override;
        /** Draws the resize line and the collapse/expand handle ON TOP of the
            children (accordion content such as the DevicePanel) and of the
            headers drawn in paint(), so the line can never look broken. */
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        juce::MouseCursor getMouseCursor() override;

        /** Width the host must give this component: the rail when collapsed,
            otherwise the expanded width clamped to [minWidth, effective max]. */
        int getTotalWidth() const noexcept
        {
            return collapsed ? sidebar::railWidth
                             : sidebar::clampWidth (width, maxWidthForWindow);
        }

        int getWidthValue() const noexcept { return width; }
        bool isCollapsed() const noexcept { return collapsed; }
        sidebar::Sections getSections() const noexcept { return sections; }

        /** Apply persisted state (does not fire onStateChanged). */
        void setWidthValue (int w);
        void setCollapsed (bool shouldBeCollapsed);
        void setSections (sidebar::Sections s);

        /** Effective maximum expanded width for the current window (see
            sidebar::effectiveMaxWidth). Set by the host in resized() BEFORE it
            reads getTotalWidth(). */
        void setMaxWidthForWindow (int w);

        void toggleCollapsed();

        /** Fired on every live layout change (a resize drag frame) so the host can
            re-layout and repaint WITHOUT persisting. */
        std::function<void()> onLayoutChanged;

        /** Fired once a gesture completes (resize mouse-up) or a state toggle
            happens (section open/close, collapse/expand, rail click), so the host
            can re-layout AND persist. Never fired per drag frame. */
        std::function<void()> onStateChanged;

        /** One-line summary under the «Channels» header (track count etc.). */
        std::function<juce::String()> channelsSummaryProvider;
        /** Multi-line body of the «Status» section. */
        std::function<juce::String()> statusTextProvider;

    private:
        enum Section { Device = 0, Channels = 1, Status = 2, numSections = 3 };

        bool sectionOpen (int section) const;
        void setSectionOpen (int section, bool open);
        juce::String sectionTitle (int section) const;
        juce::String sectionIcon (int section) const;
        int sectionContentHeight (int section) const;
        juce::Rectangle<int> headerRect (int section) const;
        juce::Rectangle<int> contentRect (int section) const;
        juce::Rectangle<int> railIconRect (int section) const;
        /** The collapse/expand toggle, at the TOP of the column in both states
            (never under the status/action rows). */
        juce::Rectangle<int> handleRect() const;
        bool isOnResizeZone (juce::Point<int> p) const;

        DevicePanel& device;

        int width = sidebar::defaultWidth;
        int maxWidthForWindow = sidebar::maxWidth;
        bool collapsed = false;
        sidebar::Sections sections;

        int hoveredSection = -1;
        int hoveredRailIcon = -1;
        bool hoveringHandle = false;
        bool hoveringResize = false;

        bool resizing = false;
        int dragStartWidth = sidebar::defaultWidth;
        int dragStartScreenX = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SidebarPanel)
    };
}
