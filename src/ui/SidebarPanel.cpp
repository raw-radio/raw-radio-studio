// raw-radio-studio — left collapsible sidebar (see SidebarPanel.h).

#include "SidebarPanel.h"

#include "ui/BrandColours.h"
#include "ui/BrandFonts.h"
#include "ui/IconCache.h"

namespace rrs
{
    namespace
    {
        /** Small header chevron: down when the section is open, right when closed. */
        void drawSectionChevron (juce::Graphics& g, juce::Rectangle<int> area, bool open,
                                 juce::Colour colour)
        {
            auto r = area.toFloat().reduced (4.0f, 3.0f);
            juce::Path p;

            if (open) // v
            {
                p.startNewSubPath (r.getX(), r.getY());
                p.lineTo (r.getCentreX(), r.getBottom());
                p.lineTo (r.getRight(), r.getY());
            }
            else // >
            {
                p.startNewSubPath (r.getX(), r.getY());
                p.lineTo (r.getRight(), r.getCentreY());
                p.lineTo (r.getX(), r.getBottom());
            }

            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        }

        /** Right-pointing chevron for the rail expand handle. */
        void drawRailChevron (juce::Graphics& g, juce::Rectangle<int> area, juce::Colour colour)
        {
            auto r = area.toFloat().reduced (7.0f, 5.0f);
            juce::Path p;
            p.startNewSubPath (r.getX(), r.getY());
            p.lineTo (r.getRight(), r.getCentreY());
            p.lineTo (r.getX(), r.getBottom());
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        }
    }

    SidebarPanel::SidebarPanel (DevicePanel& deviceToHost)
        : device (deviceToHost)
    {
        addAndMakeVisible (device);
    }

    //==============================================================================
    bool SidebarPanel::sectionOpen (int section) const
    {
        switch (section)
        {
            case Device:   return sections.device;
            case Channels: return sections.channels;
            case Status:   return sections.status;
            default:       return false;
        }
    }

    void SidebarPanel::setSectionOpen (int section, bool open)
    {
        switch (section)
        {
            case Device:   sections.device   = open; break;
            case Channels: sections.channels = open; break;
            case Status:   sections.status   = open; break;
            default: break;
        }
    }

    juce::String SidebarPanel::sectionTitle (int section) const
    {
        switch (section)
        {
            case Device:   return "Device";
            case Channels: return "Channels";
            case Status:   return "Status";
            default:       return {};
        }
    }

    juce::String SidebarPanel::sectionIcon (int section) const
    {
        switch (section)
        {
            case Device:   return "gear-six";
            case Channels: return "arrows-horizontal";
            case Status:   return "info";
            default:       return {};
        }
    }

    int SidebarPanel::sectionContentHeight (int section) const
    {
        switch (section)
        {
            case Device:   return device.getPreferredHeight();
            case Channels: return sidebar::channelsContentHeight;
            case Status:   return sidebar::statusContentHeight;
            default:       return 0;
        }
    }

    //==============================================================================
    juce::Rectangle<int> SidebarPanel::headerRect (int want) const
    {
        if (collapsed)
            return {};

        auto area = getLocalBounds();
        area.removeFromTop (sidebar::padding);

        for (int s = 0; s < numSections; ++s)
        {
            auto header = area.removeFromTop (sidebar::headerHeight);

            if (s == want)
                return header;

            if (sectionOpen (s))
                area.removeFromTop (sectionContentHeight (s));

            area.removeFromTop (sidebar::sectionGap);
        }

        return {};
    }

    juce::Rectangle<int> SidebarPanel::contentRect (int want) const
    {
        if (collapsed)
            return {};

        auto area = getLocalBounds();
        area.removeFromTop (sidebar::padding);

        for (int s = 0; s < numSections; ++s)
        {
            area.removeFromTop (sidebar::headerHeight);

            if (s == want)
                return sectionOpen (s)
                           ? area.removeFromTop (sectionContentHeight (s))
                                 .reduced (sidebar::padding, 0)
                           : juce::Rectangle<int>();

            if (sectionOpen (s))
                area.removeFromTop (sectionContentHeight (s));

            area.removeFromTop (sidebar::sectionGap);
        }

        return {};
    }

    juce::Rectangle<int> SidebarPanel::railIconRect (int section) const
    {
        const int x = (sidebar::railWidth - sidebar::railIconSize) / 2;
        int y = sidebar::railIconPadding;

        for (int s = 0; s < numSections; ++s)
        {
            if (s == section)
                return { x, y, sidebar::railIconSize, sidebar::railIconSize };

            y += sidebar::railIconSize + sidebar::railIconPadding;
        }

        return {};
    }

    juce::Rectangle<int> SidebarPanel::railHandleRect() const
    {
        const int x = (sidebar::railWidth - sidebar::railIconSize) / 2;
        return { x, juce::jmax (0, getHeight() - sidebar::railIconSize - sidebar::railIconPadding),
                 sidebar::railIconSize, sidebar::railIconSize };
    }

    bool SidebarPanel::isOnResizeZone (juce::Point<int> p) const
    {
        return ! collapsed
               && p.x >= getWidth() - sidebar::resizeZone
               && p.x < getWidth();
    }

    //==============================================================================
    void SidebarPanel::setWidthValue (int w)
    {
        width = sidebar::clampWidth (w, maxWidthForWindow);
    }

    void SidebarPanel::setMaxWidthForWindow (int w)
    {
        const int clamped = juce::jmax (sidebar::minWidth, w);

        if (clamped == maxWidthForWindow)
            return;

        maxWidthForWindow = clamped;
        resized();
        repaint();
    }

    void SidebarPanel::setCollapsed (bool shouldBeCollapsed)
    {
        collapsed = shouldBeCollapsed;
        resized();
        repaint();
    }

    void SidebarPanel::setSections (sidebar::Sections s)
    {
        sections = s;
        resized();
        repaint();
    }

    void SidebarPanel::toggleCollapsed()
    {
        collapsed = ! collapsed;
        resized();
        repaint();

        if (onStateChanged != nullptr)
            onStateChanged();
    }

    //==============================================================================
    void SidebarPanel::resized()
    {
        // The device panel is child #0 and is laid out inside its accordion
        // content; hidden when collapsed or when the Device section is closed.
        const bool deviceVisible = ! collapsed && sections.device;
        device.setVisible (deviceVisible);

        if (deviceVisible)
        {
            auto content = contentRect (Device);

            if (! content.isEmpty())
                device.setBounds (content);
        }
    }

    //==============================================================================
    void SidebarPanel::paint (juce::Graphics& g)
    {
        g.setColour (brand::bgPanel);
        g.fillRect (getLocalBounds());

        // Right-edge divider doubles as the resize affordance.
        const bool highlight = resizing || hoveringResize;

        if (! collapsed)
        {
            g.setColour (highlight ? brand::borderFocus : brand::border);
            g.fillRect (getWidth() - 1, 0, 1, getHeight());
        }

        if (collapsed)
        {
            // Rail: icon per section + a bottom expand handle.
            g.setColour (brand::border);
            g.fillRect (getWidth() - 1, 0, 1, getHeight());

            for (int s = 0; s < numSections; ++s)
            {
                auto box = railIconRect (s);

                if (box.isEmpty())
                    continue;

                const auto tint = sectionOpen (s) ? brand::accent
                                                  : (hoveredRailIcon == s ? brand::textPrimary
                                                                          : brand::textSecondary);
                IconCache::getInstance().drawIcon (g, sectionIcon (s), tint,
                                                   box.reduced (8));
            }

            const auto handle = railHandleRect();

            // Hairline above the handle.
            g.setColour (brand::border);
            g.fillRect (6, handle.getY() - 8, getWidth() - 12, 1);

            drawRailChevron (g, handle, hoveringHandle ? brand::accent : brand::textTertiary);
            return;
        }

        for (int s = 0; s < numSections; ++s)
        {
            auto header = headerRect (s);

            if (header.isEmpty())
                continue;

            const bool open = sectionOpen (s);
            const bool hover = hoveredSection == s;

            g.setColour (hover ? brand::bgElevated : brand::bgTertiary);
            g.fillRect (header);

            if (open)
            {
                g.setColour (brand::accent);
                g.fillRect (header.getX(), header.getY(), 2, header.getHeight());
            }

            auto chevron = header.removeFromLeft (26).reduced (6, 11);
            drawSectionChevron (g, chevron, open, hover ? brand::textPrimary : brand::textSecondary);

            g.setColour (brand::textPrimary);
            g.setFont (brand::uiSemiBold (12.0f));
            g.drawText (sectionTitle (s), header.reduced (6, 0), juce::Justification::centredLeft);

            if (! open)
                continue;

            auto content = contentRect (s);

            if (content.isEmpty())
                continue;

            g.setColour (brand::bgWindow);
            g.fillRect (content);

            const auto textArea = content.reduced (0, 6);

            if (s == Channels && channelsSummaryProvider != nullptr)
            {
                g.setColour (brand::textSecondary);
                g.setFont (brand::uiRegular (12.0f));
                g.drawFittedText (channelsSummaryProvider(), textArea, juce::Justification::topLeft, 3);
            }
            else if (s == Status && statusTextProvider != nullptr)
            {
                g.setColour (brand::textSecondary);
                g.setFont (brand::uiRegular (12.0f));
                g.drawFittedText (statusTextProvider(), textArea, juce::Justification::topLeft, 5);
            }
        }
    }

    //==============================================================================
    void SidebarPanel::mouseDown (const juce::MouseEvent& e)
    {
        const auto p = e.getPosition();

        if (collapsed)
        {
            for (int s = 0; s < numSections; ++s)
            {
                if (railIconRect (s).contains (p))
                {
                    // Expand and reveal the clicked section.
                    collapsed = false;
                    setSectionOpen (s, true);
                    resized();
                    repaint();

                    if (onStateChanged != nullptr)
                        onStateChanged();

                    return;
                }
            }

            if (railHandleRect().contains (p))
                toggleCollapsed();

            return;
        }

        if (isOnResizeZone (p))
        {
            resizing = true;
            dragStartWidth = width;
            dragStartScreenX = e.getScreenX();
            repaint();
            return;
        }

        for (int s = 0; s < numSections; ++s)
        {
            if (headerRect (s).contains (p))
            {
                setSectionOpen (s, ! sectionOpen (s));
                resized();
                repaint();

                if (onStateChanged != nullptr)
                    onStateChanged();

                return;
            }
        }
    }

    void SidebarPanel::mouseDrag (const juce::MouseEvent& e)
    {
        if (! resizing)
            return;

        width = sidebar::clampWidth (dragStartWidth + (e.getScreenX() - dragStartScreenX),
                                     maxWidthForWindow);

        // Live re-layout only — the host must NOT persist on every drag frame
        // (spec §1.7: write on mouse-up of the resize, and on each toggle). The
        // commit happens in mouseUp().
        if (onLayoutChanged != nullptr)
            onLayoutChanged();

        resized();
        repaint();
    }

    void SidebarPanel::mouseUp (const juce::MouseEvent&)
    {
        if (! resizing)
            return;

        resizing = false;
        repaint();

        // Commit: persist the final width once the gesture is over.
        if (onStateChanged != nullptr)
            onStateChanged();
    }

    void SidebarPanel::mouseMove (const juce::MouseEvent& e)
    {
        const auto p = e.getPosition();

        int newSection = -1;
        int newRail = -1;
        bool newHandle = false;

        if (collapsed)
        {
            for (int s = 0; s < numSections; ++s)
                if (railIconRect (s).contains (p))
                    newRail = s;

            newHandle = railHandleRect().contains (p);
        }
        else
        {
            for (int s = 0; s < numSections; ++s)
                if (headerRect (s).contains (p))
                    newSection = s;
        }

        const bool newResize = isOnResizeZone (p);

        if (newSection != hoveredSection || newRail != hoveredRailIcon
            || newHandle != hoveringHandle || newResize != hoveringResize)
        {
            hoveredSection = newSection;
            hoveredRailIcon = newRail;
            hoveringHandle = newHandle;
            hoveringResize = newResize;
            repaint();
        }
    }

    void SidebarPanel::mouseExit (const juce::MouseEvent&)
    {
        if (hoveredSection == -1 && hoveredRailIcon == -1 && ! hoveringHandle && ! hoveringResize)
            return;

        hoveredSection = -1;
        hoveredRailIcon = -1;
        hoveringHandle = false;
        hoveringResize = false;
        repaint();
    }

    juce::MouseCursor SidebarPanel::getMouseCursor()
    {
        if (isOnResizeZone (getMouseXYRelative()))
            return juce::MouseCursor::LeftRightResizeCursor;

        return juce::MouseCursor::NormalCursor;
    }
}
