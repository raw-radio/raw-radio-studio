// raw-radio-studio — plugin hosting support (Epic 3).

#include "PluginHost.h"

#include <algorithm>

namespace rrs
{
    namespace te = tracktion;

    namespace
    {
        /** The dead-mans-pedal file records plugins that crashed a previous scan
            so JUCE retries them last; it lives next to the app's settings. */
        juce::File deadMansPedalFileFor (te::Engine& engine)
        {
            return engine.getPropertyStorage().getAppPrefsFolder()
                       .getChildFile ("deadMansPedal.txt");
        }
    }

    PluginHost::PluginHost (te::Engine& engineRef)
        : juce::Thread ("rrs-plugin-scan"),
          engine (engineRef)
    {
    }

    PluginHost::~PluginHost()
    {
        stopThread (5000);
    }

    juce::String PluginHost::getCurrentPluginName() const
    {
        const juce::ScopedLock sl (nameLock);
        return currentName;
    }

    void PluginHost::startScan (bool rescanExisting, CompletionCallback callback)
    {
        if (isThreadRunning())
            return;

        rescan = rescanExisting;
        completion = std::move (callback);
        progress.store (0.0f);

        startThread();
    }

    void PluginHost::cancelScan()
    {
        signalThreadShouldExit();

        // Break a scan that is blocked waiting on a plugin/child process, rather
        // than waiting for the current file to finish.
        if (auto& pm = engine.getPluginManager(); pm.abortCurrentPluginScan)
            pm.abortCurrentPluginScan();
    }

    void PluginHost::run()
    {
        auto& pm = engine.getPluginManager();
        auto& formatManager = pm.pluginFormatManager;
        const auto deadMansFile = deadMansPedalFileFor (engine);

        const int numFormats = juce::jmax (1, formatManager.getNumFormats());
        const int before = pm.knownPluginList.getNumTypes();
        juce::String error;

        for (int i = 0; i < formatManager.getNumFormats() && ! threadShouldExit(); ++i)
        {
            auto* format = formatManager.getFormat (i);

            if (format == nullptr)
                continue;

            auto paths = format->getDefaultLocationsToSearch();

            // The list already carries Tracktion's CustomScanner, so each probe
            // goes through the child process for VST3/LADSPA (out-of-process scan).
            juce::PluginDirectoryScanner scanner (pm.knownPluginList, *format, paths,
                                                  true /* searchRecursively */, deadMansFile,
                                                  false /* allow asynchronously instantiated plugins */);

            juce::String name;

            while (! threadShouldExit() && scanner.scanNextFile (! rescan, name))
            {
                {
                    const juce::ScopedLock sl (nameLock);
                    currentName = format->getName() + ": " + name;
                }

                progress.store (((float) i + scanner.getProgress()) / (float) numFormats);
            }

            if (threadShouldExit())
                break;
        }

        const int found = pm.knownPluginList.getNumTypes() - before;

        progress.store (-1.0f);

        // Deliver the completion on the message thread so the caller can touch
        // the UI safely.
        auto cb = completion;
        completion = {};

        if (cb)
            juce::MessageManager::callAsync ([cb, found, error] { cb (found, error); });
    }

    juce::Array<juce::PluginDescription> PluginHost::getKnownPlugins (const juce::String& searchText) const
    {
        auto plugins = engine.getPluginManager().knownPluginList.getTypes();

        if (searchText.isNotEmpty())
        {
            const auto terms = juce::StringArray::fromTokens (searchText, true);

            for (int i = plugins.size(); --i >= 0;)
            {
                const auto& d = plugins.getReference (i);
                bool matchesAll = true;

                for (auto& term : terms)
                {
                    const auto haystack = d.name + " " + d.manufacturerName + " " + d.pluginFormatName
                                        + " " + d.category + " " + d.descriptiveName;

                    if (! haystack.containsIgnoreCase (term))
                    {
                        matchesAll = false;
                        break;
                    }
                }

                if (! matchesAll)
                    plugins.remove (i);
            }
        }

        struct Comparator
        {
            static int compareElements (const juce::PluginDescription& a, const juce::PluginDescription& b)
            {
                const auto byName = a.name.compareIgnoreCase (b.name);

                if (byName != 0)
                    return byName;

                const auto byManufacturer = a.manufacturerName.compareIgnoreCase (b.manufacturerName);

                if (byManufacturer != 0)
                    return byManufacturer;

                return a.pluginFormatName.compareIgnoreCase (b.pluginFormatName);
            }
        };

        Comparator comparator;
        plugins.sort (comparator);
        return plugins;
    }

    int PluginHost::getNumKnownPlugins() const
    {
        return engine.getPluginManager().knownPluginList.getNumTypes();
    }

    bool PluginHost::usesOutOfProcessScanning() const
    {
        return engine.getPluginManager().usesSeparateProcessForScanning();
    }

    juce::StringArray PluginHost::getHostedFormatNames() const
    {
        juce::StringArray names;
        auto& formats = engine.getPluginManager().pluginFormatManager;

        for (int i = 0; i < formats.getNumFormats(); ++i)
            if (auto* format = formats.getFormat (i))
                names.add (format->getName());

        names.sort (true);
        return names;
    }
}
