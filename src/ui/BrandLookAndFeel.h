// raw-radio-studio — RAW Radio LookAndFeel.
//
// Maps the default JUCE colour IDs onto the rrs::brand tokens so every stock
// component (buttons, combos, menus, sliders, scrollbars, tooltips, alerts)
// picks up the RAW Radio dark/orange theme without per-component overrides.
//
// Applied once from the application entry point via
// `juce::LookAndFeel::setDefaultLookAndFeel`; UI-only, never touched from the
// audio thread.

#pragma once

#include <JuceHeader.h>

namespace rrs
{
    class BrandLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        BrandLookAndFeel();

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrandLookAndFeel)
    };
}
