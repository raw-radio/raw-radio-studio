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

    std::shared_ptr<te::EditRenderer::Handle>
        WavExport::start (te::Edit& edit, const juce::File& destination, CompletionCallback callback,
                          bool suppressMetronome)
    {
        // FR-EXP-1: `edit.clickTrackEnabled` is read live by the ClickNode while
        // the render graph plays, so clearing it here (on the message thread,
        // before the render is built) guarantees an enabled metronome is never
        // baked into the exported WAV. The previous state is restored in every
        // completion path below, on the message thread.
        const bool metronomeWasEnabled = edit.clickTrackEnabled.get();

        if (suppressMetronome)
            edit.clickTrackEnabled = false;

        const auto restoreMetronome = [&edit, metronomeWasEnabled, suppressMetronome]
        {
            if (suppressMetronome)
                edit.clickTrackEnabled = metronomeWasEnabled;
        };

        const auto fail = [callback, destination, restoreMetronome] (const juce::String& message)
        {
            juce::MessageManager::callAsync ([callback, destination, message, restoreMetronome]
                                             {
                                                 restoreMetronome();

                                                 if (callback)
                                                     callback (false, destination, message);
                                             });
            return std::shared_ptr<te::EditRenderer::Handle>();
        };

        if (! destination.getParentDirectory().createDirectory())
            return fail ("Could not create the export directory:\n"
                         + destination.getParentDirectory().getFullPathName());

        te::Renderer::Parameters params (edit);
        params.destFile           = destination;
        params.audioFormat        = edit.engine.getAudioFileFormatManager().getWavFormat();
        params.bitDepth           = bitDepth;

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

        return handle;
    }
}
