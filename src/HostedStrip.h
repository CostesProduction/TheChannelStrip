#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <vector>
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

    // v2.33 (roadmap idea 3): true while a console-swap crossfade is in
    // flight - the outgoing slot is still fading out, or the incoming
    // one has not reached full level yet. Read by the UI timer for the
    // shell integrity readout and the Doctor report: a couple of atomic
    // loads, nothing more (no audio-thread involvement).
    bool isCrossfading() const
    {
        if (outgoingSlot.load (std::memory_order_acquire) != nullptr)
            return true;

        auto* slot = activeSlot.load (std::memory_order_acquire);
        return slot != nullptr && slot->fade.load (std::memory_order_relaxed) < 1.0f;
    }

    // v2.16.1: the hosted plugin can change its own latency at runtime
    // (e.g. toggling oversampling inside the Lindell consoles). The
    // processor calls this from its processBlock so the cached value above
    // stays current. Cost per block: one atomic load plus one plain int
    // read - JUCE's hosted VST3 wrapper updates its latency cache when the
    // plugin signals kLatencyChanged, and our own setLatencySamples on the
    // TCS side re-triggers kLatencyChanged on the wrapper, so the DAW
    // re-compensates within a few blocks.
    void refreshReportedLatency()
    {
        if (auto* plugin = getActivePlugin())
            reportedLatency.store (plugin->getLatencySamples(), std::memory_order_relaxed);
    }

    // -----------------------------------------------------------------
    // v2.47.0 (the EQ-Verite curve): the shell must KNOW when the
    // hosted console's response changed, so the transfer estimators
    // can restart (TransferAnalyzer::requestFlush - the long 5 s
    // averaging window is what makes the EQ curve read the truth, and
    // the flush is what keeps it reactive). Two events feed this
    // callback, installed once by the processor at construction (a
    // std::function written once, then only read - safe to invoke from
    // any thread):
    //   - a HOSTED PARAMETER moved (an AudioProcessorParameter::Listener
    //     attached to every parameter of the active plugin; the
    //     listener is detached before the plugin retires, so a dying
    //     plugin can never call back into a half-dead shell);
    //   - the plugin itself was swapped or cleared (setPlugin /
    //     clearPlugin - a different console IS a different response).
    // The callback only ever does an atomic store
    // (TransferAnalyzer::requestFlush), so the listener thread context
    // (JUCE: message thread or the parameter's own thread) is fine.
    // -----------------------------------------------------------------
    std::function<void()> onConsoleResponseChanged;

    // Test/support hook: how many hosted parameters currently carry
    // the change relay (the flush test asserts the listener wiring
    // without touching the console's audio).
    int getRelayedParameterCount() const { return (int) relayedParameters.size(); }

private:
    struct HostedSlot
    {
        std::unique_ptr<juce::AudioProcessor> plugin;
        std::atomic<float> fade { 0.0f }; // 0 = silent, 1 = at full level
    };

    // v2.47.0: the parameter-change relay (see onConsoleResponseChanged).
    // One listener object attached to every parameter of the ACTIVE
    // plugin; parameterValueChanged only invokes the callback (an
    // atomic store in the processor's wiring) - no locks, no audio
    // work, whatever thread the parameter notifies on.
    struct ParamRelay : public juce::AudioProcessorParameter::Listener
    {
        HostedStrip* owner = nullptr;

        void parameterValueChanged (int, float) override
        {
            if (owner != nullptr && owner->onConsoleResponseChanged)
                owner->onConsoleResponseChanged();
        }

        void parameterGestureChanged (int, bool) override {}
    };

    void attachRelayTo (juce::AudioProcessor& plugin);
    void detachRelayFromAll();

    ParamRelay paramRelay;
    // message thread only (setPlugin / clearPlugin / destructor); the
    // audio thread never touches it, retireOutgoingNow included (it
    // hands destruction to a background thread, and the relay is
    // always detached BEFORE a plugin retires).
    std::vector<juce::AudioProcessorParameter*> relayedParameters;

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
