#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>

// ===========================================================================
// v2.102.0: the automation bridge - the shell finally exposes parameters
// to the DAW.
//
// Since v2.6 the wrapper exposed NO parameter to the host, so Studio
// One's automation lanes had nothing to grab: every real knob lives in
// the hosted console, invisible to the DAW. This bank gives the DAW a
// hand: the analyser tilt is a real discrete parameter (parameter 0 of
// the shell), and one proxy slot stands for each hosted parameter
// position, re-targeted at whatever console currently holds the slot.
//
// THE ONE-SHOT HOST PARAMETER TABLE. A DAW reads a plugin's parameter
// table once, at load: JUCE's VST3 wrapper builds the whole VST3 table
// in a single pass at install time (the population loop is guarded by
// "getParameterCount() <= 0") and never revisits it, and the one flag
// the wrapper never refreshes is kCanAutomate (the parameter-info
// update only re-reads title/units/steps/default). A parameter added
// after construction is therefore invisible to the host forever, and a
// parameter marked non-automatable at load can never become
// automatable later. The bank takes the hint: it exists IN FULL from
// the processor constructor, every slot automatable from day one,
// named "(idle)" until a console is hosted.
//
// THE RE-TARGET SWAP. When HostedStrip installs or clears a plugin
// (the onActivePluginChanged callback) the bank re-targets slot i at
// the console's parameter i. Metadata (name/label/steps/flags) is
// COPIED into an immutable config - the console's parameter object is
// only read live for its VALUE, so a console that renames its own
// knobs mid-flight can never tear a read - and the previous config is
// parked in a graveyard vector (audio threads may still hold it).
// The processor follows the re-target with updateHostDisplay
// (parameterInfoChanged) so the host re-reads the slot titles. Slot i
// keeps one VST3 parameter ID for the plugin's whole life, so
// automation curves survive plugin updates and console swaps.
//
// THE TWO DIRECTIONS.
//   DAW -> console: the host calls the proxy's setValue (audio thread
//   during playback, message thread at rest). The proxy adopts the
//   value in its cache first (this is what suppresses the echo), then
//   pushes it into the hosted parameter with setValueNotifyingHost -
//   the console reacts, its editor follows, and the preset "*" logic
//   sees a knob move like any other.
//   Console -> DAW: the processor's parameterValueChanged /
//   parameterGestureChanged (the v2.27 listeners, attached to every
//   hosted parameter) also forward to slot i. The cache is only bumped
//   when the value actually moved - a DAW-originated echo is already at
//   the new value and drops out - and the gesture forwarding lets the
//   host record the move (touch/latch write modes).
//
// WHY SILENT RE-TARGETS. A re-target adopts the console's current
// values WITHOUT sending value notifications: writing a burst of
// values the moment a console is swapped could flood automation lanes
// armed in a write mode. The DAW display catches up on the first real
// knob move, which is the honest moment anyway.
// ===========================================================================

class AutomationProxy : public juce::AudioProcessorParameter
{
public:
    AutomationProxy (juce::String idToUse, int slotIndexToUse)
        : slotId (std::move (idToUse)), slotIndex (slotIndexToUse)
    {
        config.store (new Config{}, std::memory_order_release);
    }

    int getSlotIndex() const noexcept { return slotIndex; }
    const juce::String& getSlotId() const noexcept { return slotId; }

    bool hasTarget() const
    {
        return config.load (std::memory_order_acquire)->target != nullptr;
    }

    // Re-target this slot at a hosted parameter (or nullptr = idle).
    // Instantiation or message thread, never the audio thread. The
    // pointer is deliberately non-const: setValue must push DAW moves
    // into the console with setValueNotifyingHost.
    void retarget (juce::AudioProcessorParameter* target)
    {
        auto newConfig = std::make_unique<Config>();

        if (target != nullptr)
        {
            newConfig->target  = target;
            newConfig->name   = target->getName (128);
            newConfig->label  = target->getLabel();
            newConfig->steps  = target->getNumSteps();
            newConfig->discrete = target->isDiscrete();
            newConfig->inverted = target->isOrientationInverted();
            newConfig->meta     = target->isMetaParameter();
            cachedValue.store (target->getValue(), std::memory_order_relaxed);
        }
        else
        {
            cachedValue.store (0.0f, std::memory_order_relaxed);
        }

        auto* previous = config.exchange (newConfig.release(), std::memory_order_acq_rel);

        if (previous != nullptr)
        {
            // retired configs live until the plugin dies: an audio thread
            // may still hold the pointer we just swapped out.
            const juce::ScopedLock lock (graveyardLock);
            graveyard.emplace_back (previous);
        }
    }

    // ---- the host side of the proxy (read from any thread) ----

    float getValue() const override
    {
        auto* cfg = config.load (std::memory_order_acquire);
        return cfg->target != nullptr ? cfg->target->getValue() : 0.0f;
    }

    // DAW -> console. Called by the host wrapper: the audio thread
    // during playback, the message thread at rest.
    void setValue (float newValue) override
    {
        auto* cfg = config.load (std::memory_order_acquire);
        if (cfg->target == nullptr)
            return;

        // adopt first - this is the echo filter (see forwardValueChanged)
        cachedValue.store (newValue, std::memory_order_relaxed);
        cfg->target->setValueNotifyingHost (newValue);
    }

    float getDefaultValue() const override
    {
        auto* cfg = config.load (std::memory_order_acquire);
        return cfg->target != nullptr ? cfg->target->getDefaultValue() : 0.0f;
    }

    juce::String getName (int maximumStringLength) const override
    {
        auto* cfg = config.load (std::memory_order_acquire);
        return maximumStringLength > 0 ? cfg->name.substring (0, maximumStringLength)
                                       : cfg->name;
    }

    juce::String getLabel() const override
    {
        return config.load (std::memory_order_acquire)->label;
    }

    int getNumSteps() const override
    {
        return config.load (std::memory_order_acquire)->steps;
    }

    bool isDiscrete() const override
    {
        return config.load (std::memory_order_acquire)->discrete;
    }

    bool isOrientationInverted() const override
    {
        return config.load (std::memory_order_acquire)->inverted;
    }

    bool isMetaParameter() const override
    {
        return config.load (std::memory_order_acquire)->meta;
    }

    // ALWAYS true. The flag is frozen at host-load time (see the header
    // note), and an idle slot that could not become automatable later
    // would be a dead lane forever.
    bool isAutomatable() const override { return true; }

    juce::String getText (float normalisedValue, int maximumStringLength) const override
    {
        auto* cfg = config.load (std::memory_order_acquire);
        return cfg->target != nullptr
            ? cfg->target->getText (normalisedValue, maximumStringLength)
            : juce::String (normalisedValue, 3);
    }

    float getValueForText (const juce::String& text) const override
    {
        auto* cfg = config.load (std::memory_order_acquire);
        if (cfg->target != nullptr)
            return cfg->target->getValueForText (text);
        return juce::jlimit (0.0f, 1.0f, text.getFloatValue());
    }

    // ---- the console side of the proxy (called by the processor) ----

    // Console -> DAW: forward a value change. A DAW-originated echo is
    // already at the new value (setValue adopted it first) and drops
    // out here; a console-side knob move finds the cache at the old
    // value and goes through.
    void forwardValueChanged (float newValue)
    {
        if (! hasTarget())
            return;

        const auto previous = cachedValue.exchange (newValue, std::memory_order_acq_rel);

        if (previous != newValue)
            sendValueChangedMessageToListeners (newValue);
    }

    // Console -> DAW: forward the knob's touch so write-mode automation
    // (touch/latch) records the move.
    void forwardGesture (bool hasBegun)
    {
        if (! hasTarget())
            return;

        if (hasBegun)
            beginChangeGesture();
        else
            endChangeGesture();
    }

private:
    struct Config
    {
        // non-const on purpose: the DAW->console path writes through it
        juce::AudioProcessorParameter* target = nullptr;
        juce::String name { "(idle)" };
        juce::String label;
        int  steps    = 0x7fffffff; // AudioProcessorParameter's continuous default
        bool discrete = false;
        bool inverted = false;
        bool meta     = false;
    };

    const juce::String slotId;
    const int slotIndex;
    std::atomic<Config*> config { nullptr };
    std::atomic<float> cachedValue { 0.0f };

    juce::CriticalSection graveyardLock;
    std::vector<std::unique_ptr<Config>> graveyard;
};

// ===========================================================================
// The bank: one fixed-size set of proxy slots, born whole in the
// processor constructor and re-targeted at the active console on every
// swap. 128 slots cover every console the shell hosts with headroom
// (the internal Costes strip exposes 56 parameters); a console with
// more parameters than slots logs a warning and keeps the extra ones
// off the DAW's lanes (they still work on the console's own GUI).
// ===========================================================================
class AutomationProxyBank
{
public:
    static constexpr int numSlots = 128;

    AutomationProxyBank()
    {
        for (int i = 0; i < numSlots; ++i)
        {
            auto proxy = std::make_unique<AutomationProxy> ("tcsAuto" + juce::String (i), i);
            slots.push_back (proxy.get());
            owned.push_back (std::move (proxy));
        }
    }

    // Hand a slot to the processor's addParameter (the AudioProcessor
    // owns it from then on); the bank keeps the raw pointer for
    // forwarding. Call exactly once per slot, from the constructor.
    std::unique_ptr<AutomationProxy> takeProxy (int slotIndex)
    {
        // MOVE the slot out: the returned unique_ptr hands the proxy to
        // the processor's addParameter, and owned[i] ends up null
        // WITHOUT destroying anything. Never write owned[i] = nullptr
        // here - unique_ptr assignment DELETES the pointee, and the
        // processor would own a freed proxy (the build-2 crash).
        return std::move (owned[(size_t) slotIndex]);
    }

    AutomationProxy* getProxy (int slotIndex) const
    {
        if (slotIndex < 0 || slotIndex >= numSlots)
            return nullptr;
        return slots[(size_t) slotIndex];
    }

    int getLiveSlotCount() const
    {
        int live = 0;
        for (auto* slot : slots)
            if (slot->hasTarget())
                ++live;
        return live;
    }

    // Re-target the whole bank at the active plugin (nullptr = every
    // slot back to idle). Called from HostedStrip's
    // onActivePluginChanged - the instantiation thread or the message
    // thread, never the audio thread.
    void retarget (juce::AudioProcessor* active)
    {
        // a function-local static keeps the header self-contained: the
        // empty list for the idle case, one instance for the whole process
        static const juce::Array<juce::AudioProcessorParameter*> emptyParameters;

        const juce::Array<juce::AudioProcessorParameter*>& params =
            active != nullptr ? active->getParameters() : emptyParameters;

        const int hostedCount = (int) params.size();

        for (int i = 0; i < numSlots; ++i)
            slots[(size_t) i]->retarget (i < hostedCount ? params.getUnchecked (i) : nullptr);

        if (hostedCount > numSlots)
            juce::Logger::writeToLog ("[TheChannelStrip] automation bank full: "
                                      + juce::String (hostedCount - numSlots)
                                      + " parameters of '"
                                      + (active != nullptr ? active->getName() : juce::String())
                                      + "' are not exposed to the DAW");
    }

    void forwardValueChanged (int hostedParamIndex, float newValue)
    {
        if (auto* slot = getProxy (hostedParamIndex))
            slot->forwardValueChanged (newValue);
    }

    void forwardGesture (int hostedParamIndex, bool hasBegun)
    {
        if (auto* slot = getProxy (hostedParamIndex))
            slot->forwardGesture (hasBegun);
    }

private:
    std::vector<AutomationProxy*> slots;                  // non-owning, stable forever
    std::vector<std::unique_ptr<AutomationProxy>> owned;  // emptied by takeProxy
};
