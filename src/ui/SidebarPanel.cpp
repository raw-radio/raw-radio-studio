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

        /** Chevron for the collapse/expand handle. `pointRight` = expand (drawn
            while collapsed); otherwise point left = collapse (while expanded). */
        void drawHandleChevron (juce::Graphics& g, juce::Rectangle<int> area, bool pointRight,
                                juce::Colour colour)
        {
            auto r = area.toFloat().reduced (area.getWidth() * 0.30f, area.getHeight() * 0.22f);
            juce::Path p;

            if (pointRight)
            {
                p.startNewSubPath (r.getX(), r.getY());
                p.lineTo (r.getRight(), r.getCentreY());
                p.lineTo (r.getX(), r.getBottom());
            }
            else
            {
                p.startNewSubPath (r.getRight(), r.getY());
                p.lineTo (r.getX(), r.getCentreY());
                p.lineTo (r.getRight(), r.getBottom());
            }

            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
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
        // Start below the top collapse/expand handle (which is at the top of the
        // rail), plus the separating hairline.
        int y = sidebar::railIconPadding * 2 + sidebar::railIconSize;

        for (int s = 0; s < numSections; ++s)
        {
            if (s == section)
                return { x, y, sidebar::railIconSize, sidebar::railIconSize };

            y += sidebar::railIconSize + sidebar::railIconPadding;
        }

        return {};
    }

    juce::Rectangle<int> SidebarPanel::handleRect() const
    {
        return collapsed ? sidebar::handleRectRail()
                         : sidebar::handleRectExpanded (getWidth());
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

        if (collapsed)
        {
            // Rail: one icon per section. The collapse/expand handle and the
            // resize line are drawn in paintOverChildren() so they sit on top.
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

    void SidebarPanel::paintOverChildren (juce::Graphics& g)
    {
        // The right-edge resize line is drawn ON TOP of everything (headers are
        // painted in paint(); the DevicePanel is a child). Drawn in paint() it was
        // overpainted by the full-width accordion headers, so it looked broken.
        const bool highlight = resizing || hoveringResize;

        g.setColour (highlight ? brand::borderFocus : brand::border);
        g.fillRect (getWidth() - 1, 0, 1, getHeight());

        // The collapse/expand handle, always visible and on top, at the TOP of
        // the column (never under the status/action rows).
        const auto h = handleRect();

        if (collapsed)
        {
            // Hairline separating the handle from the section icons.
            g.setColour (brand::border);
            g.fillRect (6, h.getBottom() + 4, getWidth() - 12, 1);
        }

        g.setColour (brand::bgWindow);
        g.fillRoundedRectangle (h.toFloat(), 5.0f);
        g.setColour (hoveringHandle ? brand::borderFocus : brand::border);
        g.drawRoundedRectangle (h.toFloat().reduced (0.5f), 5.0f, hoveringHandle ? 1.5f : 1.0f);
        drawHandleChevron (g, h, collapsed, hoveringHandle ? brand::accent : brand::textPrimary);
    }

    //==============================================================================
    void SidebarPanel::mouseDown (const juce::MouseEvent& e)
    {
        const auto p = e.getPosition();

        // Collapse/expand handle first: it sits at the top of the column in both
        // states and must win over the header beneath it (expanded) and the rail
        // icons (collapsed).
        if (handleRect().contains (p))
        {
            toggleCollapsed();
            return;
        }

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

        const bool overHandle = handleRect().contains (p);

        int newSection = -1;
        int newRail = -1;

        if (collapsed)
        {
            for (int s = 0; s < numSections; ++s)
                if (railIconRect (s).contains (p))
                    newRail = s;
        }
        else if (! overHandle)
        {
            for (int s = 0; s < numSections; ++s)
                if (headerRect (s).contains (p))
                    newSection = s;
        }

        const bool newResize = isOnResizeZone (p);

        if (newSection != hoveredSection || newRail != hoveredRailIcon
            || overHandle != hoveringHandle || newResize != hoveringResize)
        {
            hoveredSection = newSection;
            hoveredRailIcon = newRail;
            hoveringHandle = overHandle;
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
        const auto p = getMouseXYRelative();

        // The resize zone wins (it is at the very edge, outside the handle).
        if (isOnResizeZone (p))
            return juce::MouseCursor::LeftRightResizeCursor;

        if (handleRect().contains (p))
            return juce::MouseCursor::PointingHandCursor;

        if (collapsed)
        {
            for (int s = 0; s < numSections; ++s)
                if (railIconRect (s).contains (p))
                    return juce::MouseCursor::PointingHandCursor;
        }

        return juce::MouseCursor::NormalCursor;
    }
}
