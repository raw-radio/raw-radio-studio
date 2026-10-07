// raw-radio-studio — per-track stems + master export (Epic 3).

#include "StemsExport.h"

#include "AudioEngine.h"

namespace rrs
{
    namespace te = tracktion;

    namespace
    {
        /** Session render rate, mirroring WavExport: the open device's rate, or
            the 48 kHz Epic 1 default when no device is open. */
        double sessionRenderRate (te::Edit& edit)
        {
            auto& dm = edit.engine.getDeviceManager();
            auto rate = dm.getSampleRate();

            if (dm.deviceManager.getCurrentAudioDevice() == nullptr || rate <= 0.0)
                rate = AudioEngine::defaultSampleRate;

            return rate;
        }

        /** A unique, legal file name for a stem ("<track>.wav"), avoiding
            collisions with other stems and with files already on disk. */
        juce::File uniqueStemFile (const juce::File& directory, const juce::String& name,
                                   juce::StringArray& usedNames)
        {
            auto base = juce::File::createLegalFileName (name.isNotEmpty() ? name : "Track");

            if (base.isEmpty())
                base = "Track";

            auto candidate = base;

            for (int suffix = 2; usedNames.contains (candidate, true)
                                  || directory.getChildFile (candidate + ".wav").existsAsFile(); ++suffix)
                candidate = base + " " + juce::String (suffix);

            usedNames.add (candidate);
            return directory.getChildFile (candidate + ".wav");
        }

        struct BatchState
        {
            int expected = 0;
            int finished = 0;
            juce::String error;
        };
    }

    //==============================================================================
    StemsExport::Handle::Handle (std::shared_ptr<te::RenderQueue> q,
                                 std::shared_ptr<std::atomic<bool>> token)
        : queue (std::move (q)), alive (std::move (token))
    {
    }

    StemsExport::Handle::~Handle()
    {
        cancel();
    }

    void StemsExport::Handle::cancel() noexcept
    {
        if (alive != nullptr)
            alive->store (false, std::memory_order_release);

        if (queue != nullptr)
            queue->cancelAll();
    }

    float StemsExport::Handle::getProgress() const
    {
        return queue != nullptr ? queue->getTotalProgress() : 0.0f;
    }

    //==============================================================================
    juce::File StemsExport::defaultDirectoryFor (const juce::File& editFile)
    {
        const auto base = editFile != juce::File() ? editFile.getFileNameWithoutExtension()
                                                    : juce::String ("Session");
        const auto directory = editFile != juce::File() ? editFile.getParentDirectory()
                                                         : juce::File::getSpecialLocation (
                                                               juce::File::userDocumentsDirectory);
        return directory.getChildFile (base + " stems");
    }

    std::shared_ptr<StemsExport::Handle>
        StemsExport::start (te::Edit& edit, const juce::File& directory,
                            CompletionCallback callback, bool includeMaster)
    {
        auto alive = std::make_shared<std::atomic<bool>> (true);
        auto queue = std::make_shared<te::RenderQueue>();

        if (! directory.createDirectory())
        {
            if (callback)
                juce::MessageManager::callAsync ([callback, directory]
                                                 { callback (false, directory, 0,
                                                             "Could not create the export directory:\n"
                                                             + directory.getFullPathName()); });

            return std::make_shared<Handle> (nullptr, alive);
        }

        const auto sampleRate = sessionRenderRate (edit);
        const auto baseName = edit.editFileRetriever ? edit.editFileRetriever().getFileNameWithoutExtension()
                                                     : juce::String ("Session");

        juce::StringArray usedNames;
        int added = 0;

        auto addSpec = [&] (te::RenderSpecification spec)
        {
            if (auto job = te::createRenderJob (edit, spec))
            {
                queue->addJob (std::move (*job));
                ++added;
            }
        };

        // One stem per track carrying clips (input/record tracks and imported
        // backing tracks). Empty bus/return tracks are skipped.
        for (auto* track : te::getAudioTracks (edit))
        {
            if (track == nullptr || track->getClips().isEmpty())
                continue;

            te::RenderSpecification spec;
            spec.destination       = uniqueStemFile (directory, track->getName(), usedNames);
            spec.format            = te::RenderFormat::wav;
            spec.bitDepth          = bitDepth;
            spec.sampleRate        = sampleRate;
            spec.includeTails      = false;
            spec.usePlugins        = true;
            spec.useMasterPlugins  = false;   // stems are pre-master
            spec.tracks            = { track->itemID };
            addSpec (std::move (spec));
        }

        if (includeMaster)
        {
            te::RenderSpecification spec;
            spec.destination       = uniqueStemFile (directory, baseName + " master", usedNames);
            spec.format            = te::RenderFormat::wav;
            spec.bitDepth          = bitDepth;
            spec.sampleRate        = sampleRate;
            spec.includeTails      = false;
            spec.usePlugins        = true;
            spec.useMasterPlugins  = true;
            // Empty `tracks` => render the whole Edit (the master mix).
            addSpec (std::move (spec));
        }

        if (added == 0)
        {
            if (callback)
                juce::MessageManager::callAsync ([callback, directory]
                                                 { callback (false, directory, 0,
                                                             "There is nothing to export (no clips)."); });

            return std::make_shared<Handle> (nullptr, alive);
        }

        auto state = std::make_shared<BatchState>();
        state->expected = added;

        queue->onJobFinished = [state] (te::RenderQueue::Job& job)
        {
            ++state->finished;

            if (job.getState() != te::RenderQueue::Job::State::completed && state->error.isEmpty())
                state->error = job.getError().isNotEmpty()
                                   ? job.getError()
                                   : ("Render failed: " + job.getName());
        };

        queue->onFinished = [alive, callback, directory, state]
        {
            if (alive == nullptr || ! alive->load (std::memory_order_acquire))
                return;

            if (callback)
                callback (state->error.isEmpty(), directory, state->finished, state->error);
        };

        queue->start();

        return std::make_shared<Handle> (queue, alive);
    }
}
