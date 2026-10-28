// ---------------------------------------------------------------------------
// tests/TcsTestsMain.cpp - v2.36.0 offline test harness (roadmap point 8)
//
// A zero-dependency console executable that exercises the processor end to
// end with synthetic material and MOCK hosted plugins (no third-party
// plugin is needed for the default suite):
//
//   - processor basics (createPluginFilter, bus layouts, console labels)
//   - bit-exact passthrough with no hosted plugin (stereo AND mono)
//   - measurement gating (nothing measured while the editor is closed)
//   - LUFS short-term metering (absolute window + 6 dB tracking)
//   - spectrum analyser peak location (1 kHz and 10 kHz)
//   - measured EQ curve accuracy in weak-content bands (v2.21: strong
//     + weak two-tone passthrough reads flat, bass-only leaves the
//     treble unmeasured - the relative validity gate)
//   - EQ rejection of UNCORRELATED console noise (v2.22: a mock console
//     that adds an independent 12 kHz tone; the cross-spectrum transfer
//     reads flat 0 dB there while the coherence dips below 1 - the old
//     power ratio would have read the tone as a +3 dB phantom boost)
//   - console slot status reporting (v2.24, roadmap point 7: missing /
//     blacklisted / moved-file slots each carry their own explanation,
//     and clicking an unavailable console posts an explicit message
//     instead of doing nothing)
//   - silent-console watch (v2.24, roadmap point 7: a licence-expired
//     stand-in console emits digital silence while the input is loud -
//     exactly ONE explicit warning per hosted instance, none for a
//     healthy console, none for a silent input, re-armed on swap)
//   - named presets (v2.26, roadmap point 4: save / list / load /
//     export / import / delete against the real preset folder, plus
//     the explicit refusals - nothing hosted, missing file, corrupt
//     file; v2.28: an import LOADS the preset - into the console, as
//     its current one; v2.29: Save / Load / Delete as three actions;
//     v2.30: the list is RECURSIVE (root = general presets, each
//     sub-folder = an album, shown as "[album]" entries), a Save
//     anywhere becomes the current preset AT ITS FILE (album saves
//     included), Load never copies anything - it just loads from
//     wherever the file lives - and the current preset's own FILE
//     travels with the session, so an album preset restores as an
//     album preset
//   - current-preset tracking (v2.27: the saved/loaded preset becomes
//     the console's current one, a knob move raises the "*" once the
//     editor timer drains it, reloading or re-saving clears it, the
//     name and the "*" survive a session roundtrip, deleting the
//     current preset disowns it)
//   - hover readout mapping and formatting (v2.19, roadmap point 3)
//   - state save/restore roundtrip (display options + version tag)
//   - latency forwarding, static AND runtime changes (the v2.16.1 chain)
//   - click-free console crossfade (bounded slew + convergence)
//   - native mono negotiation vs stereo-bridge fallback (the v2.14
//     machinery, driven by a mock that accepts / refuses the mono layout)
//   - stereo <-> mono hot switching without re-creating the instance
//   - realtime-safety audit (v2.32): the harness replaces the global
//     operator new / delete pair with a counting sentinel, then proves
//     every steady-state processBlock branch - stereo with the editor
//     open and closed, native mono, the mono bridge, and no console
//     hosted - runs ALLOCATION-FREE after warm-up
//   - torture tests (v2.32): a 50-swap console storm mid-playback with
//     a full clear in the middle, sample-rate / block-size / format
//     churn on a never-recreated instance, and hostile session states
//     (garbage bytes, empty, truncated, huge valid-XML fakes) fed
//     during playback
//
// Run after a build:
//
//     ./build/TcsTests_artefacts/Release/TcsTests
//
// It prints one FAIL line per failed check and exits non-zero if anything
// failed, so it can be scripted. Pass --integration to additionally host
// a REAL console found in the saved plugin database
// (~/Library/Application Support/TheChannelStrip/scanned_plugins.xml).
// The integration mode loads a third-party module INSIDE this test binary:
// in the DAW the shell never does that (v2.9.1), but here a hostile plugin
// can only kill a throwaway test process.
//
// Design notes:
//  - MockConsole is a real juce::AudioProcessor, so the whole production
//    path runs: HostedStrip::setPlugin snapshots its latency, processBlock
//    crossfades it in, refreshReportedLatency picks up runtime changes,
//    tryApplyMonoLayout negotiates the real bus machinery, and the stereo
//    bridge down-mixes exactly as in the DAW.
//  - Every JUCE API used here was verified against the JUCE 8.0.6 module
//    headers (setPlayConfigDetails/setBusesLayout are public,
//    getLatencySamples is NOT virtual - the mock reports latency through
//    the public setLatencySamples - and processBlock(double) has a
//    default implementation that is never called here).
//  - The mock instances that are swapped OUT are destroyed on a background
//    thread after a 150 ms grace period (HostedStrip::retireActiveIntoOut-
//    going). main() sleeps before returning so nothing tears down after
//    the JUCE shutdown.
// ---------------------------------------------------------------------------

#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <new>
#include <vector>

#include <juce_events/juce_events.h>                 // ScopedJuceInitialiser_GUI
#include <juce_audio_processors/juce_audio_processors.h>

#include "../src/PluginProcessor.h"
#include "../src/PluginEditor.h"   // v2.19: SpectrumPanel readout statics

// The plugin entry point is defined in PluginProcessor.cpp but not declared
// in any header (the wrapper targets pick it up from
// juce_audio_plugin_client, which a console app does not link). Declare it
// explicitly so this translation unit can call it.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

// ---------------------------------------------------------------------------
// v2.32 realtime-safety audit: the harness replaces the global
// operator new / operator delete pair (malloc / aligned_alloc + free,
// the standard sentinel pattern), so a test can COUNT every heap
// allocation made while a flag is raised. The steady-state processBlock
// must allocate NOTHING: a std::vector / std::string / std::function
// growth on the audio thread means a heap lock with an unbounded worst
// case - a dropout hazard by definition. The replacement is active
// process-wide from static start-up, so every allocation of this test
// binary (harness, production sources, statically linked JUCE code)
// passes through it.
//
// Two honest limits, audited by READING the code instead (documented
// here so the audit stays truthful):
//   - juce::HeapBlock (and therefore juce::AudioBuffer's internal
//     storage) goes straight to malloc and is NOT counted. The only
//     HeapBlock users on the audio path are the HostedStrip scratch /
//     dry buffers and the processor's mono stereoBridge: they are
//     sized in prepareToPlay and only ever re-sized with
//     avoidReallocating = true, i.e. never in steady state.
//   - when a console fade-out COMPLETES inside processBlock,
//     HostedStrip launches the retirement of the outgoing instance on
//     a background thread (one std::function + one thread per console
//     SWITCH - never per block; the v2.1 click-free swap design). The
//     audit windows below simply contain no swap, and the swap storm
//     covers that path separately for stability.
// ---------------------------------------------------------------------------
namespace tcsAllocSentinel
{
    std::atomic<bool>      recording { false };
    std::atomic<long long> count     { 0 };
}

void* operator new (std::size_t size)
{
    if (size == 0)
        ++size;

    if (tcsAllocSentinel::recording.load (std::memory_order_relaxed))
        tcsAllocSentinel::count.fetch_add (1, std::memory_order_relaxed);

    if (void* p = std::malloc (size))
        return p;

    throw std::bad_alloc {};
}

void* operator new[] (std::size_t size)
{
    return ::operator new (size);
}

void* operator new (std::size_t size, std::align_val_t alignment)
{
    if (tcsAllocSentinel::recording.load (std::memory_order_relaxed))
        tcsAllocSentinel::count.fetch_add (1, std::memory_order_relaxed);

    // C++17 std::aligned_alloc requires a size that is a multiple of
    // the alignment.
    const std::size_t a = (std::size_t) alignment < sizeof (void*) * 2
        ? sizeof (void*) * 2
        : (std::size_t) alignment;
    const std::size_t rounded = juce::jmax ((size + a - 1) / a * a, a);

    if (void* p = std::aligned_alloc (a, rounded))
        return p;

    throw std::bad_alloc {};
}

void* operator new[] (std::size_t size, std::align_val_t alignment)
{
    return ::operator new (size, alignment);
}

// Every new path above returns malloc / aligned_alloc memory, so every
// delete path frees with free() - the pairs always match.
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete (void* p, std::align_val_t) noexcept { std::free (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { std::free (p); }

// ---------------------------------------------------------------------------
// tiny framework
// ---------------------------------------------------------------------------
namespace
{
int numChecks = 0;
int numFailed = 0;

void beginTest (const juce::String& name)
{
    std::cout << "[ RUN  ] " << name << std::endl;
}

void expectTrue (bool condition, const juce::String& description)
{
    ++numChecks;

    if (! condition)
    {
        ++numFailed;
        std::cout << "        FAIL: " << description << std::endl;
    }
}

void expectNear (double value, double target, double tolerance,
                 const juce::String& description)
{
    ++numChecks;

    if (std::abs (value - target) > tolerance)
    {
        ++numFailed;
        std::cout << "        FAIL: " << description
                  << " (got " << juce::String (value, 4)
                  << ", expected " << juce::String (target, 2)
                  << " +/- " << juce::String (tolerance, 2) << ")" << std::endl;
    }
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

// Continuous-phase sine source (the phase carries across blocks).
struct SineSource
{
    SineSource (double frequencyHz, double sampleRateHz, float amplitudeToUse)
        : omega (juce::MathConstants<double>::twoPi * frequencyHz / sampleRateHz),
          amplitude (amplitudeToUse)
    {}

    void fill (juce::AudioBuffer<float>& buffer)
    {
        for (int c = 0; c < buffer.getNumChannels(); ++c)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (c, i, amplitude * (float) std::sin (phase + omega * (double) i));

        phase += omega * (double) buffer.getNumSamples();
    }

    double phase = 0.0;
    double omega;
    float  amplitude;
};

void fillSilence (juce::AudioBuffer<float>& buffer)
{
    buffer.clear();
}

void processBlocks (TheChannelStripAudioProcessor& proc, SineSource& src,
                    int numBlocks, juce::AudioBuffer<float>& block)
{
    for (int n = 0; n < numBlocks; ++n)
    {
        juce::MidiBuffer noMidi;
        src.fill (block);
        proc.processBlock (block, noMidi);
    }
}

float peakOf (const juce::AudioBuffer<float>& buffer, int channel = 0)
{
    float m = 0.0f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        m = std::max (m, std::abs (buffer.getSample (channel, i)));

    return m;
}

float maxAbsDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    const int numChannels = juce::jmin (a.getNumChannels(), b.getNumChannels());
    const int numSamples  = juce::jmin (a.getNumSamples(),  b.getNumSamples());
    float m = 0.0f;

    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < numSamples; ++i)
            m = std::max (m, std::abs (a.getSample (c, i) - b.getSample (c, i)));

    return m;
}

// A canonical main-bus layout, like a DAW negotiates before prepareToPlay.
// v2.18.1 crash fix: BusesLayout::getChannelSet (bool, int) is an UNCHECKED
// Array::getReference (juce_AudioProcessor.h) - on a default-constructed
// BusesLayout the bus arrays are EMPTY, so getChannelSet (true, 0) returned
// a reference into a nullptr array and assigning to it segfaulted on the
// first test (the production code never hits this because it always starts
// from a real getBusesLayout(), whose arrays match the plugin's buses).
// Build the layout through the PUBLIC inputBuses/outputBuses members
// instead - the canonical way to construct a layout from scratch.
juce::AudioProcessor::BusesLayout makeLayout (bool mono)
{
    juce::AudioProcessor::BusesLayout layout;
    const auto set = mono ? juce::AudioChannelSet::mono()
                          : juce::AudioChannelSet::stereo();
    layout.inputBuses.add (set);
    layout.outputBuses.add (set);
    return layout;
}

// ---------------------------------------------------------------------------
// MockConsole: a real AudioProcessor standing in for a hosted console.
// It applies a settable gain, reports a settable latency, accepts or
// refuses the native mono layout, and records the channel count of every
// buffer it is handed (the stereo bridge must feed it 2 channels, native
// mono must feed it 1).
// ---------------------------------------------------------------------------
class MockConsole : public juce::AudioProcessor
{
public:
    explicit MockConsole (bool acceptsMonoLayout)
        : AudioProcessor (BusesProperties()
                            .withInput  ("Main", juce::AudioChannelSet::stereo(), true)
                            .withOutput ("Main", juce::AudioChannelSet::stereo(), true)),
          acceptsMono (acceptsMonoLayout)
    {}

    std::atomic<float> gain { 1.0f };
    std::atomic<int>   lastChannelCount { 0 };
    std::atomic<int>   processCallCount { 0 };

    const juce::String getName() const override { return "MockConsole"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto in  = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();

        if (in != out)
            return false;

        if (in == juce::AudioChannelSet::stereo())
            return true;

        return in == juce::AudioChannelSet::mono() && acceptsMono;
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        lastChannelCount.store (buffer.getNumChannels());
        ++processCallCount;

        const float g = gain.load();

        if (g != 1.0f)
            buffer.applyGain (g);
    }

    double getTailLengthSeconds() const override { return 2.0; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    const bool acceptsMono;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MockConsole)
};

// ---------------------------------------------------------------------------
// MockPresetConsole (v2.26): a hosted console whose state is a single
// int, so the preset save/list/apply paths can assert byte-exactly
// what was saved and re-applied. It records the last state it was
// GIVEN, so a test can tell "the preset was applied" from "nothing
// happened". The pure-virtual boilerplate matches MockConsole above
// exactly (getProgramName (int) - see the v2.24.1 lesson).
// ---------------------------------------------------------------------------
class MockPresetConsole : public juce::AudioProcessor
{
public:
    MockPresetConsole()
        : AudioProcessor (BusesProperties()
                            .withInput  ("Main", juce::AudioChannelSet::stereo(), true)
                            .withOutput ("Main", juce::AudioChannelSet::stereo(), true))
    {}

    std::atomic<int> value { 0 };            // the "console state"
    std::atomic<int> applyCallCount { 0 };

    const juce::String getName() const override { return "MockPresetConsole"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto in  = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();

        return in == out && in == juce::AudioChannelSet::stereo();
    }

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}

    double getTailLengthSeconds() const override { return 2.0; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& mb) override
    {
        const int v = value.load();

        mb.setSize (sizeof (v));
        mb.copyFrom (&v, 0, sizeof (v));
    }

    void setStateInformation (const void* data, int sizeInBytes) override
    {
        if (sizeInBytes < (int) sizeof (int))
            return;

        int v = 0;
        std::memcpy (&v, data, sizeof (v));

        value.store (v);
        ++applyCallCount;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MockPresetConsole)
};

// ---------------------------------------------------------------------------
// MockToneConsole: a hosted console that passes the signal through
// UNCHANGED but adds an independent 12 kHz tone at its output - a
// deterministic stand-in for the real consoles' noise / dither floor
// sitting above their lowpass section. The tone is UNCORRELATED with
// the program: a power-ratio EQ measures it as a gain (the phantom
// rise), a cross-spectrum transfer rejects it (it averages out of the
// complex Sxy) while the coherence records its presence.
// ---------------------------------------------------------------------------
class MockToneConsole : public juce::AudioProcessor
{
public:
    MockToneConsole()
        : AudioProcessor (BusesProperties()
                            .withInput  ("Main", juce::AudioChannelSet::stereo(), true)
                            .withOutput ("Main", juce::AudioChannelSet::stereo(), true))
    {}

    const juce::String getName() const override { return "MockToneConsole"; }

    void prepareToPlay (double sampleRate, int) override
    {
        omega = juce::MathConstants<double>::twoPi * 12000.0 / sampleRate;
        tonePhase = 0.0;
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet()
                && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int numSamples = buffer.getNumSamples();

        for (int i = 0; i < numSamples; ++i)
        {
            const float s = 0.02f * (float) std::sin (tonePhase);
            tonePhase += omega;

            for (int c = 0; c < buffer.getNumChannels(); ++c)
                buffer.setSample (c, i, buffer.getSample (c, i) + s);
        }
    }

    double getTailLengthSeconds() const override { return 2.0; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    double omega = 0.0;
    double tonePhase = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MockToneConsole)
};

// ---------------------------------------------------------------------------
// MockSilenceConsole (v2.24, roadmap point 7): the licence-expired
// stand-in. It accepts the buses like any console but emits DIGITAL
// SILENCE - exactly what a lapsed licence looks like on a Plugin
// Alliance console: the plugin loads happily, every meter reads
// nothing. The v2.24 silent-console watch must flag it ONCE while the
// input is loud, and must stay quiet when the input itself is silent.
// ---------------------------------------------------------------------------
class MockSilenceConsole : public juce::AudioProcessor
{
public:
    MockSilenceConsole()
        : AudioProcessor (BusesProperties()
                            .withInput  ("Main", juce::AudioChannelSet::stereo(), true)
                            .withOutput ("Main", juce::AudioChannelSet::stereo(), true))
    {}

    const juce::String getName() const override { return "MockSilenceConsole"; }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet()
            && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.clear();
    }

    double getTailLengthSeconds() const override { return 2.0; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MockSilenceConsole)
};

// ---------------------------------------------------------------------------
// NoiseSource: deterministic broadband noise (xorshift32, fixed seed, so
// every run - and the reference simulation - sees the same statistics).
// Fills every channel with the SAME stream, like SineSource.
// ---------------------------------------------------------------------------
struct NoiseSource
{
    explicit NoiseSource (float amplitudeToUse) : amplitude (amplitudeToUse) {}

    void fill (juce::AudioBuffer<float>& buffer)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;

            const float s = amplitude * ((float) ((double) state / 2147483648.0) - 1.0f);

            for (int c = 0; c < buffer.getNumChannels(); ++c)
                buffer.setSample (c, i, s);
        }
    }

    juce::uint32 state = 0x12345678u;
    float amplitude;
};

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

void testProcessorBasics()
{
    beginTest ("processor basics");

    std::unique_ptr<juce::AudioProcessor> filter (createPluginFilter());
    expectTrue (filter != nullptr, "createPluginFilter returns a processor");

    auto* proc = dynamic_cast<TheChannelStripAudioProcessor*> (filter.get());
    expectTrue (proc != nullptr, "createPluginFilter returns a TheChannelStripAudioProcessor");
    expectTrue (proc->getName() == "TheChannelStrip", "processor name");
    expectTrue (proc->getLatencySamples() == 0, "no latency without a hosted plugin");
    expectTrue (proc->getTailLengthSeconds() == 2.0, "tail length");

    // bus layouts: stereo and mono in == out are supported, a mismatch is not
    expectTrue (proc->setBusesLayout (makeLayout (false)), "stereo layout supported");
    expectTrue (proc->setBusesLayout (makeLayout (true)),  "mono layout supported");

    juce::AudioProcessor::BusesLayout mismatch;
    mismatch.inputBuses.add (juce::AudioChannelSet::mono());
    mismatch.outputBuses.add (juce::AudioChannelSet::stereo());
    expectTrue (! proc->setBusesLayout (mismatch), "mono-in / stereo-out rejected");

    // console inventory and display order (v2.17)
    expectTrue (TheChannelStripAudioProcessor::numConsoles == 9, "nine console slots");
    expectTrue (proc->getConsoleSlots().size() == 9, "nine console slots reported");

    const char* const expectedLabels[9] =
        { "A9099", "SSL E", "SSL G", "SSL J", "Bx N",
          "Lindell N", "Lindell A", "Focusrite", "A200" };

    for (int i = 0; i < 9; ++i)
        expectTrue (TheChannelStripAudioProcessor::consoleLabel (i) == expectedLabels[i],
                    "console label " + juce::String (i) + " (" + expectedLabels[i] + ")");

    // out-of-range selection must be a harmless no-op
    proc->selectConsole (-1);
    proc->selectConsole (TheChannelStripAudioProcessor::numConsoles);
    expectTrue (proc->getCurrentPluginIdent().isEmpty(), "invalid console index changes nothing");
}

void testPassthroughNoPlugin()
{
    beginTest ("passthrough without a hosted plugin");

    juce::MidiBuffer noMidi;
    juce::AudioBuffer<float> reference;

    // stereo: with nothing hosted, the audio path must be untouched
    {
        TheChannelStripAudioProcessor proc;
        proc.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> block (2, 512);
        SineSource src (1000.0, 48000.0, 0.5f);
        src.fill (block);
        reference.makeCopyOf (block);
        proc.processBlock (block, noMidi);
        expectTrue (maxAbsDifference (block, reference) == 0.0f,
                    "stereo passthrough is bit-exact");
    }

    // mono: the v2.8.2 bridge up-mixes, hosts nothing, down-mixes back -
    // (x + x) / 2 is exact, so this is bit-exact too
    {
        TheChannelStripAudioProcessor proc;
        proc.releaseResources();
        expectTrue (proc.setBusesLayout (makeLayout (true)), "mono layout accepted");
        proc.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> block (1, 512);
        SineSource src (440.0, 48000.0, 0.3f);
        src.fill (block);
        reference.makeCopyOf (block);
        proc.processBlock (block, noMidi);
        expectTrue (maxAbsDifference (block, reference) == 0.0f,
                    "mono passthrough is bit-exact");
    }
}

void testMeasurementGating()
{
    beginTest ("measurement gating (editor closed vs open)");

    // editor closed (the default): processBlock must skip the meters
    {
        TheChannelStripAudioProcessor proc;
        proc.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> block (2, 512);
        SineSource src (1000.0, 48000.0, 0.5f);
        processBlocks (proc, src, 100, block);   // ~1 s of loud signal

        expectTrue (proc.getLufsIn().getShortTermLufs() < -70.0f,
                    "a closed editor measures no loudness");
    }

    // editor open: the same signal must be measured
    {
        TheChannelStripAudioProcessor proc;
        proc.prepareToPlay (48000.0, 512);
        proc.setEditorOpen (true);

        juce::AudioBuffer<float> block (2, 512);
        SineSource src (1000.0, 48000.0, 0.5f);
        processBlocks (proc, src, 100, block);

        expectTrue (proc.getLufsIn().getShortTermLufs() > -70.0f,
                    "an open editor measures loudness");
    }
}

void testLufsShortTerm()
{
    beginTest ("LUFS short-term meter");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEditorOpen (true);

    juce::AudioBuffer<float> block (2, 512);

    // a 1 kHz sine at ~-23 dBFS RMS PER CHANNEL (amplitude 0.1) in BOTH
    // channels, for 4 s (the short-term window is 3 s).
    // v2.18.2 fix: BS.1770 loudness SUMS the mean-square power of every
    // channel, so an identical stereo pair reads ~3 dB louder than a
    // single channel (-23 dBFS/channel -> ~-20 LUFS, not -23). The meter
    // was right and the old expectation was wrong. The harness biquad
    // K-weight gives ~+0.43 dB at 1 kHz (vs ~+0.69 dB for the exact
    // BS.1770 coefficient set), so the expected reading is ~-20.25.
    SineSource src (1000.0, 48000.0, 0.1f);
    processBlocks (proc, src, (int) (4.0 * 48000.0 / 512.0), block);

    const float lufs = proc.getLufsIn().getShortTermLufs();
    expectNear (lufs, -20.0, 2.5,
                "short-term LUFS of a stereo -23 dBFS RMS 1 kHz sine "
                "(BS.1770 channel power sum)");

    // +6 dB: the reading must follow the volume (the v2.8.1 lesson)
    SineSource louder (1000.0, 48000.0, 0.2f);
    processBlocks (proc, louder, (int) (4.0 * 48000.0 / 512.0), block);

    const float lufsLoud = proc.getLufsIn().getShortTermLufs();
    expectNear ((double) lufsLoud - (double) lufs, 6.0, 1.0,
                "short-term LUFS tracks a 6 dB volume change");

    // through the passthrough chain (nothing hosted) the correction is ~0
    float delta = 0.0f;
    expectTrue (proc.getGainCorrectionDb (delta),
                "gain correction readout is valid with signal");
    expectNear (delta, 0.0, 1.0, "gain correction ~0 with nothing hosted");
}

void testSilence()
{
    beginTest ("silence handling");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEditorOpen (true);

    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer noMidi;

    for (int n = 0; n < 100; ++n)   // ~1 s of silence
    {
        fillSilence (block);
        proc.processBlock (block, noMidi);
    }

    expectTrue (proc.getLufsIn().getShortTermLufs() < -70.0f,
                "silent input keeps the meter at its floor");

    float delta = 0.0f;
    expectTrue (! proc.getGainCorrectionDb (delta),
                "no gain correction readout in silence");
}

void testSpectrumPeak()
{
    beginTest ("spectrum analyser peak location");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEditorOpen (true);

    juce::AudioBuffer<float> block (2, 512);

    // 2 s of a 1 kHz tone: the analyser computes one 8192-point frame per
    // ~170 ms, so several frames are available
    SineSource src (1000.0, 48000.0, 0.3f);
    processBlocks (proc, src, (int) (2.0 * 48000.0 / 512.0), block);

    float bins[SpectrumAnalyzer::numScopeBins];
    proc.getAnalyzerIn().copyScopeData (bins, SpectrumAnalyzer::numScopeBins);

    int peakBin = 0;
    for (int b = 1; b < SpectrumAnalyzer::numScopeBins; ++b)
        if (bins[b] > bins[peakBin])
            peakBin = b;

    const double peakHz = proc.getAnalyzerIn().getBinFrequency (peakBin);
    expectTrue (peakHz > 700.0 && peakHz < 1400.0,
                "a 1 kHz tone peaks near 1 kHz (got "
                + juce::String (peakHz, 1) + " Hz)");
}

// v2.20 (points 3/4/5): high-frequency detection. A 10 kHz tone must
// land in the right display bin with the new energy aggregation (the
// old arithmetic mean of the covered FFT bins read the treble low and
// noisy above ~5 kHz, where one display bin spans a dozen+ FFT bins).
void testSpectrumHighFrequency()
{
    beginTest ("spectrum analyser high-frequency peak");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEditorOpen (true);

    juce::AudioBuffer<float> block (2, 512);

    SineSource src (10000.0, 48000.0, 0.3f);
    processBlocks (proc, src, (int) (2.0 * 48000.0 / 512.0), block);

    float bins[SpectrumAnalyzer::numScopeBins];
    proc.getAnalyzerIn().copyScopeData (bins, SpectrumAnalyzer::numScopeBins);

    int peakBin = 0;
    for (int b = 1; b < SpectrumAnalyzer::numScopeBins; ++b)
        if (bins[b] > bins[peakBin])
            peakBin = b;

    const double peakHz = proc.getAnalyzerIn().getBinFrequency (peakBin);
    expectTrue (peakHz > 8500.0 && peakHz < 11500.0,
                "a 10 kHz tone peaks near 10 kHz (got "
                + juce::String (peakHz, 1) + " Hz)");

    // the tone's bin must read well above the empty bins around it (the
    // energy aggregation keeps a genuine tone ~20 dB over the floor)
    expectTrue (bins[peakBin] - bins[0] > 12.0f,
                "the 10 kHz peak stands well above the 20 Hz floor (delta "
                + juce::String (bins[peakBin] - bins[0], 1) + " dB)");
}

// v2.21: the measured EQ curve in weak-content bands. This is the
// offline reproduction of Clement's LP-filtered mixes:
//  - a strong 500 Hz tone plus a weak 10 kHz tone (-26 dB relative,
//    like a mix lowpassed at 9 kHz) through a bit-exact passthrough
//    must read a FLAT 0 dB EQ at BOTH frequencies - the weak band
//    included;
//  - a bass-only tone (a kick with no real HF content) must NOT
//    produce a measured EQ in the treble at all - the relative
//    validity gate (peak - 45 dB, Smaart-style coherence blanking)
//    keeps the curve from reporting noise-over-noise there. The old
//    absolute -80 dB gate let leakage and dither floors through, which
//    is what drew phantom rises above 8 kHz on the real console.
void testEqWeakContent()
{
    beginTest ("EQ curve accuracy in weak-content bands");

    // composite source: two continuous-phase sines in one signal
    struct TwoToneSource
    {
        TwoToneSource (double f1, double a1, double f2, double a2, double sr)
            : w1 (juce::MathConstants<double>::twoPi * f1 / sr),
              w2 (juce::MathConstants<double>::twoPi * f2 / sr),
              amp1 ((float) a1), amp2 ((float) a2)
        {}

        void fill (juce::AudioBuffer<float>& buffer)
        {
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float s = amp1 * (float) std::sin (phase1)
                              + amp2 * (float) std::sin (phase2);
                phase1 += w1;
                phase2 += w2;

                for (int c = 0; c < buffer.getNumChannels(); ++c)
                    buffer.setSample (c, i, s);
            }
        }

        double phase1 = 0.0, phase2 = 0.0, w1, w2;
        float amp1, amp2;
    };

    // the display bin whose band [f_b, f_b+1) CONTAINS the frequency.
    // A tone's energy lands in the band that covers it - which is not
    // always the band whose CENTRE is nearest (at 10 kHz the centres sit
    // ~276 Hz apart, more than half a band away from the tone).
    auto coveringBin = [] (const SpectrumAnalyzer& ana, double freq)
    {
        for (int b = 0; b < SpectrumAnalyzer::numScopeBins - 1; ++b)
            if (ana.getBinFrequency (b) <= freq
                && ana.getBinFrequency (b + 1) > freq)
                return b;

        return SpectrumAnalyzer::numScopeBins - 1;
    };

    {
        TheChannelStripAudioProcessor proc;
        proc.prepareToPlay (48000.0, 512);
        proc.setEditorOpen (true);

        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer noMidi;

        // 4 s: one 8192-point analyser frame per ~170 ms
        TwoToneSource src (500.0, 0.4, 10000.0, 0.02, 48000.0);

        for (int n = 0; n < (int) (4.0 * 48000.0 / 512.0); ++n)
        {
            src.fill (block);
            proc.processBlock (block, noMidi);
        }

        // drive the panel's analysis without a visible editor
        SpectrumPanel panel (proc);

        for (int t = 0; t < 200; ++t)
            panel.runAnalysisTick();

        const int bin500 = coveringBin (proc.getAnalyzerIn(), 500.0);
        const int bin10k = coveringBin (proc.getAnalyzerIn(), 10000.0);

        expectTrue (panel.isEqBinMeasured (bin500),
                    "the strong 500 Hz band is measured");

        expectNear (panel.getEqDbAtBin (bin500), 0.0, 1.5,
                   "the EQ reads flat 0 dB in the strong band (got "
                   + juce::String (panel.getEqDbAtBin (bin500), 2) + " dB)");

        expectTrue (panel.isEqBinMeasured (bin10k),
                    "the weak 10 kHz band (-26 dB relative) is measured");

        expectNear (panel.getEqDbAtBin (bin10k), 0.0, 1.5,
                    "the EQ reads flat 0 dB in the weak treble band (got "
                    + juce::String (panel.getEqDbAtBin (bin10k), 2) + " dB)");
    }

    // negative case: bass-only material (a kick with no real HF). The
    // treble must stay UNMEASURED - no curve, no phantom rise
    {
        TheChannelStripAudioProcessor proc;
        proc.prepareToPlay (48000.0, 512);
        proc.setEditorOpen (true);

        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer noMidi;

        SineSource src (100.0, 48000.0, 0.4f);

        for (int n = 0; n < (int) (4.0 * 48000.0 / 512.0); ++n)
        {
            src.fill (block);
            proc.processBlock (block, noMidi);
        }

        SpectrumPanel panel (proc);

        for (int t = 0; t < 200; ++t)
            panel.runAnalysisTick();

        const int bin10k = coveringBin (proc.getAnalyzerIn(), 10000.0);

        expectTrue (panel.isEqBinMeasured (coveringBin (proc.getAnalyzerIn(), 100.0)),
                    "the bass band of the bass-only tone is measured");

        expectTrue (! panel.isEqBinMeasured (bin10k),
                    "bass-only material leaves the treble unmeasured "
                    "(relative gate, no noise-over-noise EQ)");
    }
}

// v2.22: THE discriminator for the cross-spectrum transfer measurement.
// A console that adds an UNCORRELATED tone (its noise / dither floor, a
// mock of what sits above the SSL J's lowpass) must NOT show up on the
// EQ curve: the complex Sxy accumulation averages the random-phase tone
// away, while the coherence g2 = |Sxy|^2 / (Sxx * Syy) records that the
// output band is only partly explained by the input (g2 < 1). The
// v2.21.1 power ratio Pout/Pin would read the tone as a +3 dB phantom
// boost at 12 kHz; the reference simulation measured exactly that
// failure mode on the old engine and a flat 0 dB (+/- 0.1) with the
// cross-spectrum, with g2 ~ 0.5 at the tone band and 1.0 elsewhere.
void testEqUncorrelatedConsoleNoise()
{
    beginTest ("EQ rejects uncorrelated console noise (cross-spectrum)");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEditorOpen (true);

    // broadband input: every band carries honest energy, so the input
    // gate alone cannot hide the console's tone - only the cross-
    // spectrum can reject it. This is the point of the test.
    NoiseSource src (0.2f);
    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer noMidi;

    auto mock = std::make_unique<MockToneConsole>();
    proc.getHostedStrip().setPlugin (std::move (mock));

    // 6 s (the cross-spectrum EMAs average over the last ~8 frames of
    // ~170 ms; the random-phase tone only cancels across frames)
    for (int n = 0; n < (int) (6.0 * 48000.0 / 512.0); ++n)
    {
        src.fill (block);
        proc.processBlock (block, noMidi);
    }

    SpectrumPanel panel (proc);

    for (int t = 0; t < 200; ++t)
        panel.runAnalysisTick();

    // the display bin whose band [f_b, f_b+1) CONTAINS the frequency
    auto coveringBin = [] (const TransferAnalyzer& ana, double freq)
    {
        for (int b = 0; b < TransferAnalyzer::numScopeBins - 1; ++b)
            if (ana.getBinFrequency (b) <= freq
                && ana.getBinFrequency (b + 1) > freq)
                return b;

        return TransferAnalyzer::numScopeBins - 1;
    };

    const int bin500 = coveringBin (proc.getTransferAnalyzer(), 500.0);
    const int bin12k = coveringBin (proc.getTransferAnalyzer(), 12000.0);

    float eqDb[TransferAnalyzer::numScopeBins];
    float eqCoherence[TransferAnalyzer::numScopeBins];

    proc.getTransferAnalyzer().copyTransferData (eqDb, eqCoherence,
                                                 TransferAnalyzer::numScopeBins);

    // the clean band is fully coherent (the console is a pure passthrough
    // there, so the output is entirely explained by the input)
    expectTrue (eqCoherence[bin500] > 0.9f,
                "the clean band's coherence is ~1 (got "
                + juce::String (eqCoherence[bin500], 3) + ")");

    // the tone band's coherence dips below 1 but stays above the 0.25
    // gate: the tone is present (uncorrelated with the input) yet the
    // band is still measured - and the EQ must IGNORE the tone.
    // Reference simulation: g2 ~ 0.5 at the tone band.
    expectTrue (eqCoherence[bin12k] < 0.9f,
                "the tone band's coherence dips below 1 (the tone is there; got "
                + juce::String (eqCoherence[bin12k], 3) + ")");
    expectTrue (eqCoherence[bin12k] >= 0.25f,
                "the tone band's coherence stays above the validity gate");
    expectTrue (panel.isEqBinMeasured (bin12k),
                "the tone band is measured (both gates pass)");

    expectNear (panel.getEqDbAtBin (bin12k), 0.0, 1.5,
                "the EQ ignores the uncorrelated tone (the old power ratio "
                "read ~ +3 dB there; got "
                + juce::String (panel.getEqDbAtBin (bin12k), 2) + " dB)");

    expectNear (panel.getEqDbAtBin (bin500), 0.0, 1.0,
                "the clean passband reads flat 0 dB (got "
                + juce::String (panel.getEqDbAtBin (bin500), 2) + " dB)");
}

// v2.19 (roadmap point 3): the hover readout's static helpers - the
// inverse frequency mapping and the value formatting - are exercised
// here without a live editor (the panel itself needs the GUI, the math
// does not).
// v2.24 (roadmap point 7): every way a console can be unavailable must be
// REPORTED, not just greyed out. Three distinct situations are driven
// deterministically: nothing scanned (plain missing), a file the scanner
// blacklisted (it crashed the out-of-process helper), and a scanned
// description whose file has since vanished (moved / uninstalled).
void testConsoleSlotStatus()
{
    beginTest ("console slot status: missing / blacklisted / moved file (v2.24, point 7)");

    // The processor's DESTRUCTOR writes the known list back to
    // scanned_plugins.xml, and this test scrubs that list - so the real
    // database file is backed up before the scrub and restored after the
    // instance is destroyed (a machine with a real scan history keeps it).
    TheChannelStripAudioProcessor* proc = new TheChannelStripAudioProcessor();
    proc->prepareToPlay (48000.0, 512);

    const juce::File dbFile = proc->getAppDataDir().getChildFile ("scanned_plugins.xml");
    const juce::File dbBackup = dbFile.getSiblingFile ("scanned_plugins.tcs-test-backup");
    dbBackup.deleteFile();

    const bool backedUp = dbFile.existsAsFile() && dbFile.copyFileTo (dbBackup);

    // scrub the database loaded from disk so the suite is deterministic
    // even on a machine with a real scanned_plugins.xml
    {
        const juce::ScopedLock lock (proc->getScanLock());
        proc->getHostedStrip().getKnownPluginList().clear();
    }

    // 1. nothing installed: every slot is plain missing
    for (const auto& slot : proc->getConsoleSlots())
        expectTrue (! slot.found && ! slot.blacklisted && ! slot.fileMissing,
                    "empty database: slot '" + slot.name + "' is plain missing");

    proc->selectConsole (1);   // bx_console SSL 4000 E
    expectTrue (proc->getHostStatus().contains ("was not found"),
                "clicking a missing console posts the not-found explanation");
    expectTrue (! proc->getHostStatus().startsWith ("ERROR:")
                && ! proc->getHostStatus().startsWith ("WARNING:"),
                "not-found is an explanation, not an error");

    // 2. blacklisted: the scanner recorded a crash for a file that
    //    belongs to the SSL J slot
    proc->getHostedStrip().getKnownPluginList().addToBlacklist (
        "/Library/Audio/Plug-Ins/VST3/bx_console SSL 9000 J.vst3");

    {
        const auto slots = proc->getConsoleSlots();
        expectTrue (slots[3].blacklisted, "a blacklisted file marks the SSL J slot");
        expectTrue (! slots[3].found,     "a blacklisted plugin is not hostable");
        expectTrue (! slots[3].fileMissing,
                    "a blacklisted slot is not confused with a moved file");
    }

    proc->selectConsole (3);
    expectTrue (proc->getHostStatus().contains ("blacklisted"),
                "clicking a blacklisted console says so explicitly");

    // 3. moved file: a ghost description matches the SSL G slot but
    //    points at a file that does not exist on disk
    {
        juce::PluginDescription ghost;
        ghost.name = "bx_console SSL 4000 G (ghost)";
        ghost.pluginFormatName = "VST3";
        ghost.fileOrIdentifier = "/nonexistent/ghost/bx_console SSL 4000 G.vst3";
        ghost.uniqueId = 0x12345678;

        const juce::ScopedLock lock (proc->getScanLock());
        expectTrue (proc->getHostedStrip().getKnownPluginList().addType (ghost),
                    "the ghost description enters the known list");
    }

    {
        const auto slots = proc->getConsoleSlots();
        expectTrue (slots[2].found && slots[2].fileMissing,
                    "a scanned description whose file is gone reports fileMissing");
    }

    proc->selectConsole (2);
    expectTrue (proc->getHostStatus().contains ("no longer exists"),
                "clicking a moved-file console names the dead path");
    expectTrue (proc->getHostStatus().contains ("nonexistent/ghost"),
                "the message quotes the file that no longer exists");

    // the instance dies HERE (its destructor writes the scrubbed list);
    // then the real database is put back exactly as it was
    delete proc;

    if (backedUp)
    {
        expectTrue (dbBackup.copyFileTo (dbFile),
                    "the real scanned_plugins.xml is restored after the test");
        dbBackup.deleteFile();
    }
    else
    {
        dbFile.deleteFile();  // there was no database: leave none behind
    }
}

// v2.26 (roadmap point 4): named presets. The whole file-level
// lifecycle runs against the REAL Presets folder (next to the plugin
// database): save without a hosted console (refused, explicit), save
// with a hosted mock, list, apply on the hosted mock, a missing
// preset, then the v2.29 three actions: Save-to-file anywhere (in and
// out of the preset folder, sub-folders included - the ones inside
// the folder's root become the current preset, the others touch
// nothing), Load from anywhere (loaded into the console, copied into
// the root, becoming the current one), delete of a selected preset,
// browse-delete of an arbitrary file (disowning the current preset
// when its own file goes), and a corrupt file. Only the test's own
// preset files are touched, under a distinctive
// name, and everything is cleaned up again at the end.
void testPresetManager()
{
    beginTest ("preset save / list / load / file save / file delete "
               "(v2.26-v2.30, point 4)");

    const auto unique = juce::String ("Tcs Test Preset 2710");
    const auto uniqueFile = unique + ".tcsPreset";  // the sanitised filename

    // 1. nothing hosted: save is refused with an explicit message
    {
        TheChannelStripAudioProcessor proc;

        expectTrue (! proc.savePreset (unique),
                    "saving a preset without a hosted console fails");
        expectTrue (proc.getHostStatus().contains ("no console is hosted"),
                    "the refusal says why in the status area");
    }

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    const auto rootFile = [&] { return proc.getPresetsDir().getChildFile (uniqueFile); };
    const auto currentFileIs = [&] (const juce::File& f)
    { return proc.getCurrentPresetFile() == f; };

    // clean slate (a leftover from an interrupted earlier run)
    proc.deletePreset (unique);

    // 2. save the hosted console's state (the mock's state = one int)
    auto mock = std::make_unique<MockPresetConsole>();
    mock->value.store (42);
    auto* mockPtr = mock.get();
    proc.getHostedStrip().setPlugin (std::move (mock));

    expectTrue (proc.savePreset (unique), "saving the hosted state as a preset");
    expectTrue (rootFile().existsAsFile(),
                "the preset file exists on disk");
    expectTrue (proc.getCurrentPresetName() == unique
                && currentFileIs (rootFile()),
                "the saved preset becomes the current one, at its file");

    // 3. the list reports it, with the console it belongs to and, v2.30,
    //    its album (empty for a general/root preset)
    {
        const auto presets = proc.getAvailablePresets();
        bool listed = false, consoleOk = false, albumOk = false;

        for (const auto& p : presets)
        {
            if (p.name == unique)
            {
                listed = true;
                consoleOk = p.consoleName == "MockPresetConsole";
                albumOk = p.album.isEmpty();
            }
        }

        expectTrue (listed, "the saved preset is listed");
        expectTrue (consoleOk, "the preset remembers which console it belongs to");
        expectTrue (albumOk, "a root preset has no album");
    }

    // 4. apply: the preset state goes back into the hosted plugin
    mockPtr->value.store (7);   // turn the "console knobs" away from the saved state

    expectTrue (proc.applyPreset (unique), "loading the preset applies it");
    expectTrue (mockPtr->value.load() == 42,
                "the hosted console is back to the saved state");

    // 5. a missing preset is refused explicitly (never a silent no-op)
    expectTrue (! proc.applyPreset ("Tcs Test Preset nonexistent"),
                "loading a preset that does not exist fails");
    expectTrue (proc.getHostStatus().contains ("could not be loaded"),
                "the missing-preset message says so");

    // 6. v2.30 Save-to-file: the CURRENT console state is written to an
    //    arbitrary destination and becomes the current preset AT THAT
    //    FILE (a "Save As" anywhere - even outside the folder - is the
    //    console's new current preset)
    const auto dest = proc.getAppDataDir().getChildFile (uniqueFile);
    dest.deleteFile();

    expectTrue (proc.savePresetTo (dest),
                "saving the current state to a file outside the folder");
    expectTrue (dest.existsAsFile(), "the saved file exists");
    expectTrue (proc.getCurrentPresetName() == unique
                && currentFileIs (dest),
                "a save anywhere becomes the current preset, at that file");

    // 6b. the same save inside a SUB-FOLDER of the preset folder (an
    //     album): the file exists, becomes the current one, AND is now
    //     listed - the v2.30 listing is recursive, albums included
    const auto albumDir = proc.getPresetsDir().getChildFile ("Tcs Test Album");
    const auto destSub = albumDir.getChildFile (uniqueFile);
    albumDir.createDirectory();
    destSub.deleteFile();

    expectTrue (proc.savePresetTo (destSub),
                "saving the current state into a sub-folder");
    expectTrue (destSub.existsAsFile(),
                "the sub-folder file exists");
    expectTrue (currentFileIs (destSub),
                "the album save becomes the current preset, at the album file");
    {
        int rootListed = 0, albumListed = 0;
        bool albumOk = false;

        for (const auto& p : proc.getAvailablePresets())
        {
            if (p.name == unique)
            {
                if (p.album.isEmpty())
                    ++rootListed;
                else if (p.album == "Tcs Test Album")
                {
                    ++albumListed;
                    albumOk = p.file == destSub;
                }
            }
        }

        expectTrue (rootListed == 1,
                    "the root preset is listed once");
        expectTrue (albumListed == 1 && albumOk,
                    "the album preset is listed too, tagged with its album and its file");
    }

    // 7. delete by name: the ROOT file goes, but the current preset -
    //    which now lives in the album - is NOT disowned
    expectTrue (proc.deletePreset (unique), "delete removes the root preset");
    expectTrue (! rootFile().existsAsFile(),
                "the root preset file is gone");

    {
        const auto presets = proc.getAvailablePresets();
        int stillThere = 0;

        for (const auto& p : presets)
            if (p.name == unique)
                ++stillThere;

        expectTrue (stillThere == 1,
                    "only the album copy is still listed");
    }

    expectTrue (proc.getCurrentPresetName() == unique
                && currentFileIs (destSub),
                "deleting the root copy does not disown the album preset");

    // 8. v2.30 Load: LOADS the preset - into the console, as its current
    //    one, at its own file - and NEVER COPIES anything: a file from
    //    outside the folder does not land in the root
    mockPtr->value.store (9);   // turn the "console knobs" away first

    expectTrue (proc.importPreset (dest), "load brings the preset back");
    expectTrue (! rootFile().existsAsFile(),
                "the loaded file was NOT copied into the preset folder");
    expectTrue (mockPtr->value.load() == 42,
                "the loaded preset is loaded into the hosted console");
    expectTrue (proc.getCurrentPresetName() == unique
                && currentFileIs (dest),
                "the loaded preset becomes the console's current one, at its file");
    expectTrue (! proc.isCurrentPresetModified(),
                "a loaded preset starts unmodified");

    // 8b. loading a file that already IS in the preset folder (here: an
    //     album) loads it too, clearing any pending "*" - v2.28
    mockPtr->value.store (11);
    proc.parameterValueChanged (0, 0.5f);   // star the current preset first
    proc.drainPresetDirtyFlag();

    expectTrue (proc.importPreset (destSub),
                "loading an album preset succeeds");
    expectTrue (mockPtr->value.load() == 42,
                "the album preset is loaded into the console");
    expectTrue (proc.getCurrentPresetName() == unique
                && ! proc.isCurrentPresetModified()
                && currentFileIs (destSub),
                "loading makes the preset current at the album file and clears its \"*\"");

    // 8c. v2.30: the current preset's own FILE travels with the session.
    //     Save side: the real session's <consoleState> for the current
    //     preset carries presetFile, relative to the preset folder for
    //     a file inside it (the directly-set mock has an EMPTY ident,
    //     which the save keeps but the restore deliberately skips - a
    //     real hosted console always has a non-empty one), so the
    //     attribute is inspected here and the restore is driven by the
    //     fake session below, exactly like the v2.27 tracking test.
    {
        juce::MemoryBlock sessionBlock;
        proc.getStateInformation (sessionBlock);

        auto xml = juce::AudioProcessor::getXmlFromBinary (sessionBlock.getData(),
                                                           (int) sessionBlock.getSize());
        bool fileSaved = false;

        if (xml != nullptr)
            for (auto* e : xml->getChildIterator())
                if (e->hasTagName ("consoleState")
                    && e->getStringAttribute ("presetName") == unique
                    && e->getStringAttribute ("presetFile")
                           == "Tcs Test Album/" + uniqueFile)
                    fileSaved = true;

        expectTrue (fileSaved,
                    "the album preset's own file is saved with the session,"
                    " relative to the preset folder");

        // restore side: a session that carries the album file restores
        // it, so the combo pre-selects the preset in its album and Save
        // re-fills there (the v2.27 fake-session pattern)
        const auto fakeIdent = juce::String ("VST3|/fake/ssl|bx_console SSL 4000 E");
        auto session = std::make_unique<juce::XmlElement> ("TCS");
        session->setAttribute ("version", "2.36.0");
        session->setAttribute ("hostedIdent", fakeIdent);

        auto* cs = session->createNewChildElement ("consoleState");
        cs->setAttribute ("ident", fakeIdent);
        cs->setAttribute ("state", juce::MemoryBlock (5, true).toBase64Encoding());
        cs->setAttribute ("presetName", unique);
        cs->setAttribute ("presetModified", 0);
        cs->setAttribute ("presetFile", "Tcs Test Album/" + uniqueFile);

        juce::MemoryBlock restoredBlock;
        juce::AudioProcessor::copyXmlToBinary (*session, restoredBlock);

        TheChannelStripAudioProcessor restored;
        restored.setStateInformation (restoredBlock.getData(), (int) restoredBlock.getSize());

        expectTrue (restored.getCurrentPresetName() == unique,
                    "the current preset's name comes back with the session");
        expectTrue (restored.getCurrentPresetFile() == destSub,
                    "so does its own file - the album preset stays an album preset");
    }

    // 8d. browse-to-delete: deleting the current preset's OWN file (the
    //     album file here) removes it and disowns the tracking
    expectTrue (proc.deletePresetFile (destSub),
                "deletePresetFile removes the album file");
    expectTrue (! destSub.existsAsFile(),
                "the album file is gone");
    expectTrue (proc.getCurrentPresetName().isEmpty(),
                "deleting the current preset's own file disowns it");

    // 8e. v2.30: a Save into the ROOT of the preset folder becomes the
    //     console's current preset right away (the general list)
    const auto rootSaveName = juce::String ("Tcs Test Root Save");
    const auto rootSaveFile = proc.getPresetsDir().getChildFile ("Tcs Test Root Save.tcsPreset");

    expectTrue (proc.savePresetTo (rootSaveFile),
                "saving into the root of the preset folder");
    expectTrue (proc.getCurrentPresetName() == rootSaveName
                && ! proc.isCurrentPresetModified()
                && currentFileIs (rootSaveFile),
                "a root save becomes the current preset, unmodified");
    {
        bool listed = false;

        for (const auto& p : proc.getAvailablePresets())
            if (p.name == rootSaveName)
                listed = true;

        expectTrue (listed, "the root save joins the preset list");
    }

    // 9. a corrupt file is refused cleanly, never crashed on
    {
        const auto corrupt = proc.getPresetsDir().getChildFile ("Tcs Corrupt Preset.tcsPreset");
        corrupt.replaceWithText ("this is not a preset file at all");

        expectTrue (! proc.applyPreset ("Tcs Corrupt Preset"),
                    "loading a corrupt preset file fails cleanly");
        expectTrue (! proc.importPreset (corrupt),
                    "loading a corrupt file fails cleanly");

        corrupt.deleteFile();
    }

    // 10. cleanup: leave the preset folder exactly as it was found
    proc.deletePreset (rootSaveName);
    albumDir.deleteFile();   // the (now empty) album folder
    dest.deleteFile();
}

// v2.27: current-preset tracking - the name the combo box mirrors, the
// "*" that appears when a console knob moves, and the session
// roundtrip of both. The parameter listener is driven directly (the
// drain is exactly what the editor timer calls); the mock console is
// only pushed states, exactly like the real ones.
void testCurrentPresetTracking()
{
    beginTest ("current-preset tracking: name + \"*\" + session roundtrip (v2.27)");

    const auto unique = juce::String ("Tcs Test Preset 2727");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.deletePreset (unique); // clean slate from an interrupted run

    auto mock = std::make_unique<MockPresetConsole>();
    mock->value.store (42);
    auto* mockPtr = mock.get();
    proc.getHostedStrip().setPlugin (std::move (mock));

    // 1. nothing tracked yet: no name, no "*"
    expectTrue (proc.getCurrentPresetName().isEmpty(),
                "no preset is associated with the hosted console at first");
    expectTrue (! proc.isCurrentPresetModified(),
                "no preset means no \"*\" marker");

    // 2. a knob move on a console with no preset stays unstarred
    proc.parameterValueChanged (0, 0.5f);
    proc.drainPresetDirtyFlag();

    expectTrue (! proc.isCurrentPresetModified(),
                "a knob move without a current preset does not raise \"*\"");

    // 3. saving makes the preset the console's current one, unmodified
    expectTrue (proc.savePreset (unique), "saving the hosted state as a preset");

    expectTrue (proc.getCurrentPresetName() == unique,
                "a successful save makes the preset the console's current one");
    expectTrue (! proc.isCurrentPresetModified(),
                "a saved preset starts unmodified");

    // 4. a knob move raises the lock-free flag; the drain (the editor
    // timer's job) turns it into the "*"
    proc.parameterValueChanged (0, 0.5f);

    expectTrue (! proc.isCurrentPresetModified(),
                "before the drain, the \"*\" has not landed yet");
    proc.drainPresetDirtyFlag();
    expectTrue (proc.isCurrentPresetModified(),
                "a knob move after the drain marks the preset modified");
    expectTrue (proc.getCurrentPresetName() == unique,
                "the \"*\" does not change the preset's name");

    // 5. reloading the preset clears the "*" (the pushed state is
    // ours, so the listener is muted - and the knobs now match again)
    expectTrue (proc.applyPreset (unique), "reloading the preset applies it");
    expectTrue (mockPtr->value.load() == 42,
                "the console is back to the saved state");
    expectTrue (! proc.isCurrentPresetModified(),
                "reloading the preset clears the \"*\"");
    expectTrue (proc.getCurrentPresetName() == unique,
                "the name survives the reload");

    // 6. session persistence: the name AND the "*" are written as
    // attributes on the console's <consoleState> element
    proc.parameterValueChanged (0, 0.5f);
    proc.drainPresetDirtyFlag();

    juce::MemoryBlock saved;
    proc.getStateInformation (saved);

    std::unique_ptr<juce::XmlElement> xml (
        juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize()));

    expectTrue (xml != nullptr && xml->hasTagName ("TCS"),
                "the saved state parses as TCS XML");

    bool nameSaved = false, starSaved = false;

    if (xml != nullptr)
        for (auto* e : xml->getChildIterator())
            if (e->hasTagName ("consoleState")
                && e->getStringAttribute ("presetName") == unique)
            {
                nameSaved = true;
                starSaved = e->getIntAttribute ("presetModified") == 1;
            }

    expectTrue (nameSaved, "the current preset's name is saved with the session");
    expectTrue (starSaved, "so is its \"*\" state");

    // 7. a session that carries a preset name restores it, so the
    // combo box shows it again on reload
    const auto fakeIdent = juce::String ("VST3|/fake/ssl|bx_console SSL 4000 E");
    auto session = std::make_unique<juce::XmlElement> ("TCS");
    session->setAttribute ("version", "2.36.0");
    session->setAttribute ("hostedIdent", fakeIdent);

    auto* cs = session->createNewChildElement ("consoleState");
    cs->setAttribute ("ident", fakeIdent);
    cs->setAttribute ("state", juce::MemoryBlock (5, true).toBase64Encoding());
    cs->setAttribute ("presetName", unique);
    cs->setAttribute ("presetModified", 1);

    juce::MemoryBlock sessionBlock;
    juce::AudioProcessor::copyXmlToBinary (*session, sessionBlock);

    TheChannelStripAudioProcessor restored;
    restored.setStateInformation (sessionBlock.getData(), (int) sessionBlock.getSize());

    expectTrue (restored.getCurrentPluginIdent() == fakeIdent,
                "the session's hosted ident is restored");
    expectTrue (restored.getCurrentPresetName() == unique,
                "the current preset's name comes back with the session");
    expectTrue (restored.isCurrentPresetModified(),
                "and so does its \"*\" state");

    // 7b. a console whose preset is tracked but whose state block was
    // not saved (it was not instantiated at save time) still carries
    // its preset through a roundtrip - v2.27.1 regression guard.
    auto* cs2 = session->createNewChildElement ("consoleState");
    cs2->setAttribute ("ident", juce::String ("VST3|/fake/other|bx_console SSL 4000 G"));
    cs2->setAttribute ("presetName", juce::String ("Tcs Other Preset"));
    cs2->setAttribute ("presetModified", 1);

    juce::MemoryBlock session2Block;
    juce::AudioProcessor::copyXmlToBinary (*session, session2Block);

    TheChannelStripAudioProcessor restored2;
    restored2.setStateInformation (session2Block.getData(), (int) session2Block.getSize());

    juce::MemoryBlock saved2;
    restored2.getStateInformation (saved2);

    std::unique_ptr<juce::XmlElement> xml2 (
        juce::AudioProcessor::getXmlFromBinary (saved2.getData(), (int) saved2.getSize()));

    bool otherNameSaved = false;

    if (xml2 != nullptr)
        for (auto* e : xml2->getChildIterator())
            if (e->hasTagName ("consoleState")
                && e->getStringAttribute ("ident") == "VST3|/fake/other|bx_console SSL 4000 G"
                && e->getStringAttribute ("presetName") == "Tcs Other Preset"
                && e->getIntAttribute ("presetModified") == 1)
                otherNameSaved = true;

    expectTrue (otherNameSaved,
                "a preset tracked without a console state block survives the roundtrip");

    // 8. deleting the current preset disowns it
    expectTrue (proc.deletePreset (unique), "cleanup: delete the preset");

    expectTrue (proc.getCurrentPresetName().isEmpty(),
                "deleting the current preset clears its name");
    expectTrue (! proc.isCurrentPresetModified(),
                "and its \"*\" state");
}

// v2.24 (roadmap point 7): the silent-console watch. A licence-expired
// console loads but emits digital silence; while the input is loud the
// watch must post exactly ONE explicit warning per hosted instance.
// Negative cases: a healthy passthrough console and a silent input (a
// paused track) must never trigger it. Re-hosting re-arms the watch.
void testSilentConsoleWarning()
{
    beginTest ("silent-console watch, licence-expired stand-in (v2.24, point 7)");

    // positive: loud input into a silent console -> one explicit warning
    {
        TheChannelStripAudioProcessor proc;
        proc.prepareToPlay (48000.0, 512);
        proc.setEditorOpen (true);
        proc.getHostedStrip().setPlugin (std::make_unique<MockSilenceConsole>());

        juce::AudioBuffer<float> block (2, 512);
        SineSource src (1000.0, 48000.0, 0.5f);
        processBlocks (proc, src, 800, block);   // ~8.5 s of loud input

        const auto status = proc.getHostStatus();
        expectTrue (status.startsWith ("WARNING:"),
                    "a silent hosted console while the input is loud is a WARNING");
        expectTrue (status.contains ("produces no output"),
                    "the warning says what it measures");
        expectTrue (status.contains ("licence"),
                    "the warning names the likely cause");
        expectTrue (proc.getSilenceWarningCount() == 1,
                    "the warning fires exactly once per hosted instance");

        // keep feeding: still exactly one
        processBlocks (proc, src, 100, block);
        expectTrue (proc.getSilenceWarningCount() == 1,
                    "continued silence does not re-fire the warning");
    }

    // negative 1: a console that passes the signal through never warns
    {
        TheChannelStripAudioProcessor proc;
        proc.prepareToPlay (48000.0, 512);
        proc.setEditorOpen (true);
        proc.getHostedStrip().setPlugin (std::make_unique<MockConsole> (true));

        juce::AudioBuffer<float> block (2, 512);
        SineSource src (1000.0, 48000.0, 0.5f);
        processBlocks (proc, src, 800, block);

        expectTrue (proc.getSilenceWarningCount() == 0,
                    "a healthy passthrough console never triggers the warning");
    }

    // negative 2: a silent console fed a SILENT input: no warning - the
    // input-active gate must hold even though the output is dead quiet
    {
        TheChannelStripAudioProcessor proc;
        proc.prepareToPlay (48000.0, 512);
        proc.setEditorOpen (true);
        proc.getHostedStrip().setPlugin (std::make_unique<MockSilenceConsole>());

        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer noMidi;

        for (int n = 0; n < 800; ++n)
        {
            fillSilence (block);
            proc.processBlock (block, noMidi);
        }

        expectTrue (proc.getSilenceWarningCount() == 0,
                    "a paused track (silent input) does not trigger the warning");
    }

    // re-arm: swapping consoles resets the watch, so a silent console
    // hosted again warns again
    {
        TheChannelStripAudioProcessor proc;
        proc.prepareToPlay (48000.0, 512);
        proc.setEditorOpen (true);

        juce::AudioBuffer<float> block (2, 512);
        SineSource src (1000.0, 48000.0, 0.5f);

        proc.getHostedStrip().setPlugin (std::make_unique<MockSilenceConsole>());
        processBlocks (proc, src, 800, block);
        expectTrue (proc.getSilenceWarningCount() == 1, "first hosting warns");

        proc.getHostedStrip().setPlugin (std::make_unique<MockConsole> (true));
        processBlocks (proc, src, 100, block);   // ~1 s of healthy pass
        expectTrue (proc.getSilenceWarningCount() == 1, "healthy pass adds no warning");

        proc.getHostedStrip().setPlugin (std::make_unique<MockSilenceConsole>());
        // 1000 blocks (~10.7 s): the output meter's 3 s short-term window
        // still carries the HEALTHY console's last hops after the swap,
        // so the warning needs ~3 s (window flush) + 5 s (watch hold).
        processBlocks (proc, src, 1000, block);
        expectTrue (proc.getSilenceWarningCount() == 2,
                    "re-hosting a silent console warns again (the watch re-arms)");
    }
}

void testHoverReadout()
{
    beginTest ("hover readout mapping and formatting");

    const double width = 900.0;

    // the inverse mapping matches the 16 Hz - 24 kHz log axis at the
    // edges and at the geometric centre 16 * sqrt (1500)
    expectNear (SpectrumPanel::frequencyForX (0.0, width), 16.0, 0.01,
                "x = 0 reads the left edge 16 Hz");
    expectNear (SpectrumPanel::frequencyForX (width, width), 24000.0, 0.01,
                "x = width reads the right edge 24 kHz");
    expectNear (SpectrumPanel::frequencyForX (width / 2.0, width),
                16.0 * std::sqrt (1500.0), 0.01,
                "the geometric centre reads 16*sqrt(1500) Hz");

    // out-of-range x clamps to the axis, and a zero-width panel is safe
    expectNear (SpectrumPanel::frequencyForX (-50.0, width), 16.0, 0.01,
                "x beyond the left edge clamps to 16 Hz");
    expectNear (SpectrumPanel::frequencyForX (width + 50.0, width), 24000.0, 0.01,
                "x beyond the right edge clamps to 24 kHz");
    expectNear (SpectrumPanel::frequencyForX (100.0, 0.0), 16.0, 0.01,
                "a zero-width panel reads the left edge");

    // frequency formatting: integer Hz below 1 kHz, two-decimal kHz above
    expectTrue (SpectrumPanel::formatFrequency (45.0) == "45 Hz",
                "45 Hz formats as \"45 Hz\"");
    expectTrue (SpectrumPanel::formatFrequency (1000.0) == "1.00 kHz",
                "1 kHz formats as \"1.00 kHz\"");
    expectTrue (SpectrumPanel::formatFrequency (12500.0) == "12.50 kHz",
                "12.5 kHz formats as \"12.50 kHz\"");

    // level formatting: one decimal, the -100 dB floor reads "--"
    expectTrue (SpectrumPanel::formatReadoutDb (-47.26f) == "-47.3",
                "levels format with one decimal");
    expectTrue (SpectrumPanel::formatReadoutDb (-120.0f) == "--",
                "the -100 dB floor reads \"--\"");
}

void testStateRoundtrip()
{
    beginTest ("state save/restore roundtrip");

    TheChannelStripAudioProcessor proc;
    proc.setTiltDb (-3.0f);
    proc.setAnaDbDiv (6.0f);
    proc.setEqDbDiv (2.0f);
    proc.setAnalyserVisible (false);
    proc.setSpecTau (0.15f);
    proc.setLufsTarget (-37.0f);

    juce::MemoryBlock saved;
    proc.getStateInformation (saved);
    expectTrue (saved.getSize() > 0, "saved state is not empty");

    auto xml = TheChannelStripAudioProcessor::getXmlFromBinary (saved.getData(),
                                                                (int) saved.getSize());
    expectTrue (xml != nullptr && xml->hasTagName ("TCS"),
                "saved state parses as TCS xml");
    expectTrue (xml != nullptr && xml->getStringAttribute ("version") == "2.36.0",
                "saved state carries the current version tag (2.36.0)");

    TheChannelStripAudioProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());

    expectNear (restored.getTiltDb(), -3.0,  0.01, "tilt restored");
    expectNear (restored.getAnaDbDiv(), 6.0,  0.01, "spectrum dB/div restored");
    expectNear (restored.getEqDbDiv(),  2.0,  0.01, "EQ dB/div restored");
    expectTrue (! restored.getAnalyserVisible(), "analyser visibility restored");
    expectNear (restored.getSpecTau(), 0.15, 0.01, "response speed restored");
    expectNear (restored.getLufsTarget(), -37.0, 0.01, "loudness reference restored");
}

void testLatencyForwarding()
{
    beginTest ("latency forwarding (static and runtime changes)");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> block (2, 512);
    SineSource src (1000.0, 48000.0, 0.4f);

    // nothing hosted: the forwarded latency settles at 0
    processBlocks (proc, src, 4, block);
    expectTrue (proc.getForwardedLatency() == 0,
                "no plugin -> no latency forwarded");

    // inject a mock console that reports 5000 samples
    auto mock = std::make_unique<MockConsole> (true);
    mock->setLatencySamples (5000);
    auto* mockPtr = mock.get();
    proc.getHostedStrip().setPlugin (std::move (mock));

    processBlocks (proc, src, 2, block);
    expectTrue (proc.getForwardedLatency() == 5000,
                "static latency forwarded to the host");
    expectTrue (proc.getLatencySamples() == 5000,
                "the host-visible latency matches");

    // v2.16.1: the console changes its own latency at runtime (the
    // Lindell oversamplers do exactly this); the next block must forward it
    mockPtr->setLatencySamples (7000);
    processBlocks (proc, src, 1, block);
    expectTrue (proc.getForwardedLatency() == 7000,
                "runtime latency change forwarded on the next block");
    expectTrue (proc.getLatencySamples() == 7000,
                "the host-visible latency follows runtime changes");
}

void testCrossfadeClickFree()
{
    beginTest ("click-free console swap");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    juce::MidiBuffer noMidi;

    // console A: identity gain
    auto mockA = std::make_unique<MockConsole> (true);
    proc.getHostedStrip().setPlugin (std::move (mockA));

    // 100 Hz sine, amplitude 0.5: natural inter-sample slew ~0.0065
    SineSource src (100.0, 48000.0, 0.5f);
    juce::AudioBuffer<float> block (2, 512);

    // let the fade-in finish (50 ms = 2400 samples)
    processBlocks (proc, src, 8, block);

    // steady state under A
    src.fill (block);
    proc.processBlock (block, noMidi);
    const float peakA = peakOf (block);
    expectNear (peakA, 0.5, 0.02, "output sits at console A's level before the swap");

    // console B applies -6 dB (gain 0.5)
    auto mockB = std::make_unique<MockConsole> (true);
    mockB->gain.store (0.5f);
    auto* mockBPtr = mockB.get();
    proc.getHostedStrip().setPlugin (std::move (mockB));

    // the swap window: the crossfade is 2400 samples; a hard cut would
    // jump by up to ~0.25, the 50 ms fade must stay at the sine's natural
    // slew. Track the worst inter-sample step across the whole window.
    double maxStep = 0.0;
    float lastSample = 0.0f;
    bool first = true;

    for (int n = 0; n < 10; ++n)   // 5120 samples > 2400
    {
        src.fill (block);
        proc.processBlock (block, noMidi);

        for (int i = 0; i < block.getNumSamples(); ++i)
        {
            const float s = block.getSample (0, i);

            if (! first)
                maxStep = std::max (maxStep, (double) std::abs (s - lastSample));

            lastSample = s;
            first = false;
        }
    }

    expectTrue (maxStep < 0.02,
                "the swap introduces no discontinuity (max step "
                + juce::String (maxStep, 4) + ")");

    expectTrue (mockBPtr->processCallCount.load() > 0,
                "the new console is processing");

    // steady state under B: -6 dB
    processBlocks (proc, src, 3, block);
    src.fill (block);
    proc.processBlock (block, noMidi);
    const float peakB = peakOf (block);
    expectNear (peakB, 0.25, 0.02, "output converges to console B's level after the swap");
}

void testMonoNative()
{
    beginTest ("mono track, native mono layout accepted");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    auto mock = std::make_unique<MockConsole> (true);   // accepts mono
    mock->setLatencySamples (111);
    auto* mockPtr = mock.get();
    proc.getHostedStrip().setPlugin (std::move (mock));

    juce::AudioBuffer<float> block (2, 512);
    SineSource src (250.0, 48000.0, 0.3f);
    processBlocks (proc, src, 8, block);   // fade-in done

    // the track switches to mono (DAW order: release, layout, re-prepare)
    proc.releaseResources();
    expectTrue (proc.setBusesLayout (makeLayout (true)), "mono layout accepted");
    proc.prepareToPlay (48000.0, 512);

    expectTrue (proc.getChannelReadout() == "track: mono (native)",
                "readout reports native mono (got '"
                + proc.getChannelReadout() + "')");

    // mono processing: the console itself must see a 1-channel buffer
    juce::AudioBuffer<float> monoBlock (1, 512);
    juce::AudioBuffer<float> reference;
    juce::MidiBuffer noMidi;

    for (int n = 0; n < 4; ++n)
    {
        src.fill (monoBlock);
        reference.makeCopyOf (monoBlock);
        proc.processBlock (monoBlock, noMidi);
        expectTrue (maxAbsDifference (monoBlock, reference) < 1e-6f,
                    "native mono passthrough is transparent");
    }

    expectTrue (mockPtr->lastChannelCount.load() == 1,
                "the console received a native mono buffer");
    expectTrue (proc.getForwardedLatency() == 111,
                "latency still forwarded in native mono");
}

void testMonoBridge()
{
    beginTest ("mono track, native mono refused -> stereo bridge");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    auto mock = std::make_unique<MockConsole> (false);   // refuses mono
    mock->setLatencySamples (64);
    auto* mockPtr = mock.get();
    proc.getHostedStrip().setPlugin (std::move (mock));

    juce::AudioBuffer<float> block (2, 512);
    SineSource src (250.0, 48000.0, 0.3f);
    processBlocks (proc, src, 8, block);   // fade-in done

    proc.releaseResources();
    expectTrue (proc.setBusesLayout (makeLayout (true)), "mono layout accepted");
    proc.prepareToPlay (48000.0, 512);

    expectTrue (proc.getChannelReadout() == "track: mono (bridge: refused)",
                "readout reports the refused layout (got '"
                + proc.getChannelReadout() + "')");

    // the bridge must feed the console a 2-channel buffer and down-mix
    // the result back; with an identity mock the mono output equals the
    // input bit-exactly ((x + x) / 2 is exact in floating point)
    juce::AudioBuffer<float> monoBlock (1, 512);
    juce::AudioBuffer<float> reference;
    juce::MidiBuffer noMidi;

    for (int n = 0; n < 4; ++n)
    {
        src.fill (monoBlock);
        reference.makeCopyOf (monoBlock);
        proc.processBlock (monoBlock, noMidi);
        expectTrue (maxAbsDifference (monoBlock, reference) == 0.0f,
                    "bridge down-mix is transparent");
    }

    expectTrue (mockPtr->lastChannelCount.load() == 2,
                "the bridged console received a stereo buffer");
    expectTrue (proc.getForwardedLatency() == 64,
                "latency still forwarded over the bridge");
}

void testFormatHotSwitch()
{
    beginTest ("stereo <-> mono hot switch (instance survives)");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    auto mock = std::make_unique<MockConsole> (true);
    mock->setLatencySamples (32);
    auto* mockPtr = mock.get();
    proc.getHostedStrip().setPlugin (std::move (mock));

    juce::AudioBuffer<float> stereoBlock (2, 512);
    juce::AudioBuffer<float> monoBlock  (1, 512);
    SineSource src (500.0, 48000.0, 0.4f);

    processBlocks (proc, src, 8, stereoBlock);   // fade-in done

    const auto* instanceBefore = proc.getHostedStrip().getActivePlugin();
    expectTrue (instanceBefore != nullptr, "a console is hosted");

    // ---- stereo -> mono ----
    proc.releaseResources();
    expectTrue (proc.setBusesLayout (makeLayout (true)), "switch to mono layout");
    proc.prepareToPlay (48000.0, 512);
    processBlocks (proc, src, 4, monoBlock);
    expectTrue (proc.getChannelReadout().startsWith ("track: mono"),
                "mono readout after the switch (got '"
                + proc.getChannelReadout() + "')");

    // ---- mono -> stereo (mid-playback) ----
    proc.releaseResources();
    expectTrue (proc.setBusesLayout (makeLayout (false)), "switch back to stereo");
    proc.prepareToPlay (48000.0, 512);
    processBlocks (proc, src, 4, stereoBlock);
    expectTrue (proc.getChannelReadout() == "track: stereo",
                "stereo readout after switching back");

    const auto* instanceAfter = proc.getHostedStrip().getActivePlugin();
    expectTrue (instanceBefore == instanceAfter,
                "the hosted instance is NEVER re-created across format switches");
    expectTrue (mockPtr->processCallCount.load() > 0,
                "the console kept processing across both switches");
    expectTrue (proc.getForwardedLatency() == 32,
                "latency survived the format switches");
}

// ---------------------------------------------------------------------------
// v2.33 (roadmap ideas 3+4: shell integrity readout + Doctor report)
// ---------------------------------------------------------------------------

// v2.33 (roadmap idea 3): the shell integrity readout. Since v2.34 the
// editor's sidebar shows the merged two-line block instead, but the
// full wording (shell: unity included) still lives in the Doctor
// report, which prints this string verbatim - so the asserts pin the
// wording, not just keywords: unity, crossfade state, forwarded
// latency, sentinel.
void testShellIntegrityReadout()
{
    beginTest ("shell integrity readout (v2.33 bandeau)");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    auto line = proc.getShellIntegrityLine();
    expectTrue (line.contains ("shell: unity"),
                "the readout states the shell is at unity gain");
    expectTrue (line.contains ("crossfade: idle"),
                "no console -> the crossfade reads idle");
    expectTrue (line.contains ("latency: 0"),
                "nothing hosted -> the forwarded latency reads 0");
    expectTrue (line.contains ("sentinel: armed"),
                "the silence sentinel starts armed");

    // a hosted console's latency reaches the readout
    auto mock = std::make_unique<MockConsole> (true);
    mock->setLatencySamples (222);
    proc.getHostedStrip().setPlugin (std::move (mock));

    juce::AudioBuffer<float> block (2, 512);
    SineSource src (1000.0, 48000.0, 0.5f);
    processBlocks (proc, src, 2, block);

    line = proc.getShellIntegrityLine();
    expectTrue (line.contains ("latency: 222"),
                "the hosted console's latency reaches the readout");

    // a swap makes the crossfade read ACTIVE until the fade has run out
    proc.getHostedStrip().setPlugin (std::make_unique<MockConsole> (true));
    expectTrue (proc.getShellIntegrityLine().contains ("crossfade: active"),
                "mid-swap the readout shows an active crossfade");

    processBlocks (proc, src, 12, block);   // 12 x 512 = 6144 samples > 50 ms fade
    expectTrue (proc.getShellIntegrityLine().contains ("crossfade: idle"),
                "the readout settles back to idle once the fade is done");

    // the licence-expired sentinel: a loud-but-silent console flips the
    // readout to "fired"; hosting a new console re-arms it (the watch
    // re-arms on the first block the new console sees)
    proc.setEditorOpen (true);
    proc.getHostedStrip().setPlugin (std::make_unique<MockSilenceConsole>());
    processBlocks (proc, src, 880, block);   // ~9.3 s of loud input (crossfade eats 50 ms)

    expectTrue (proc.getShellIntegrityLine().contains ("sentinel: fired"),
                "a silent console under loud input flips the sentinel");

    proc.getHostedStrip().setPlugin (std::make_unique<MockConsole> (true));
    processBlocks (proc, src, 2, block);
    expectTrue (proc.getShellIntegrityLine().contains ("sentinel: armed"),
                "hosting a fresh console re-arms the sentinel");
}

// v2.33 (roadmap idea 4): the Doctor report - built entirely on the
// message thread from state the harness already exercises; since v2.34
// the heartbeat button next to Rescan shows it (and it is written next
// to the plugin database). The
// asserts pin its key sections and the facts that must travel with a
// support request.
void testDoctorReport()
{
    beginTest ("doctor report (v2.33 logo click)");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    auto report = proc.buildDoctorReport();
    expectTrue (report.contains ("TheChannelStrip 2.36.0"),
                "the report carries the shell version");
    expectTrue (report.contains ("wrapper: "),
                "the report names the wrapper");
    expectTrue (report.contains ("JUCE: "),
                "the report names the JUCE version");
    expectTrue (report.contains ("sample rate: 48000 Hz"),
                "the report states the sample rate");
    expectTrue (report.contains ("block size: 512 samples"),
                "the report states the block size");
    expectTrue (report.contains ("hosted console:")
                && report.contains ("  none"),
                "no console hosted -> the report says none");
    expectTrue (report.contains ("crossfade: idle"),
                "the report carries the integrity crossfade state");
    expectTrue (report.contains ("sentinel: armed"),
                "the report carries the sentinel armed");
    expectTrue (report.contains ("plugin database:")
                && report.contains ("consoles:"),
                "the report has a plugin database section");
    expectTrue (report.contains ("bx_console SSL 4000 E"),
                "every supported console appears in the scan summary");
    expectTrue (report.contains ("presets: "),
                "the report counts the presets on disk");
    expectTrue (report.contains ("console memory (per-console remembered states)"),
                "the report has a console memory section");
    expectTrue (report.contains ("last status message:"),
                "the report ends with the last status message");

    // with a hosted console the report picks up its name and latency
    auto mock = std::make_unique<MockConsole> (true);
    mock->setLatencySamples (222);
    proc.getHostedStrip().setPlugin (std::move (mock));

    juce::AudioBuffer<float> block (2, 512);
    SineSource src (1000.0, 48000.0, 0.5f);
    processBlocks (proc, src, 2, block);

    report = proc.buildDoctorReport();
    expectTrue (report.contains ("name: MockConsole"),
                "the hosted console's name reaches the report");
    expectTrue (report.contains ("latency: 222"),
                "the hosted console's forwarded latency reaches the report");

    // a fired sentinel and the warning it posted both show up
    proc.setEditorOpen (true);
    proc.getHostedStrip().setPlugin (std::make_unique<MockSilenceConsole>());
    processBlocks (proc, src, 880, block);

    report = proc.buildDoctorReport();
    expectTrue (report.contains ("sentinel: fired"),
                "the fired sentinel shows in the report");
    expectTrue (report.contains ("WARNING:") && report.contains ("produces no output"),
                "the report carries the silence warning itself");
    expectTrue (report.contains ("name: MockSilenceConsole"),
                "the report names the currently hosted console");
}

// v2.34 (zone A rework): the merged sidebar status lines - the old
// channel-format readout and the v2.33 integrity bandeau collapsed into
// ONE two-line block with zero redundancy. The asserts pin both lines
// and the fact that the constant "shell: unity" wording is gone (it
// lives in the Doctor report only now).
void testSidebarStatusLines()
{
    beginTest ("sidebar status lines (v2.34 merged block)");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    auto lines = proc.getSidebarStatusLines();
    expectTrue (lines.contains ("track: stereo"),
                "line one carries the channel format");
    expectTrue (lines.contains ("crossfade: idle"),
                "no console -> the crossfade reads idle");
    expectTrue (lines.contains ("latency: 0"),
                "nothing hosted -> the forwarded latency reads 0");
    expectTrue (lines.contains ("sentinel: armed"),
                "the silence sentinel starts armed");
    expectTrue (! lines.contains ("shell: unity"),
                "the constant unity fact moved to the Doctor report only");

    // v2.34.1 (mojibake fix): the separator must be a real UTF-8 middle
    // dot. juce::String (const char*) reads ASCII (one codepoint per
    // byte above 127), which garbled every accent and made the dots read
    // as two glyphs since v2.33 - the fix routes the literals through
    // the direct String (CharPointer_UTF8 (...)) constructor, and these
    // asserts pin that.
    expectTrue (lines.contains (juce::String (juce::CharPointer_UTF8 ("\xc2\xb7"))),
                "the separator is a real UTF-8 middle dot");
    expectTrue (! lines.contains (juce::String (juce::CharPointer_UTF8 ("\xc3\x82"))),
                "no Latin-1 mojibake glyph in the status lines");

    // a hosted console's latency reaches the merged block
    auto mock = std::make_unique<MockConsole> (true);
    mock->setLatencySamples (222);
    proc.getHostedStrip().setPlugin (std::move (mock));

    juce::AudioBuffer<float> block (2, 512);
    SineSource src (1000.0, 48000.0, 0.5f);
    processBlocks (proc, src, 2, block);

    lines = proc.getSidebarStatusLines();
    expectTrue (lines.contains ("latency: 222"),
                "the hosted console's latency reaches the merged block");

    // a swap makes the crossfade read ACTIVE until the fade has run out
    proc.getHostedStrip().setPlugin (std::make_unique<MockConsole> (true));
    expectTrue (proc.getSidebarStatusLines().contains ("crossfade: active"),
                "mid-swap the merged block shows an active crossfade");

    processBlocks (proc, src, 12, block);   // 12 x 512 = 6144 samples > 50 ms fade
    expectTrue (proc.getSidebarStatusLines().contains ("crossfade: idle"),
                "the merged block settles back to idle once the fade is done");

    // the licence-expired sentinel: a loud-but-silent console flips the
    // block to "fired"; hosting a new console re-arms it
    proc.setEditorOpen (true);
    proc.getHostedStrip().setPlugin (std::make_unique<MockSilenceConsole>());
    processBlocks (proc, src, 880, block);   // ~9.3 s of loud input (crossfade eats 50 ms)

    expectTrue (proc.getSidebarStatusLines().contains ("sentinel: fired"),
                "a silent console under loud input flips the sentinel");

    proc.getHostedStrip().setPlugin (std::make_unique<MockConsole> (true));
    processBlocks (proc, src, 2, block);
    expectTrue (proc.getSidebarStatusLines().contains ("sentinel: armed"),
                "hosting a fresh console re-arms the sentinel");
}

// ---------------------------------------------------------------------------
// v2.32 (roadmap: torture tests + realtime-safety audit)
// ---------------------------------------------------------------------------

// The realtime-safety audit (idea 2): every steady-state branch of
// processBlock must run allocation-free. Each scenario warms every
// lazy one-time cost up first (buffer sizing, first FFT frames, first
// latency report), then raises the sentinel flag around a steady-state
// run. No expectTrue may run inside a recording window (its juce::String
// argument allocates); the flag is always lowered before asserting.
void testRealtimeSafetyAllocations()
{
    beginTest ("realtime safety: steady-state processBlock never allocates");

    // Background mock-retirement threads from earlier tests must be
    // done before the first recording window (they only free memory,
    // but the windows must be perfectly quiet).
    juce::Thread::sleep (300);

    // 0) the sentinel itself must be live, otherwise the audit below
    //    would pass vacuously.
    tcsAllocSentinel::count.store (0);
    tcsAllocSentinel::recording.store (true);
    {
        std::vector<int> probe;
        probe.reserve (1000);
    }
    tcsAllocSentinel::recording.store (false);

    expectTrue (tcsAllocSentinel::count.load() > 0,
                "the allocation sentinel intercepts operator new");

    // 1) the FULL stereo path with an open editor: passthrough + hosted
    //    console + both LUFS meters + both spectrum analysers + the
    //    cross-spectrum transfer measurement. This is the heaviest the
    //    shell ever gets.
    {
        TheChannelStripAudioProcessor proc;
        proc.prepareToPlay (48000.0, 256);
        proc.setEditorOpen (true);
        proc.getHostedStrip().setPlugin (std::make_unique<MockConsole> (true));

        juce::AudioBuffer<float> block (2, 256);
        juce::MidiBuffer noMidi;
        SineSource src (1000.0, 48000.0, 0.4f);

        for (int n = 0; n < 120; ++n)   // ~0.64 s of warm-up
        {
            src.fill (block);
            proc.processBlock (block, noMidi);
        }

        tcsAllocSentinel::count.store (0);
        tcsAllocSentinel::recording.store (true);

        for (int n = 0; n < 60; ++n)   // ~0.32 s of steady state
        {
            src.fill (block);
            proc.processBlock (block, noMidi);
        }

        tcsAllocSentinel::recording.store (false);

        expectTrue (tcsAllocSentinel::count.load() == 0,
                    "stereo path, editor open: no allocation in steady state");

        // editor closed: the measurement path is skipped entirely, so
        // the same steady state must be just as clean.
        proc.setEditorOpen (false);

        tcsAllocSentinel::count.store (0);
        tcsAllocSentinel::recording.store (true);

        for (int n = 0; n < 20; ++n)
        {
            src.fill (block);
            proc.processBlock (block, noMidi);
        }

        tcsAllocSentinel::recording.store (false);

        expectTrue (tcsAllocSentinel::count.load() == 0,
                    "stereo path, editor closed: no allocation in steady state");

        // 2) the native-mono path (mono track + a console that
        //    accepted the mono layout) - same scope, same instance.
        proc.releaseResources();
        proc.setBusesLayout (makeLayout (true));
        proc.prepareToPlay (48000.0, 256);

        juce::AudioBuffer<float> monoBlock (1, 256);

        for (int n = 0; n < 20; ++n)   // warm-up: mono scratch buffers
        {
            src.fill (monoBlock);
            proc.processBlock (monoBlock, noMidi);
        }

        tcsAllocSentinel::count.store (0);
        tcsAllocSentinel::recording.store (true);

        for (int n = 0; n < 30; ++n)
        {
            src.fill (monoBlock);
            proc.processBlock (monoBlock, noMidi);
        }

        tcsAllocSentinel::recording.store (false);

        expectTrue (tcsAllocSentinel::count.load() == 0,
                    "native mono path: no allocation in steady state");
    }

    // 3) the mono BRIDGE path (a console that refused the native mono
    //    layout: the stereo bridge up/down-mixes every block).
    {
        TheChannelStripAudioProcessor proc;
        proc.prepareToPlay (48000.0, 256);
        proc.setEditorOpen (true);
        proc.getHostedStrip().setPlugin (std::make_unique<MockConsole> (false));

        proc.releaseResources();
        proc.setBusesLayout (makeLayout (true));
        proc.prepareToPlay (48000.0, 256);

        juce::AudioBuffer<float> monoBlock (1, 256);
        juce::MidiBuffer noMidi;
        SineSource src (1000.0, 48000.0, 0.4f);

        for (int n = 0; n < 20; ++n)   // warm-up: the bridge sizes itself
        {
            src.fill (monoBlock);
            proc.processBlock (monoBlock, noMidi);
        }

        tcsAllocSentinel::count.store (0);
        tcsAllocSentinel::recording.store (true);

        for (int n = 0; n < 30; ++n)
        {
            src.fill (monoBlock);
            proc.processBlock (monoBlock, noMidi);
        }

        tcsAllocSentinel::recording.store (false);

        expectTrue (tcsAllocSentinel::count.load() == 0,
                    "mono bridge path: no allocation in steady state");
    }

    // 4) no console hosted at all (the pure shell + measurement path).
    {
        TheChannelStripAudioProcessor proc;
        proc.prepareToPlay (48000.0, 256);
        proc.setEditorOpen (true);

        juce::AudioBuffer<float> block (2, 256);
        juce::MidiBuffer noMidi;
        SineSource src (1000.0, 48000.0, 0.4f);

        for (int n = 0; n < 120; ++n)   // warm-up: first FFT frames
        {
            src.fill (block);
            proc.processBlock (block, noMidi);
        }

        tcsAllocSentinel::count.store (0);
        tcsAllocSentinel::recording.store (true);

        for (int n = 0; n < 60; ++n)
        {
            src.fill (block);
            proc.processBlock (block, noMidi);
        }

        tcsAllocSentinel::recording.store (false);

        expectTrue (tcsAllocSentinel::count.load() == 0,
                    "no console hosted: no allocation in steady state");
    }
}

// What a user flipping console buttons as fast as the UI allows does
// (idea 1): a new plugin every two blocks, so the 50 ms crossfade is
// interrupted mid-flight over and over, plus a full clear in the middle
// of the storm. The strip must keep producing sane bounded audio,
// never lose the hosted slot, and converge on the last console.
void testTortureConsoleSwapStorm()
{
    beginTest ("torture: console swap storm during playback");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEditorOpen (true);   // stress the measurement path too

    juce::AudioBuffer<float> block (2, 512);
    SineSource src (250.0, 48000.0, 0.4f);

    MockConsole* lastMock = nullptr;

    for (int n = 0; n < 50; ++n)
    {
        auto mock = std::make_unique<MockConsole> (true);
        mock->gain.store ((n % 2 == 0) ? 1.0f : 0.5f);

        if (n == 49)
            lastMock = mock.get();

        proc.getHostedStrip().setPlugin (std::move (mock));
        processBlocks (proc, src, 2, block);

        // one full clear mid-storm: the strip must survive hosting
        // nothing for a moment, then go straight back to swapping
        if (n == 24)
            proc.clearHostedPlugin();
    }

    expectTrue (proc.getHostedStrip().getActivePlugin() == lastMock,
                "after the storm the last console is hosted");

    // 100 blocks = the last interrupted crossfade completes and the
    // output must have converged on the last console's gain (0.5).
    processBlocks (proc, src, 100, block);

    expectTrue (lastMock->lastChannelCount.load() == 2,
                "the last console was fed a stereo buffer throughout");
    expectNear ((double) peakOf (block), 0.4 * 0.5, 0.01,
                "the output converged on the last console (peak ~0.2)");
    expectTrue (lastMock->processCallCount.load() >= 100,
                "the last console processed every settling block");

    // a full clear at the end must land on EXACT dry passthrough
    proc.clearHostedPlugin();
    processBlocks (proc, src, 12, block);   // the outgoing fade (~5 blocks)

    juce::AudioBuffer<float> reference;
    juce::MidiBuffer noMidi;
    src.fill (block);
    reference.makeCopyOf (block);
    proc.processBlock (block, noMidi);

    expectTrue (maxAbsDifference (block, reference) == 0.0f,
                "after clearing, the shell is exactly transparent again");
    expectTrue (proc.getHostedStrip().getActivePlugin() == nullptr,
                "no console is hosted after the clear");

    // and the strip must be fully serviceable right after the storm
    auto fresh = std::make_unique<MockConsole> (true);
    fresh->gain.store (0.25f);
    auto* freshPtr = fresh.get();
    proc.getHostedStrip().setPlugin (std::move (fresh));

    processBlocks (proc, src, 100, block);

    expectNear ((double) peakOf (block), 0.4 * 0.25, 0.01,
                "a console hosted after the storm behaves normally");
    expectTrue (freshPtr->lastChannelCount.load() == 2,
                "the post-storm console received a stereo buffer");
}

// A host that keeps changing its mind mid-session (idea 1): sample
// rate, block size and track format all churn while the same console
// instance keeps playing. The instance must NEVER be re-created, the
// readout must always tell the truth, and the audio must stay bounded.
void testTortureFormatAndRateChurn()
{
    beginTest ("torture: sample-rate / block-size / format churn");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    auto mock = std::make_unique<MockConsole> (true);   // accepts native mono
    mock->setLatencySamples (222);
    proc.getHostedStrip().setPlugin (std::move (mock));

    juce::AudioBuffer<float> block (2, 512);
    SineSource src (250.0, 48000.0, 0.4f);
    processBlocks (proc, src, 8, block);   // fade-in done

    const auto* instanceBefore = proc.getHostedStrip().getActivePlugin();
    expectTrue (instanceBefore != nullptr, "a console is hosted before the churn");

    struct Leg { double rate; int blockSize; bool mono; };
    const Leg legs[] = {
        { 44100.0,   64, false },
        { 96000.0, 2048, false },
        { 48000.0,    1, false },
        { 48000.0,  512, true  },
        { 88200.0,  128, true  },
        { 48000.0,  512, false }
    };

    int legsDone = 0;
    juce::MidiBuffer noMidi;

    for (const auto& leg : legs)
    {
        juce::AudioBuffer<float> legBlock (leg.mono ? 1 : 2, leg.blockSize);
        SineSource legSrc (250.0, leg.rate, 0.4f);

        // enough audio per leg for a reliable peak measurement even at
        // a block size of 1
        const int legBlocks = juce::jmax (10, 2048 / leg.blockSize);

        proc.releaseResources();
        expectTrue (proc.setBusesLayout (makeLayout (leg.mono)),
                    "layout switch accepted (leg " + juce::String (legsDone) + ")");
        proc.prepareToPlay (leg.rate, leg.blockSize);

        float legPeak = 0.0f;

        for (int n = 0; n < legBlocks; ++n)
        {
            legSrc.fill (legBlock);
            proc.processBlock (legBlock, noMidi);
            legPeak = std::max (legPeak, peakOf (legBlock));
        }

        const bool readoutOk = leg.mono
            ? proc.getChannelReadout().startsWith ("track: mono")
            : proc.getChannelReadout() == "track: stereo";

        expectTrue (readoutOk,
                    "readout truthful after the leg (got '"
                    + proc.getChannelReadout() + "')");
        expectTrue (legPeak > 0.3f && legPeak < 1.0f,
                    "audio stayed alive and bounded through the leg");
        ++legsDone;
    }

    expectTrue (proc.getHostedStrip().getActivePlugin() == instanceBefore,
                "the hosted instance was NEVER re-created through the churn");
    expectTrue (proc.getForwardedLatency() == 222,
                "latency forwarding survived the churn");
}

// Hostile session material (idea 1): the DAW (or a corrupted project
// file) feeds the shell garbage states of every flavour, including a
// valid-XML session whose huge fake console states name a supported
// console on a path that is not scanned. Nothing may crash, the hosted
// console must keep processing throughout, and a final clean roundtrip
// must bring the shell back to a consistent state.
void testTortureHostileSessions()
{
    beginTest ("torture: hostile session states during playback");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEditorOpen (true);

    auto mock = std::make_unique<MockConsole> (false);
    auto* mockPtr = mock.get();
    proc.getHostedStrip().setPlugin (std::move (mock));

    juce::AudioBuffer<float> block (2, 512);
    SineSource src (500.0, 48000.0, 0.4f);
    processBlocks (proc, src, 8, block);

    const auto* instanceBefore = proc.getHostedStrip().getActivePlugin();
    const int callsBefore = mockPtr->processCallCount.load();
    expectTrue (instanceBefore != nullptr, "a console is hosted before the abuse");

    // 1) five rapid save/restore roundtrips mid-playback
    juce::MemoryBlock saved;
    proc.getStateInformation (saved);

    for (int n = 0; n < 5; ++n)
    {
        proc.setStateInformation (saved.getData(), (int) saved.getSize());
        processBlocks (proc, src, 2, block);
    }

    expectTrue (proc.getHostedStrip().getActivePlugin() == instanceBefore,
                "the hosted instance survives rapid session roundtrips");
    expectTrue (mockPtr->processCallCount.load() > callsBefore,
                "audio kept flowing through the session roundtrips");

    // 2) garbage bytes
    const char garbage[] = "MUSDKASKELDJFNEFGARBAGE!1234NONSENSE";
    proc.setStateInformation (garbage, (int) (sizeof (garbage) - 1));
    processBlocks (proc, src, 2, block);

    // 3) zero-size state
    proc.setStateInformation (saved.getData(), 0);
    processBlocks (proc, src, 2, block);

    // 4) a truncated copy of the REAL saved state (cut mid-stream, so
    //    the XML text inside the magic-number container is corrupt)
    juce::MemoryBlock truncated (saved.getData(), saved.getSize() / 3);
    proc.setStateInformation (truncated.getData(), (int) truncated.getSize());
    processBlocks (proc, src, 2, block);

    expectTrue (proc.getHostedStrip().getActivePlugin() == instanceBefore,
                "the hosted instance survives garbage / empty / truncated states");
    expectTrue (mockPtr->processCallCount.load() > callsBefore + 6,
                "audio kept flowing through the hostile states");

    // 5) valid XML, hostile content: a 100 KB fake state per attribute
    //    (an ~1 MB session), naming a SUPPORTED console on a path that
    //    is never scanned (the v2.27 fake-ident pattern - the re-hosting
    //    is attempted and politely refused, never crashes).
    {
        const auto bigB64 = juce::String::repeatedString ("A", 100000);

        auto session = std::make_unique<juce::XmlElement> ("TCS");
        session->setAttribute ("version", "999.999.999");
        session->setAttribute ("hostedIdent", "VST3|/nowhere/fake|bx_console SSL 4000 E");
        session->setAttribute ("hostedState", bigB64);

        for (int i = 0; i < 9; ++i)
        {
            auto* e = session->createNewChildElement ("consoleState");
            e->setAttribute ("ident", "VST3|/nowhere/fake" + juce::String (i)
                                + "|bx_console SSL 4000 G");
            e->setAttribute ("state", bigB64);
            e->setAttribute ("presetName", "Hostile Preset " + juce::String (i));
            e->setAttribute ("presetModified", 1);
        }

        juce::MemoryBlock hostileBlock;
        juce::AudioProcessor::copyXmlToBinary (*session, hostileBlock);
        proc.setStateInformation (hostileBlock.getData(),
                                  (int) hostileBlock.getSize());
        processBlocks (proc, src, 2, block);
    }

    expectTrue (proc.getHostedStrip().getActivePlugin() == instanceBefore,
                "the hosted instance survives the huge hostile session");

    // 6) a final clean roundtrip must restore a consistent shell: the
    //    mock still hosted, still processing, ident back to clean.
    proc.setStateInformation (saved.getData(), (int) saved.getSize());
    processBlocks (proc, src, 4, block);

    expectTrue (proc.getHostedStrip().getActivePlugin() == instanceBefore,
                "the hosted instance is intact after the final roundtrip");
    expectTrue (proc.getCurrentPluginIdent().isEmpty(),
                "the clean session restored a clean hosted ident");
    expectTrue (mockPtr->processCallCount.load() > callsBefore + 14,
                "the console kept processing through the entire abuse");
    expectNear ((double) peakOf (block), 0.4, 0.02,
                "the output is still the expected signal at the end");
}

void testIntegrationRealConsole()
{
    beginTest ("integration: real console from the saved plugin database");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    const auto slots = proc.getConsoleSlots();
    const TheChannelStripAudioProcessor::ConsoleSlot* found = nullptr;

    for (const auto& s : slots)
        if (s.found)
        {
            found = &s;
            break;
        }

    if (found == nullptr)
    {
        std::cout << "        SKIP: no supported console in the saved plugin "
                     "database (launch the Standalone once to scan)" << std::endl;
        return;
    }

    std::cout << "        hosting '" << found->desc.name << "'" << std::endl;

    juce::String error;
    auto instance = proc.getHostedStrip().getFormatManager()
                        .createPluginInstance (found->desc, 48000.0, 512, error);

    if (instance == nullptr)
    {
        std::cout << "        SKIP: cannot instantiate (" << error << ")" << std::endl;
        return;
    }

    const int pluginLatency = instance->getLatencySamples();
    proc.getHostedStrip().setPlugin (std::move (instance));

    juce::AudioBuffer<float> stereoBlock (2, 512);
    juce::AudioBuffer<float> monoBlock  (1, 512);
    SineSource src (1000.0, 48000.0, 0.3f);

    processBlocks (proc, src, 100, stereoBlock);   // ~1 s stereo
    expectTrue (proc.getForwardedLatency() == pluginLatency,
                "the real console's latency is forwarded");

    // mono: native or bridge, both are acceptable - log which one won
    proc.releaseResources();
    proc.setBusesLayout (makeLayout (true));
    proc.prepareToPlay (48000.0, 512);
    processBlocks (proc, src, 100, monoBlock);   // ~1 s mono

    std::cout << "        mono path: '" << proc.getChannelReadout() << "'" << std::endl;
    expectTrue (proc.getChannelReadout().startsWith ("track: mono ("),
                "a mono path was negotiated (native or bridge)");

    // back to stereo mid-playback
    proc.releaseResources();
    proc.setBusesLayout (makeLayout (false));
    proc.prepareToPlay (48000.0, 512);
    processBlocks (proc, src, 100, stereoBlock);
    expectTrue (proc.getChannelReadout() == "track: stereo",
                "stereo path after switching back");

    // clearing must return to transparent dry passthrough
    proc.clearHostedPlugin();
    juce::AudioBuffer<float> reference;
    src.fill (stereoBlock);
    reference.makeCopyOf (stereoBlock);
    juce::MidiBuffer noMidi;
    proc.processBlock (stereoBlock, noMidi);
    expectTrue (maxAbsDifference (stereoBlock, reference) == 0.0f,
                "dry passthrough after clearing the console");
}
} // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main (int argc, char* argv[])
{
    const bool integration = (argc > 1 && juce::String (argv[1]) == "--integration");

    // The hosting code (formats, plugin instantiation) needs the JUCE
    // messaging system. A console app provides its own main, so the RAII
    // initialiser here sets it up and tears it down on return.
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "TheChannelStrip offline test harness - v2.36.0" << std::endl;
    std::cout << "---------------------------------------------" << std::endl;

    testProcessorBasics();
    testPassthroughNoPlugin();
    testMeasurementGating();
    testLufsShortTerm();
    testSilence();
    testSpectrumPeak();
    testSpectrumHighFrequency();
    testEqWeakContent();
    testEqUncorrelatedConsoleNoise();
    testConsoleSlotStatus();
    testPresetManager();
    testCurrentPresetTracking();
    testSilentConsoleWarning();
    testHoverReadout();
    testStateRoundtrip();
    testLatencyForwarding();
    testCrossfadeClickFree();
    testMonoNative();
    testMonoBridge();
    testFormatHotSwitch();
    testShellIntegrityReadout();
    testDoctorReport();
    testSidebarStatusLines();
    testRealtimeSafetyAllocations();
    testTortureConsoleSwapStorm();
    testTortureFormatAndRateChurn();
    testTortureHostileSessions();

    if (integration)
        testIntegrationRealConsole();
    else
        std::cout << "(pass --integration to also host a real console "
                     "from the saved plugin database)" << std::endl;

    std::cout << "---------------------------------------------" << std::endl;
    std::cout << (numChecks - numFailed) << " / " << numChecks
              << " checks passed, " << numFailed << " failed" << std::endl;

    // HostedSlot teardown: plugins swapped out during the tests are
    // destroyed on background threads after a 150 ms grace period. Give
    // them time to finish BEFORE the JUCE shutdown (the initialiser's
    // destructor), so no mock is deleted after JUCE has gone away.
    juce::Thread::sleep (400);

    if (numFailed == 0)
    {
        std::cout << "ALL TESTS PASSED" << std::endl;
        return 0;
    }

    return 1;
}
