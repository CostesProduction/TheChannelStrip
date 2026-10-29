// ---------------------------------------------------------------------------
// tests/TcsTestsMain.cpp - v2.51.0 offline test harness (roadmap point 8)
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
//   - EQ curve (v2.47.0 -> v2.49.0, Clement's A/B: ONE curve, its
//     estimator under comparison - v2.48 drew the max-hold blend,
//     v2.49 draws the PURE optimised H1 cross-spectrum member "sans
//     mon idée de Max Hold"): the single curve reads flat in
//     passthrough, reacts within ~1 s to a knob turned through the
//     REAL hosted parameter (the flush + fast post-flush EMA), and
//     persists after the signal stops (v2.12 gate, unchanged); a
//     knob DOWN also re-converges within ~2 s (the flush works in
//     BOTH directions - the discriminator vs the max-hold blend,
//     which held the old response up to 5 s);
//   - EQ curve frequency smoothing (v2.43): the kernel kills the
//     band-scale jitter of the raw per-band estimates while the
//     passthrough still reads flat 0 dB
//   - EQ curve under a low cut over bursty bass (v2.50, the comb
//     regression): a mock console whose 24 dB/oct low cut at 300 Hz
//     is the benchmark's filter A, a bursty bass-with-harmonics
//     programme, and the measured curve asserted against the filter's
//     ANALYTIC response in the 60-213 Hz zone - the v2.44-v2.49
//     smoothing-on-accumulators measured ~9 dB RMS / 23 dB max comb
//     teeth there, the v2.50 kernel-after-the-ratio reads within
//     ~1 dB of the truth
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
//   - v2.51 scale ladder: legacy 10/6 and 4/2 dB/div session values
//     are mapped to the new 15/12/9 and 8/6/4/2 ladders on load
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

#include <algorithm>
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

    // v2.43 (testEqCurveSmoothing): OPTIONAL deterministic band-limited
    // noise injection - a console's shaped noise floor living INSIDE
    // the console, uncorrelated with the input. A fixed-seed xorshift
    // white source through a 2 kHz RBJ band-pass (Q 0.5), added
    // identically to every channel so the mono mixdown keeps its full
    // level. Calibrated against NoiseSource (0.3): a gain of 0.3 puts
    // the in-band noise PSD equal to the input PSD (per-band coherence
    // ~0.5) - the reference simulation's console-noise scenario, on
    // which the RAW per-band estimates jitter ~2 dB band-to-band.
    // OFF by default: every existing mock-based test is untouched.
    void setBandNoise (float gainToUse) { bandNoiseGain.store (gainToUse); }

    // v2.50.0 (testEqLowcutCombRegression): OPTIONAL 24 dB/oct low cut
    // at 300 Hz INSIDE the console - two cascaded RBJ high-pass
    // biquads (Q 0.5412 / 1.3066 = the two sections of a 4th-order
    // Butterworth). This is the exact filter of the 2026-10-29
    // offline benchmark's scenario A, the one that reproduced
    // Clement's screenshot comb (~11 dB peak-to-peak teeth between
    // 60-213 Hz with the v2.44-v2.49 smoothing-on-accumulators).
    // The analytic magnitude is reproduced by lowcutMagnitudeDb()
    // below, so the test asserts the measured EQ curve against the
    // TRUE response. OFF by default: every existing mock-based test
    // is untouched.
    void setLowcut (bool on) { lowcutOn.store (on); }

    const juce::String getName() const override { return "MockConsole"; }

    void prepareToPlay (double sampleRate, int) override
    {
        // the 2 kHz band-pass (RBJ cookbook, b1 = 0): coefficients are
        // normalised here so the audio loop stays a 4-tap recurrence
        const double w0 = juce::MathConstants<double>::twoPi * 2000.0 / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * 0.5);
        const double a0 = 1.0 + alpha;

        nb0 = (float) (alpha / a0);
        nb2 = (float) (-alpha / a0);
        na1 = (float) (-2.0 * std::cos (w0) / a0);
        na2 = (float) ((1.0 - alpha) / a0);

        // deterministic from run to run: fixed seed, zeroed filter state
        noiseState = 0x1B7ED9Bu;
        noiseX1 = noiseX2 = noiseY1 = noiseY2 = 0.0f;

        // the v2.50 low cut: RBJ high-pass coefficients for both
        // sections (fc 300 Hz, Q 0.5412 / 1.3066), normalised so the
        // audio loop is a plain 5-tap recurrence per section
        for (int s = 0; s < 2; ++s)
        {
            const double q = (s == 0) ? 0.54119610 : 1.30656296;
            const double w0 = juce::MathConstants<double>::twoPi * 300.0
                              / sampleRate;
            const double alpha = std::sin (w0) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            const double cosw0 = std::cos (w0);

            lcb0[s] = (float) ((1.0 + cosw0) * 0.5 / a0);
            lcb1[s] = (float) (-(1.0 + cosw0) / a0);
            lcb2[s] = lcb0[s];
            lca1[s] = (float) (-2.0 * cosw0 / a0);
            lca2[s] = (float) ((1.0 - alpha) / a0);

            for (int c = 0; c < 2; ++c)
                lcX1[s][c] = lcX2[s][c] = lcY1[s][c] = lcY2[s][c] = 0.0f;
        }
    }

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

        const float noiseGain = bandNoiseGain.load();

        if (noiseGain > 0.0f)
        {
            const int numSamples = buffer.getNumSamples();

            for (int i = 0; i < numSamples; ++i)
            {
                noiseState ^= noiseState << 13;
                noiseState ^= noiseState >> 17;
                noiseState ^= noiseState << 5;

                const float white = noiseGain
                    * ((float) ((double) noiseState / 2147483648.0) - 1.0f);
                const float y = nb0 * white + nb2 * noiseX2
                                  - na1 * noiseY1 - na2 * noiseY2;

                noiseX2 = noiseX1; noiseX1 = white;
                noiseY2 = noiseY1; noiseY1 = y;

                for (int c = 0; c < buffer.getNumChannels(); ++c)
                    buffer.setSample (c, i, buffer.getSample (c, i) + y);
            }
        }

        // the v2.50 low cut: two cascaded high-pass sections per channel
        if (lowcutOn.load())
        {
            const int numSamples = buffer.getNumSamples();

            for (int c = 0; c < buffer.getNumChannels(); ++c)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    float x = buffer.getSample (c, i);

                    for (int s = 0; s < 2; ++s)
                    {
                        const float y = lcb0[s] * x + lcb1[s] * lcX1[s][c]
                                      + lcb2[s] * lcX2[s][c]
                                      - lca1[s] * lcY1[s][c]
                                      - lca2[s] * lcY2[s][c];

                        lcX2[s][c] = lcX1[s][c];
                        lcX1[s][c] = x;
                        lcY2[s][c] = lcY1[s][c];
                        lcY1[s][c] = y;
                        x = y;
                    }

                    buffer.setSample (c, i, x);
                }
            }
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
    const bool acceptsMono;

    std::atomic<float> bandNoiseGain { 0.0f };
    juce::uint32 noiseState = 0x1B7ED9Bu;
    float nb0 = 0.0f, nb2 = 0.0f, na1 = 0.0f, na2 = 0.0f;
    float noiseX1 = 0.0f, noiseX2 = 0.0f, noiseY1 = 0.0f, noiseY2 = 0.0f;

    // v2.50.0: the optional 24 dB/oct low cut (see setLowcut)
    std::atomic<bool> lowcutOn { false };
    float lcb0[2] = { 0.0f, 0.0f }, lcb1[2] = { 0.0f, 0.0f };
    float lcb2[2] = { 0.0f, 0.0f }, lca1[2] = { 0.0f, 0.0f };
    float lca2[2] = { 0.0f, 0.0f };
    float lcX1[2][2] = {}, lcX2[2][2] = {};
    float lcY1[2][2] = {}, lcY2[2][2] = {};

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
// MockParamConsole (v2.47.0): a passthrough console whose gain knob is a
// REAL AudioProcessorParameter - the thing the v2.46 mocks' plain atomics
// cannot express. The shell's parameter relay (HostedStrip) attaches to
// every hosted parameter, so setValueNotifyingHost must travel the full
// production path: parameter -> relay -> TransferAnalyzer::requestFlush.
// That flush is the reactivity half of the long 5 s averaging window -
// without it the EQ-Verite curve would take ~9 s to read a knob turn.
// ---------------------------------------------------------------------------
class MockParamConsole : public juce::AudioProcessor
{
public:
    MockParamConsole()
        : AudioProcessor (BusesProperties()
                            .withInput  ("Main", juce::AudioChannelSet::stereo(), true)
                            .withOutput ("Main", juce::AudioChannelSet::stereo(), true))
    {
        gainParam = new juce::AudioParameterFloat (
            juce::ParameterID { "gain", 1 }, "Gain",
            juce::NormalisableRange<float> (0.0f, 4.0f), 1.0f);
        addParameter (gainParam);
    }

    // the knob the test turns - a hosted parameter like a real
    // console's output knob, not a test back door
    juce::AudioParameterFloat* gainParam = nullptr;

    const juce::String getName() const override { return "MockParamConsole"; }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet()
                && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.applyGain (gainParam->get());
    }

    double getTailLengthSeconds() const override { return 0.0; }
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
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MockParamConsole)
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

// v2.41 (Clement's remarks 2/3): the spectrum analysers answer to a
// STRICTER gate than the LUFS meters - they stop when the analyser
// strip is hidden (a hidden strip has nobody to feed), while the LUFS
// meters stay on the editor-open gate alone (they feed the gain
// staging readout and the v2.24 silent-console watch). Re-showing the
// strip must restart the spectra from a clean slate.
void testAnalyserGating()
{
    beginTest ("analyser gating (strip hidden: spectra frozen, LUFS alive)");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEditorOpen (true);

    juce::AudioBuffer<float> block (2, 512);
    SineSource src (1000.0, 48000.0, 0.5f);

    // ~1 s with the strip shown: several 8192-point frames computed
    processBlocks (proc, src, 100, block);

    float bins[SpectrumAnalyzer::numScopeBins];
    proc.getAnalyzerIn().copyScopeData (bins, SpectrumAnalyzer::numScopeBins);

    float peakShown = bins[0];
    for (int b = 1; b < SpectrumAnalyzer::numScopeBins; ++b)
        peakShown = juce::jmax (peakShown, bins[b]);

    expectTrue (peakShown > -80.0f,
                "with the strip shown the spectrum fills up (peak "
                + juce::String (peakShown, 1) + " dB)");

    // hide the strip: the spectra must freeze exactly
    proc.setAnalyserVisible (false);

    float frozen[SpectrumAnalyzer::numScopeBins];
    proc.getAnalyzerIn().copyScopeData (frozen, SpectrumAnalyzer::numScopeBins);

    processBlocks (proc, src, 150, block);   // ~1.6 s of loud signal

    float after[SpectrumAnalyzer::numScopeBins];
    proc.getAnalyzerIn().copyScopeData (after, SpectrumAnalyzer::numScopeBins);

    bool identical = true;
    for (int b = 0; b < SpectrumAnalyzer::numScopeBins; ++b)
        // exact bit equality via </> (no -Wfloat-equal): the values are
        // never NaN, so !(a<b || a>b) IS a != that compares the stored
        // floats exactly
        if (after[b] < frozen[b] || after[b] > frozen[b])
            identical = false;

    expectTrue (identical, "a hidden strip freezes the spectra");

    // the LUFS meters keep measuring while the strip is hidden
    expectTrue (proc.getLufsIn().getShortTermLufs() > -70.0f,
                "the LUFS meters stay on the editor gate alone");

    // re-show: the analyser resets, so the next frame starts from silence
    proc.setAnalyserVisible (true);

    float cleared[SpectrumAnalyzer::numScopeBins];
    proc.getAnalyzerIn().copyScopeData (cleared, SpectrumAnalyzer::numScopeBins);

    bool atFloor = true;
    for (int b = 0; b < SpectrumAnalyzer::numScopeBins; ++b)
        if (cleared[b] > -99.9f)
            atFloor = false;

    expectTrue (atFloor, "re-showing resets the spectra to the floor");

    // and it fills up again
    processBlocks (proc, src, 100, block);
    proc.getAnalyzerIn().copyScopeData (after, SpectrumAnalyzer::numScopeBins);

    float peakBack = after[0];
    for (int b = 1; b < SpectrumAnalyzer::numScopeBins; ++b)
        peakBack = juce::jmax (peakBack, after[b]);

    expectTrue (peakBack > -80.0f,
                "the spectra run again once the strip is shown (peak "
                + juce::String (peakBack, 1) + " dB)");
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

// v2.42 (point 4): the gain-staging readout is the meters' 5 s power-domain
// exponential average, gated per hop below -70 LUFS. It stays "--" under
// 1 s of gated signal, holds EXACTLY through a short silence gap, and
// invalidates after 3 s of silence (the schedule the draining short-term
// window always had, so a muted console cannot keep flashing a stale
// correction).
void testGainCorrectionSmoothing()
{
    beginTest ("gain correction smoothing (5 s power average, gated)");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEditorOpen (true);

    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer noMidi;
    SineSource src (1000.0, 48000.0, 0.1f);

    // ~0.85 s of signal: 8 hops, under the 10-hop (1 s) warm-up
    for (int n = 0; n < 80; ++n)
    {
        src.fill (block);
        proc.processBlock (block, noMidi);
    }

    float delta = 0.0f;
    expectTrue (! proc.getGainCorrectionDb (delta),
                "the smoothed readout stays invalid under 1 s of signal");

    // ~6 s more: valid, and ~0 (nothing hosted - both meters see the same
    // bit-exact passthrough, so the two averages stay identical)
    for (int n = 0; n < 560; ++n)
    {
        src.fill (block);
        proc.processBlock (block, noMidi);
    }

    expectTrue (proc.getGainCorrectionDb (delta),
                "the smoothed readout is valid after 1 s of gated signal");
    expectNear ((double) delta, 0.0, 0.05,
                "the smoothed correction is ~0 with nothing hosted");

    // a 1 s silence gap: the average holds EXACTLY (gated, not dragged)
    for (int n = 0; n < 100; ++n)
    {
        fillSilence (block);
        proc.processBlock (block, noMidi);
    }

    float held = 0.0f;
    expectTrue (proc.getGainCorrectionDb (held),
                "the readout holds through a 1 s silence gap");
    // exact bit equality via </> (no -Wfloat-equal): the meters were not
    // updated during the gap, so the two reads must be identical
    expectTrue (! (held < delta || held > delta),
                "the held value is bit-identical to the pre-gap value");

    // 3 s more of silence: the readout invalidates like the old ST window
    for (int n = 0; n < 320; ++n)
    {
        fillSilence (block);
        proc.processBlock (block, noMidi);
    }

    expectTrue (! proc.getGainCorrectionDb (delta),
                "3 s of silence invalidates the smoothed readout");
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

// v2.41 (Clement's staircase remark): below ~213 Hz at 48 kHz a display
// band is NARROWER than one FFT bin (5.86 Hz), so the old integer
// rounding fed several consecutive bands from the SAME bin - the
// visible stairs in the low end (up to ~10 identical points near
// 20 Hz). The fractional sampling interpolates between the two
// neighbouring bin powers at each band's exact frequency, so with
// broadband noise no two adjacent low bands may read identically
// anymore. This test FAILS on the pre-v2.41 aggregation.
void testBassResolution()
{
    beginTest ("bass resolution: no FFT-bin stairs below 200 Hz (v2.41)");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEditorOpen (true);

    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer noMidi;

    // 2 s of deterministic broadband noise: the bass bands all carry
    // real, different energy (no floor clamp to hide behind). Driven
    // block by block - processBlocks() takes a SineSource only.
    NoiseSource src (0.3f);

    for (int n = 0; n < (int) (2.0 * 48000.0 / 512.0); ++n)
    {
        src.fill (block);
        proc.processBlock (block, noMidi);
    }

    float raw[SpectrumAnalyzer::numScopeBins];
    proc.getAnalyzerIn().copyRawScopeData (raw, SpectrumAnalyzer::numScopeBins);

    // the RAW band energies (pre spatial pass): count the longest run
    // of IDENTICAL consecutive bands whose low edge sits below 200 Hz
    // (the region where a band is sub-bin; 200 Hz is around index 85)
    int longest = 1, run = 1;

    for (int b = 1; b < SpectrumAnalyzer::numScopeBins; ++b)
    {
        if (proc.getAnalyzerIn().getBinFrequency (b) >= 200.0)
            break;

        // exact equality via </> (no -Wfloat-equal): identical stored
        // floats are exactly what a stair IS - the point of the test
        if (raw[b] < raw[b - 1] || raw[b] > raw[b - 1])
        {
            run = 1;
        }
        else
        {
            ++run;
            longest = juce::jmax (longest, run);
        }
    }

    expectTrue (longest <= 2,
                "no more than 2 identical low bands in a row (longest run "
                + juce::String (longest) + ", was ~10 pre-v2.41)");
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

    // 6 s (since v2.47 the cross-spectrum EMAs average over the last
    // ~30 frames of ~170 ms - tau ~5 s, the energy-weighted window;
    // the stationary mock needs no settling margin - the ratio
    // estimates are self-normalising - and the random-phase tone only
    // cancels across frames, all the more over the longer window)
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

    // the tone band's coherence dips below the CLEAN band's but stays
    // above the 0.25 gate: the tone is present (uncorrelated with the
    // input) yet the band is still measured - and the EQ must IGNORE
    // the tone. v2.44.0: the dip is asserted RELATIVE to the clean
    // band, because the wide Savitzky-Golay window (~1.2 octave)
    // dilutes a narrow artifact's dip with ~30 coherent neighbours:
    // the reference simulation on this exact scenario measures
    // g2 = 0.50 raw, 0.81 with the v2.43.1 13-tap cascade, 0.93 with
    // the SG-31 kernel (clean band 1.00). The dip shrinks with the
    // window but never disappears, and a band with no tone shows no
    // dip at all, so a 0.05 margin discriminates presence from
    // absence regardless of the kernel width (the EQ assertions below
    // carry the "the tone is rejected" part of the contract).
    expectTrue (eqCoherence[bin12k] < eqCoherence[bin500] - 0.05f,
                "the tone band's coherence dips below the clean band (the "
                "tone is there; got " + juce::String (eqCoherence[bin12k], 3)
                + " vs " + juce::String (eqCoherence[bin500], 3) + ")");
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

// v2.43 (Clement's "smoother un peu cette courbe brute d'EQ" remark):
// every display band is an INDEPENDENT H1 measurement, so on real
// material the RAW curve jitters along the frequency axis - the
// estimators' residual variance, not the console's response. The mock
// injects a deterministic band-limited noise floor INSIDE the console
// (uncorrelated with the input, in-band level equal to the input PSD -
// the reference simulation's console-noise scenario, where the raw
// curve's band-to-band jaggedness measured ~1.35 dB). The v2.43
// frequency-domain smoothing over the accumulated cross-spectra must
// bring that jaggedness down while the curve stays flat 0 dB (the
// console is a passthrough): smooth, but still an honest measurement.
//
// v2.44.0 (Clement's "lissage malin" framing): the display shows the
// SMOOTHING of the 256 (log f, dB) points, not the points - a
// Savitzky-Golay cubic fit over a 31-band (~1.2 octave) window, which
// preserves slopes and EQ peaks while killing the band-scale jitter
// AND the slow half-octave waves the earlier binomial kernels let
// through. The reference simulation on this exact mock scenario:
// jaggedness max 1.35 dB raw / 0.086 (13-tap cascade, v2.43.1) /
// 0.053 dB (SG-31), rms 0.58 / 0.028 / 0.022.
void testEqCurveSmoothing()
{
    beginTest ("EQ curve frequency smoothing (v2.43)");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEditorOpen (true);

    // deterministic broadband input, same source as the bass tests
    NoiseSource src (0.3f);
    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer noMidi;

    auto mock = std::make_unique<MockConsole> (true);
    mock->setBandNoise (0.3f); // in-band noise PSD == input PSD, g2 ~ 0.5
    proc.getHostedStrip().setPlugin (std::move (mock));

    // 4 s: ~24 cross-spectrum frames of 8192 samples; the EMAs average
    // over the last ~8 of them, as in the reference simulation
    for (int n = 0; n < (int) (4.0 * 48000.0 / 512.0); ++n)
    {
        src.fill (block);
        proc.processBlock (block, noMidi);
    }

    // the RAW analyser output (the panel's display EMA would blur the
    // metric - the frequency smoothing itself is under test here)
    float eqDb[TransferAnalyzer::numScopeBins];
    float eqCoherence[TransferAnalyzer::numScopeBins];

    proc.getTransferAnalyzer().copyTransferData (eqDb, eqCoherence,
                                                 TransferAnalyzer::numScopeBins);

    // the analysis region: well inside both the input's broadband
    // energy and the injected noise band
    const TransferAnalyzer& ana = proc.getTransferAnalyzer();

    auto coveringBin = [&ana] (double freq)
    {
        for (int b = 0; b < TransferAnalyzer::numScopeBins - 1; ++b)
            if (ana.getBinFrequency (b) <= freq
                && ana.getBinFrequency (b + 1) > freq)
                return b;

        return TransferAnalyzer::numScopeBins - 1;
    };

    const int lo = coveringBin (700.0);
    const int hi = coveringBin (3500.0);

    // 1. the scenario is honest: every band in the region is really
    // measured, with the coherence dipped by the injected noise but
    // well above the 0.25 display gate (the simulation had g2 ~ 0.5
    // in-band). If this fails the test measured nothing.
    float g2min = 1.0f;
    double meanDb = 0.0;

    for (int b = lo; b <= hi; ++b)
    {
        g2min = juce::jmin (g2min, eqCoherence[b]);
        meanDb += (double) eqDb[b];
    }

    meanDb /= (double) juce::jmax (1, hi - lo + 1);

    expectTrue (g2min >= 0.3f,
                "the noise scenario is measured (min coherence "
                + juce::String (g2min, 3) + " >= 0.3, above the 0.25 gate)");

    // 2. smooth but HONEST: the passthrough still reads flat 0 dB on
    // average - an over-smoothing that destroyed the measurement
    // would fail this
    expectTrue (std::abs (meanDb) < 0.5,
                "the smoothed curve stays flat 0 dB on average (got "
                + juce::String (meanDb, 3) + " dB)");

    // 3. THE metrics: band-to-band jaggedness - the deviation from
    // the 3-band local mean across the region, max and rms (regression
    // guards against the smoothing disappearing entirely). Reference
    // simulation on this exact scenario: raw 1.35 max / 0.58 rms,
    // 13-tap cascade (v2.43.1) 0.086 / 0.028, Savitzky-Golay (v2.44.0)
    // 0.053 / 0.022 dB.
    float jaggedness = 0.0f;
    double jagSq = 0.0;

    for (int b = lo + 1; b < hi; ++b)
    {
        const float localMean = (eqDb[b - 1] + eqDb[b] + eqDb[b + 1]) / 3.0f;
        const float dev = std::abs (eqDb[b] - localMean);
        jaggedness = juce::jmax (jaggedness, dev);
        jagSq += (double) dev * (double) dev;
    }

    const double jagRms = std::sqrt (jagSq / (double) juce::jmax (1, hi - lo - 1));

    expectTrue (jaggedness < 0.8f,
                "the EQ curve is frequency-smoothed (max band-to-band "
                "deviation " + juce::String (jaggedness, 3) + " dB < 0.8; "
                "the raw per-band estimates measured ~1.4 dB in the "
                "reference simulation)");

    expectTrue (jagRms < 0.1,
                "the smoothing pipeline is intact (rms band-to-band "
                "deviation " + juce::String (jagRms, 3)
                + " dB < 0.1; raw estimates measure ~0.58 in the "
                  "reference simulation)");

    // 4. v2.44.0: the kernel itself, analytically (deterministic, no
    // audio needed). The Savitzky-Golay cubic over 31 bands must
    // (a) sum to 1 - a flat neighbourhood passes through exactly -
    // and (b) attenuate a half-octave-period wave (period 15 bands,
    // the "vaguelettes" Clement kept seeing): the reference
    // simulation measures 0.27 residual amplitude for the SG kernel,
    // while the v2.43.1 13-tap cascade leaves 0.76 - the 0.45
    // threshold discriminates them with margin on both sides.
    double coeffSum = 0.0;
    double waveRe = 0.0, waveIm = 0.0;

    for (int k = -TransferAnalyzer::smoothHalfWidth;
         k <= TransferAnalyzer::smoothHalfWidth; ++k)
    {
        const double c = TransferAnalyzer::smoothCoeffs[k
            + TransferAnalyzer::smoothHalfWidth];
        const double ph = juce::MathConstants<double>::twoPi
                              * (double) k / 15.0;
        coeffSum += c;
        waveRe += c * std::cos (ph);
        waveIm += c * std::sin (ph);
    }

    expectNear (coeffSum, 1.0, 1.0e-9,
                "the smoothing kernel preserves a flat curve exactly "
                "(sum of coefficients = 1, got "
                + juce::String (coeffSum, 12) + ")");

    const double waveResidue = std::sqrt (waveRe * waveRe + waveIm * waveIm);

    expectTrue (waveResidue < 0.45,
                "the kernel is the wide Savitzky-Golay fit, which "
                "kills the slow half-octave waves (residual amplitude "
                + juce::String (waveResidue, 3)
                + " < 0.45; the v2.43.1 cascade left 0.76, the raw "
                  "estimate is 1.0)");
}

// ---------------------------------------------------------------------------
// v2.50.0 (Clement's "peigne dans le tres grave" report on v2.49): the
// comb regression test. The 2026-10-29 offline benchmark (19 estimator
// variants, 5 programmes x 4 filters x clean / -85 dB console noise)
// traced the artifact to the v2.44 Savitzky-Golay pass being applied to
// the four ACCUMULATED spectra: on bursty bass the accumulators of
// neighbouring bands differ by orders of magnitude, so the ratio of
// smoothed values is biased towards the LOUD neighbours' ratio - the
// teeth measured 9.2 dB RMS / 23.5 dB max against the analytic truth
// in the 60-213 Hz zone under a 24 dB/oct low cut at 300 Hz (the
// screenshot showed ~11 dB peak-to-peak). v2.50.0 computes H from the
// RAW accumulators and applies the SAME 31-tap kernel to the dB curve
// (bench: 0.53 dB RMS / 1.1 dB max - better than the offline Welch
// ceiling and than the v2.48 max-hold blends), and weights the EMA by
// the band's own energy so the estimate holds between notes instead
// of decaying into the floor.
//
// This test reproduces the scenario END TO END: a bursty
// bass-with-harmonics programme (55 Hz fundamental + a decaying
// harmonic series, a 1.4 s / 1.0 s loud-quiet envelope cycle), a
// mock console whose low cut IS the benchmark's filter A, and the
// measured EQ curve asserted against the mock's ANALYTIC response in
// exactly the 60-213 Hz zone where the comb lived. The v2.44-v2.49
// code fails this by roughly an order of magnitude; the fixed code
// reads within ~1 dB of the truth.
//
// The exact magnitude of the implemented filter (two cascaded RBJ
// high-pass sections, Q 0.5412 / 1.3066 - identical to the mock's
// coefficients):
//   |H(jw)|^2 per section = w^4 / ((w^2-w0^2)^2 + (w*w0/Q)^2)
static double lowcutMagnitudeDb (double freq)
{
    const double w0 = juce::MathConstants<double>::twoPi * 300.0 / 48000.0;
    const double w = juce::MathConstants<double>::twoPi * freq / 48000.0;
    const double qs[2] = { 0.54119610, 1.30656296 };

    double magSq = 1.0;

    for (double q : qs)
    {
        const double dw = w * w - w0 * w0;
        magSq *= (w * w) * (w * w) / (dw * dw + (w * w0 / q) * (w * w0 / q));
    }

    return 10.0 * std::log10 (magSq);
}

void testEqLowcutCombRegression()
{
    beginTest ("EQ curve under a low cut over bursty bass (v2.50 comb fix)");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEditorOpen (true);

    auto mock = std::make_unique<MockConsole> (true);
    mock->setLowcut (true);
    proc.getHostedStrip().setPlugin (std::move (mock));

    // bursty bass: 55 Hz fundamental + decaying harmonic series (so
    // the SG neighbourhood of the checked zone is fed up to ~900 Hz,
    // as on real programme), with a loud/quiet envelope cycle
    static constexpr int numPartials = 10;
    const double freqs[numPartials] = { 55.0, 110.0, 165.0, 220.0, 275.0,
                                        330.0, 440.0, 550.0, 660.0, 880.0 };
    const double amps[numPartials] = { 0.30, 0.09, 0.05, 0.035, 0.025,
                                       0.02, 0.015, 0.012, 0.01, 0.008 };

    double phase[numPartials] = {};

    const double sr = 48000.0;
    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer noMidi;

    // 12 s = 1125 blocks of 512 exactly, ~70 cross-spectrum frames -
    // the EMA tail is long settled
    const int numBlocks = (int) ((12.0 * sr) / 512.0);

    for (int blk = 0; blk < numBlocks; ++blk)
    {
        for (int i = 0; i < 512; ++i)
        {
            const double t = (double) ((long long) blk * 512 + i) / sr;
            const double cycle = std::fmod (t, 2.4);
            const double env = (cycle < 1.4) ? 1.0 : 0.12;

            double v = 0.0;

            for (int p = 0; p < numPartials; ++p)
            {
                v += amps[p] * std::sin (phase[p]);
                phase[p] += juce::MathConstants<double>::twoPi * freqs[p] / sr;
            }

            const float s = (float) (v * env);

            for (int c = 0; c < 2; ++c)
                block.setSample (c, i, s);
        }

        proc.processBlock (block, noMidi);
    }

    float eqDb[TransferAnalyzer::numScopeBins];
    float eqCoherence[TransferAnalyzer::numScopeBins];

    proc.getTransferAnalyzer().copyTransferData (eqDb, eqCoherence,
                                                 TransferAnalyzer::numScopeBins);

    const TransferAnalyzer& ana = proc.getTransferAnalyzer();

    // the comb zone: bands whose low edge sits in 60-213 Hz (the
    // v2.41 narrow-band region) with a displayable truth
    std::vector<int> zone;

    for (int b = 0; b < TransferAnalyzer::numScopeBins; ++b)
    {
        const double f = ana.getBinFrequency (b);

        if (f >= 60.0 && f <= 213.0 && std::abs (lowcutMagnitudeDb (f)) <= 40.0)
            zone.push_back (b);
    }

    expectTrue (zone.size() >= 8,
                "the scenario feeds at least 8 checkable low bands (got "
                + juce::String ((int) zone.size()) + ")");

    if (zone.size() < 8)
        return; // nothing checkable - the expect above already flagged it

    // 1. every checked band is honestly measured (the coherence gate
    // would blank it otherwise)
    float g2min = 1.0f;

    for (int b : zone)
        g2min = juce::jmin (g2min, eqCoherence[b]);

    expectTrue (g2min > 0.25f,
                "the comb zone is measured (min coherence "
                + juce::String (g2min, 3) + " > 0.25 gate)");

    // 2. THE regression metric: the measured curve against the
    // analytic response - median and max |error| over the zone
    std::vector<double> err;

    for (int b : zone)
        err.push_back (std::abs ((double) eqDb[b] - lowcutMagnitudeDb (ana.getBinFrequency (b))));

    std::sort (err.begin(), err.end());

    const double median = err[err.size() / 2];
    const double worst = err.back();

    expectTrue (median < 1.5,
                "the low-cut zone reads the true response (median |err| "
                + juce::String (median, 3) + " dB < 1.5; the v2.44-v2.49 "
                "comb measured ~9 dB RMS here)");

    expectTrue (worst < 3.5,
                "no comb teeth left (max |err| " + juce::String (worst, 3)
                + " dB < 3.5; the v2.44-v2.49 comb measured up to 23 dB)");

    // 3. smoothness: no band-to-band teeth LEFT ON TOP of the filter's
    // own steep slope - the metric is the deviation from the TRUE
    // successive deltas (a 24 dB/oct low cut legitimately descends
    // ~0.8 dB per band; the v2.44-v2.49 comb added teeth ON TOP of
    // that slope). The reference simulation measured 0.13 dB mean
    // deviation for the fixed code, 2.3 dB for the comb.
    double jagSum = 0.0;
    int jagCount = 0;

    for (size_t i = 0; i + 1 < zone.size(); ++i)
    {
        if (zone[i + 1] == zone[i] + 1)
        {
            const double f0 = ana.getBinFrequency (zone[i]);
            const double f1 = ana.getBinFrequency (zone[i + 1]);
            const double curveDelta = (double) eqDb[zone[i + 1]] - (double) eqDb[zone[i]];
            const double truthDelta = lowcutMagnitudeDb (f1) - lowcutMagnitudeDb (f0);

            jagSum += std::abs (curveDelta - truthDelta);
            ++jagCount;
        }
    }

    const double jagMean = jagCount > 0 ? jagSum / (double) jagCount : 0.0;

    expectTrue (jagMean < 0.6,
                "the curve follows the filter's slope without teeth (mean "
                "|delta deviation| " + juce::String (jagMean, 3)
                + " dB < 0.6; the v2.44-v2.49 comb measured ~2.3)");
}

// v2.46.0 (Clement's idea) -> v2.48.0 (a full member) -> v2.49.0 (off
// the drawn curve, kept fully alive): the "EQ @ max"
// machinery - dB MaxOut minus dB MaxIn over 5-second sliding maxima
// of the SMOOTHED IN/OUT curves. For a linear console the max is
// homogeneous (max (H*x) = H * max (x)) and the IN/OUT one-poles are
// identical, so the difference is the EXACT transfer at the programme's
// peak drive. In v2.48 this value was one of the two members of the
// single EQ curve (mean with the 2 s window); since v2.49.0 the drawn
// curve is the pure H1 member (Clement's A/B build), so the max
// family is tested HERE ONLY, through its hooks - the ballistics
// contract it must keep is unchanged and still under test: flat
// 0 dB in passthrough, IMMEDIATE on a knob up (the max only moves up),
// and HOLDING on a knob down (the old peak stays in the 5 s sliding
// window).
void testEqMaxHoldTrace()
{
    beginTest ("EQ @ max trace: exact, immediate up, holding down (v2.46)");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEditorOpen (true);

    NoiseSource src (0.3f);
    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer noMidi;

    auto mock = std::make_unique<MockConsole> (true);
    MockConsole* mockPtr = mock.get();
    proc.getHostedStrip().setPlugin (std::move (mock));

    // v2.46.0 build 2 - the harness cadence lesson: the panel is driven
    // at its REAL rate (a 30 Hz tick per ~3 audio blocks, as when the
    // timer runs) while the audio streams. Feeding everything first and
    // ticking afterwards leaves the analysers frozen on ONE raw frame,
    // and the 5 s maxima then measure that single frame's per-band
    // jitter (a few dB on noise) instead of the smoothed envelope the
    // product computes - build 1's false 3.31 dB on this very
    // assertion came from exactly that, not from the trace logic.
    SpectrumPanel panel (proc);

    auto drive = [&] (double seconds)
    {
        const int blocks = (int) (seconds * 48000.0 / 512.0);

        for (int n = 0; n < blocks; ++n)
        {
            src.fill (block);
            proc.processBlock (block, noMidi);

            if (n % 3 == 0)
                panel.runAnalysisTick();
        }
    };

    // ~3 s of broadband passthrough (fills the 150-frame window with
    // converged display values; the relative max gate wakes the band)
    drive (3.0);

    const TransferAnalyzer& ana = proc.getTransferAnalyzer();
    auto coveringBin = [&ana] (double freq)
    {
        for (int b = 0; b < TransferAnalyzer::numScopeBins - 1; ++b)
            if (ana.getBinFrequency (b) <= freq
                && ana.getBinFrequency (b + 1) > freq)
                return b;

        return TransferAnalyzer::numScopeBins - 1;
    };

    const int bin500 = coveringBin (500.0);

    // 1. the band really carried programme energy
    expectTrue (panel.isEqMaxBinMeasured (bin500),
                "the 500 Hz band is measured by the max trace");

    // 2. passthrough: the max difference reads a flat 0 dB transfer
    expectNear (panel.getEqMaxDbAtBin (bin500), 0.0, 1.5,
                "the max-trace EQ reads flat 0 dB in passthrough (got "
                + juce::String (panel.getEqMaxDbAtBin (bin500), 2) + " dB)");

    // 3. knob UP (+6 dB): the max only ever moves up, so the trace
    // reacts within ~1-2 s of display, not within an EMA settle -
    // ~94 display ticks while the new level streams in
    mockPtr->gain.store (2.0f);
    drive (3.0);

    expectNear (panel.getEqMaxDbAtBin (bin500), 6.0, 1.5,
                "the max-trace EQ reacts immediately on a knob up (got "
                + juce::String (panel.getEqMaxDbAtBin (bin500), 2)
                + " dB, expected ~+6)");

    // 4. knob DOWN: the +6 dB peak is still inside the 5 s sliding
    // window, so the trace HOLDS its value - the documented cost
    mockPtr->gain.store (1.0f);
    drive (1.0);

    for (int t = 0; t < 10; ++t)
        panel.runAnalysisTick();

    expectTrue (panel.getEqMaxDbAtBin (bin500) > 3.0f,
                "the max-trace EQ holds the recent peak after a knob "
                "down (got "
                + juce::String (panel.getEqMaxDbAtBin (bin500), 2)
                + " dB > +3)");
}

// v2.47.0 -> v2.49.0 (Clement's A/B call: "implémente seulement ta
// méthode H1 optimisée, sans mon idée de Max Hold"): the
// panel draws a SINGLE EQ curve, now the PURE optimised H1
// cross-spectrum member - the same estimator, gates and display
// chain the v2.48 blend read in its junk-fallback bands, with NO
// max-hold members voting (the max windows are still computed and
// keep their own trace test, but they no longer feed the drawn
// curve). The contracts, exercised end to end with the REAL
// production path at the real 30 Hz panel cadence (the build 2
// lesson of v2.46: one tick per ~3 audio blocks, never
// feed-then-tick):
//
//   1. flat 0 dB in passthrough (the H1 estimator reads the exact
//      identity transfer as 0 dB);
//   2. a knob turned through the REAL hosted parameter (not a test
//      back door) fires the parameter relay -> the deterministic
//      flush restarts the estimators, and the fast post-flush EMA
//      reads the new response within ~1 s of audio;
//   3. THE DISCRIMINATOR VS THE MAX-HOLD BLEND: a knob DOWN also
//      re-converges within ~2 s - the flush works in both
//      directions. The v2.48 blend would still HOLD the old +6 dB
//      response here (the maxima keep the peak up to 5 s in their
//      sliding windows); the pure H1 must have left it. This is the
//      assertion that proves the curve carries no max vote;
//   4. the v2.12 persistence: the signal stops, the curve stays.
void testEqVeriteCurve()
{
    beginTest ("EQ curve: pure H1, knob reactivity both ways, persistence (v2.49)");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEditorOpen (true);

    NoiseSource src (0.3f);
    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer noMidi;

    auto mock = std::make_unique<MockParamConsole>();
    MockParamConsole* mockPtr = mock.get();
    proc.getHostedStrip().setPlugin (std::move (mock));

    // the parameter relay is attached to the hosted knob - the flush
    // plumbing (the heart of the H1 method's knob reactivity, now in
    // BOTH directions) is alive before anything else is asserted
    expectTrue (proc.getHostedStrip().getRelayedParameterCount() == 1,
                "the hosted parameter carries the change relay");

    SpectrumPanel panel (proc);

    // the real cadence: one panel tick per ~3 audio blocks
    auto drive = [&] (double seconds)
    {
        const int blocks = (int) (seconds * 48000.0 / 512.0);

        for (int n = 0; n < blocks; ++n)
        {
            src.fill (block);
            proc.processBlock (block, noMidi);

            if (n % 3 == 0)
                panel.runAnalysisTick();
        }
    };

    const TransferAnalyzer& ana = proc.getTransferAnalyzer();
    auto coveringBin = [&ana] (double freq)
    {
        for (int b = 0; b < TransferAnalyzer::numScopeBins - 1; ++b)
            if (ana.getBinFrequency (b) <= freq
                && ana.getBinFrequency (b + 1) > freq)
                return b;

        return TransferAnalyzer::numScopeBins - 1;
    };

    const int bin500 = coveringBin (500.0);

    // 1. ~3 s of broadband passthrough: the pure H1 curve reads flat
    drive (3.0);

    expectTrue (panel.isEqBinMeasured (bin500),
                "the 500 Hz band is measured by the pure H1 curve");

    expectNear (panel.getEqDbAtBin (bin500), 0.0, 1.5,
                "the pure H1 curve reads flat 0 dB in passthrough (got "
                + juce::String (panel.getEqDbAtBin (bin500), 2) + " dB)");

    // 2. the knob turn: +6 dB through the REAL hosted parameter, so
    // the relay fires and the flush restarts the estimators - the
    // fast post-flush EMA converges the H1 within ~1 s (the v2.47
    // reference simulation measured ~+5.3 dB one second after the
    // turn; the threshold keeps the v2.46/2.48 margin).
    // setValueNotifyingHost expects a NORMALIZED 0-1 value, not the raw
    // gain - convert the linear gain 2.0 (i.e. +6 dB) first.
    mockPtr->gainParam->setValueNotifyingHost (
        mockPtr->gainParam->convertTo0to1 (2.0f));
    drive (1.0);

    expectTrue (panel.getEqDbAtBin (bin500) > 3.0f,
                "the pure H1 curve reacts within ~1 s of a hosted "
                "knob turn (got "
                + juce::String (panel.getEqDbAtBin (bin500), 2)
                + " dB > +3; the relay fired the flush, the fast "
                  "post-flush EMA carries the new response)");

    // 3. converged: the 5 s window has settled on the new response -
    // the curve reads the true +6 dB transfer
    drive (4.0);

    expectNear (panel.getEqDbAtBin (bin500), 6.0, 1.5,
                "the pure H1 curve settles on the true +6 dB response "
                "(got " + juce::String (panel.getEqDbAtBin (bin500), 2)
                + " dB)");

    // 4. THE DISCRIMINATOR: the knob goes back DOWN, through the
    // real parameter again - the relay fires a second flush, and the
    // H1 re-measures from scratch: ~2 s of audio later the curve must
    // have LEFT the +6 dB plateau. The v2.48 max-hold blend would
    // still read +6 here (its maxima hold the old peak up to 5 s) -
    // this is the assertion that carries "sans mon idée de Max Hold".
    mockPtr->gainParam->setValueNotifyingHost (
        mockPtr->gainParam->convertTo0to1 (1.0f));
    drive (2.0);

    expectTrue (panel.getEqDbAtBin (bin500) < 3.0f,
                "the pure H1 curve re-converges DOWN within ~2 s of "
                "a knob-down (got "
                + juce::String (panel.getEqDbAtBin (bin500), 2)
                + " dB < +3; the blend would still hold the old +6 dB "
                  "peak in its 5 s max windows)");

    // 5. the v2.12 persistence, unchanged: the signal stops, the
    // gates close, the sticky flags and held values keep the curve
    // on screen - now holding the converged 0 dB response
    for (int n = 0; n < (int) (1.0 * 48000.0 / 512.0); ++n)
    {
        fillSilence (block);
        proc.processBlock (block, noMidi);

        if (n % 3 == 0)
            panel.runAnalysisTick();
    }

    expectNear (panel.getEqDbAtBin (bin500), 0.0, 1.5,
                "the pure H1 curve persists after the signal stops "
                "(v2.12 gate, got "
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
        session->setAttribute ("version", "2.49.0");
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
    session->setAttribute ("version", "2.49.0");
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
    proc.setAnaDbDiv (12.0f); // v2.51 scale: must survive unchanged
    proc.setEqDbDiv (4.0f);   // v2.51 scale: must survive unchanged
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
    expectTrue (xml != nullptr && xml->getStringAttribute ("version") == "2.51.0",
                "saved state carries the current version tag (2.51.0)");

    TheChannelStripAudioProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());

    expectNear (restored.getTiltDb(), -3.0,  0.01, "tilt restored");
    expectNear (restored.getAnaDbDiv(), 12.0,  0.01, "spectrum dB/div restored");
    expectNear (restored.getEqDbDiv(),  4.0,  0.01, "EQ dB/div restored");
    expectTrue (! restored.getAnalyserVisible(), "analyser visibility restored");
    expectNear (restored.getSpecTau(), 0.15, 0.01, "response speed restored");
    expectNear (restored.getLufsTarget(), -37.0, 0.01, "loudness reference restored");
}

// v2.51.0 (Clement's analyser display rework): the scale ladder
// changed - the grid went from nine to SIX divisions, so the analyser
// scales are now 15/12/9 dB per division ([+30;-60], [+24;-48],
// [+18;-36]) and the EQ scales 8/6/4/2 dB per division (+/-24/18/12/6).
// The legacy remap is VERSION-GATED: the EQ ladder's 4 dB/div is
// ambiguous - a pre-2.51 session saved 4 meaning +/-18 dB, while a
// 2.51 session saves 4 meaning +/-12 dB - so the mapping only fires
// when the session's version attribute is older than 2.51.0 (the
// first harness build caught the ungated remap breaking the 2.51
// roundtrip of a 4). For those legacy sessions: analyser 10 (was
// [+30;-60]) -> 15, 6 (was [+18;-36]) -> 9; EQ 4 (was +/-18) -> 6,
// 2 (was +/-9) -> 2 (the new +/-6 dB scale).
void testDisplayScaleLegacyMapping()
{
    beginTest ("v2.51 scale ladder: legacy session scales are mapped");

    // builds a minimal session tagged with a given version and the
    // two scale attributes (the fake-session pattern), loads it and
    // returns the scales the processor settled on
    auto loadScales = [] (const char* version, double ana, double eq)
    {
        auto session = std::make_unique<juce::XmlElement> ("TCS");
        session->setAttribute ("version", version);
        session->setAttribute ("anaDbDiv", ana);
        session->setAttribute ("eqDbDiv", eq);

        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (*session, block);

        TheChannelStripAudioProcessor proc;
        proc.setStateInformation (block.getData(), (int) block.getSize());

        return std::make_pair (proc.getAnaDbDiv(), proc.getEqDbDiv());
    };

    // a v2.50 session with the old WIDE scales reopens on the new
    // scales with the SAME visual range
    {
        const auto s = loadScales ("2.50.0", 10.0, 4.0);

        expectNear (s.first, 15.0, 0.01,
                    "legacy 10 dB/div maps to 15 dB/div (same [+30;-60] range)");
        expectNear (s.second, 6.0, 0.01,
                    "legacy EQ 4 dB/div (was +/-18) maps to 6 dB/div (+/-18)");
    }

    // a v2.50 session with the old FINE scales maps down the same way
    {
        const auto s = loadScales ("2.50.0", 6.0, 2.0);

        expectNear (s.first, 9.0, 0.01,
                    "legacy 6 dB/div maps to 9 dB/div (same [+18;-36] range)");
        expectNear (s.second, 2.0, 0.01,
                    "legacy EQ 2 dB/div stays 2 dB/div (now +/-6 dB)");
    }

    // a v2.51 session passes through UNMAPPED - including the
    // ambiguous 4, which the new ladder reads as +/-12 dB (the exact
    // roundtrip the ungated remap used to break)
    {
        const auto s = loadScales ("2.51.0", 12.0, 4.0);

        expectNear (s.first, 12.0, 0.01,
                    "a current session's analyser scale is not remapped");
        expectNear (s.second, 4.0, 0.01,
                    "a current session keeps EQ 4 dB/div - it means +/-12 dB");
    }
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
    expectTrue (report.contains ("TheChannelStrip 2.51.0"),
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

    std::cout << "TheChannelStrip offline test harness - v2.51.0" << std::endl;
    std::cout << "---------------------------------------------" << std::endl;

    testProcessorBasics();
    testPassthroughNoPlugin();
    testMeasurementGating();
    testAnalyserGating();
    testLufsShortTerm();
    testSilence();
    testGainCorrectionSmoothing();
    testSpectrumPeak();
    testSpectrumHighFrequency();
    testBassResolution();
    testEqWeakContent();
    testEqUncorrelatedConsoleNoise();
    testEqCurveSmoothing();
    testEqLowcutCombRegression();
    testEqMaxHoldTrace();
    testEqVeriteCurve();
    testConsoleSlotStatus();
    testPresetManager();
    testCurrentPresetTracking();
    testSilentConsoleWarning();
    testHoverReadout();
    testStateRoundtrip();
    testDisplayScaleLegacyMapping();
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
