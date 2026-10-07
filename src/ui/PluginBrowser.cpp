// raw-radio-studio — plugin browser (Epic 3).

#include "PluginBrowser.h"

#include "ui/BrandColours.h"
#include "ui/BrandFonts.h"

namespace rrs
{
    namespace te = tracktion;

    PluginBrowser::PluginBrowser (PluginHost& hostRef, Session& sessionRef)
        : host (hostRef), session (sessionRef)
    {
        addAndMakeVisible (trackBox);
        addAndMakeVisible (search);
        addAndMakeVisible (knownList);
        addAndMakeVisible (trackPluginBox);
        addAndMakeVisible (statusLabel);

        for (auto* button : { &scanButton, &refreshButton, &insertButton,
                              &openButton, &removeButton, &closeButton })
            addAndMakeVisible (*button);

        trackBox.setTooltip ("Track to insert the plugin onto");
        search.setTextToShowWhenEmpty ("Search plugins (name, maker, format)", brand::textTertiary);
        search.setFont (brand::uiRegular (14.0f));
        search.onTextChange = [this] { refreshKnownList(); };

        knownList.setRowHeight (26);
        knownList.setColour (juce::ListBox::backgroundColourId, brand::bgWindow);
        knownList.setColour (juce::ListBox::outlineColourId, brand::border);
        knownList.setOutlineThickness (1);

        trackPluginBox.setTooltip ("Plugins already on the track");

        statusLabel.setFont (brand::uiRegular (12.0f));
        statusLabel.setColour (juce::Label::textColourId, brand::textSecondary);
        statusLabel.setJustificationType (juce::Justification::centredLeft);

        scanButton.setTooltip ("Scan plugin folders (out of process)");
        refreshButton.setTooltip ("Refresh the list from the known plugins");
        insertButton.setTooltip ("Insert the selected plugin onto the track");
        openButton.setTooltip ("Open the selected track plugin's editor");
        removeButton.setTooltip ("Remove the selected track plugin");
        closeButton.setTooltip ("Close the plugin browser");

        trackBox.onChange = [this] { refreshTrackPlugins(); updateStatus(); };
        scanButton.onClick = [this]
        {
            if (host.isScanning())
                return;

            scanning = true;
            updateStatus();

            juce::Component::SafePointer<PluginBrowser> safe (this);
            host.startScan (true, [safe] (int, juce::String)
                            {
                                if (auto* self = safe.getComponent())
                                {
                                    self->scanning = false;
                                    self->refreshKnownList();
                                    self->updateStatus();
                                }
                            });
        };
        refreshButton.onClick = [this] { refreshKnownList(); updateStatus(); };
        insertButton.onClick = [this] { insertSelected(); };
        openButton.onClick = [this] { openSelectedTrackPlugin(); };
        removeButton.onClick = [this] { removeSelectedTrackPlugin(); };
        closeButton.onClick = [this] { if (onClose) onClose(); };

        // A simple poll keeps the scan progress / status fresh without a timer
        // member (the panel is only visible while the user is in it).
        refreshTrackList();
        refreshKnownList();
        refreshTrackPlugins();
        updateStatus();

        startTimerHz (5);
    }

    PluginBrowser::~PluginBrowser()
    {
        stopTimer();
    }

    void PluginBrowser::refresh()
    {
        refreshTrackList();
        refreshKnownList();
        refreshTrackPlugins();
        updateStatus();
    }

    void PluginBrowser::timerCallback()
    {
        // Keep the scan progress / status line live while the panel is open.
        if (isShowing())
            updateStatus();
    }

    //==============================================================================
    void PluginBrowser::paint (juce::Graphics& g)
    {
        g.fillAll (brand::bgPanel);
        g.setColour (brand::border);
        g.drawRect (getLocalBounds(), 1);

        g.setColour (brand::textPrimary);
        g.setFont (brand::uiSemiBold (16.0f));
        g.drawText ("Plugins", getLocalBounds().removeFromTop (34).reduced (14, 0),
                    juce::Justification::centredLeft);

        g.setColour (brand::textSecondary);
        g.setFont (brand::uiRegular (12.0f));
        g.drawText ("Track plugins", getLocalBounds().removeFromBottom (getLocalBounds().getHeight() / 3)
                                             .removeFromTop (22).reduced (14, 0),
                    juce::Justification::centredLeft);
    }

    void PluginBrowser::resized()
    {
        auto area = getLocalBounds().reduced (14);
        area.removeFromTop (34); // title

        auto top = area.removeFromTop (30);
        trackBox.setBounds (top.removeFromLeft (juce::jmin (260, top.getWidth() / 2)));
        top.removeFromLeft (8);
        search.setBounds (top);

        area.removeFromTop (8);

        // Bottom third: the track's current plugins + actions.
        auto bottom = area.removeFromBottom (juce::jmax (120, area.getHeight() / 3));
        area.removeFromBottom (8);

        auto bottomActions = bottom.removeFromBottom (32);
        openButton.setBounds (bottomActions.removeFromLeft (90));
        bottomActions.removeFromLeft (8);
        removeButton.setBounds (bottomActions.removeFromLeft (100));
        bottom.removeFromBottom (6);
        trackPluginBox.setBounds (bottom.removeFromTop (30));

        // Middle: the known-plugin list + scan/insert actions.
        auto listArea = area;
        auto listActions = listArea.removeFromBottom (32);
        scanButton.setBounds (listActions.removeFromLeft (90));
        listActions.removeFromLeft (8);
        refreshButton.setBounds (listActions.removeFromLeft (100));
        listActions.removeFromLeft (8);
        insertButton.setBounds (listActions.removeFromLeft (90));
        listActions.removeFromLeft (8);
        statusLabel.setBounds (listActions);
        knownList.setBounds (listArea);

        // Close in the bottom-right corner.
        closeButton.setBounds (getWidth() - 14 - 90, getHeight() - 14 - 30, 90, 30);
    }

    //==============================================================================
    int PluginBrowser::selectedTrackIndex() const
    {
        const auto id = trackBox.getSelectedId();
        return id > 0 ? id - 1 : -1;
    }

    void PluginBrowser::refreshTrackList()
    {
        trackBox.clear (juce::dontSendNotification);

        for (int i = 0; i < session.getNumAudioTracks(); ++i)
        {
            auto name = session.getTrackName (i);

            if (name.isEmpty())
                name = "Track " + juce::String (i + 1);

            trackBox.addItem (name, i + 1);
        }

        if (session.getNumAudioTracks() > 0)
            trackBox.setSelectedId (1, juce::dontSendNotification);
    }

    void PluginBrowser::refreshKnownList()
    {
        known = host.getKnownPlugins (search.getText());
        knownList.updateContent();
        knownList.deselectAllRows();
    }

    void PluginBrowser::refreshTrackPlugins()
    {
        trackPluginBox.clear (juce::dontSendNotification);

        const auto index = selectedTrackIndex();

        for (int i = 0; i < session.getNumPlugins (index); ++i)
        {
            const auto info = session.getPluginInfo (index, i);
            auto label = info.name;

            if (info.format.isNotEmpty())
                label << "  (" << info.format << ")";

            if (info.missing)
                label << "  - missing";

            trackPluginBox.addItem (label, i + 1);
        }
    }

    void PluginBrowser::updateStatus()
    {
        const auto index = selectedTrackIndex();
        juce::String text;

        if (scanning || host.isScanning())
        {
            const auto progress = host.getProgress();
            text = "Scanning"
                 + (progress >= 0.0f ? " (" + juce::String (juce::roundToInt (progress * 100.0f)) + "%)" : juce::String())
                 + (host.getCurrentPluginName().isNotEmpty() ? ": " + host.getCurrentPluginName() : juce::String());
        }
        else
        {
            text = juce::String (host.getNumKnownPlugins()) + " plugin(s) known";

            if (index >= 0)
                text << "   |   " << session.getTrackName (index) << ": "
                     << session.getNumPlugins (index) << " inserted";
        }

        statusLabel.setText (text, juce::dontSendNotification);
    }

    //==============================================================================
    int PluginBrowser::getNumRows()
    {
        return known.size();
    }

    void PluginBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool rowIsSelected)
    {
        if (! juce::isPositiveAndBelow (row, known.size()))
            return;

        if (rowIsSelected)
        {
            g.setColour (brand::accentMuted);
            g.fillRect (0, 0, width, height);
        }

        const auto& d = known.getReference (row);

        g.setColour (brand::textPrimary);
        g.setFont (brand::uiRegular (13.0f));
        g.drawText (d.name, 8, 0, width - 120, height, juce::Justification::centredLeft);

        g.setColour (brand::textTertiary);
        g.setFont (brand::uiRegular (11.0f));
        g.drawText (d.manufacturerName + "  " + d.pluginFormatName, width - 200, 0, 192, height,
                    juce::Justification::centredRight);
    }

    void PluginBrowser::listBoxItemDoubleClicked (int, const juce::MouseEvent&)
    {
        insertSelected();
    }

    //==============================================================================
    void PluginBrowser::insertSelected()
    {
        const auto row = knownList.getSelectedRow();
        const auto index = selectedTrackIndex();

        if (! juce::isPositiveAndBelow (row, known.size()) || index < 0)
        {
            updateStatus();
            return;
        }

        if (session.insertPlugin (index, known.getReference (row), -1))
        {
            refreshTrackPlugins();
            trackPluginBox.setSelectedId (session.getNumPlugins (index), juce::dontSendNotification);
        }
        else
        {
            statusLabel.setText (session.getLastError(), juce::dontSendNotification);
        }

        updateStatus();
    }

    void PluginBrowser::openSelectedTrackPlugin()
    {
        const auto index = selectedTrackIndex();
        const auto plugin = trackPluginBox.getSelectedId() - 1;

        if (index >= 0 && plugin >= 0)
            session.showPluginEditor (index, plugin);
    }

    void PluginBrowser::removeSelectedTrackPlugin()
    {
        const auto index = selectedTrackIndex();
        const auto plugin = trackPluginBox.getSelectedId() - 1;

        if (index >= 0 && plugin >= 0)
        {
            session.removePlugin (index, plugin);
            refreshTrackPlugins();
        }

        updateStatus();
    }
}
