// raw-radio-studio — WAV export (Epic 1).

#include "WavExport.h"

#include "AudioEngine.h"

namespace rrs
{
    namespace te = tracktion;

    juce::File WavExport::defaultDestinationFor (const juce::File& editFile)
    {
        const auto base = editFile != juce::File() ? editFile.getFileNameWithoutExtension()
                                                    : juce::String ("Session");
        const auto directory = editFile != juce::File() ? editFile.getParentDirectory()
                                                         : juce::File::getSpecialLocation (
                                                               juce::File::userDocumentsDirectory);
        return directory.getChildFile (base + " mix.wav");
    }

    std::shared_ptr<WavExport::Handle>
        WavExport::start (te::Edit& edit, const juce::File& destination, CompletionCallback callback,
                          bool suppressMetronome)
    {
        // Liveness token shared with every deferred completion path. The returned
        // Handle owns it; cancelling the handle (explicitly or from its destructor)
        // clears it, so a queued metronome restore can never fire after the owning
        // component has been torn down and re-enable/disable the click out of turn.
        auto alive = std::make_shared<std::atomic<bool>> (true);

        // FR-EXP-1: `edit.clickTrackEnabled` is read live by the ClickNode while
        // the render graph plays, so clearing it here (on the message thread,
        // before the render is built) guarantees an enabled metronome is never
        // baked into the exported WAV. The previous state is restored in every
        // completion path below, on the message thread.
        //
        // The click state is captured as its (ref-counted) ValueTree rather than
        // a raw `Edit*`: the completion callback is dispatched asynchronously and
        // must not dereference an Edit that may have been torn down in the
        // meantime. The ValueTree copy stays valid on its own.
        auto clickState = std::make_shared<juce::ValueTree> (
            edit.state.getOrCreateChildWithName (te::IDs::CLICKTRACK, nullptr));
        const bool metronomeWasEnabled = (bool) clickState->getProperty (te::IDs::active, false);

        if (suppressMetronome)
            clickState->setProperty (te::IDs::active, false, nullptr);

        const auto restoreMetronome = [clickState, metronomeWasEnabled, suppressMetronome, alive]
        {
            // Skip once the export handle is gone: a later teardown pass owns the
            // click state from that point on and must not be overwritten by this
            // stale deferred restore.
            if (suppressMetronome && alive != nullptr && alive->load (std::memory_order_acquire))
                clickState->setProperty (te::IDs::active, metronomeWasEnabled, nullptr);
        };

        const auto fail = [callback, destination, restoreMetronome, alive] (const juce::String& message)
        {
            juce::MessageManager::callAsync ([callback, destination, message, restoreMetronome]
                                             {
                                                 restoreMetronome();

                                                 if (callback)
                                                     callback (false, destination, message);
                                             });

            // Still hand back a Handle so the caller can invalidate the queued
            // restore by destroying/cancelling it during teardown.
            return std::make_shared<Handle> (nullptr, alive);
        };

        if (! destination.getParentDirectory().createDirectory())
            return fail ("Could not create the export directory:\n"
                         + destination.getParentDirectory().getFullPathName());

        te::Renderer::Parameters params (edit);
        params.destFile           = destination;
        params.audioFormat        = edit.engine.getAudioFileFormatManager().getWavFormat();
        params.bitDepth           = bitDepth;

        // FR-EXP-1 / FR-MIX-1: the exported WAV must match what the engineer
        // hears in playback, i.e. the *full* mixer state. Track plugins
        // (per-track fader/pan/mute) are on by default, but the master plugin
        // chain — the master volume plugin (fader/pan/mute) and its master-bus
        // plugins — is OFF by default in Renderer::Parameters
        // (`useMasterPlugins = false`). Without this the offline render ignored
        // the master fader/mute entirely, so a mix balanced against a loud
        // backing track exported at full level and buried the voice.
        params.usePlugins         = true;
        params.useMasterPlugins   = true;

        // When no audio device is open (export can be triggered headless or after
        // the device was closed) Tracktion's DeviceManager::getSampleRate() does
        // NOT return 0 — it returns a 44100 Hz placeholder — so a `<= 0` test
        // alone let the render silently run at 44.1 kHz. Detect the no-device
        // case explicitly and use the Epic 1 default (48 kHz, NFR-A-4). When a
        // device IS open, keep its session sample rate.
        auto& deviceManager = edit.engine.getDeviceManager();
        auto sampleRate = deviceManager.getSampleRate();

        if (deviceManager.deviceManager.getCurrentAudioDevice() == nullptr
            || sampleRate <= 0.0)
            sampleRate = AudioEngine::defaultSampleRate;

        params.sampleRateForAudio = sampleRate;
        params.blockSizeForAudio  = 512;
        params.time               = { te::TimePosition(),
                                      te::TimePosition::fromSeconds (edit.getLength().inSeconds()) };

        auto handle = te::EditRenderer::render (
            params,
            [callback, destination, restoreMetronome] (tl::expected<juce::File, std::string> result)
            {
                const bool success = result.has_value();
                const auto file = success ? result.value() : destination;
                const auto error = success ? juce::String() : juce::String (result.error());

                juce::MessageManager::callAsync ([callback, success, file, error, restoreMetronome]
                                                 {
                                                     // Restore before the caller's callback so the
                                                     // Session/UI sees the click back in its original
                                                     // state by the time it reacts to the export.
                                                     restoreMetronome();

                                                     if (callback)
                                                         callback (success, file, error);
                                                 });
            });

        if (handle == nullptr)
            return fail ("Could not start the render. The session may be empty.");

        return std::make_shared<Handle> (handle, alive);
    }
}
