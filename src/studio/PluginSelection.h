// raw-radio-studio — insert-plugin eligibility (Epic 3 / owner bug fix).
//
// The track insert chain hosts AUDIO EFFECTS only. Instruments, generators,
// panners and MIDI utilities are not effects and must not be inserted on an
// audio track:
//
//   * AU `kAudioUnitType_MusicDevice` (e.g. DLSMusicDevice) — JUCE reports
//     `PluginDescription::isInstrument == true` and category "Synth". As an
//     insert it has no audio input, so the track goes silent.
//   * AU `kAudioUnitType_Panner` (e.g. HRTFPanner) — JUCE reports category
//     "Panner". Measured here at 2-in/2-out it still mangles the insert to
//     roughly -28 dB (effectively silent), so it must not be offered either.
//   * Generators / mixers / MIDI effects — same reasoning.
//
// Instruments belong to Epic 5 (virtual instruments); they are filtered out of
// the insert browser and rejected by `Session::insertPlugin` before the track is
// touched, so a bad plugin can never silence the session.
//
// The predicates are pure (juce::String only) so they unit-test without an
// engine or a real plugin.

#pragma once

#include <juce_core/juce_core.h>

namespace rrs::plugin_selection
{
    /** True when a scanner category names something that is not an audio effect.
        An empty (unknown) category is NOT flagged, so plugins whose format does
        not report a category are still offered. */
    inline bool isNonEffectCategory (const juce::String& category)
    {
        if (category.isEmpty())
            return false;

        return category.containsIgnoreCase ("instrument")
            || category.containsIgnoreCase ("synth")
            || category.containsIgnoreCase ("generator")
            || category.containsIgnoreCase ("panner")
            || category.containsIgnoreCase ("mixer")
            || category.containsIgnoreCase ("midieffect")   // AU "MidiEffects"
            || category.containsIgnoreCase ("midi effect");
    }

    /** True when a plugin is an instrument or any other non-effect that must not
        be used as a track insert. */
    inline bool isInstrumentOrNonEffect (bool isInstrument, const juce::String& category)
    {
        return isInstrument || isNonEffectCategory (category);
    }

    /** True when positive channel counts describe a layout the stereo insert
        path cannot process safely (e.g. 1-in/2-out, or no audio input at all).
        Unknown (0/0) counts are not flagged, so we never reject on missing
        metadata. */
    inline bool hasIncompatibleInsertLayout (int numInputChannels, int numOutputChannels)
    {
        if (numOutputChannels <= 0)
            return false;             // layout unknown — cannot judge

        if (numInputChannels == 0)
            return true;              // generator/instrument with no audio input

        return numInputChannels != numOutputChannels;
    }

    /** Empty when the plugin may be used as an effect on a stereo track,
        otherwise a clear, user-facing rejection reason. */
    inline juce::String insertRejectionReason (bool isInstrument,
                                               const juce::String& category,
                                               int numInputChannels,
                                               int numOutputChannels)
    {
        if (isInstrumentOrNonEffect (isInstrument, category))
            return "This is an instrument or other non-effect plugin. The insert chain "
                   "accepts audio effects only; instruments are coming in a later version.";

        if (hasIncompatibleInsertLayout (numInputChannels, numOutputChannels))
            return "This plugin's channel layout (" + juce::String (numInputChannels) + " in / "
                 + juce::String (numOutputChannels) + " out) is not compatible with a stereo "
                   "insert and could silence the track, so it was not inserted.";

        return {};
    }
}
