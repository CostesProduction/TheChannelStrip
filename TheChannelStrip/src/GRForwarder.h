#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// NOTE: juce_VST3ClientExtensions.h must NOT be included directly here.
// It is an unguarded JUCE header (no pragma once) that is already pulled
// in by juce_audio_processors.h; including it again redefines
// VST3ClientExtensions and breaks the build (the v2.15.1 lesson).

// v2.15.5: Studio One gain-reduction forwarding.
//
// Studio One displays a plug-in's gain reduction on the channel meter when
// the plug-in's VST3 edit controller implements PreSonus' public
// IGainReductionInfo extension. The bx_console models forward their gate +
// compressor GR that way when inserted directly; hosted inside
// TheChannelStrip the console's controller is hidden behind our own VST3
// wrapper, so Studio One saw nothing.
//
// This class closes the gap with a pure 1:1 pull chain, nothing more:
//
//   Studio One --queryInterface(IGainReductionInfo)--> our VST3 wrapper
//     -> AudioProcessor::getVST3ClientExtensions() (JUCE 8 hook)
//       -> GRForwarder::getVST3ClientExtensions()
//         -> our IGainReductionInfo::getGainReductionValueInDb()
//           -> the hosted console's own IGainReductionInfo
//
// No polling thread, no timer, zero audio-thread cost: the host calls us
// from its own metering thread and we forward the call to the console
// under a lock shared with the console swap. With no console hosted, or a
// console that does not expose the extension, the answer is 0.0 dB (the
// PreSonus contract). The extension only exists in the VST3 wrapper; the
// AU and Standalone wrappers never call the hook, so they are unaffected.
class GRForwarder
{
public:
    GRForwarder();
    ~GRForwarder();

    // Called by the processor once a hosted console is fully instantiated
    // (end of instantiateHosted): queries the console's own
    // IGainReductionInfo through its VST3 IComponent/IEditController and
    // stores it for forwarding. Safe to call repeatedly: the previous
    // console's COM references are released first (the old instance may
    // still be crossfading out).
    void connect (juce::AudioPluginInstance& instance);

    // Releases every borrowed COM reference (clearHostedPlugin, processor
    // destructor). Idempotent.
    void disconnect();

    // The value our own IGainReductionInfo hands to the host: the console's
    // current GR, or 0.0 when nothing is connected. Called on the host's
    // metering thread.
    double pullGainReductionDb() const;

    // Hook for AudioProcessor::getVST3ClientExtensions(): the JUCE VST3
    // wrapper queries this object when the host asks our edit controller
    // for interfaces the wrapper itself does not implement.
    juce::VST3ClientExtensions* getVST3ClientExtensions();

    // The COM objects defined in the .cpp keep a raw owner pointer, so the
    // Pimpl type must be named here (v2.15.2 lesson).
    class Pimpl;

private:
    Pimpl* pimpl = nullptr;
};
