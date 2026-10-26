#pragma once

#include <atomic>
#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>

// Hosts one third-party plugin instance at a time with click-free swapping:
// two slots (active + outgoing) swapped through atomic pointers and a 50 ms
// crossfade. Old instances are destroyed on a background thread.
//
// v2.1: no canonical parameter mapping and no output-knob detection any
// more. The shell only hosts the plugin (with click-free swapping); the
// measured LUFS correction is displayed for the user to apply by hand on
// the hosted plugin's own controls.
class HostedStrip
{
public:
    HostedStrip();
    ~HostedStrip();

    void prepareToPlay (double newSampleRate, int samplesPerBlock, int maxChannels);
    void releaseResources();
    void processBlock (juce::AudioBuffer<float>& buffer);

    // Install a freshly created plugin instance; it fades in over 50 ms while
    // any previous instance fades out.
    void setPlugin (std::unique_ptr<juce::AudioProcessor> newPlugin);
    void clearPlugin();

    bool hasActivePlugin() const
    {
        return activeSlot.load (std::memory_order_acquire) != nullptr;
    }

    juce::AudioProcessor* getActivePlugin() const
    {
        auto* slot = activeSlot.load (std::memory_order_acquire);
        return slot != nullptr ? slot->plugin.get() : nullptr;
    }

    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }
    juce::KnownPluginList& getKnownPluginList() { return knownPluginList; }

    int getReportedLatency() const
    {
        return reportedLatency.load (std::memory_order_relaxed);
    }

private:
    struct HostedSlot
    {
        std::unique_ptr<juce::AudioProcessor> plugin;
        std::atomic<float> fade { 0.0f }; // 0 = silent, 1 = at full level
    };

    void retireOutgoingNow();
    void retireActiveIntoOutgoing (HostedSlot* oldActive);

    // v2.14: copies the dry signal into a per-plugin scratch sized to what
    // THAT plugin expects (each hosted instance keeps its own bus layout -
    // a native mono console, or a stereo console still fading out on a
    // track that just switched format).
    void copyToScratch (juce::AudioBuffer<float>& scratch,
                        const juce::AudioBuffer<float>& source,
                        int pluginChannels);

    std::atomic<HostedSlot*> activeSlot   { nullptr };
    std::atomic<HostedSlot*> outgoingSlot { nullptr };
    std::atomic<int> reportedLatency { 0 };

    double sampleRate = 48000.0;
    int samplesPerBlock = 512;
    int numChannels = 2;
    int fadeLengthSamples = 2400; // ~50 ms @ 48 kHz

    juce::AudioBuffer<float> dryCopy, scratchA, scratchB;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
};
