// raw-radio-studio — RAW Radio LookAndFeel (colour-ID mapping).

#include "BrandLookAndFeel.h"

#include "BrandColours.h"

namespace rrs
{
    BrandLookAndFeel::BrandLookAndFeel()
    {
        using juce::ResizableWindow;
        using juce::Label;
        using juce::TextButton;
        using juce::ComboBox;
        using juce::TextEditor;
        using juce::PopupMenu;
        using juce::Slider;
        using juce::ScrollBar;
        using juce::TooltipWindow;
        using juce::ToggleButton;
        using juce::AlertWindow;

        // Windows / panels.
        setColour (ResizableWindow::backgroundColourId, brand::bgWindow);
        setColour (juce::DocumentWindow::textColourId,    brand::textPrimary);

        // Labels.
        setColour (Label::textColourId,            brand::textPrimary);
        setColour (Label::backgroundColourId,      juce::Colours::transparentBlack);
        setColour (Label::outlineColourId,         juce::Colours::transparentBlack);

        // Buttons.
        setColour (TextButton::buttonColourId,     brand::bgTertiary);
        setColour (TextButton::buttonOnColourId,   brand::accentMuted);
        setColour (TextButton::textColourOffId,    brand::textPrimary);
        setColour (TextButton::textColourOnId,     brand::accent);

        // Combo boxes.
        setColour (ComboBox::backgroundColourId,        brand::bgPanel);
        setColour (ComboBox::textColourId,              brand::textPrimary);
        setColour (ComboBox::outlineColourId,           brand::border);
        setColour (ComboBox::focusedOutlineColourId,    brand::borderFocus);
        setColour (ComboBox::arrowColourId,             brand::textSecondary);
        setColour (ComboBox::buttonColourId,            brand::bgTertiary);

        // Text editors.
        setColour (TextEditor::backgroundColourId,          brand::bgPanel);
        setColour (TextEditor::textColourId,                brand::textPrimary);
        setColour (TextEditor::outlineColourId,             brand::border);
        setColour (TextEditor::focusedOutlineColourId,      brand::borderFocus);
        setColour (TextEditor::highlightColourId,           brand::accentMuted);
        setColour (TextEditor::highlightedTextColourId,     brand::textPrimary);
        setColour (TextEditor::shadowColourId,              juce::Colours::transparentBlack);

        // Popup menus.
        setColour (PopupMenu::backgroundColourId,             brand::bgPanel);
        setColour (PopupMenu::textColourId,                   brand::textPrimary);
        setColour (PopupMenu::headerTextColourId,             brand::textSecondary);
        setColour (PopupMenu::highlightedBackgroundColourId,  brand::accentMuted);
        setColour (PopupMenu::highlightedTextColourId,        brand::textPrimary);

        // Sliders.
        setColour (Slider::backgroundColourId, brand::bgElevated);
        setColour (Slider::trackColourId,      brand::bgElevated);
        setColour (Slider::thumbColourId,      brand::accent);
        setColour (Slider::textBoxTextColourId,        brand::textPrimary);
        setColour (Slider::textBoxBackgroundColourId,  brand::bgPanel);
        setColour (Slider::textBoxOutlineColourId,     brand::border);

        // Scroll bars.
        setColour (ScrollBar::backgroundColourId, brand::bgWindow);
        setColour (ScrollBar::thumbColourId,      brand::bgElevated);

        // Tooltips.
        setColour (TooltipWindow::backgroundColourId, brand::bgTertiary);
        setColour (TooltipWindow::textColourId,       brand::textPrimary);
        setColour (TooltipWindow::outlineColourId,    brand::border);

        // Toggle ticks (monitor role by default; arm overrides per-component).
        setColour (ToggleButton::textColourId,        brand::textPrimary);
        setColour (ToggleButton::tickColourId,        brand::success);
        setColour (ToggleButton::tickDisabledColourId, brand::textDisabled);

        // Alert windows / native-style message boxes.
        setColour (AlertWindow::backgroundColourId, brand::bgPanel);
        setColour (AlertWindow::textColourId,       brand::textPrimary);
        setColour (AlertWindow::outlineColourId,    brand::border);
    }
}
