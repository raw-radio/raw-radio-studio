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
        addAndMakeVisible (knownLabel);
        addAndMakeVisible (knownList);
        addAndMakeVisible (trackPluginLabel);
        addAndMakeVisible (trackPluginBox);
        addAndMakeVisible (statusLabel);
        addAndMakeVisible (presetsLabel);
        addAndMakeVisible (presetSelectLabel);
        addAndMakeVisible (presetBox);
        addAndMakeVisible (presetNameLabel);
        addAndMakeVisible (presetName);

        for (auto* button : { &scanButton, &refreshButton, &insertButton,
                              &openButton, &removeButton, &savePresetButton, &loadPresetButton,
                              &closeButton })
            addAndMakeVisible (*button);

        // Section headers (semi-bold) + inline field labels. Each label lives in
        // its own laid-out row so it can never slide under another control.
        for (auto* label : { &knownLabel, &trackPluginLabel, &presetsLabel })
        {
            label->setFont (brand::uiSemiBold (12.0f));
            label->setColour (juce::Label::textColourId, brand::textSecondary);
            label->setJustificationType (juce::Justification::centredLeft);
        }

        knownLabel.setText ("Available plugins", juce::dontSendNotification);
        trackPluginLabel.setText ("Track plugin", juce::dontSendNotification);
        presetsLabel.setText ("Presets", juce::dontSendNotification);

        for (auto* label : { &presetSelectLabel, &presetNameLabel })
        {
            label->setFont (brand::uiRegular (12.0f));
            label->setColour (juce::Label::textColourId, brand::textTertiary);
            label->setJustificationType (juce::Justification::centredLeft);
        }

        presetSelectLabel.setText ("Preset", juce::dontSendNotification);
        presetNameLabel.setText ("New", juce::dontSendNotification);

        trackBox.setTooltip ("Track to insert the plugin onto");
        search.setTextToShowWhenEmpty ("Search plugins (name, maker, format)", brand::textTertiary);
        search.setFont (brand::uiRegular (14.0f));
        search.onTextChange = [this] { refreshKnownList(); };

        knownList.setRowHeight (26);
        knownList.setColour (juce::ListBox::backgroundColourId, brand::bgWindow);
        knownList.setColour (juce::ListBox::outlineColourId, brand::border);
        knownList.setOutlineThickness (1);

        trackPluginBox.setTooltip ("Plugins already on the track");

        // FR-MIX-6: user presets for the selected plugin.
        presetBox.setTooltip ("User presets for the selected plugin");
        presetName.setTextToShowWhenEmpty ("Preset name", brand::textTertiary);
        presetName.setFont (brand::uiRegular (14.0f));
        presetName.setTooltip ("Name for a new user preset, then Save preset");

        statusLabel.setFont (brand::uiRegular (12.0f));
        statusLabel.setColour (juce::Label::textColourId, brand::textSecondary);
        statusLabel.setJustificationType (juce::Justification::centredLeft);

        scanButton.setTooltip ("Scan plugin folders (out of process)");
        refreshButton.setTooltip ("Refresh the list from the known plugins");
        insertButton.setTooltip ("Insert the selected plugin onto the track");
        openButton.setTooltip ("Open the selected track plugin's editor");
        removeButton.setTooltip ("Remove the selected track plugin");
        savePresetButton.setTooltip ("Save the plugin's current state as a named user preset");
        loadPresetButton.setTooltip ("Load the selected user preset into the plugin");
        closeButton.setTooltip ("Close the plugin browser");

        trackBox.onChange = [this] { refreshTrackPlugins(); updateStatus(); };

        // When a different plugin on the track is selected, its preset list is
        // the one shown.
        trackPluginBox.onChange = [this] { refreshPresets(); updateStatus(); };
        presetBox.onChange = [this] { updatePresetControls(); };
        presetName.onTextChange = [this] { updatePresetControls(); };
        presetName.onReturnKey = [this] { savePresetClicked(); };
        savePresetButton.onClick = [this] { savePresetClicked(); };
        loadPresetButton.onClick = [this] { loadPresetClicked(); };
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
    }

    void PluginBrowser::resized()
    {
        // Spacing scale (mirrors the rest of the app): 4 / 8 / 12 / 16.
        constexpr int margin = 14;
        constexpr int gap = 8;
        constexpr int labelH = 16;
        constexpr int labelFieldGap = 4;
        constexpr int sectionGap = 12;
        constexpr int rowH = 28;
        constexpr int actionW = 100;
        constexpr int fieldLabelW = 64;
        constexpr int listActionH = 32;

        // Height of the bottom block (track plugin + presets + actions), fixed so
        // it always fits and can never overlap the known-plugin list above it.
        constexpr int lowerBlockH = labelH + labelFieldGap + rowH // track plugin
                                    + sectionGap
                                    + labelH + labelFieldGap + rowH + 6 + rowH // presets
                                    + sectionGap
                                    + listActionH;

        auto area = getLocalBounds().reduced (margin);
        area.removeFromTop (34); // title (painted)

        // Track selector + search.
        auto top = area.removeFromTop (30);
        trackBox.setBounds (top.removeFromLeft (juce::jmin (260, top.getWidth() / 2)));
        top.removeFromLeft (gap);
        search.setBounds (top);

        area.removeFromTop (gap);

        // Bottom block, anchored so it can never overlap the known list.
        auto lower = area.removeFromBottom (lowerBlockH);
        area.removeFromBottom (gap);

        // --- Track's existing plugins ---
        trackPluginLabel.setBounds (lower.removeFromTop (labelH));
        lower.removeFromTop (labelFieldGap);
        trackPluginBox.setBounds (lower.removeFromTop (rowH));
        lower.removeFromTop (sectionGap);

        // --- Presets: select + Load, then name + Save (explicit rows) ---
        presetsLabel.setBounds (lower.removeFromTop (labelH));
        lower.removeFromTop (labelFieldGap);

        auto presetRow = lower.removeFromTop (rowH);
        presetSelectLabel.setBounds (presetRow.removeFromLeft (fieldLabelW));
        presetRow.removeFromLeft (gap);
        loadPresetButton.setBounds (presetRow.removeFromRight (actionW));
        presetRow.removeFromRight (gap);
        presetBox.setBounds (presetRow);

        lower.removeFromTop (6);

        auto saveRow = lower.removeFromTop (rowH);
        presetNameLabel.setBounds (saveRow.removeFromLeft (fieldLabelW));
        saveRow.removeFromLeft (gap);
        savePresetButton.setBounds (saveRow.removeFromRight (actionW));
        saveRow.removeFromRight (gap);
        presetName.setBounds (saveRow);

        lower.removeFromTop (sectionGap);

        // --- Track-plugin actions, with Close on the right ---
        auto actions = lower.removeFromTop (listActionH);
        closeButton.setBounds (actions.removeFromRight (90));
        actions.removeFromRight (gap);
        openButton.setBounds (actions.removeFromLeft (90));
        actions.removeFromLeft (gap);
        removeButton.setBounds (actions.removeFromLeft (100));

        // --- Known-plugin list (middle), actions below it with a real gap ---
        knownLabel.setBounds (area.removeFromTop (labelH));
        area.removeFromTop (labelFieldGap);

        auto listActions = area.removeFromBottom (listActionH);
        area.removeFromBottom (gap); // keeps Scan/Refresh off the list border

        knownList.setBounds (area);

        scanButton.setBounds (listActions.removeFromLeft (90));
        listActions.removeFromLeft (gap);
        refreshButton.setBounds (listActions.removeFromLeft (100));
        listActions.removeFromLeft (gap);
        insertButton.setBounds (listActions.removeFromLeft (90));
        listActions.removeFromLeft (gap);
        statusLabel.setBounds (listActions);
    }

    //==============================================================================
    int PluginBrowser::selectedTrackIndex() const
    {
        const auto id = trackBox.getSelectedId();
        return id > 0 ? id - 1 : -1;
    }

    void PluginBrowser::refreshTrackList()
    {
        // Preserve the user's current track selection across a refresh (e.g. a
        // timer/`refresh()` or a session change): only fall back to the first
        // track when the previous selection no longer exists.
        const auto previousId = trackBox.getSelectedId();

        trackBox.clear (juce::dontSendNotification);

        const auto numTracks = session.getNumAudioTracks();

        for (int i = 0; i < numTracks; ++i)
        {
            auto name = session.getTrackName (i);

            if (name.isEmpty())
                name = "Track " + juce::String (i + 1);

            trackBox.addItem (name, i + 1);
        }

        if (numTracks > 0)
        {
            const auto idToSelect = juce::isPositiveAndBelow (previousId - 1, numTracks)
                                        ? previousId
                                        : 1;
            trackBox.setSelectedId (idToSelect, juce::dontSendNotification);
        }
    }

    void PluginBrowser::refreshKnownList()
    {
        known = host.getKnownPlugins (search.getText());
        knownList.updateContent();
        knownList.deselectAllRows();
    }

    void PluginBrowser::refreshTrackPlugins()
    {
        // Preserve the current plugin selection across a refresh; default to the
        // first plugin so the preset list has something to show.
        const auto previousId = trackPluginBox.getSelectedId();

        trackPluginBox.clear (juce::dontSendNotification);

        const auto index = selectedTrackIndex();
        const auto count = session.getNumPlugins (index);

        for (int i = 0; i < count; ++i)
        {
            const auto info = session.getPluginInfo (index, i);
            auto label = info.name;

            if (info.format.isNotEmpty())
                label << "  (" << info.format << ")";

            if (info.missing)
                label << "  - missing";

            trackPluginBox.addItem (label, i + 1);
        }

        if (count > 0)
        {
            const auto idToSelect = juce::isPositiveAndBelow (previousId - 1, count) ? previousId : 1;
            trackPluginBox.setSelectedId (idToSelect, juce::dontSendNotification);
        }

        refreshPresets();
    }

    int PluginBrowser::selectedPluginIndex() const
    {
        const auto id = trackPluginBox.getSelectedId();
        return id > 0 ? id - 1 : -1;
    }

    void PluginBrowser::refreshPresets()
    {
        // Preserve the visible preset across a refresh when it still exists.
        const auto previous = presetBox.getText();

        presetBox.clear (juce::dontSendNotification);

        const auto index = selectedTrackIndex();
        const auto plugin = selectedPluginIndex();

        if (index >= 0 && plugin >= 0)
        {
            const auto names = session.listPluginPresets (index, plugin);

            for (int i = 0; i < names.size(); ++i)
                presetBox.addItem (names[i], i + 1);
        }

        for (int i = 0; i < presetBox.getNumItems(); ++i)
            if (presetBox.getItemText (i) == previous)
            {
                presetBox.setSelectedId (i + 1, juce::dontSendNotification);
                break;
            }

        updatePresetControls();
    }

    void PluginBrowser::updatePresetControls()
    {
        const bool hasPlugin = selectedTrackIndex() >= 0 && selectedPluginIndex() >= 0;
        savePresetButton.setEnabled (hasPlugin
                                     && PluginPresets::isValidPresetName (presetName.getText()));
        loadPresetButton.setEnabled (hasPlugin && presetBox.getSelectedId() > 0);
    }

    void PluginBrowser::savePresetClicked()
    {
        const auto index = selectedTrackIndex();
        const auto plugin = selectedPluginIndex();
        auto name = presetName.getText().trim();

        if (name.isEmpty())
            name = presetBox.getText().trim();

        if (index < 0 || plugin < 0)
        {
            statusLabel.setText ("Select a track plugin first.", juce::dontSendNotification);
            return;
        }

        if (session.savePluginPreset (index, plugin, name))
        {
            presetName.clear();
            refreshPresets();

            for (int i = 0; i < presetBox.getNumItems(); ++i)
                if (presetBox.getItemText (i) == name)
                {
                    presetBox.setSelectedId (i + 1, juce::dontSendNotification);
                    break;
                }

            statusLabel.setText ("Saved preset: " + name, juce::dontSendNotification);
        }
        else
        {
            statusLabel.setText (session.getLastError(), juce::dontSendNotification);
        }

        updatePresetControls();
    }

    void PluginBrowser::loadPresetClicked()
    {
        const auto index = selectedTrackIndex();
        const auto plugin = selectedPluginIndex();
        const auto name = presetBox.getText().trim();

        if (index < 0 || plugin < 0 || name.isEmpty())
        {
            statusLabel.setText ("Select a preset to load.", juce::dontSendNotification);
            return;
        }

        if (session.loadPluginPreset (index, plugin, name))
            statusLabel.setText ("Loaded preset: " + name, juce::dontSendNotification);
        else
            statusLabel.setText (session.getLastError(), juce::dontSendNotification);

        updatePresetControls();
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
