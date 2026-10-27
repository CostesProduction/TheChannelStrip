#include "HostedStrip.h"

namespace
{
    // AudioProcessor::process() is protected; processBlock() is the public
    // entry point. One shared empty MIDI buffer for all hosted rendering.
    juce::MidiBuffer noMidi;

}

HostedStrip::HostedStrip()
{
    formatManager.addDefaultFormats();
}

HostedStrip::~HostedStrip()
{
    retireOutgoingNow();

    if (auto* active = activeSlot.exchange (nullptr, std::memory_order_acq_rel))
    {
        if (active->plugin != nullptr)
            active->plugin->releaseResources();
        delete active;
    }
}

void HostedStrip::prepareToPlay (double newSampleRate, int newSamplesPerBlock, int maxChannels)
{
    sampleRate = juce::jmax (8000.0, newSampleRate);
    samplesPerBlock = juce::jmax (1, newSamplesPerBlock);
    numChannels = juce::jmax (1, maxChannels);
    fadeLengthSamples = juce::jmax (64, (int) (0.05 * sampleRate));

    dryCopy.setSize  (numChannels, samplesPerBlock);
    scratchA.setSize (numChannels, samplesPerBlock);
    scratchB.setSize (numChannels, samplesPerBlock);

    for (auto* slot : { activeSlot.load (std::memory_order_acquire),
                        outgoingSlot.load (std::memory_order_acquire) })
    {
        if (slot != nullptr && slot->plugin != nullptr)
            slot->plugin->prepareToPlay (sampleRate, samplesPerBlock);
    }
}

void HostedStrip::releaseResources()
{
    for (auto* slot : { activeSlot.load (std::memory_order_acquire),
                        outgoingSlot.load (std::memory_order_acquire) })
    {
        if (slot != nullptr && slot->plugin != nullptr)
            slot->plugin->releaseResources();
    }
}

void HostedStrip::setPlugin (std::unique_ptr<juce::AudioProcessor> newPlugin)
{
    if (newPlugin == nullptr)
        return;

    newPlugin->prepareToPlay (sampleRate, samplesPerBlock);
    reportedLatency.store (newPlugin->getLatencySamples(), std::memory_order_relaxed);

    auto slot = std::make_unique<HostedSlot>();
    slot->plugin = std::move (newPlugin);
    slot->fade.store (0.0f, std::memory_order_relaxed);

    auto* raw = slot.release();
    auto* oldActive = activeSlot.exchange (raw, std::memory_order_acq_rel);

    if (oldActive != nullptr)
        retireActiveIntoOutgoing (oldActive);
}

void HostedStrip::clearPlugin()
{
    reportedLatency.store (0, std::memory_order_relaxed);

    auto* oldActive = activeSlot.exchange (nullptr, std::memory_order_acq_rel);

    if (oldActive != nullptr)
        retireActiveIntoOutgoing (oldActive);
    else
        retireOutgoingNow();
}

void HostedStrip::retireActiveIntoOutgoing (HostedSlot* oldActive)
{
    if (oldActive == nullptr)
        return;

    oldActive->fade.store (1.0f, std::memory_order_relaxed);

    auto* prev = outgoingSlot.exchange (oldActive, std::memory_order_acq_rel);

    if (prev != nullptr)
    {
        // There was still a plugin fading out. Destroy it on a background
        // thread after a grace period so in-flight audio has drained.
        // Capture the raw pointer and take ownership inside the lambda so
        // the lambda stays copyable for std::function.
        juce::Thread::launch ([prev] ()
        {
            std::unique_ptr<HostedSlot> obj (prev);
            juce::Thread::sleep (150);

            if (obj->plugin != nullptr)
            {
                obj->plugin->releaseResources();
                obj->plugin.reset();
            }
        });
    }
}

void HostedStrip::retireOutgoingNow()
{
    auto* old = outgoingSlot.exchange (nullptr, std::memory_order_acq_rel);
    if (old == nullptr)
        return;

    // Destroy the retired plugin on a background thread: destructors of
    // hosted plugins can be arbitrarily slow. Raw-pointer capture keeps the
    // lambda copyable.
    juce::Thread::launch ([old] ()
    {
        std::unique_ptr<HostedSlot> obj (old);

        if (obj->plugin != nullptr)
        {
            obj->plugin->releaseResources();
            obj->plugin.reset();
        }
    });
}

void HostedStrip::copyToScratch (juce::AudioBuffer<float>& scratch,
                                 const juce::AudioBuffer<float>& source,
                                 int pluginChannels)
{
    const int numSamples = source.getNumSamples();
    const int srcChans   = source.getNumChannels();
    const int chans      = juce::jmax (pluginChannels, srcChans);

    scratch.setSize (chans, numSamples, false, false, true);

    // A plugin configured for more channels than the buffer has gets the
    // source channel duplicated (L = R = mono) - exactly the v2.8.2
    // bridge. The reverse case (a mono-configured plugin given a wider
    // buffer) is safe: the plugin only reads its own first channel.
    for (int c = 0; c < chans; ++c)
        scratch.copyFrom (c, 0, source, juce::jmin (c, srcChans - 1), 0, numSamples);
}

void HostedStrip::processBlock (juce::AudioBuffer<float>& buffer)
{
    auto* active = activeSlot.load (std::memory_order_acquire);
    auto* outgoing = outgoingSlot.load (std::memory_order_acquire);

    if (active == nullptr && outgoing == nullptr)
        return; // dry signal passes through untouched

    const int numSamples = buffer.getNumSamples();
    const int numChans = buffer.getNumChannels();

    dryCopy.makeCopyOf (buffer);

    if (active != nullptr && active->plugin != nullptr)
    {
        // v2.14: size the scratch to what this plugin expects, not to the
        // wrapper's track format (see copyToScratch).
        copyToScratch (scratchA, dryCopy, active->plugin->getTotalNumInputChannels());
        active->plugin->processBlock (scratchA, noMidi);
    }

    if (outgoing != nullptr && outgoing->plugin != nullptr)
    {
        copyToScratch (scratchB, dryCopy, outgoing->plugin->getTotalNumInputChannels());
        outgoing->plugin->processBlock (scratchB, noMidi);
    }

    const float fadeStep = 1.0f / (float) fadeLengthSamples;
    const float faStart = (active != nullptr)
        ? active->fade.load (std::memory_order_relaxed) : 1.0f;
    const float foStart = (outgoing != nullptr)
        ? outgoing->fade.load (std::memory_order_relaxed) : 0.0f;

    for (int c = 0; c < numChans; ++c)
    {
        float* dst = buffer.getWritePointer (c);
        const float* dry = dryCopy.getReadPointer (c);
        const float* pa = (active != nullptr && active->plugin != nullptr)
            ? scratchA.getReadPointer (c) : nullptr;
        const float* po = (outgoing != nullptr && outgoing->plugin != nullptr)
            ? scratchB.getReadPointer (c) : nullptr;

        float fa = faStart;
        float fo = foStart;

        for (int i = 0; i < numSamples; ++i)
        {
            float wetSum = 0.0f;
            float sample = 0.0f;

            if (pa != nullptr) { sample += pa[i] * fa; wetSum += fa; }
            if (po != nullptr) { sample += po[i] * fo; wetSum += fo; }

            const float dryCoef = juce::jmax (0.0f, 1.0f - wetSum);
            dst[i] = sample + dry[i] * dryCoef;

            fa = juce::jmin (1.0f, fa + fadeStep);
            fo = juce::jmax (0.0f, fo - fadeStep);
        }
    }

    if (active != nullptr)
        active->fade.store (juce::jmin (1.0f, faStart + (float) numSamples * fadeStep),
                            std::memory_order_relaxed);

    if (outgoing != nullptr)
    {
        const float foEnd = juce::jmax (0.0f, foStart - (float) numSamples * fadeStep);
        outgoing->fade.store (foEnd, std::memory_order_relaxed);

        if (foEnd <= 0.0f)
            retireOutgoingNow();
    }
}
