// ---------------------------------------------------------------------------
// tests/TcsTestsMain.cpp - v2.54.0 offline test harness (roadmap point 8)
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
//   - v2.52: the tilt / Fast-Slow buttons restore the session onto the
//     analyser strip's own buttons (they moved out of the sidebar) and
//     the speed value is canonicalised to exactly one of the two taus
//   - v2.53: the scale +/- ZOOM buttons - the click-to-cycle is gone;
//     the fresh defaults are 12 dB/div spectra / 4 dB/div EQ, the
//     pure-static ladders step one zoom per press and clamp at their
//     ends, and the panel restores the saved zooms and mirrors the
//     endpoints in its buttons' enabled states
//   - v2.54 (roadmap point A): the torture batch - adversarial audio
//     (full-scale DC, hard squares, one-sample transients, channel
//     asymmetry, 22050 / 176400 Hz at 1 / 4096 frames, NaN input as a
//     documented survival limit), state corner cases (hostile
//     ATTRIBUTES in a valid container snap onto the ladders, 50
//     rapid save / restore cycles are a byte-identical fixed point)
//     and display stress (1000 analysis ticks over a sweep, a
//     deterministic display-chaos sequence, degenerate panel sizes)
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
//   - v2.51.1 hotfix (the Studio Pro 8.1.2 crash): a superseded
//     instantiation aborts before the plugin is even attempted, a
//     duplicate restore with nothing to apply is skipped without
//     touching the hosted console, and concurrent setPlugin /
//     clearPlugin storms leave the parameter relay consistent (the
//     exact double free that aborted the DAW)
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
#include <limits>
#include <memory>
#include <new>
#include <thread>
#include <vector>

#include <juce_events/juce_events.h>                 // ScopedJuceInitialiser_GUI
#include <juce_audio_processors/juce_audio_processors.h>

#include "../src/PluginProcessor.h"
#include "../src/PluginEditor.h"   // v2.19: SpectrumPanel readout statics
#include "../src/CostesStrip.h"    // v2.55.0: the internal strip's API
#include "../src/GRForwarder.h"    // v2.67.0: the Studio One GR forwarding

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

// a ceiling check that still prints its reading on failure - a silent
// expectTrue (x <= y) leaves a failed build unreadable (the v2.58.0
// lesson: the two failing checks printed no values)
void expectAtMost (double value, double maximum,
                   const juce::String& description)
{
    ++numChecks;

    if (value > maximum)
    {
        ++numFailed;
        std::cout << "        FAIL: " << description
                  << " (got " << juce::String (value, 4)
                  << ", expected at most " << juce::String (maximum, 2)
                  << ")" << std::endl;
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


// v2.47.0 -> v2.49.0 (Clement's A/B call: "implémente seulement ta
// méthode H1 optimisée, sans mon idée de Max Hold"): the
// panel draws a SINGLE EQ curve, now the PURE optimised H1
// cross-spectrum member - the same estimator, gates and display
// chain the v2.48 blend read in its junk-fallback bands, with NO
// max-hold members voting (the whole max family was retired in
// v2.54 - roadmap point A - after never feeding the drawn curve). The contracts, exercised end to end with the REAL
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
    expectTrue (xml != nullptr && xml->getStringAttribute ("version") == TCS_VERSION,
                "saved state carries the current version tag (" TCS_VERSION ")");

    TheChannelStripAudioProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());

    expectNear (restored.getTiltDb(), -3.0,  0.01, "tilt restored");
    expectNear (restored.getAnaDbDiv(), 12.0,  0.01, "spectrum dB/div restored");
    expectNear (restored.getEqDbDiv(),  4.0,  0.01, "EQ dB/div restored");
    expectTrue (! restored.getAnalyserVisible(), "analyser visibility restored");
    expectNear (restored.getSpecTau(), 0.15, 0.01, "response speed restored");
    expectNear (restored.getLufsTarget(), -37.0, 0.01, "loudness reference restored");
}

// v2.52 (point B): the tilt and response-speed buttons moved from the
// sidebar onto the analyser strip - SpectrumPanel owns, wires and
// RESTORES them itself now. This locks in the two guarantees the move
// must keep: (1) a panel constructed after the session state was
// loaded restores the persisted tilt/speed onto its own buttons (the
// old editor-side restore moved into the panel); (2) the speed value
// is canonicalised to exactly one of the two buttons (any legacy
// in-between value snaps to the closest, defaulting to Slow).
void testAnalyserStripButtons()
{
    beginTest ("v2.52: the strip's own tilt/speed buttons restore the session");

    {
        TheChannelStripAudioProcessor proc;
        proc.setTiltDb (-3.0f);
        proc.setSpecTau (0.15f); // Fast

        SpectrumPanel panel (proc);

        expectTrue (panel.getTiltButton (0)->getToggleState(),
                    "the -3 tilt button is selected on the strip");
        expectTrue (! panel.getTiltButton (1)->getToggleState(),
                    "the default -4.5 tilt button is not selected");
        expectTrue (panel.getSpeedButton (0)->getToggleState(),
                    "the Fast speed button is selected on the strip");
        expectTrue (! panel.getSpeedButton (1)->getToggleState(),
                    "the Slow speed button is not selected");
    }

    {
        // the default panel: nothing saved -> -4.5 tilt, Slow speed,
        // and the processor value canonicalised to the exact tau
        TheChannelStripAudioProcessor proc;
        SpectrumPanel panel (proc);

        expectTrue (panel.getTiltButton (1)->getToggleState(),
                    "a fresh panel selects the default -4.5 tilt");
        expectTrue (panel.getSpeedButton (1)->getToggleState(),
                    "a fresh panel selects the default Slow speed");
        expectNear (proc.getSpecTau(), 1.0, 0.001,
                    "the speed is canonicalised to exactly Slow's tau");
    }
}

// v2.53 (point B2): the scale zoom buttons. The v2.51-v2.52
// click-to-cycle is gone: each scale column now carries its own "+"
// and "-", children of SpectrumPanel (the v2.52 tilt/speed pattern).
// This locks in the four guarantees of the move: (1) a fresh
// processor carries the NEW defaults (12 dB/div spectra = Clement's
// [+24;-48] starting point, 4 dB/div EQ = +/-12 dB); (2) the
// ladders - pure statics since v2.53 - step exactly one zoom per
// press and CLAMP at their ends ("+" cannot magnify past 9 / 2
// dB/div, "-" cannot widen past 15 / 8); (3) a panel constructed
// after the session state was loaded restores the persisted zooms
// onto itself; (4) the buttons mirror the ladders' endpoints -
// disabled at the rung they cannot leave, alive everywhere else.
void testAnalyserZoomButtons()
{
    beginTest ("v2.53: the scale +/- zoom buttons");

    // (1) the new defaults - a fresh processor, nothing saved
    {
        TheChannelStripAudioProcessor proc;

        expectNear (proc.getAnaDbDiv(), 12.0, 0.001,
                    "the fresh analyser default is 12 dB/div ([+24;-48])");
        expectNear (proc.getEqDbDiv(), 4.0, 0.001,
                    "the fresh EQ default is 4 dB/div (+/-12 dB)");
    }

    // (2) the ladders: one zoom per step, clamped at both ends
    {
        // "+" magnifies: 15 -> 12 -> 9, then stays
        expectNear (SpectrumPanel::anaZoomIn (15.0f), 12.0, 0.001,
                    "spectrum +: 15 -> 12 dB/div");
        expectNear (SpectrumPanel::anaZoomIn (12.0f), 9.0, 0.001,
                    "spectrum +: 12 -> 9 dB/div");
        expectNear (SpectrumPanel::anaZoomIn (9.0f), 9.0, 0.001,
                    "spectrum + clamps at 9 dB/div");

        // "-" widens: 9 -> 12 -> 15, then stays
        expectNear (SpectrumPanel::anaZoomOut (9.0f), 12.0, 0.001,
                    "spectrum -: 9 -> 12 dB/div");
        expectNear (SpectrumPanel::anaZoomOut (12.0f), 15.0, 0.001,
                    "spectrum -: 12 -> 15 dB/div");
        expectNear (SpectrumPanel::anaZoomOut (15.0f), 15.0, 0.001,
                    "spectrum - clamps at 15 dB/div");

        // EQ "+": 8 -> 6 -> 4 -> 2, then stays
        expectNear (SpectrumPanel::eqZoomIn (8.0f), 6.0, 0.001,
                    "EQ +: 8 -> 6 dB/div");
        expectNear (SpectrumPanel::eqZoomIn (6.0f), 4.0, 0.001,
                    "EQ +: 6 -> 4 dB/div");
        expectNear (SpectrumPanel::eqZoomIn (4.0f), 2.0, 0.001,
                    "EQ +: 4 -> 2 dB/div");
        expectNear (SpectrumPanel::eqZoomIn (2.0f), 2.0, 0.001,
                    "EQ + clamps at 2 dB/div");

        // EQ "-": 2 -> 4 -> 6 -> 8, then stays
        expectNear (SpectrumPanel::eqZoomOut (2.0f), 4.0, 0.001,
                    "EQ -: 2 -> 4 dB/div");
        expectNear (SpectrumPanel::eqZoomOut (4.0f), 6.0, 0.001,
                    "EQ -: 4 -> 6 dB/div");
        expectNear (SpectrumPanel::eqZoomOut (6.0f), 8.0, 0.001,
                    "EQ -: 6 -> 8 dB/div");
        expectNear (SpectrumPanel::eqZoomOut (8.0f), 8.0, 0.001,
                    "EQ - clamps at 8 dB/div");
    }

    // (3) + (4): the panel restores the session zooms and mirrors the
    // ladders' endpoints in its buttons' enabled states
    {
        // the most magnified rung: "+" disabled, "-" alive
        TheChannelStripAudioProcessor proc;
        proc.setAnaDbDiv (9.0f);
        proc.setEqDbDiv (2.0f);

        SpectrumPanel panel (proc);

        expectNear (panel.getDbPerDiv(), 9.0, 0.001,
                    "the panel restores the saved 9 dB/div spectrum zoom");
        expectNear (panel.getEqDbPerDiv(), 2.0, 0.001,
                    "the panel restores the saved 2 dB/div EQ zoom");
        expectTrue (! panel.getAnaZoomButton (0)->isEnabled(),
                    "spectrum + is disabled at 9 dB/div");
        expectTrue (panel.getAnaZoomButton (1)->isEnabled(),
                    "spectrum - is alive at 9 dB/div");
        expectTrue (! panel.getEqZoomButton (0)->isEnabled(),
                    "EQ + is disabled at 2 dB/div");
        expectTrue (panel.getEqZoomButton (1)->isEnabled(),
                    "EQ - is alive at 2 dB/div");
    }

    {
        // the widest rung: "+" alive, "-" disabled
        TheChannelStripAudioProcessor proc;
        proc.setAnaDbDiv (15.0f);
        proc.setEqDbDiv (8.0f);

        SpectrumPanel panel (proc);

        expectTrue (panel.getAnaZoomButton (0)->isEnabled(),
                    "spectrum + is alive at 15 dB/div");
        expectTrue (! panel.getAnaZoomButton (1)->isEnabled(),
                    "spectrum - is disabled at 15 dB/div");
        expectTrue (panel.getEqZoomButton (0)->isEnabled(),
                    "EQ + is alive at 8 dB/div");
        expectTrue (! panel.getEqZoomButton (1)->isEnabled(),
                    "EQ - is disabled at 8 dB/div");
    }
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
    expectTrue (report.contains ("TheChannelStrip " TCS_VERSION),
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

    // v2.57.0 (Clement's remark 3): the estimated CPU load. A fresh
    // prepare re-arms the metering window and retires the reading
    // (negative = the sidebar shows "--"); then a second of simulated
    // playback - real 10 ms gaps between blocks, like a host feeding
    // 512-sample blocks at 48 kHz - closes the window and publishes a
    // sane percent of that real time
    beginTest ("v2.57.0: the estimated CPU load window");

    proc.prepareToPlay (48000.0, 512);

    expectTrue (proc.getEstimatedCpuLoadPercent() < 0.0,
                "no CPU reading before a full window of audio");

    for (int i = 0; i < 120; ++i)
    {
        processBlocks (proc, src, 1, block);
        juce::Thread::sleep (10);   // 120 blocks x ~10 ms > 1 s of real time
    }

    const double cpu = proc.getEstimatedCpuLoadPercent();
    expectTrue (cpu >= 0.0 && cpu <= 100.0,
                "a full window publishes a CPU percent within [0, 100]");
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

// ---------------------------------------------------------------------------
// v2.54.0 (roadmap point A): the display-side torture batch - three
// new contracts that complete the v2.18 torture ideas (console storms,
// rate churn and hostile sessions above). All of them run the REAL
// production path with the editor open (measurement included):
//
//   1. adversarial AUDIO: full-scale DC (both polarities), hard
//      squares, one-sample transients, channel asymmetry, the extreme
//      rates / block sizes the churn test leaves out (22050 and
//      176400 Hz, 1 and 4096 frames) and even NaN input. Nothing may
//      crash, clean material must come out clean and bounded, and a
//      releaseResources / prepareToPlay restart must reset the path.
//      The NaN case is a DOCUMENTED LIMIT, not a bug: the measurement
//      one-poles legitimately smear a NaN over their window, so the
//      contract is survival plus a clean restart - never live
//      decontamination;
//   2. state corner cases: a perfectly valid XML container whose
//      ATTRIBUTES are hostile (a bare element, empty strings, garbage
//      / absurd / negative / fractional scale values) must keep the
//      defaults or snap onto the ladders, and 50 rapid save / restore
//      cycles must be a byte-identical fixed point;
//   3. display stress: 1000 analysis ticks over an evolving sweep
//      with every measured bin finite, a deterministic chaos sequence
//      of display toggles interleaved with session save / loads whose
//      final state is exactly the prescribed one, and degenerate
//      panel sizes (0x0 and 10x10 must survive resized()).
// ---------------------------------------------------------------------------
void testTortureAdversarialAudio()
{
    beginTest ("torture: adversarial audio (DC, square, transients, NaN)");

    auto allFinite = [] (const juce::AudioBuffer<float>& b)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (c, i)))
                    return false;

        return true;
    };

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEditorOpen (true);   // the measurement path runs too

    auto mock = std::make_unique<MockConsole> (false);   // gain 1: identity
    auto* mockPtr = mock.get();
    proc.getHostedStrip().setPlugin (std::move (mock));

    juce::MidiBuffer noMidi;
    juce::AudioBuffer<float> block (2, 512);

    // 1) full-scale DC, both polarities: bounded, finite, at unity
    for (const float dc : { 1.0f, -1.0f })
    {
        for (int n = 0; n < 200; ++n)   // ~2 s
        {
            for (int c = 0; c < block.getNumChannels(); ++c)
                for (int i = 0; i < block.getNumSamples(); ++i)
                    block.setSample (c, i, dc);

            proc.processBlock (block, noMidi);
        }

        expectTrue (allFinite (block), "full-scale DC stays finite");
        expectNear ((double) peakOf (block), std::abs ((double) dc), 0.01,
                    "DC passes through at unity (no clipping, no fold)");
    }

    // 2) a hard full-scale square wave
    for (int n = 0; n < 200; ++n)
    {
        for (int i = 0; i < block.getNumSamples(); ++i)
        {
            const float s = (((n * 512 + i) % 64) < 32) ? 1.0f : -1.0f;

            block.setSample (0, i, s);
            block.setSample (1, i, s);
        }

        proc.processBlock (block, noMidi);
    }

    expectTrue (allFinite (block), "the full-scale square stays finite");
    expectTrue (peakOf (block) <= 1.0f, "the square never exceeds unity");
    expectTrue (peakOf (block, 1) <= 1.0f, "channel 2 never exceeds unity");

    // 3) silence right after the loud material: both the dry and the
    //    hosted paths are linear in silence, so the output must be
    //    exactly silent (whatever the crossfade state)
    fillSilence (block);
    proc.processBlock (block, noMidi);

    expectTrue (peakOf (block) == 0.0f && peakOf (block, 1) == 0.0f,
                "silence after loud material comes out exactly silent");

    // 4) one-sample full-scale transients in silence
    block.clear();

    for (int burst = 0; burst < 16; ++burst)
    {
        for (int c = 0; c < 2; ++c)
            block.setSample (c, burst * 31, (burst % 2 == 0) ? 1.0f : -1.0f);

        proc.processBlock (block, noMidi);
    }

    expectTrue (allFinite (block), "one-sample transients stay finite");
    expectTrue (peakOf (block) <= 1.0f, "transients never exceed unity");

    // 5) asymmetric channels: a full-scale square on L, silence on R
    for (int n = 0; n < 50; ++n)
    {
        for (int i = 0; i < block.getNumSamples(); ++i)
        {
            block.setSample (0, i, (((n * 512 + i) % 64) < 32) ? 1.0f : -1.0f);
            block.setSample (1, i, 0.0f);
        }

        proc.processBlock (block, noMidi);
    }

    expectTrue (allFinite (block), "asymmetric material stays finite");
    expectTrue (peakOf (block, 1) < 1e-9f, "the silent channel stays silent");
    expectTrue (mockPtr->lastChannelCount.load() == 2,
                "the console still sees a stereo buffer");

    // 6) the extremes the churn test leaves out: 22050 and 176400 Hz
    //    at block sizes 1 and 4096, square material
    {
        struct Leg { double rate; int blockSize; };
        const Leg legs[] = { { 22050.0, 1 }, { 22050.0, 4096 },
                             { 176400.0, 1 }, { 176400.0, 4096 } };

        for (const auto& leg : legs)
        {
            proc.releaseResources();
            proc.prepareToPlay (leg.rate, leg.blockSize);

            juce::AudioBuffer<float> legBlock (2, leg.blockSize);
            int phase = 0;

            for (int n = 0; n < 64; ++n)
            {
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < leg.blockSize; ++i)
                        legBlock.setSample (c, i,
                            ((phase++ % 64) < 32) ? 1.0f : -1.0f);

                proc.processBlock (legBlock, noMidi);
            }

            const juce::String legName = juce::String (leg.rate, 0)
                                              + " Hz / "
                                              + juce::String (leg.blockSize)
                                              + " frames";

            expectTrue (allFinite (legBlock),
                        "squares stay finite at " + legName);
            expectTrue (peakOf (legBlock) <= 1.0f && peakOf (legBlock, 1) <= 1.0f,
                        "output stays bounded at " + legName);
        }
    }

    // 7) NaN input: a DOCUMENTED limit, not a bug. The measurement
    //    one-poles legitimately smear the NaN, and the gain-correction
    //    READOUT is never applied to the audio, so the only contract
    //    is survival: no crash, the console keeps processing, and a
    //    clean releaseResources / prepareToPlay restart resets the
    //    audio path (the passthrough itself carries no state).
    {
        const float nanValue = std::numeric_limits<float>::quiet_NaN();

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < block.getNumSamples(); ++i)
                block.setSample (c, i, nanValue);

        const int callsBefore = mockPtr->processCallCount.load();

        proc.processBlock (block, noMidi);
        proc.processBlock (block, noMidi);

        expectTrue (mockPtr->processCallCount.load() >= callsBefore + 2,
                    "a NaN block flows through without crashing");

        proc.releaseResources();
        proc.prepareToPlay (48000.0, 512);

        SineSource src (500.0, 48000.0, 0.4f);
        processBlocks (proc, src, 100, block);

        expectTrue (allFinite (block),
                    "after a clean restart the output is finite again");
        expectNear ((double) peakOf (block), 0.4, 0.02,
                    "after a clean restart the signal is faithful again");
    }
}

void testTortureStateCornerCases()
{
    beginTest ("torture: state corner cases (hostile attributes, fixed point)");

    // a valid TCS container whose ATTRIBUTES are hostile: nullptr means
    // the attribute is absent, an empty string means present-but-empty
    auto loadScales = [] (const char* version, const char* ana, const char* eq)
    {
        auto session = std::make_unique<juce::XmlElement> ("TCS");

        if (version != nullptr)
            session->setAttribute ("version", version);
        if (ana != nullptr)
            session->setAttribute ("anaDbDiv", ana);
        if (eq != nullptr)
            session->setAttribute ("eqDbDiv", eq);

        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (*session, block);

        TheChannelStripAudioProcessor proc;
        proc.setStateInformation (block.getData(), (int) block.getSize());

        return std::pair (proc.getAnaDbDiv(), proc.getEqDbDiv());
    };

    // a bare <TCS/> element: every attribute missing, defaults intact
    {
        const auto s = loadScales (nullptr, nullptr, nullptr);

        expectTrue (s.first == 12.0f && s.second == 4.0f,
                    "a bare TCS element keeps the fresh scale defaults");
    }

    // empty-string attributes read as "not present" - defaults intact
    {
        const auto s = loadScales ("9.9.9", "", "");

        expectTrue (s.first == 12.0f && s.second == 4.0f,
                    "empty scale attributes keep the fresh defaults");
    }

    // a futuristic version is MODERN: no legacy remap, the ladder
    // values load with their current meaning (the eq 4 = +/-12 case)
    {
        const auto s = loadScales ("9.9.9", "15", "4");

        expectTrue (s.first == 15.0f && s.second == 4.0f,
                    "a futuristic version keeps the modern scale meanings");
    }

    // absurd magnitudes snap onto the WIDE end of each ladder
    {
        const auto s = loadScales ("9.9.9", "99999", "99999");

        expectTrue (s.first == 15.0f && s.second == 8.0f,
                    "absurd scale values snap to the widest rungs");
    }

    // non-numeric strings parse to 0 and snap onto the NARROW end
    {
        const auto s = loadScales ("9.9.9", "abc", "abc");

        expectTrue (s.first == 9.0f && s.second == 2.0f,
                    "garbage scale strings snap to the narrow rungs");
    }

    // negative values behave like 0: the bottom rungs
    {
        const auto s = loadScales ("9.9.9", "-5", "-5");

        expectTrue (s.first == 9.0f && s.second == 2.0f,
                    "negative scale values snap to the bottom rungs");
    }

    // fractional off-ladder values snap by threshold, never crash
    {
        const auto s = loadScales ("9.9.9", "13.5", "2.6");

        expectTrue (s.first == 15.0f && s.second == 2.0f,
                    "fractional scales snap onto the ladder by threshold");
    }

    // 50 rapid save / restore cycles mid-playback: the loop must be a
    // byte-identical fixed point (every save equals the first one) and
    // the hosted console must keep processing throughout
    {
        TheChannelStripAudioProcessor proc;
        proc.prepareToPlay (48000.0, 512);
        proc.setEditorOpen (true);

        auto mock = std::make_unique<MockConsole> (false);
        auto* mockPtr = mock.get();
        proc.getHostedStrip().setPlugin (std::move (mock));

        juce::AudioBuffer<float> block (2, 512);
        SineSource src (500.0, 48000.0, 0.3f);
        processBlocks (proc, src, 8, block);   // the crossfade settles

        juce::MemoryBlock reference;
        proc.getStateInformation (reference);

        bool fixedPoint = true;

        for (int n = 0; n < 50; ++n)
        {
            juce::MemoryBlock saved;
            proc.getStateInformation (saved);
            fixedPoint = fixedPoint && saved == reference;

            proc.setStateInformation (saved.getData(), (int) saved.getSize());
            processBlocks (proc, src, 2, block);
        }

        expectTrue (fixedPoint,
                    "50 save / restore cycles are a byte-identical fixed point");
        expectTrue (proc.getHostedStrip().getActivePlugin() != nullptr,
                    "the hosted console survives the 50 cycles");
        expectNear ((double) peakOf (block), 0.3, 0.02,
                    "audio stays faithful through the 50 cycles");
        // exactly 8 settling blocks + 50 x 2 cycle blocks = 108 calls:
        // the console processed EVERY block, none was dropped
        expectTrue (mockPtr->processCallCount.load() >= 108,
                    "the console kept processing through the 50 cycles");
    }
}

void testTortureDisplayStress()
{
    beginTest ("torture: 1000 analysis ticks, display chaos, degenerate sizes");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEditorOpen (true);

    juce::MidiBuffer noMidi;
    juce::AudioBuffer<float> block (2, 512);

    SpectrumPanel panel (proc);

    // 1) 1000 ticks over a full-range sweep (40 Hz - 20 kHz, the real
    //    cadence of one tick per ~3 blocks): every measured EQ bin must
    //    stay finite - no NaN, no drift
    {
        double phase = 0.0;
        int measuredFinite = 0, measuredCount = 0;

        for (int tick = 0; tick < 1000; ++tick)
        {
            const double freq = 40.0
                * std::pow (2.0, 9.0 * (tick % 100) / 99.0);
            const double omega = juce::MathConstants<double>::twoPi
                                      * freq / 48000.0;

            for (int n = 0; n < 3; ++n)
            {
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < block.getNumSamples(); ++i)
                        block.setSample (c, i,
                            0.4f * (float) std::sin (phase + omega * i));

                phase += omega * (double) block.getNumSamples();
                proc.processBlock (block, noMidi);
            }

            panel.runAnalysisTick();
        }

        for (int b = 0; b < SpectrumAnalyzer::numScopeBins; ++b)
        {
            if (! panel.isEqBinMeasured (b))
                continue;

            ++measuredCount;

            if (std::isfinite (panel.getEqDbAtBin (b)))
                ++measuredFinite;
        }

        expectTrue (measuredCount > 0,
                    "the sweep left measured EQ bins behind");
        expectTrue (measuredFinite == measuredCount,
                    "every measured EQ bin is finite after 1000 ticks");
    }

    // 2) a deterministic chaos sequence: display toggles and session
    //    save / loads interleaved with audio - no crash, both scales
    //    must stay ON their ladders, and the pinned pre-chaos session
    //    must restore exactly
    {
        juce::MemoryBlock pinned;
        proc.getStateInformation (pinned);   // the pre-chaos defaults

        SineSource src (500.0, 48000.0, 0.3f);

        for (int n = 0; n < 100; ++n)
        {
            proc.setAnalyserVisible (n % 3 == 0);
            proc.setTiltDb ((n % 2 == 0) ? -3.0f : -6.0f);
            proc.setAnaDbDiv ((n % 2 == 0)
                                  ? SpectrumPanel::anaZoomOut (proc.getAnaDbDiv())
                                  : SpectrumPanel::anaZoomIn (proc.getAnaDbDiv()));
            proc.setEqDbDiv ((n % 3 == 0)
                                 ? SpectrumPanel::eqZoomIn (proc.getEqDbDiv())
                                 : SpectrumPanel::eqZoomOut (proc.getEqDbDiv()));
            proc.setLufsTarget ((n % 4 == 0) ? -14.0f : -23.0f);

            if (n % 10 == 0)
            {
                juce::MemoryBlock blob;
                proc.getStateInformation (blob);
                proc.setStateInformation (blob.getData(), (int) blob.getSize());
            }

            for (int k = 0; k < 3; ++k)
            {
                src.fill (block);
                proc.processBlock (block, noMidi);
            }

            panel.runAnalysisTick();
        }

        const float ana = proc.getAnaDbDiv();
        const float eq = proc.getEqDbDiv();

        expectTrue ((ana == 15.0f || ana == 12.0f || ana == 9.0f)
                        && (eq == 8.0f || eq == 6.0f || eq == 4.0f || eq == 2.0f),
                    "the chaos kept both scales on their ladders");

        proc.setStateInformation (pinned.getData(), (int) pinned.getSize());

        expectTrue (proc.getAnalyserVisible(),
                    "the pre-chaos session restores the visible flag");
        expectNear (proc.getTiltDb(), -4.5, 0.001,
                    "the pre-chaos session restores the tilt");
        expectNear (proc.getAnaDbDiv(), 12.0, 0.001,
                    "the pre-chaos session restores the spectrum zoom");
        expectNear (proc.getEqDbDiv(), 4.0, 0.001,
                    "the pre-chaos session restores the EQ zoom");
        expectNear ((double) proc.getLufsTarget(), -23.0, 0.001,
                    "the pre-chaos session restores the LUFS reference");
    }

    // 3) degenerate sizes: resized() must survive zero and tiny bounds
    //    (the +/- buttons land on negative coordinates - JUCE doesn't
    //    care) and the panel must come back fully serviceable
    {
        panel.setSize (0, 0);
        panel.runAnalysisTick();

        panel.setSize (10, 10);
        panel.runAnalysisTick();

        panel.setSize (860, 180);
        panel.runAnalysisTick();

        expectTrue (panel.getWidth() == 860 && panel.getHeight() == 180,
                    "the panel settles back on the real size");
        expectNear (panel.getDbPerDiv(), 12.0, 0.001,
                    "the panel's spectrum zoom survives the resize tour");
        expectNear (panel.getEqDbPerDiv(), 4.0, 0.001,
                    "the panel's EQ zoom survives the resize tour");
    }
}

// ---------------------------------------------------------------------------
// v2.51.1 hotfix tests (the Studio Pro 8.1.2 crash). The abort was a
// POINTER_BEING_FREED_WAS_NOT_ALLOCATED in operator delete from
// instantiateHosted: two overlapping instantiation runs raced the
// HostedStrip relay vector to a double free. Three contracts:
//
//   1. superseded guard - a request whose ident no longer matches
//      (checkpoint 1) aborts BEFORE createPluginInstance, so a fresh
//      shell with an EMPTY ident must refuse a ghost description
//      without touching its status area;
//   2. duplicate-restore skip - a restore with nothing left to apply
//      while a console is already hosted returns silently BEFORE the
//      ident checkpoint, so the hosted mock is left exactly as it is;
//   3. relay thread safety - concurrent setPlugin / clearPlugin runs
//      (the exact overlap that crashed the DAW) leave a CONSISTENT
//      relay: after a final clear and the 150 ms retire grace, the
//      vector is empty and nothing was freed twice.
// ---------------------------------------------------------------------------
void testSupersededInstantiationGuard()
{
    beginTest ("v2.51.1: superseded instantiation aborts before building");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    // a fresh shell has NO console selected: the guarded ident is empty
    expectTrue (proc.getCurrentPluginIdent().isEmpty(),
                "a fresh shell has an empty hosted ident");

    // a ghost description pointing at a file that does not exist
    juce::PluginDescription ghost;
    ghost.name = "bx_console SSL 4000 G (superseded)";
    ghost.pluginFormatName = "VST3";
    ghost.fileOrIdentifier = "/nonexistent/superseded/bx_console SSL 4000 G.vst3";
    ghost.uniqueId = 0x87654321;

    // a direct instantiation whose ident does not match the (empty)
    // selection must abort at checkpoint 1 - BEFORE the plugin is even
    // attempted. Both entry modes (fresh selection / pending restore)
    // are driven, and neither may leave a trace in the status area.
    proc.instantiateHostedForTest (ghost, false);
    proc.instantiateHostedForTest (ghost, true);

    expectTrue (! proc.getHostStatus().contains ("ERROR"),
                "a superseded request leaves no error status");
    expectTrue (! proc.getHostStatus().contains ("Hosted"),
                "a superseded request never hosts anything");
    expectTrue (proc.getHostedStrip().getActivePlugin() == nullptr,
                "no console was created by the superseded requests");
}

void testDuplicateRestoreSkip()
{
    beginTest ("v2.51.1: a duplicate restore with nothing to apply is skipped");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    proc.getHostedStrip().setPlugin (std::make_unique<MockSilenceConsole>());
    const auto* hostedBefore = proc.getHostedStrip().getActivePlugin();
    expectTrue (hostedBefore != nullptr, "a console is hosted before the restore");

    // a restore while a console is ALREADY hosted and the pending state
    // blob is empty is the launch-time duplicate (the editor selection
    // and the session restore converging on the same slot). It must be
    // skipped BEFORE the ident checkpoint - the hosted instance is left
    // alone instead of being torn down and rebuilt.
    juce::PluginDescription ghost;
    ghost.name = "bx_console SSL 4000 G (duplicate)";
    ghost.pluginFormatName = "VST3";
    ghost.fileOrIdentifier = "/nonexistent/duplicate/bx_console SSL 4000 G.vst3";
    ghost.uniqueId = 0x87654321;

    proc.instantiateHostedForTest (ghost, true);

    expectTrue (! proc.getHostStatus().contains ("ERROR"),
                "the duplicate restore leaves no error status");
    expectTrue (proc.getHostedStrip().getActivePlugin() == hostedBefore,
                "the duplicate restore left the hosted console untouched");
}

void testRelayThreadSafety()
{
    beginTest ("v2.51.1: relay attach/detach survives concurrent swap storms");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    // the exact overlap that aborted Studio Pro: setPlugin (which
    // detaches the old relay then attaches the new one) and clearPlugin
    // (which detaches) running on several threads at once. Each mock
    // carries a REAL AudioProcessorParameter, so every swap pushes to
    // and pops from the guarded vector - the unguarded code double
    // freed its buffer here.
    {
        auto churn = [&proc]()
        {
            for (int i = 0; i < 150; ++i)
            {
                proc.getHostedStrip().setPlugin (std::make_unique<MockParamConsole>());
                proc.getHostedStrip().clearPlugin();
            }
        };

        std::thread a (churn), b (churn), c (churn);
        a.join();
        b.join();
        c.join();
    }

    // a final clear, then let the 150 ms retire grace period drain the
    // outgoing slots before asserting the relay is exactly empty
    proc.getHostedStrip().clearPlugin();
    juce::Thread::sleep (400);

    expectTrue (proc.getHostedStrip().getRelayedParameterCount() == 0,
                "the relay is empty and consistent after the storm");
    expectTrue (proc.getHostedStrip().getActivePlugin() == nullptr,
                "no console is hosted after the final clear");
}

// ---------------------------------------------------------------------------
// v2.55.0 (roadmap C1): the internal Costes strip. It is hosted on the very
// same HostedStrip as the consoles, so the checks mirror the console tests:
// host/unhost through the title's toggle, ident, relay, latency, the audio
// response of the MVP path (trim in / low cut / high cut / trim out), the
// per-ident knob memory, and the full session roundtrip on a FRESH
// processor (the maybeRestoreHosted branch that re-hosts the internal strip
// without going through the KnownPluginList).
// ---------------------------------------------------------------------------

void testCostesStripBasics()
{
    beginTest ("v2.58.0: the Costes strip - title-toggle host/unhost, ident, memory");

    juce::MidiBuffer noMidi;

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    expectTrue (proc.getCurrentPluginIdent().isEmpty(),
                "a fresh shell hosts nothing");
    expectTrue (! proc.isCostesStripActive(),
                "a fresh shell does not report the internal strip");

    proc.toggleCostesStrip();

    auto* hosted = proc.getHostedStrip().getActivePlugin();
    expectTrue (hosted != nullptr, "the toggle hosts the internal strip");
    expectTrue (hosted != nullptr && hosted->getName() == "Costes Strip",
                "the hosted plugin is the Costes strip");
    expectTrue (proc.getCurrentPluginIdent() == CostesStrip::shellIdent(),
                "the internal ident is published");
    expectTrue (proc.isCostesStripActive(),
                "the shell reports the internal strip as active");
    expectTrue (proc.getHostedStrip().getRelayedParameterCount() == 45,
                "the strip's forty-five parameters are relayed");
    expectTrue (proc.getHostedStrip().getReportedLatency() == 0,
                "the strip reports zero latency");
    expectTrue (hosted != nullptr && hosted->getParameters().size() == 45,
                "the strip exposes exactly forty-five parameters");

    // the parked defaults (trim at 0 dB, cuts at 16 Hz / 24 kHz, order 2,
    // drive at 0 dB, the EQ's four bands at 0 dB) are unity: run past the
    // 50 ms crossfade, then compare one processed block with its own
    // reference. At 0 dB every EQ band is the bit-exact identity filter
    // (b == a coefficient-for-coefficient), so the four extra biquads
    // change nothing
    {
        juce::AudioBuffer<float> block (2, 512);
        juce::AudioBuffer<float> reference;
        SineSource src (1000.0, 48000.0, 0.5f);

        for (int i = 0; i < 12; ++i)
        {
            src.fill (block);
            proc.processBlock (block, noMidi);
        }

        src.fill (block);
        reference.makeCopyOf (block);
        proc.processBlock (block, noMidi);

        double refPeak = 0.0, outPeak = 0.0;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block.getNumSamples(); ++i)
            {
                refPeak = juce::jmax (refPeak, (double) std::abs (reference.getSample (ch, i)));
                outPeak = juce::jmax (outPeak, (double) std::abs (block.getSample (ch, i)));
            }

        expectNear (juce::Decibels::gainToDecibels (outPeak / refPeak), 0.0, 0.5,
                    "the parked defaults are unity at 1 kHz");
    }

    // the per-ident memory: a moved knob survives the unhost/rehost cycle,
    // exactly like a deselected console keeps its settings
    auto* strip = dynamic_cast<CostesStrip*> (proc.getHostedStrip().getActivePlugin());
    expectTrue (strip != nullptr, "the hosted strip is reachable as a CostesStrip");

    if (strip != nullptr)
    {
        strip->getApvts().getParameterAsValue ("trimInDb").setValue (-6.0f);

        proc.toggleCostesStrip(); // off: remembers the strip's state
        expectTrue (proc.getHostedStrip().getActivePlugin() == nullptr,
                    "the second toggle clears the slot");
        expectTrue (! proc.isCostesStripActive(),
                    "the shell does not report the strip after the clear");

        proc.toggleCostesStrip(); // on again: the remembered state applies

        auto* rehosted = dynamic_cast<CostesStrip*> (proc.getHostedStrip().getActivePlugin());
        expectTrue (rehosted != nullptr, "the strip is rehosted after the toggle cycle");

        if (rehosted != nullptr)
            expectNear (rehosted->getApvts().getRawParameterValue ("trimInDb")->load(),
                        -6.0, 0.01,
                        "the knob memory survives the toggle cycle");
    }
}

void testCostesStripAudio()
{
    beginTest ("v2.59.0: the Costes strip audio - trim, on/off cut plates, seven legal Butterworth slopes, full-band tilt, drive, oversampling");

    juce::MidiBuffer noMidi;

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    proc.toggleCostesStrip();

    auto* strip = dynamic_cast<CostesStrip*> (proc.getHostedStrip().getActivePlugin());
    expectTrue (strip != nullptr, "the strip is hosted");

    if (strip == nullptr)
        return;

    // steady-state peak of a fresh block after ~256 ms of settling (the
    // 50 ms crossfade plus the 20 ms trim smoothers plus the biquad rings)
    auto measurePeak = [&proc, &noMidi] (double frequencyHz) -> double
    {
        SineSource src (frequencyHz, 48000.0, 0.25f);
        juce::AudioBuffer<float> block (2, 512);

        for (int i = 0; i < 24; ++i)
        {
            src.fill (block);
            proc.processBlock (block, noMidi);
        }

        src.fill (block);
        proc.processBlock (block, noMidi);

        double peak = 0.0;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block.getNumSamples(); ++i)
                peak = juce::jmax (peak, (double) std::abs (block.getSample (ch, i)));

        return peak;
    };

    // coherent lock-in amplitude - the robust instrument. A peak reading of
    // a high-frequency tone is grid-quantized: at 8 kHz over 48 kHz the
    // sampling grid is exactly 6 points per cycle and never touches the
    // 90-degree crest, so the peak lands up to 1.25 dB below the true
    // amplitude, at a loss that varies with the filter's phase shift. The
    // lock-in correlates the block against the probe's own cos/sin over
    // an exact integer number of cycles and returns the true bin amplitude
    // whatever the phase. Every probe here is a multiple of 100 Hz, so one
    // 480-sample block at 48 kHz holds a whole number of cycles.
    const int lockinBlocks = 24;

    auto measureAmplitude = [&proc, &noMidi] (double frequencyHz,
                                              double probeHz) -> double
    {
        SineSource src (frequencyHz, 48000.0, 0.25f);
        juce::AudioBuffer<float> block (2, 480);

        for (int i = 0; i < lockinBlocks; ++i)
        {
            src.fill (block);
            proc.processBlock (block, noMidi);
        }

        src.fill (block);
        proc.processBlock (block, noMidi);

        // the source phase at the measured block start: omega times
        // the total number of samples filled before it
        const double probeOmega = juce::MathConstants<double>::twoPi * probeHz / 48000.0;
        const double phase0 = probeOmega * (double) (lockinBlocks * 480);

        double c = 0.0, s = 0.0;

        for (int i = 0; i < 480; ++i)
        {
            const double phase = phase0 + probeOmega * (double) i;
            const double sample = block.getSample (0, i);
            c += sample * std::cos (phase);
            s += sample * std::sin (phase);
        }

        return 2.0 * std::sqrt (c * c + s * s) / 480.0;
    };

    const double unity1k = measureAmplitude (1000.0, 1000.0);

    // trim in: +6 dB, then back to unity for the rest of the test
    strip->getApvts().getParameterAsValue ("trimInDb").setValue (6.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0) / unity1k), 6.0, 0.3,
                "trim in +6 dB reads +6 dB");

    // v2.59.0 (Clement's remark 4): the trim in sweeps -24 to +12 dB now,
    // and a typed value beyond either end clamps to it
    strip->getApvts().getParameterAsValue ("trimInDb").setValue (-30.0f);
    expectNear (strip->getApvts().getRawParameterValue ("trimInDb")->load(), -24.0, 0.01,
                "the trim clamps a typed -30 dB to its -24 dB floor");
    strip->getApvts().getParameterAsValue ("trimInDb").setValue (20.0f);
    expectNear (strip->getApvts().getRawParameterValue ("trimInDb")->load(), 12.0, 0.01,
                "the trim clamps a typed +20 dB to its +12 dB ceiling");
    strip->getApvts().getParameterAsValue ("trimInDb").setValue (0.0f);

    // v2.57.0 (Clement's remark 1): the tilt is a full-band tilt now - a
    // straight line of adjustable slope in the log(f)/dB plane. The knob
    // is the +/- 12 dB gained or lost BETWEEN 20 Hz AND 20 kHz (about
    // +/- 6 dB at the band edges, 1.2 dB per octave at full tilt), and
    // the line pivots at 632.4555 Hz, the geometric centre of the band,
    // so an octave-uniform (pink-like) spectrum keeps its loudness at
    // any tilt. Four octaves of probe points pin the line: 100 Hz, the
    // 1 kHz midline, 8 kHz and 16 kHz. Expected values are the float64
    // simulation of the exact cascade (ten octave-spaced shelves plus
    // the flat -tilt/2 dB centring), parked cuts included.
    strip->getApvts().getParameterAsValue ("tiltDb").setValue (12.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (100.0, 100.0) / unity1k), -3.2, 0.75,
                "tilt +12 dB reads -3.2 dB at 100 Hz (the log line)");
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0) / unity1k), 0.8, 0.5,
                "tilt +12 dB reads +0.8 dB at 1 kHz (halfway up the line)");
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (8000.0, 8000.0) / unity1k), 4.4, 0.75,
                "tilt +12 dB reads +4.4 dB at 8 kHz (the log line)");
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (16000.0, 16000.0) / unity1k), 5.5, 0.75,
                "tilt +12 dB reads +5.5 dB at 16 kHz (toward the band edge)");

    strip->getApvts().getParameterAsValue ("tiltDb").setValue (-12.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (100.0, 100.0) / unity1k), 3.2, 0.75,
                "tilt -12 dB reads +3.2 dB at 100 Hz (the mirrored line)");
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (8000.0, 8000.0) / unity1k), -4.4, 0.75,
                "tilt -12 dB reads -4.4 dB at 8 kHz (the mirrored line)");
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (16000.0, 16000.0) / unity1k), -5.9, 0.75,
                "tilt -12 dB reads -5.9 dB at 16 kHz (toward the band edge)");

    strip->getApvts().getParameterAsValue ("tiltDb").setValue (0.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0) / unity1k), 0.0, 0.3,
                "back at zero the tilt cascade is exactly unity");

    // the cuts, v2.59.0: every probe reads through the coherent lock-in -
    // the v2.58.0 lesson generalised. The peak instrument is grid-quantised
    // (at 16 kHz the sampling grid is 3 points per cycle and the reading
    // swings with the filter's phase shift), and below roughly -90 dB it
    // reads float32 rounding residue instead of the filter; the lock-in
    // correlates against the probe over an exact integer number of cycles
    // (100 Hz = 1 cycle and 16 kHz = 160 cycles per 480-sample block at
    // 48 kHz), so a constant offset contributes exactly zero and the
    // reading is the true bin amplitude whatever rides underneath.
    //
    // Each cut now owns an on/off plate (Clement's remark 2): false parks
    // its section count at zero - a full bypass - and the slope sweep
    // holds only the seven legal Butterworth slopes 6 / 12 / 18 / 24 /
    // 36 / 48 / 72 dB/oct (Clement's remark 3, orders 1, 2, 3, 4, 6, 8,
    // 12). Expected values are the float64 simulation of the exact
    // cascades measured the harness way; the float32 emulation tracks
    // them within 0.001 dB down to -180 dB.
    //
    // the low cut: parked reference at 100 Hz (16 Hz / order 2, high cut
    // parked), then a 500 Hz corner probed at 100 Hz. OFF reads exactly
    // the reference (the +0.00 dB is the parked low cut's own residual
    // 100 Hz response); order 1 kills 100 Hz by 14.1 dB and every legal
    // order follows the Butterworth staircase
    // JUCE 8's gainToDecibels clamps its result at the minusInfinityDb
    // floor - the default is -100 dB - so a reading deeper than that
    // comes back as exactly -100.0000 and the deep cut probes below
    // would all read the floor instead of the filter. The five checks
    // that expect more than 100 dB of cut convert through this helper,
    // which pushes the floor out to -400 dB, far below even the 72
    // dB/oct float32 readings (about -249 dB)
    auto deepDb = [] (double amplitudeRatio) -> double
    {
        return juce::Decibels::gainToDecibels (amplitudeRatio, -400.0);
    };

    const double flatLow100 = measureAmplitude (100.0, 100.0);
    strip->getApvts().getParameterAsValue ("lowCutHz").setValue (500.0f);

    strip->getApvts().getParameterAsValue ("lowCutOn").setValue (false);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (100.0, 100.0) / flatLow100), 0.0, 0.3,
                "the low cut's OFF plate bypasses the filter");

    strip->getApvts().getParameterAsValue ("lowCutOn").setValue (true);
    strip->getApvts().getParameterAsValue ("lowCutOrder").setValue (1.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (100.0, 100.0) / flatLow100), -14.1, 0.8,
                "a 6 dB/oct first-order low cut reads -14.1 dB at 100 Hz");
    strip->getApvts().getParameterAsValue ("lowCutOrder").setValue (2.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (100.0, 100.0) / flatLow100), -28.0, 1.0,
                "a 12 dB/oct low cut reads -28.0 dB at 100 Hz");
    strip->getApvts().getParameterAsValue ("lowCutOrder").setValue (3.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (100.0, 100.0) / flatLow100), -41.9, 1.0,
                "an 18 dB/oct low cut reads -41.9 dB at 100 Hz");
    strip->getApvts().getParameterAsValue ("lowCutOrder").setValue (4.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (100.0, 100.0) / flatLow100), -55.9, 1.5,
                "a 24 dB/oct low cut reads -55.9 dB at 100 Hz");
    strip->getApvts().getParameterAsValue ("lowCutOrder").setValue (6.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (100.0, 100.0) / flatLow100), -83.9, 1.5,
                "a 36 dB/oct low cut reads -83.9 dB at 100 Hz");
    strip->getApvts().getParameterAsValue ("lowCutOrder").setValue (8.0f);
    expectNear (deepDb (measureAmplitude (100.0, 100.0) / flatLow100), -111.9, 2.5,
                "a 48 dB/oct low cut reads -111.9 dB at 100 Hz");
    strip->getApvts().getParameterAsValue ("lowCutOrder").setValue (12.0f);
    expectAtMost (deepDb (measureAmplitude (100.0, 100.0) / flatLow100), -140.0,
                  "a 72 dB/oct low cut buries 100 Hz below -140 dB (the simulation says -167.8 dB)");

    // the slope parameter itself snaps to the seven legal orders
    // (Clement's remark 3): every raw value lands on the nearest legal
    // one, ties resolving to the gentler slope
    {
        const float snapRaw[]     = { 5.0f, 7.0f,  9.0f, 10.0f, 11.0f, 13.0f, 16.0f };
        const float snapLegal[]   = { 4.0f, 6.0f,  8.0f,  8.0f, 12.0f, 12.0f, 12.0f };
        const int   snapSlope[]   = {   24,   36,    48,    48,    72,    72,    72 };

        for (int i = 0; i < 7; ++i)
        {
            strip->getApvts().getParameterAsValue ("lowCutOrder").setValue (snapRaw[i]);
            expectNear (strip->getApvts().getRawParameterValue ("lowCutOrder")->load(), snapLegal[i], 0.01,
                        "an order of " + juce::String (snapRaw[i], 1)
                            + " snaps to the legal " + juce::String (snapSlope[i]) + " dB/oct slope");
        }

        // a typed "30 dB/oct" parses to order 5, the tie snapping gentler
        // to the legal 24 dB/oct (getValueForText returns normalised, so
        // the readout comes back through convertFrom0to1)
        auto* lowOrderParam = dynamic_cast<juce::RangedAudioParameter*> (
            strip->getApvts().getParameter ("lowCutOrder"));
        expectTrue (lowOrderParam != nullptr,
                    "the low cut slope parameter is a ranged parameter");

        if (lowOrderParam != nullptr)
            expectNear (lowOrderParam->convertFrom0to1 (lowOrderParam->getValueForText ("30 dB/oct")), 4.0, 0.01,
                        "a typed 30 dB/oct snaps to the legal 24 dB/oct");
    }

    strip->getApvts().getParameterAsValue ("lowCutHz").setValue (16.0f);
    strip->getApvts().getParameterAsValue ("lowCutOrder").setValue (2.0f);
    strip->getApvts().getParameterAsValue ("lowCutOn").setValue (true);

    // the high cut: parked reference at 16 kHz, then a 2 kHz corner probed
    // at 16 kHz - 3 octaves above it. The bilinear prewarp at 8x the corner
    // makes the digital slopes steeper than the analog ones (order 1 reads
    // -22.4 dB, not -18); OFF reads the parked reference's own +0.02 dB
    const double flat16k = measureAmplitude (16000.0, 16000.0);
    strip->getApvts().getParameterAsValue ("highCutHz").setValue (2000.0f);

    strip->getApvts().getParameterAsValue ("highCutOn").setValue (false);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (16000.0, 16000.0) / flat16k), 0.02, 0.3,
                "the high cut's OFF plate bypasses the filter (the +0.02 is the parked reference's own 16 kHz response)");

    strip->getApvts().getParameterAsValue ("highCutOn").setValue (true);
    strip->getApvts().getParameterAsValue ("highCutOrder").setValue (1.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (16000.0, 16000.0) / flat16k), -22.4, 0.8,
                "a 6 dB/oct first-order high cut reads -22.4 dB at 16 kHz");
    strip->getApvts().getParameterAsValue ("highCutOrder").setValue (2.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (16000.0, 16000.0) / flat16k), -44.7, 1.0,
                "a 12 dB/oct high cut reads -44.7 dB at 16 kHz");
    strip->getApvts().getParameterAsValue ("highCutOrder").setValue (3.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (16000.0, 16000.0) / flat16k), -67.1, 1.5,
                "an 18 dB/oct high cut reads -67.1 dB at 16 kHz");
    strip->getApvts().getParameterAsValue ("highCutOrder").setValue (4.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (16000.0, 16000.0) / flat16k), -89.5, 1.5,
                "a 24 dB/oct high cut reads -89.5 dB at 16 kHz (the lock-in ignores the low cut's float32 DC offset)");
    strip->getApvts().getParameterAsValue ("highCutOrder").setValue (6.0f);
    expectNear (deepDb (measureAmplitude (16000.0, 16000.0) / flat16k), -134.3, 5.0,
                "a 36 dB/oct high cut reads -134.3 dB at 16 kHz");
    strip->getApvts().getParameterAsValue ("highCutOrder").setValue (8.0f);
    expectAtMost (deepDb (measureAmplitude (16000.0, 16000.0) / flat16k), -140.0,
                  "a 48 dB/oct high cut buries 16 kHz below -140 dB (the simulation says -179.0 dB)");
    strip->getApvts().getParameterAsValue ("highCutOrder").setValue (12.0f);
    expectAtMost (deepDb (measureAmplitude (16000.0, 16000.0) / flat16k), -140.0,
                  "a 72 dB/oct high cut buries 16 kHz below -140 dB (the simulation says -262 dB in float64, about -249 dB in float32)");

    strip->getApvts().getParameterAsValue ("highCutHz").setValue (24000.0f);
    strip->getApvts().getParameterAsValue ("highCutOrder").setValue (2.0f);
    strip->getApvts().getParameterAsValue ("highCutOn").setValue (true);

    // flat midband: 125 Hz and 8 kHz within 1 dB of the 1 kHz reference.
    // 125 Hz is not a whole number of cycles per 480-sample block (1.25),
    // so the lock-in would leak there - but its grid is fine enough
    // (384 samples per cycle) that the peak reading is exact
    expectNear (juce::Decibels::gainToDecibels (measurePeak (125.0) / unity1k), 0.0, 1.0,
                "the midband is flat at 125 Hz");
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (8000.0, 8000.0) / unity1k), 0.0, 1.0,
                "the midband is flat at 8 kHz");

    // ------------------------------------------------------------------
    // v2.56.0: the drive stage - tanh saturation with an invisible
    // auto-makeup, read with the coherent lock-in above. Every probe
    // frequency is a multiple of 100 Hz, so one 480-sample block at
    // 48 kHz holds an exact integer number of cycles: the correlation
    // has no leakage and returns the true bin amplitude.
    // ------------------------------------------------------------------
    {
        // drive at 0 dB: the wet/dry blend term vanishes and the strip
        // stays clean
        strip->getApvts().getParameterAsValue ("driveDb").setValue (0.0f);
        const double cleanFund = measureAmplitude (1000.0, 1000.0);
        const double cleanH3 = measureAmplitude (1000.0, 3000.0);
        expectTrue (juce::Decibels::gainToDecibels (cleanH3 / cleanFund) <= -80.0,
                    "drive at 0 dB is clean (no third harmonic)");

        // full drive (+15.6 dB of input gain, the top of the v2.58 knob):
        // strong odd harmonics, loudness held by the makeup
        strip->getApvts().getParameterAsValue ("driveDb").setValue (15.6f);
        const double driveFund = measureAmplitude (1000.0, 1000.0);
        const double driveH3 = measureAmplitude (1000.0, 3000.0);

        expectNear (juce::Decibels::gainToDecibels (driveH3 / driveFund), -18.0, 9.0,
                    "full drive generates a strong third harmonic");
        expectNear (juce::Decibels::gainToDecibels (driveFund / cleanFund), 1.0, 1.5,
                    "the auto-makeup holds the loudness near unity at full drive");

        // oversampling: a 7 kHz tone at full drive folds its fifth
        // harmonic (35 kHz) onto 13 kHz when the tanh runs at the base
        // rate; the 2x oversampled path must push it far down
        strip->getApvts().getParameterAsValue ("driveOversampled").setValue (false);
        const double aliasFundOff = measureAmplitude (7000.0, 7000.0);
        const double aliasOff = measureAmplitude (7000.0, 13000.0);
        expectTrue (juce::Decibels::gainToDecibels (aliasOff / aliasFundOff) > -45.0,
                    "without oversampling the drive aliases onto 13 kHz");

        strip->getApvts().getParameterAsValue ("driveOversampled").setValue (true);
        const double aliasFundOn = measureAmplitude (7000.0, 7000.0);
        const double aliasOn = measureAmplitude (7000.0, 13000.0);

        expectNear (juce::Decibels::gainToDecibels (aliasFundOn / aliasFundOff), 0.0, 1.0,
                    "oversampling leaves the fundamental untouched");
        expectTrue (juce::Decibels::gainToDecibels (aliasOn / aliasFundOn) <= -60.0,
                    "the oversampled drive pushes the alias below -60 dB");
        expectTrue (juce::Decibels::gainToDecibels (aliasOff / aliasFundOff)
                        - juce::Decibels::gainToDecibels (aliasOn / aliasFundOn) >= 20.0,
                    "oversampling reduces the aliasing by at least 20 dB");

        // back to the parked defaults
        strip->getApvts().getParameterAsValue ("driveDb").setValue (0.0f);
        strip->getApvts().getParameterAsValue ("driveOversampled").setValue (false);
    }

    // ------------------------------------------------------------------
    // v2.70.0 (Clement's remarks 4 and 5): the discrete knob feel on
    // the INPUT front end - the cut corners move in two significant
    // digits, the tilt and the drive in whole dB, while the trim keeps
    // its fine 0.1 dB touch
    // ------------------------------------------------------------------
    {
        strip->getApvts().getParameterAsValue ("lowCutHz").setValue (633.0f);
        expectNear (strip->getApvts().getRawParameterValue ("lowCutHz")->load(), 630.0, 0.0001,
                    "a low cut corner set to 633 Hz snaps to two significant digits (630)");
        strip->getApvts().getParameterAsValue ("lowCutHz").setValue (500.0f);

        strip->getApvts().getParameterAsValue ("highCutHz").setValue (17550.0f);
        expectNear (strip->getApvts().getRawParameterValue ("highCutHz")->load(), 18000.0, 0.0001,
                    "a high cut corner set to 17550 Hz snaps to two significant digits (18000)");
        strip->getApvts().getParameterAsValue ("highCutHz").setValue (24000.0f);

        strip->getApvts().getParameterAsValue ("tiltDb").setValue (3.4f);
        expectNear (strip->getApvts().getRawParameterValue ("tiltDb")->load(), 3.0, 0.0001,
                    "the tilt set to 3.4 dB lands on the whole-dB grid at 3");
        strip->getApvts().getParameterAsValue ("tiltDb").setValue (0.0f);

        strip->getApvts().getParameterAsValue ("driveDb").setValue (6.4f);
        expectNear (strip->getApvts().getRawParameterValue ("driveDb")->load(), 6.0, 0.0001,
                    "the drive set to 6.4 dB lands on the whole-dB grid at 6");
        strip->getApvts().getParameterAsValue ("driveDb").setValue (0.0f);

        // the trim keeps its fine 0.1 dB touch (Clement's remark 5)
        strip->getApvts().getParameterAsValue ("trimInDb").setValue (3.3f);
        expectNear (strip->getApvts().getRawParameterValue ("trimInDb")->load(), 3.3, 0.001,
                    "the trim still moves in its fine 0.1 dB steps");
        strip->getApvts().getParameterAsValue ("trimInDb").setValue (0.0f);
    }
}

void testCostesStripSession()
{
    beginTest ("v2.59.0: the Costes strip survives the session roundtrip");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    proc.toggleCostesStrip();

    auto* strip = dynamic_cast<CostesStrip*> (proc.getHostedStrip().getActivePlugin());
    expectTrue (strip != nullptr, "the strip is hosted before the save");

    if (strip == nullptr)
        return;

    // a recognisable knob position on every parameter: the trim, both
    // cut corners, both legal slope selectors and both on/off plates,
    // the tilt, the drive and the oversampling switch
    strip->getApvts().getParameterAsValue ("trimInDb").setValue (4.5f);
    strip->getApvts().getParameterAsValue ("lowCutHz").setValue (120.0f);
    strip->getApvts().getParameterAsValue ("lowCutOrder").setValue (6.0f);
    strip->getApvts().getParameterAsValue ("lowCutOn").setValue (false);
    strip->getApvts().getParameterAsValue ("highCutHz").setValue (8000.0f);
    strip->getApvts().getParameterAsValue ("highCutOrder").setValue (3.0f);
    strip->getApvts().getParameterAsValue ("highCutOn").setValue (true);
    strip->getApvts().getParameterAsValue ("tiltDb").setValue (3.0f);
    strip->getApvts().getParameterAsValue ("driveDb").setValue (6.0f);
    strip->getApvts().getParameterAsValue ("driveOversampled").setValue (true);

    // the EQ section (v2.61.0): a recognisable position on every band -
    // a moved corner on band 1, a gain and a bandwidth on band 2, and
    // band 4 switched to its shelf. v2.62.0: the bandwidth is one of
    // the seven legal widths now, so the round-trip is exact
    strip->getApvts().getParameterAsValue ("eq1Hz").setValue (250.0f);
    strip->getApvts().getParameterAsValue ("eq2Gain").setValue (-3.5f);
    strip->getApvts().getParameterAsValue ("eq2Oct").setValue (1.5f);
    strip->getApvts().getParameterAsValue ("eq4Bell").setValue (false);

    // the dynamics section (v2.64.0): a recognisable position on both
    // halves - a moved expander threshold and range, and a compressor
    // ratio, a switched-on FAST ATTACK plate and a moved mix
    strip->getApvts().getParameterAsValue ("expThreshold").setValue (-35.0f);
    strip->getApvts().getParameterAsValue ("expRange").setValue (20.0f);
    strip->getApvts().getParameterAsValue ("compRatio").setValue (4.0f);
    strip->getApvts().getParameterAsValue ("compFastAttack").setValue (true);
    strip->getApvts().getParameterAsValue ("compMix").setValue (60.0f);

    juce::MemoryBlock state;
    proc.getStateInformation (state);
    expectTrue (state.getSize() > 0, "the session state is non-empty");

    // a FRESH processor, nothing hosted: the restore must re-host the
    // internal strip through the maybeRestoreHosted branch (no
    // KnownPluginList entry exists for it) and land every knob
    TheChannelStripAudioProcessor restored;
    restored.prepareToPlay (48000.0, 512);
    restored.setStateInformation (state.getData(), (int) state.getSize());

    expectTrue (restored.isCostesStripActive(),
                "the restore re-hosts the internal strip");

    auto* rehosted = dynamic_cast<CostesStrip*> (restored.getHostedStrip().getActivePlugin());
    expectTrue (rehosted != nullptr, "the restored strip is reachable");

    if (rehosted != nullptr)
    {
        expectNear (rehosted->getApvts().getRawParameterValue ("trimInDb")->load(), 4.5, 0.01,
                    "trim in restores");
        expectNear (rehosted->getApvts().getRawParameterValue ("lowCutHz")->load(), 120.0, 0.1,
                    "low cut restores");
        expectNear (rehosted->getApvts().getRawParameterValue ("lowCutOrder")->load(), 6.0, 0.01,
                    "the low cut's 36 dB/oct slope restores");
        expectNear (rehosted->getApvts().getRawParameterValue ("lowCutOn")->load(), 0.0, 0.01,
                    "the low cut's OFF plate restores");
        expectNear (rehosted->getApvts().getRawParameterValue ("highCutHz")->load(), 8000.0, 0.1,
                    "high cut restores");
        expectNear (rehosted->getApvts().getRawParameterValue ("highCutOrder")->load(), 3.0, 0.01,
                    "the high cut's 18 dB/oct slope restores");
        expectNear (rehosted->getApvts().getRawParameterValue ("highCutOn")->load(), 1.0, 0.01,
                    "the high cut's ON plate restores");
        expectNear (rehosted->getApvts().getRawParameterValue ("tiltDb")->load(), 3.0, 0.01,
                    "tilt restores");
        expectNear (rehosted->getApvts().getRawParameterValue ("driveDb")->load(), 6.0, 0.01,
                    "drive restores");
        expectNear (rehosted->getApvts().getRawParameterValue ("driveOversampled")->load(), 1.0, 0.01,
                    "the oversampling switch restores");
        expectNear (rehosted->getApvts().getRawParameterValue ("eq1Hz")->load(), 250.0, 0.1,
                    "the EQ band 1 corner restores");
        expectNear (rehosted->getApvts().getRawParameterValue ("eq2Gain")->load(), -3.5, 0.01,
                    "the EQ band 2 gain restores");
        expectNear (rehosted->getApvts().getRawParameterValue ("eq2Oct")->load(), 1.5, 0.01,
                    "the EQ band 2 bandwidth restores");
        expectNear (rehosted->getApvts().getRawParameterValue ("eq4Bell")->load(), 0.0, 0.01,
                    "the EQ band 4 shelf mode restores");
        expectNear (rehosted->getApvts().getRawParameterValue ("expThreshold")->load(), -35.0, 0.01,
                    "the expander threshold restores");
        expectNear (rehosted->getApvts().getRawParameterValue ("expRange")->load(), 20.0, 0.01,
                    "the expander range restores");
        expectNear (rehosted->getApvts().getRawParameterValue ("compRatio")->load(), 4.0, 0.01,
                    "the compressor ratio restores");
        expectNear (rehosted->getApvts().getRawParameterValue ("compFastAttack")->load(), 1.0, 0.01,
                    "the compressor's FAST ATTACK plate restores");
        expectNear (rehosted->getApvts().getRawParameterValue ("compMix")->load(), 60.0, 0.01,
                    "the compressor mix restores");
    }

    expectTrue (restored.getHostedStrip().getRelayedParameterCount() == 45,
                "the restored strip's forty-five parameters are relayed");
}

void testCostesStripMigration()
{
    beginTest ("v2.59.0: pre-2.59 sessions migrate (an old OFF slope becomes an OFF plate)");

    // a fresh strip's own state, aged back to the v2.58 format: the order
    // children hold plain integers (0 meant "off" - the filter bypassed)
    // and no lowCutOn / highCutOn children exist
    CostesStrip stripProc;
    stripProc.prepareToPlay (48000.0, 512);

    juce::MemoryBlock state;
    stripProc.getStateInformation (state);

    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    expectTrue (xml != nullptr, "the strip state parses as XML");

    if (xml == nullptr)
        return;

    for (int i = xml->getNumChildElements() - 1; i >= 0; --i)
    {
        const auto id = xml->getChildElement (i)->getStringAttribute ("id");

        if (id == "lowCutOn" || id == "highCutOn")
            xml->removeChildElement (xml->getChildElement (i), true);
    }

    // the v2.58 way: an illegal integer 5 on the low cut (v2.58 allowed
    // every order up to 16) and a 0 - the old "off" - on the high cut
    for (int i = 0; i < xml->getNumChildElements(); ++i)
    {
        auto* child = xml->getChildElement (i);
        const auto id = child->getStringAttribute ("id");

        if (id == "lowCutOrder")
            child->setAttribute ("value", "5");
        else if (id == "highCutOrder")
            child->setAttribute ("value", "0");
    }

    juce::MemoryBlock aged;
    juce::AudioProcessor::copyXmlToBinary (*xml, aged);
    expectTrue (aged.getSize() > 0, "the aged session state is non-empty");

    // a FRESH strip loads the aged session: the illegal order 5 snaps to
    // the legal 4 with the plate ON, and the old high cut "off" migrates
    // to a switched-OFF plate parked at the 12 dB/oct default slope
    CostesStrip agedProc;
    agedProc.prepareToPlay (48000.0, 512);
    agedProc.setStateInformation (aged.getData(), (int) aged.getSize());

    expectNear (agedProc.getApvts().getRawParameterValue ("lowCutOrder")->load(), 4.0, 0.01,
                "the aged illegal order 5 loads as the legal 24 dB/oct (tie to the gentler slope)");
    expectNear (agedProc.getApvts().getRawParameterValue ("lowCutOn")->load(), 1.0, 0.01,
                "an aged session without ON plates switches the low cut ON");
    expectNear (agedProc.getApvts().getRawParameterValue ("highCutOrder")->load(), 2.0, 0.01,
                "the aged high cut order 0 parks at the 12 dB/oct default");
    expectNear (agedProc.getApvts().getRawParameterValue ("highCutOn")->load(), 0.0, 0.01,
                "the aged high cut OFF slope migrates to the OFF plate");

    // the migrated state saves and reloads stable: a quick re-save keeps
    // the plates and the snapped orders
    juce::MemoryBlock resaved;
    agedProc.getStateInformation (resaved);

    CostesStrip reloadProc;
    reloadProc.prepareToPlay (48000.0, 512);
    reloadProc.setStateInformation (resaved.getData(), (int) resaved.getSize());

    expectNear (reloadProc.getApvts().getRawParameterValue ("lowCutOrder")->load(), 4.0, 0.01,
                "the migrated low cut order survives a re-save");
    expectNear (reloadProc.getApvts().getRawParameterValue ("highCutOn")->load(), 0.0, 0.01,
                "the migrated OFF plate survives a re-save");
}

// v2.70.1: Clement's Studio One report - some strip parameters come
// back wrong at every song reopen (an SC LPF back at 120 Hz, the
// expander range back at 40 dB). The v2.59 session test covers the
// trim, the cuts, the EQ and a couple of dynamics knobs, but never the
// sidechain selectors; these blocks drive the exact reported knobs
// through the paths a host adds around a restore: a second restore
// with the same state, the away-and-back per-ident memory, an open
// editor, and the save/restore fixed point. If any of these fail, the
// printed reading (got vs expected) IS the diagnosis.
void testCostesStripSessionDeep()
{
    // ---- 1: the four SC corners and the expander range ride the
    //          shell's own blob, and a host's SECOND restore with the
    //          same state keeps them too ----
    beginTest ("v2.70.1: the SC corners and the expander range survive the roundtrip (and a double restore)");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.toggleCostesStrip();

    auto* strip = dynamic_cast<CostesStrip*> (proc.getHostedStrip().getActivePlugin());
    expectTrue (strip != nullptr, "the strip is hosted before the save");

    if (strip != nullptr)
    {
        strip->getApvts().getParameterAsValue ("compHpf").setValue (90.0f);
        strip->getApvts().getParameterAsValue ("compScLpf").setValue (800.0f);
        strip->getApvts().getParameterAsValue ("expScHpf").setValue (150.0f);
        strip->getApvts().getParameterAsValue ("expRange").setValue (20.0f);
        // expScLpf stays at its 24000 Hz "off" rest position: the
        // untouched rest position must ride the blob untouched too

        juce::MemoryBlock state;
        proc.getStateInformation (state);
        expectTrue (state.getSize() > 0, "the session state is non-empty");

        TheChannelStripAudioProcessor restored;
        restored.prepareToPlay (48000.0, 512);
        restored.setStateInformation (state.getData(), (int) state.getSize());

        expectTrue (restored.isCostesStripActive(),
                    "the restore re-hosts the internal strip");

        auto* rehosted = dynamic_cast<CostesStrip*> (restored.getHostedStrip().getActivePlugin());
        expectTrue (rehosted != nullptr, "the restored strip is reachable");

        if (rehosted != nullptr)
        {
            expectNear (rehosted->getApvts().getRawParameterValue ("compHpf")->load(), 90.0, 0.01,
                        "the compressor's 90 Hz SC HPF corner restores");
            expectNear (rehosted->getApvts().getRawParameterValue ("compScLpf")->load(), 800.0, 0.1,
                        "the compressor's 800 Hz SC LPF corner restores (the reported knob)");
            expectNear (rehosted->getApvts().getRawParameterValue ("expScHpf")->load(), 150.0, 0.1,
                        "the expander's 150 Hz SC HPF corner restores");
            expectNear (rehosted->getApvts().getRawParameterValue ("expScLpf")->load(), 24000.0, 0.1,
                        "the expander's untouched SC LPF rest position restores as off");
            expectNear (rehosted->getApvts().getRawParameterValue ("expRange")->load(), 20.0, 0.01,
                        "the expander range restores (the reported knob)");
        }

        expectTrue (restored.getHostedStrip().getRelayedParameterCount() >= 45,
                    "the restored strip's forty-five parameters are relayed");

        // a host can restore twice (Studio One's launch sequence does):
        // the second pass re-hosts a fresh strip and re-applies the
        // same blob - every knob must still be where the session left it
        restored.setStateInformation (state.getData(), (int) state.getSize());

        auto* rehosted2 = dynamic_cast<CostesStrip*> (restored.getHostedStrip().getActivePlugin());
        expectTrue (rehosted2 != nullptr, "the strip is re-hosted by the second restore");

        if (rehosted2 != nullptr)
        {
            expectNear (rehosted2->getApvts().getRawParameterValue ("compScLpf")->load(), 800.0, 0.1,
                        "the SC LPF corner survives a second restore with the same state");
            expectNear (rehosted2->getApvts().getRawParameterValue ("expRange")->load(), 20.0, 0.01,
                        "the expander range survives a second restore with the same state");
        }
    }

    // ---- 2: a pre-2.69 continuous corner snaps to its nearest legal
    //          corner on load - never to the ladder's ends ----
    beginTest ("v2.70.1: a pre-2.69 continuous SC corner snaps to its nearest legal corner, never the ends");

    CostesStrip agedStrip;
    agedStrip.prepareToPlay (48000.0, 512);

    juce::MemoryBlock agedState;
    agedStrip.getStateInformation (agedState);

    auto agedXml = juce::AudioProcessor::getXmlFromBinary (agedState.getData(),
                                                           (int) agedState.getSize());
    expectTrue (agedXml != nullptr, "the strip state parses as XML");

    if (agedXml != nullptr)
    {
        // the v2.68 way: continuous log-spaced values that sit BETWEEN
        // two legal corners, and the old span's own floor (100 Hz)
        for (int i = 0; i < agedXml->getNumChildElements(); ++i)
        {
            auto* child = agedXml->getChildElement (i);
            const auto id = child->getStringAttribute ("id");

            if (id == "compScLpf")
                child->setAttribute ("value", "812.4");
            else if (id == "expRange")
                child->setAttribute ("value", "33.37");
        }

        juce::MemoryBlock aged;
        juce::AudioProcessor::copyXmlToBinary (*agedXml, aged);
        expectTrue (aged.getSize() > 0, "the aged session state is non-empty");

        CostesStrip loaded;
        loaded.prepareToPlay (48000.0, 512);
        loaded.setStateInformation (aged.getData(), (int) aged.getSize());

        expectNear (loaded.getApvts().getRawParameterValue ("compScLpf")->load(), 800.0, 0.1,
                    "an aged 812.4 Hz continuous corner snaps to the 800 Hz legal corner");
        expectNear (loaded.getApvts().getRawParameterValue ("expRange")->load(), 33.4, 0.01,
                    "an aged 33.37 dB range snaps onto the 0.1 dB grid at 33.4");

        // the old span's floor: 100 Hz is legal in the pre-2.69
        // continuous range but BELOW the first corner now - it must
        // clamp UP to 120 Hz, not park anywhere else
        for (int i = 0; i < agedXml->getNumChildElements(); ++i)
        {
            auto* child = agedXml->getChildElement (i);

            if (child->getStringAttribute ("id") == "compScLpf")
                child->setAttribute ("value", "100");
        }

        juce::MemoryBlock agedFloor;
        juce::AudioProcessor::copyXmlToBinary (*agedXml, agedFloor);
        expectTrue (agedFloor.getSize() > 0, "the floor-aged session state is non-empty");

        CostesStrip loadedFloor;
        loadedFloor.prepareToPlay (48000.0, 512);
        loadedFloor.setStateInformation (agedFloor.getData(), (int) agedFloor.getSize());

        expectNear (loadedFloor.getApvts().getRawParameterValue ("compScLpf")->load(), 120.0, 0.1,
                    "the old span's 100 Hz floor clamps up to the first legal corner (120 Hz)");
    }

    // ---- 3: the away-and-back per-ident memory keeps every knob ----
    beginTest ("v2.70.1: switching away and back keeps every knob (the per-ident memory)");

    TheChannelStripAudioProcessor memoryProc;
    memoryProc.prepareToPlay (48000.0, 512);
    memoryProc.toggleCostesStrip();

    auto* hosted = dynamic_cast<CostesStrip*> (memoryProc.getHostedStrip().getActivePlugin());
    expectTrue (hosted != nullptr, "the strip is hosted for the memory test");

    if (hosted != nullptr)
    {
        hosted->getApvts().getParameterAsValue ("compScLpf").setValue (3200.0f);
        hosted->getApvts().getParameterAsValue ("expRange").setValue (12.5f);
        hosted->getApvts().getParameterAsValue ("expScHpf").setValue (90.0f);

        memoryProc.toggleCostesStrip();   // away: the state is remembered
        memoryProc.toggleCostesStrip();   // and back: the memory applies

        auto* back = dynamic_cast<CostesStrip*> (memoryProc.getHostedStrip().getActivePlugin());
        expectTrue (back != nullptr, "the strip is re-hosted by the second toggle");

        if (back != nullptr)
        {
            expectNear (back->getApvts().getRawParameterValue ("compScLpf")->load(), 3200.0, 0.1,
                        "the away-and-back memory keeps the SC LPF corner");
            expectNear (back->getApvts().getRawParameterValue ("expRange")->load(), 12.5, 0.01,
                        "the away-and-back memory keeps the expander range");
            expectNear (back->getApvts().getRawParameterValue ("expScHpf")->load(), 90.0, 0.1,
                        "the away-and-back memory keeps the SC HPF corner");
        }
    }

    // ---- 4: an editor opened on the restored strip, and a second
    //          restore after it closes - the exact GUI-timing paths ----
    beginTest ("v2.70.1: an open editor never disturbs the restored knobs");

    TheChannelStripAudioProcessor editorProc;
    editorProc.prepareToPlay (48000.0, 512);
    editorProc.toggleCostesStrip();

    auto* editorStrip = dynamic_cast<CostesStrip*> (editorProc.getHostedStrip().getActivePlugin());
    expectTrue (editorStrip != nullptr, "the strip is hosted before the editor-open save");

    if (editorStrip != nullptr)
    {
        editorStrip->getApvts().getParameterAsValue ("compScLpf").setValue (800.0f);
        editorStrip->getApvts().getParameterAsValue ("expRange").setValue (20.0f);

        juce::MemoryBlock editorState;
        editorProc.getStateInformation (editorState);

        TheChannelStripAudioProcessor editorRestored;
        editorRestored.prepareToPlay (48000.0, 512);
        editorRestored.setStateInformation (editorState.getData(), (int) editorState.getSize());

        auto* restoredStrip = dynamic_cast<CostesStrip*> (editorRestored.getHostedStrip().getActivePlugin());

        if (restoredStrip != nullptr)
        {
            // the GUI opens on the freshly restored strip: the slider
            // attachments' initial sync must pull, never push
            {
                CostesStripEditor editor (*restoredStrip);

                expectNear (restoredStrip->getApvts().getRawParameterValue ("compScLpf")->load(), 800.0, 0.1,
                            "the SC LPF corner is untouched by the editor's opening");
                expectNear (restoredStrip->getApvts().getRawParameterValue ("expRange")->load(), 20.0, 0.01,
                            "the expander range is untouched by the editor's opening");
            }

            // the editor is gone, THEN the host restores again (a
            // reopen with the GUI already visited): same blob, same
            // knobs
            editorRestored.setStateInformation (editorState.getData(), (int) editorState.getSize());
        }

        auto* afterEditor = dynamic_cast<CostesStrip*> (editorRestored.getHostedStrip().getActivePlugin());
        expectTrue (afterEditor != nullptr, "the strip is re-hosted after the editor's cycle");

        if (afterEditor != nullptr)
        {
            expectNear (afterEditor->getApvts().getRawParameterValue ("compScLpf")->load(), 800.0, 0.1,
                        "the SC LPF corner survives the editor cycle and the second restore");
            expectNear (afterEditor->getApvts().getRawParameterValue ("expRange")->load(), 20.0, 0.01,
                        "the expander range survives the editor cycle and the second restore");
        }
    }

    // ---- 5: fifty save / restore cycles with the real strip are a
    //          byte-identical fixed point ----
    beginTest ("v2.70.1: fifty save / restore cycles are a byte-identical fixed point with the real strip");

    TheChannelStripAudioProcessor cycleProc;
    cycleProc.prepareToPlay (48000.0, 512);
    cycleProc.toggleCostesStrip();

    auto* cycleStrip = dynamic_cast<CostesStrip*> (cycleProc.getHostedStrip().getActivePlugin());
    expectTrue (cycleStrip != nullptr, "the strip is hosted before the fixed-point cycles");

    if (cycleStrip != nullptr)
    {
        cycleStrip->getApvts().getParameterAsValue ("compScLpf").setValue (800.0f);
        cycleStrip->getApvts().getParameterAsValue ("expRange").setValue (20.0f);
        cycleStrip->getApvts().getParameterAsValue ("expScHpf").setValue (150.0f);

        juce::MemoryBlock reference;
        cycleProc.getStateInformation (reference);

        bool fixedPoint = true;

        for (int n = 0; n < 50; ++n)
        {
            juce::MemoryBlock saved;
            cycleProc.getStateInformation (saved);
            fixedPoint = fixedPoint && saved == reference;

            cycleProc.setStateInformation (saved.getData(), (int) saved.getSize());
        }

        expectTrue (fixedPoint,
                    "50 save / restore cycles are a byte-identical fixed point");

        auto* cycledStrip = dynamic_cast<CostesStrip*> (cycleProc.getHostedStrip().getActivePlugin());

        if (cycledStrip != nullptr)
        {
            expectNear (cycledStrip->getApvts().getRawParameterValue ("compScLpf")->load(), 800.0, 0.1,
                        "the SC LPF corner is intact after 50 cycles");
            expectNear (cycledStrip->getApvts().getRawParameterValue ("expRange")->load(), 20.0, 0.01,
                        "the expander range is intact after 50 cycles");
        }

        expectTrue (cycleProc.getHostedStrip().getRelayedParameterCount() >= 45,
                    "the cycled strip's parameters stay relayed");
    }
}

// v2.70.2: Clement's Studio One report - turning knobs inside the shell
// (consoles or the Costes strip) never marked the song modified, while
// any other plugin's knobs saved fine. The shell exposes no parameter
// to the DAW, so the host only hears about a state change if the shell
// says so: updateHostDisplay with nonParameterStateChanged (the VST3
// wrapper maps it to setDirty). This listener counts those
// notifications; the blocks below verify that every user action
// raises one and that a session restore raises none.
struct HostDirtyCountingListener : public juce::AudioProcessorListener
{
    int nonParameterCount = 0;

    void audioProcessorChanged (juce::AudioProcessor*,
                                const juce::AudioProcessorListener::ChangeDetails& details) override
    {
        if (details.nonParameterStateChanged)
            ++nonParameterCount;
    }

    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
};

void testHostDirtyNotification()
{
    // ---- 1: a knob move inside the hosted strip notifies the host ----
    beginTest ("v2.70.2: a strip knob move notifies the host of a state change");

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.toggleCostesStrip();

    HostDirtyCountingListener listener;
    proc.addListener (&listener);

    auto* strip = dynamic_cast<CostesStrip*> (proc.getHostedStrip().getActivePlugin());
    expectTrue (strip != nullptr, "the strip is hosted before the knob move");

    if (strip != nullptr)
    {
        expectTrue (listener.nonParameterCount == 0,
                    "no notification before any user action");

        strip->getApvts().getParameterAsValue ("compScLpf").setValue (800.0f);

        expectTrue (listener.nonParameterCount >= 1,
                    "a hosted knob move raises the host notification");
    }

    proc.removeListener (&listener);

    // ---- 2: a session restore never notifies (the song stays clean) ----
    beginTest ("v2.70.2: a session restore never marks the song dirty");

    TheChannelStripAudioProcessor saver;
    saver.prepareToPlay (48000.0, 512);
    saver.toggleCostesStrip();

    auto* saverStrip = dynamic_cast<CostesStrip*> (saver.getHostedStrip().getActivePlugin());
    expectTrue (saverStrip != nullptr, "the saving strip is hosted");

    if (saverStrip != nullptr)
        saverStrip->getApvts().getParameterAsValue ("expRange").setValue (20.0f);

    juce::MemoryBlock state;
    saver.getStateInformation (state);
    expectTrue (state.getSize() > 0, "the saved session state is non-empty");

    TheChannelStripAudioProcessor restored;
    restored.prepareToPlay (48000.0, 512);

    HostDirtyCountingListener restoreListener;
    restored.addListener (&restoreListener);

    // the listener is attached BEFORE the restore, so any spurious
    // notification would be counted - and Studio One restores twice
    restored.setStateInformation (state.getData(), (int) state.getSize());
    restored.setStateInformation (state.getData(), (int) state.getSize());

    expectTrue (restoreListener.nonParameterCount == 0,
                "a session restore (twice, listener attached first) raises no notification");

    restored.removeListener (&restoreListener);

    // ---- 3: toggling the strip off and on notifies (the slot is state) ----
    beginTest ("v2.70.2: toggling the strip notifies the host");

    TheChannelStripAudioProcessor toggler;
    toggler.prepareToPlay (48000.0, 512);
    toggler.toggleCostesStrip(); // on

    HostDirtyCountingListener toggleListener;
    toggler.addListener (&toggleListener);

    expectTrue (toggleListener.nonParameterCount == 0,
                "the toggle test starts clean");

    toggler.toggleCostesStrip(); // off: the slot empties
    expectTrue (toggleListener.nonParameterCount >= 1,
                "switching the strip off raises the host notification");

    toggler.toggleCostesStrip(); // on again
    expectTrue (toggleListener.nonParameterCount >= 2,
                "switching the strip back on raises the host notification");

    toggler.removeListener (&toggleListener);

    // ---- 4: the UI-only setters notify (their values ride the state) ----
    beginTest ("v2.70.2: the display setters notify the host");

    TheChannelStripAudioProcessor setterProc;
    setterProc.prepareToPlay (48000.0, 512);

    HostDirtyCountingListener setterListener;
    setterProc.addListener (&setterListener);

    setterProc.setTiltDb (-3.0f);
    expectTrue (setterListener.nonParameterCount >= 1,
                "the tilt notifies");

    setterProc.setAnaDbDiv (12.0f);
    setterProc.setEqDbDiv (4.0f);
    setterProc.setSpecTau (0.15f);
    setterProc.setLufsTarget (-37.0f);
    expectTrue (setterListener.nonParameterCount >= 5,
                "every display setter notifies");

    setterProc.setAnalyserVisible (false);
    expectTrue (setterListener.nonParameterCount >= 6,
                "the analyser visibility notifies");

    setterProc.removeListener (&setterListener);

    // ---- 5: preset actions notify (the tracking rides the state) ----
    beginTest ("v2.70.2: preset save / load / delete notify the host");

    const auto unique = juce::String ("Tcs Test Preset 2702 HostDirty");

    TheChannelStripAudioProcessor presetProc;
    presetProc.prepareToPlay (48000.0, 512);
    presetProc.toggleCostesStrip();

    // clean slate (a leftover from an interrupted earlier run)
    presetProc.deletePreset (unique);

    HostDirtyCountingListener presetListener;
    presetProc.addListener (&presetListener);

    expectTrue (presetProc.savePreset (unique), "the strip state saves as a preset");
    expectTrue (presetListener.nonParameterCount >= 1,
                "saving a preset raises the host notification");

    expectTrue (presetProc.applyPreset (unique), "the preset loads back on the strip");
    expectTrue (presetListener.nonParameterCount >= 2,
                "loading a preset raises the host notification");

    expectTrue (presetProc.deletePreset (unique), "the preset file is deleted");
    expectTrue (presetListener.nonParameterCount >= 3,
                "deleting a preset raises the host notification");

    presetProc.removeListener (&presetListener);
}

void testCostesStripSectionIcons()
{
    beginTest ("v2.60.0: the five section icons and the equally spaced slope knob");

    CostesStrip stripProc;
    stripProc.prepareToPlay (48000.0, 512);

    // ---- the icons ----
    // the editor carries exactly five section glyphs, one per knob, in
    // column order (trim, low cut, high cut, tilt, drive) - each visible
    // and sitting inside the panel after the layout pass
    CostesStripEditor editor (stripProc);

    // v2.67.0 (Clement's remark): the panel adopts the shell's own
    // width - v2.78.0: the signature leaves the left edge, the EQ
    // caption words and the band intertitles go, and the console
    // drops another 50 px - 860 x 764
    expectTrue (editor.getWidth() == 860 && editor.getHeight() == 764,
                "the strip panel opens as the tightened three-column console (860 x 764)");

    const auto icons = editor.getSectionIcons();
    const CostesStripEditor::SectionIcon::Icon expectedTypes[5] =
    {
        CostesStripEditor::SectionIcon::Icon::trim,
        CostesStripEditor::SectionIcon::Icon::lowCut,
        CostesStripEditor::SectionIcon::Icon::highCut,
        CostesStripEditor::SectionIcon::Icon::tilt,
        CostesStripEditor::SectionIcon::Icon::drive
    };

    for (int i = 0; i < 5; ++i)
    {
        expectTrue (icons[(size_t) i] != nullptr,
                    "the editor exposes a section icon for every knob");
        expectTrue (icons[(size_t) i]->type == expectedTypes[i],
                    "the section icons sit in column order (trim, low cut, high cut, tilt, drive)");
        expectTrue (icons[(size_t) i]->isVisible()
                        && editor.getLocalBounds().contains (icons[(size_t) i]->getBounds()),
                    "every section icon is laid out inside the panel");
    }

    // v2.78.0 (Clement's remark 2): the EQ header carries NO caption
    // and NO bell glyph any more - the EQ ON plate is the section's
    // own name, centred in the header band

    // v2.78.0 (Clement's remarks 4 and 6): every band keeps a free
    // glyph under its name now - the harness reaches the two mid
    // bells, which stand under the LMF and HMF names in their rows'
    // margin columns (the outer bands' shapes follow their
    // BELL/SHELF plates and stay out of the getter's reach)
    const auto bandIcons = editor.getEqBandIcons();
    const CostesStripEditor::SectionIcon::Icon expectedBandTypes[2] =
    {
        CostesStripEditor::SectionIcon::Icon::eq,
        CostesStripEditor::SectionIcon::Icon::eq
    };

    for (int i = 0; i < 2; ++i)
    {
        expectTrue (bandIcons[(size_t) i] != nullptr
                        && bandIcons[(size_t) i]->type == expectedBandTypes[i],
                    "the two mid bands keep their bell glyphs");
        expectTrue (bandIcons[(size_t) i] != nullptr && bandIcons[(size_t) i]->isVisible()
                        && editor.getLocalBounds().contains (bandIcons[(size_t) i]->getBounds()),
                    "every mid-band glyph is laid out inside the panel");
    }

    // the outer bands' BELL/SHELF plates (v2.70.0, Clement's remark 9,
    // redrawn in v2.78.0): each plate keeps its band's own shelf shape
    // as a member - the low shelf on band 1, the high shelf on band 4 -
    // and the free icon riding under the band's name now paints it,
    // so the plate itself reads only its LED and its BELL/SHELF words
    const auto modeButtons = editor.getEqModeButtons();
    const CostesStripEditor::SectionIcon::Icon expectedShelfGlyphs[2] =
    {
        CostesStripEditor::SectionIcon::Icon::lowShelf,
        CostesStripEditor::SectionIcon::Icon::highShelf
    };

    for (int i = 0; i < 2; ++i)
    {
        expectTrue (modeButtons[(size_t) i] != nullptr
                        && modeButtons[(size_t) i]->shelfGlyph == expectedShelfGlyphs[i],
                    "the BELL/SHELF plates keep their bands' shelf shapes for the icons under the names");
        expectTrue (modeButtons[(size_t) i] != nullptr && modeButtons[(size_t) i]->isVisible()
                        && editor.getLocalBounds().contains (modeButtons[(size_t) i]->getBounds()),
                    "every BELL/SHELF plate is laid out inside the panel");
    }

    // the console front's grain (v2.70.0, Clement's remark 1): a
    // deterministic 96 x 96 speckle tile baked once at construction,
    // ready to be tiled over the cream at paint time
    const auto& grain = editor.getGrainTexture();
    expectTrue (grain.isValid() && grain.getWidth() == 96 && grain.getHeight() == 96,
                "the panel bakes a 96 x 96 grain tile for its speckled front");



    // the fifteen dynamics sliders (v2.64.0, the sidechain pair grown
    // to four in v2.66.0): every knob and the sidechain selectors laid
    // out and visible inside the panel after the pass - the stable
    // slider order's indices 17 to 32, the compressor's LINK selector
    // (v2.72.0) at index 31, the expander's KNEE selector (v2.73.0)
    // closing at index 32
    const auto sliders = editor.getStripSliders();

    // v2.78.0 (Clement's remark 1): the front end is ONE block now
    // and its name is the COSTES CONSOLE signature - painted across
    // the top band where INPUT once read, so no label carries it.
    // The grid starts at x = 12: the margin the vertical signature
    // once owned belongs to the columns
    expectTrue (sliders[0] != nullptr && sliders[0]->getBounds().getX() >= 12
                    && sliders[0]->getBounds().getX() < 28,
                "the columns start at the panel's own left edge now - the signature's old margin is theirs");

    for (int i = 17; i < 33; ++i)
        expectTrue (sliders[(size_t) i]->isVisible()
                        && editor.getLocalBounds().contains (sliders[(size_t) i]->getBounds()),
                    "every dynamics knob and slider is laid out inside the panel");

    // v2.70.0 (Clement's remark 2): the dynamics stages read MIX,
    // THRESH, RELEASE across the top, RANGE (GAIN on the compressor)
    // and RATIO across the middle - the stable slider order's indices:
    // expander MIX 21, THRESH 17, RELEASE 19, RANGE 18, RATIO 20;
    // compressor MIX 26, THRESH 22, RELEASE 24, GAIN 25, RATIO 23
    expectTrue (sliders[21] != nullptr && sliders[17] != nullptr && sliders[19] != nullptr
                    && sliders[18] != nullptr && sliders[20] != nullptr
                    && sliders[21]->getBounds().getX() < sliders[17]->getBounds().getX()
                    && sliders[17]->getBounds().getX() < sliders[19]->getBounds().getX()
                    && sliders[19]->getBounds().getY() < sliders[18]->getBounds().getY()
                    && sliders[18]->getBounds().getY() == sliders[20]->getBounds().getY(),
                "the expander reads MIX, THRESH, RELEASE across the top, RANGE and RATIO below");
    expectTrue (sliders[26] != nullptr && sliders[22] != nullptr && sliders[24] != nullptr
                    && sliders[25] != nullptr && sliders[23] != nullptr
                    && sliders[26]->getBounds().getX() < sliders[22]->getBounds().getX()
                    && sliders[22]->getBounds().getX() < sliders[24]->getBounds().getX()
                    && sliders[24]->getBounds().getY() < sliders[25]->getBounds().getY()
                    && sliders[25]->getBounds().getY() == sliders[23]->getBounds().getY(),
                "the compressor reads MIX, THRESH, RELEASE across the top, GAIN and RATIO below");

    // the mid bells' glyphs ride UNDER their band's name now (v2.78.0,
    // Clement's remark 6): the LMF band's bell stands in its row's
    // slim margin column, one px under the engraved word, clear of
    // the GAIN knob (10) that follows
    expectTrue (sliders[10] != nullptr && bandIcons[0] != nullptr
                    && bandIcons[0]->getBounds().getRight()
                        <= sliders[10]->getBounds().getX()
                    && bandIcons[0]->getBounds().getY()
                        - editor.getEqBandLabels()[1]->getBounds().getBottom() == 1,
                "the LMF bell glyph stands right under its band's name");

    // v2.79.0 (Clement's remark 2): the mids' width sweeps lost their
    // selector line - each sweep rides its band's own margin column
    // now, all small UNDER its glyph, its readout shrunk to the bare
    // "1.0 oct" (11 is the LMF's sweep, 14 the HMF's)
    expectTrue (sliders[11] != nullptr && sliders[14] != nullptr
                    && bandIcons[0] != nullptr && bandIcons[1] != nullptr
                    && sliders[11]->getBounds().getX() >= 16
                    && sliders[11]->getBounds().getRight() <= 72
                    && sliders[11]->getBounds().getY()
                        > bandIcons[0]->getBounds().getBottom()
                    && sliders[14]->getBounds().getX() >= 16
                    && sliders[14]->getBounds().getRight() <= 72
                    && sliders[14]->getBounds().getY()
                        > bandIcons[1]->getBounds().getBottom(),
                "the mids' width sweeps ride their margin columns, under their glyphs");

    // the six dynamics meters (v2.65.0, the OUT ladders joined in
    // v2.66.0): the expander's IN/OUT/GR trio then the compressor's,
    // each laid out and visible inside the panel - the IN and OUT
    // ladders together at the stage's left edge, the GR ladder at the
    // stage's right edge
    const auto meters = editor.getDynMeters();

    for (int i = 0; i < 6; ++i)
        expectTrue (meters[(size_t) i] != nullptr && meters[(size_t) i]->isVisible()
                        && editor.getLocalBounds().contains (meters[(size_t) i]->getBounds()),
                    "every dynamics meter is laid out inside the panel");

    expectTrue (meters[0] != nullptr && meters[0]->mode == CostesStripEditor::DynamicsMeter::Mode::input
                    && meters[1] != nullptr && meters[1]->mode == CostesStripEditor::DynamicsMeter::Mode::output
                    && meters[2] != nullptr && meters[2]->mode == CostesStripEditor::DynamicsMeter::Mode::gainReduction
                    && meters[3] != nullptr && meters[3]->mode == CostesStripEditor::DynamicsMeter::Mode::input
                    && meters[4] != nullptr && meters[4]->mode == CostesStripEditor::DynamicsMeter::Mode::output
                    && meters[5] != nullptr && meters[5]->mode == CostesStripEditor::DynamicsMeter::Mode::gainReduction,
                "the meters come as IN, OUT then GR, stage by stage");

    expectTrue (meters[0] != nullptr && meters[0]->expander
                    && meters[1] != nullptr && meters[1]->expander
                    && meters[2] != nullptr && meters[2]->expander
                    && meters[3] != nullptr && ! meters[3]->expander
                    && meters[4] != nullptr && ! meters[4]->expander
                    && meters[5] != nullptr && ! meters[5]->expander,
                "the first trio reads the expander, the second the compressor");

    // v2.70.0 (Clement's remark 3): the GR ladders went LINEAR - one
    // division is one dB everywhere, only the number of divisions
    // changes: 36 LEDs over the expander's 0 to 36 (v2.72.0), 18 over
    // the compressor's 0 to 18 (v2.73.0, Clement's remark 4)
    expectTrue (meters[2] != nullptr && meters[2]->leds == 36
                    && meters[5] != nullptr && meters[5]->leds == 18
                    && meters[2]->leds == juce::roundToInt (-meters[2]->lowDb)
                    && meters[5]->leds == juce::roundToInt (-meters[5]->lowDb),
                "the GR ladders give one LED per dB - 36 over the expander's 0 to 36, 18 over the compressor's 0 to 18");

    // and their engraved scales carry their full value lists (v2.70.0,
    // Clement's remark 3; recut in v2.74.0, Clement's request) - the
    // tiers and their midpoints, drawn positive beside each ladder
    const int expectedExpGrMarks[7] = { 0, 3, 6, 12, 18, 27, 36 };
    const int expectedCompGrMarks[6] = { 0, 3, 6, 9, 12, 18 };
    bool expMarksMatch = true, compMarksMatch = true;

    for (int i = 0; i < 7; ++i)
        expMarksMatch = expMarksMatch
            && CostesStripEditor::DynamicsMeter::expGrMarks[i] == expectedExpGrMarks[i];

    for (int i = 0; i < 6; ++i)
        compMarksMatch = compMarksMatch
            && CostesStripEditor::DynamicsMeter::compGrMarks[i] == expectedCompGrMarks[i];

    expectTrue (expMarksMatch,
                "the expander's GR scale engraves 0, 3, 6, 12, 18, 27, 36 - its tiers and their midpoints");
    expectTrue (compMarksMatch,
                "the compressor's GR scale engraves 0, 3, 6, 9, 12, 18 - its tiers and their midpoints");

    // v2.72.0 (Clement's request): the GR ladders' THREE-TIER geometry -
    // each tier owns exactly a third of the bar. The expander reads
    // 0-6, 6-18, 18-36 dB, the compressor 0-3, 3-9, 9-18 (v2.73.0,
    // Clement's remark 4). The pure mapping is public so the geometry
    // can be pinned exactly: 0 dB is fraction 0, the first tier
    // boundary exactly a third, the second two thirds, the ladder's
    // full scale 1.0
    expectTrue (std::abs (CostesStripEditor::DynamicsMeter::grFractionForDb (0.0f, 36.0f, 6.0f, 18.0f)) < 0.0001f
                    && std::abs (CostesStripEditor::DynamicsMeter::grFractionForDb (6.0f, 36.0f, 6.0f, 18.0f) - 1.0f / 3.0f) < 0.0001f
                    && std::abs (CostesStripEditor::DynamicsMeter::grFractionForDb (18.0f, 36.0f, 6.0f, 18.0f) - 2.0f / 3.0f) < 0.0001f
                    && std::abs (CostesStripEditor::DynamicsMeter::grFractionForDb (36.0f, 36.0f, 6.0f, 18.0f) - 1.0f) < 0.0001f,
                "the expander's GR ladder puts 0, 6, 18 and 36 dB at the top, the thirds and the bottom");

    expectTrue (std::abs (CostesStripEditor::DynamicsMeter::grFractionForDb (0.0f, 18.0f, 3.0f, 9.0f)) < 0.0001f
                    && std::abs (CostesStripEditor::DynamicsMeter::grFractionForDb (3.0f, 18.0f, 3.0f, 9.0f) - 1.0f / 3.0f) < 0.0001f
                    && std::abs (CostesStripEditor::DynamicsMeter::grFractionForDb (9.0f, 18.0f, 3.0f, 9.0f) - 2.0f / 3.0f) < 0.0001f
                    && std::abs (CostesStripEditor::DynamicsMeter::grFractionForDb (18.0f, 18.0f, 3.0f, 9.0f) - 1.0f) < 0.0001f,
                "the compressor's GR ladder puts 0, 3, 9 and 18 dB at the top, the thirds and the bottom");

    expectTrue (std::abs (CostesStripEditor::DynamicsMeter::grFractionForDb (3.0f, 36.0f, 6.0f, 18.0f) - 1.0f / 6.0f) < 0.0001f
                    && std::abs (CostesStripEditor::DynamicsMeter::grFractionForDb (1.5f, 18.0f, 3.0f, 9.0f) - 1.0f / 6.0f) < 0.0001f,
                "half the first tier sits at the first sixth on BOTH ladders - 3 dB expander, 1.5 dB compressor");

    expectTrue (std::abs (CostesStripEditor::DynamicsMeter::grFractionForDb (12.0f, 36.0f, 6.0f, 18.0f) - 0.5f) < 0.0001f
                    && std::abs (CostesStripEditor::DynamicsMeter::grFractionForDb (6.0f, 18.0f, 3.0f, 9.0f) - 0.5f) < 0.0001f,
                "the second tier's midpoint sits at the half on BOTH ladders - 12 dB expander, 6 dB compressor");

    expectTrue (std::abs (CostesStripEditor::DynamicsMeter::grFractionForDb (27.0f, 36.0f, 6.0f, 18.0f) - 5.0f / 6.0f) < 0.0001f
                    && std::abs (CostesStripEditor::DynamicsMeter::grFractionForDb (13.5f, 18.0f, 3.0f, 9.0f) - 5.0f / 6.0f) < 0.0001f,
                "the third tier's midpoint sits at the five-sixths line - 27 dB expander, 13.5 dB compressor");

    {
        bool expMonotonic = true, compMonotonic = true;

        for (int j = 0; j < 35; ++j)
            expMonotonic = expMonotonic
                && CostesStripEditor::DynamicsMeter::grFractionForDb ((float) j, 36.0f, 6.0f, 18.0f)
                   < CostesStripEditor::DynamicsMeter::grFractionForDb ((float) (j + 1), 36.0f, 6.0f, 18.0f);

        for (int j = 0; j < 17; ++j)
            compMonotonic = compMonotonic
                && CostesStripEditor::DynamicsMeter::grFractionForDb ((float) j, 18.0f, 3.0f, 9.0f)
                   < CostesStripEditor::DynamicsMeter::grFractionForDb ((float) (j + 1), 18.0f, 3.0f, 9.0f);

        expectTrue (expMonotonic && compMonotonic,
                    "the three-tier GR geometry never folds back - every rung sits strictly below the one above it, across both tier boundaries");
    }

    // v2.75.0 (Clement's spec): the two dynamics stages stack INSIDE
    // the console's middle column - the expander's whole trio at the
    // TOP, the compressor's UNDER it, the two IN ladders plumb on one
    // vertical line, both stages the same height
    expectTrue (meters[0] != nullptr && meters[3] != nullptr
                    && meters[3]->getBounds().getY() > meters[2]->getBounds().getBottom()
                    && meters[3]->getBounds().getX() == meters[0]->getBounds().getX()
                    && meters[0]->getHeight() == meters[3]->getHeight()
                    && meters[2]->getHeight() == meters[5]->getHeight(),
                "the expander's stage sits at the top of the middle column, the compressor's under it - two stacked stages");

    // v2.76.0 (Clement's remark): the dynamics stages returned to
    // their v2.74 height - the ladders stand 211 px tall again, the
    // 500-series density of the one-row band
    expectTrue (meters[0] != nullptr && meters[3] != nullptr
                    && meters[0]->getHeight() == 211
                    && meters[3]->getHeight() == 211,
                "the dynamics stages stand at their v2.74 height again - the ladders 211 px tall");

    // v2.76.0 (Clement's remark): the front end reads TWO KNOBS A
    // ROW - TRIM and TILT (0 and 3) share the first line, the two
    // cuts (HPF and LPF, 1 and 2) the line under it, then the EQ's
    // first band (7) closes the column
    expectTrue (sliders[0] != nullptr && sliders[3] != nullptr
                    && sliders[1] != nullptr && sliders[2] != nullptr
                    && sliders[7] != nullptr
                    && sliders[0]->getBounds().getY() == sliders[3]->getBounds().getY()
                    && sliders[1]->getBounds().getY() == sliders[2]->getBounds().getY()
                    && sliders[1]->getBounds().getY() > sliders[0]->getBounds().getY()
                    && sliders[7]->getBounds().getY() > sliders[1]->getBounds().getY(),
                "column 1 reads two knobs a row - TRIM and TILT on one line, HPF and LPF on the next, the EQ under it");

    // v2.78.0 (Clement's remarks): the EQ reads SSL-style - the HIGHS
    // at the top of the stack, the lows at the bottom (15 is the HF
    // band's FREQ, 12 HMF's, 9 LMF's, 7 the LF's) - and the four rows
    // sit at one tight 115 px pitch (v2.79.0: the selector lines are
    // gone, the rows own their whole room), the caption words gone
    expectTrue (sliders[15] != nullptr && sliders[12] != nullptr
                    && sliders[9] != nullptr && sliders[7] != nullptr
                    && sliders[15]->getBounds().getY() < sliders[12]->getBounds().getY()
                    && sliders[12]->getBounds().getY() < sliders[9]->getBounds().getY()
                    && sliders[9]->getBounds().getY() < sliders[7]->getBounds().getY()
                    && sliders[12]->getBounds().getY() - sliders[15]->getBounds().getY() == 115
                    && sliders[9]->getBounds().getY() - sliders[12]->getBounds().getY() == 115
                    && sliders[7]->getBounds().getY() - sliders[9]->getBounds().getY() == 115,
                "the EQ reads SSL-style - HF at the top, LF at the bottom, four rows at one tight 115 px pitch");

    // and the band names themselves: each name stands in a slim
    // margin column at its row's left, LEVEL WITH ITS KNOBS (LMF's
    // name at the left of its GAIN knob, 10), with its band's own
    // glyph riding right under the words
    {
        const auto bandLabels = editor.getEqBandLabels();
        expectTrue (bandLabels[1] != nullptr && sliders[10] != nullptr
                        && bandLabels[1]->getText() == "LMF"
                        && bandLabels[1]->getBounds().getRight() <= sliders[10]->getBounds().getX()
                        && bandLabels[1]->getBounds().getY() >= sliders[10]->getBounds().getY()
                        && bandLabels[1]->getBounds().getBottom() <= sliders[10]->getBounds().getBottom(),
                    "the band names stand level with their knobs - LMF engraved at the left of its GAIN knob");
    }

    // and the knobs themselves read the SSL way (v2.78.0, Clement's
    // remark 9): the GAIN at the LEFT of each band's row, the FREQ at
    // its right (LMF's pair: 10 and 9)
    expectTrue (sliders[10] != nullptr && sliders[9] != nullptr
                    && sliders[10]->getBounds().getX() < sliders[9]->getBounds().getX()
                    && sliders[10]->getBounds().getY() == sliders[9]->getBounds().getY()
                    && sliders[10]->getBounds().getHeight() == sliders[9]->getBounds().getHeight(),
                "each EQ row reads GAIN at the left, FREQ at the right - the SSL's own order");

    // and the three columns themselves: the trim knob inside the
    // 230 px front end, the expander's THRESH (17) inside the middle
    // column, DRIVE (4) inside the 142 px output chain at the right
    expectTrue (sliders[0] != nullptr && sliders[17] != nullptr && sliders[4] != nullptr
                    && sliders[0]->getBounds().getRight() <= 242
                    && sliders[17]->getBounds().getX() >= 254
                    && sliders[17]->getBounds().getRight() <= 694
                    && sliders[4]->getBounds().getX() >= 706,
                "the console reads as three columns - the front end, the dynamics, the output chain");

    // the reserved frames (v2.75.0, Clement's spec): the TRANSIENT
    // slot engraved between the two stages, PHASE INVERT, STEREO
    // PAN, STEREO WIDTH and MONO MAKER stacked above DRIVE in column
    // 3, OUTPUT TRIM under it - the empty frames the coming builds
    // will fill
    {
        const auto& transient = editor.getTransientFrame();
        const auto& reserved = editor.getReservedFrames();

        expectTrue (transient.getY() > meters[2]->getBounds().getBottom()
                        && meters[3]->getBounds().getY() > transient.getBottom()
                        && transient.getHeight() == 220
                        && reserved[0].getY() < reserved[1].getY()
                        && reserved[1].getY() < reserved[2].getY()
                        && reserved[2].getY() < reserved[3].getY()
                        && reserved[3].getBottom() <= sliders[4]->getBounds().getY()
                        && sliders[4]->getBounds().getBottom() <= reserved[4].getY(),
                    "the reserved frames sit engraved in the grid - TRANSIENT at its own 220 px between the stages, the output chain around DRIVE");
    }

    // v2.72.0 (Clement's request): each stage's GR ladder sits at the
    // LEFT of its own block now, between the OUT meter and the knobs -
    // the columns read IN, OUT, GR, then the controls
    expectTrue (meters[2] != nullptr && sliders[17] != nullptr
                    && meters[2]->getBounds().getX() > meters[1]->getBounds().getRight()
                    && meters[2]->getBounds().getRight() < sliders[17]->getBounds().getX(),
                "the expander's GR ladder reads IN, OUT, GR - left of its knobs");

    expectTrue (meters[5] != nullptr && sliders[22] != nullptr
                    && meters[5]->getBounds().getX() > meters[4]->getBounds().getRight()
                    && meters[5]->getBounds().getRight() < sliders[22]->getBounds().getX(),
                "the compressor's GR ladder reads IN, OUT, GR - left of its knobs");

    // the dynamics knobs wear the EQ's own cap colours (v2.68.0,
    // Clement's remark): the expander the HMF band's ivory, the
    // compressor the LMF band's saturated blue - index 17 is the
    // expander's THRESH, 22 the compressor's
    expectTrue (sliders[17] != nullptr
                    && sliders[17]->findColour (juce::Slider::backgroundColourId)
                           == juce::Colour (0xffefe0bc),
                "the expander's knobs wear the HMF band's saturated ivory cap");
    expectTrue (sliders[22] != nullptr
                    && sliders[22]->findColour (juce::Slider::backgroundColourId)
                           == juce::Colour (0xff6f9fd4),
                "the compressor's knobs wear the LMF band's saturated blue cap");

    // the dynamics knobs grew to the SSL's own 72 px square (v2.79.0,
    // Clement's remark 4): the caption line the SC row once took is
    // gone, so the two knob rows own their whole height
    expectTrue (sliders[17] != nullptr && sliders[0] != nullptr
                    && sliders[17]->getHeight() <= sliders[0]->getHeight()
                    && sliders[17]->getHeight() > 64
                    && sliders[17]->getWidth() == 72,
                "the dynamics knobs grew to the row-1 columns' own 72");

    // the paint itself must survive degenerate sizes: the glyphs draw
    // against whatever bounds the layout hands them
    {
        const int testSizes[][2] = { { 860, 764 }, { 400, 120 }, { 3, 3 }, { 2000, 800 } };

        for (const auto& size : testSizes)
        {
            editor.setSize (size[0], size[1]);

            juce::Image canvas (juce::Image::ARGB, juce::jmax (1, size[0]),
                                juce::jmax (1, size[1]), true);
            juce::Graphics g (canvas);

            for (auto* icon : editor.getSectionIcons())
                icon->paint (g);

            for (auto* icon : bandIcons)
                icon->paint (g);



            // the meters paint against degenerate bounds too - an
            // empty ladder, never a division
            for (auto* meter : meters)
                meter->paint (g);

            editor.paint (g);
        }
    }

    // the peak hold (v2.67.0, Clement's remark): every ladder takes
    // its reading's own extreme from the first paint and holds it - a
    // fresh strip holds silence (the IN and OUT ladders) and unity
    // (the GR ladders). The degenerate sizes above paint nothing
    // (the early return), so the first real paint here arms the hold
    {
        juce::Image canvas (juce::Image::ARGB, 860, 764, true);
        juce::Graphics g (canvas);

        for (int i = 0; i < 6; ++i)
        {
            meters[(size_t) i]->paint (g);

            const float expected = (i % 3 == 2) ? 0.0f : -120.0f;

            expectTrue (meters[(size_t) i]->holdSet,
                        "every ladder takes its first peak from its first paint");
            expectTrue (std::abs (meters[(size_t) i]->getPeakHoldDb() - expected) < 0.01f,
                        "a fresh strip holds silence on the IN and OUT ladders, unity on the GR ladders");
        }
    }

    editor.setSize (860, 764);

    // ---- the equally spaced slope knob (Clement's remark 1) ----
    // the seven legal orders map to the seven equal knob positions
    // 0, 1/6 ... 1 - no value-linear huddle of the weak slopes - and
    // every legal order round-trips through its normalised step with
    // no drift, so sessions load exactly as they were saved
    auto* lowOrderParam = dynamic_cast<juce::RangedAudioParameter*> (
        stripProc.getApvts().getParameter ("lowCutOrder"));
    expectTrue (lowOrderParam != nullptr,
                "the low cut slope parameter is a ranged parameter");

    if (lowOrderParam != nullptr)
    {
        const float legalOrders[] = { 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f };

        for (int i = 0; i < 7; ++i)
        {
            stripProc.getApvts().getParameterAsValue ("lowCutOrder").setValue (legalOrders[i]);

            expectNear (lowOrderParam->getValue(), (float) i / 6.0f, 0.001,
                         "the seven legal slopes sit at seven EQUAL knob positions (spacing 1/6)");
            expectNear (stripProc.getApvts().getRawParameterValue ("lowCutOrder")->load(),
                        legalOrders[i], 0.0001,
                        "every legal order round-trips through its knob step without drift");
        }
    }
}

void testCostesStripKnobDefaultsAndCaps()
{
    beginTest ("v2.63.0: command-click defaults on every knob and slider, and the SSL cap colours");

    CostesStrip stripProc;
    stripProc.prepareToPlay (48000.0, 512);

    CostesStripEditor editor (stripProc);

    // every knob and slider of the strip, in the accessor's stable
    // order: the five row-1 knobs, the two cut slopes, the EQ's
    // FREQ/GAIN knobs and width sweeps band by band, then each stage's
    // five knobs and its sidechain pair (v2.66.0: the expander's
    // attack knob became a FAST/SLOW plate; v2.72.0: the compressor's
    // LINK selector joins as the 32nd control; v2.73.0: the expander's
    // KNEE selector closes the array as the 33rd)
    const auto sliders = editor.getStripSliders();

    const char* paramIds[33] =
    {
        "trimInDb", "lowCutHz", "highCutHz", "tiltDb", "driveDb",
        "lowCutOrder", "highCutOrder",
        "eq1Hz", "eq1Gain", "eq2Hz", "eq2Gain", "eq2Oct",
        "eq3Hz", "eq3Gain", "eq3Oct", "eq4Hz", "eq4Gain",
        "expThreshold", "expRange", "expRelease", "expRatio", "expMix",
        "compThreshold", "compRatio", "compRelease", "compGain", "compMix",
        "compHpf", "compScLpf", "expScHpf", "expScLpf",
        "compLink", "expKnee"
    };

    const float expectedDefaults[33] =
    {
        0.0f, 16.0f, 24000.0f, 0.0f, 0.0f,
        2.0f, 2.0f,
        100.0f, 0.0f, 400.0f, 0.0f, 1.0f,
        1600.0f, 0.0f, 1.0f, 7200.0f, 0.0f,
        -50.0f, 0.0f, 0.3f, 2.0f, 100.0f,
        0.0f, 2.0f, 0.25f, 0.0f, 100.0f, 16.0f,
        24000.0f, 16.0f, 24000.0f,
        3.0f, 0.0f
    };

    // a legal non-default position per control, so the restore below is
    // never a no-op
    const float perturbed[33] =
    {
        3.3f, 120.0f, 8000.0f, 2.5f, 6.0f,
        6.0f, 3.0f,
        250.0f, -3.5f, 1000.0f, 4.0f, 3.0f,
        2500.0f, -6.0f, 2.0f, 9000.0f, 7.5f,
        -22.5f, 18.0f, 1.4f, 7.0f, 35.0f,
        -18.5f, 9.0f, 0.8f, 11.0f, 60.0f, 320.0f,
        2500.0f, 400.0f, 2500.0f,
        0.0f, 16.0f
    };

    // ---- the command-click default (Clement's remark 2): every control
    // knows its default, and the exact path the mouse takes - through
    // the attachment, into the parameter tree - lands it there ----
    for (int i = 0; i < 33; ++i)
    {
        expectNear (sliders[(size_t) i]->cmdClickDefault, expectedDefaults[i], 0.0001,
                    "every knob and slider carries its command-click default");

        stripProc.getApvts().getParameterAsValue (paramIds[i]).setValue (perturbed[i]);
        sliders[(size_t) i]->applyCmdClickDefault();

        expectNear (stripProc.getApvts().getRawParameterValue (paramIds[i])->load(),
                    expectedDefaults[i], 0.0001,
                    "a command-click restores the parameter's default through the attachment");
    }

    // ---- the cap colours (remark 4 + v2.62.0 + v2.68.0): the 23
    // rotary knobs carry their band's colour on the
    // backgroundColourId - the row-1 caps stay black EXCEPT the pastel
    // red high cut, the LPF now (v2.63.0), the EQ bands speak the SSL
    // colours, and the dynamics knobs wear their stage's own colour
    // (v2.68.0, Clement's remark): the expander borrows the HMF band's
    // off-white, the compressor the LMF band's pastel blue - the
    // two stages echo the EQ's own palette. The expander's attack
    // knob became a FAST/SLOW plate in v2.66.0, so one rotary cap left the
    // loop. The eight linear selectors draw no cap, so they are not in
    // the loop ----
    const int knobIndex[23] = { 0, 1, 2, 3, 4, 7, 8, 9, 10, 12, 13, 15, 16,
                               17, 18, 19, 20, 21,
                               22, 23, 24, 25, 26 };
    const unsigned int knobCap[23] =
    {
        0xff23232au, 0xff23232au, 0xffcd7f6du, 0xff23232au, 0xff23232au,
        0xff23232au, 0xff23232au,
        0xff6f9fd4u, 0xff6f9fd4u,
        0xffefe0bcu, 0xffefe0bcu,
        0xffcd7f6du, 0xffcd7f6du,
        0xffefe0bcu, 0xffefe0bcu, 0xffefe0bcu,
        0xffefe0bcu, 0xffefe0bcu,
        0xff6f9fd4u, 0xff6f9fd4u, 0xff6f9fd4u,
        0xff6f9fd4u, 0xff6f9fd4u
    };

    for (int k = 0; k < 23; ++k)
        expectTrue (sliders[(size_t) knobIndex[k]]->findColour (juce::Slider::backgroundColourId, true)
                        == juce::Colour (knobCap[k]),
                    "each rotary knob wears its band's cap colour (LPF and HF saturated red, LMF saturated blue, HMF saturated ivory, saturated ivory expander, saturated blue compressor)");

    // v2.75.0 (Clement's spec): the front end is the console's first
    // column - the trim knob at its head, the EQ's first band under
    // the cuts, both inside the same 230 px column and stacked in
    // the sound's own order
    expectTrue (sliders[0] != nullptr && sliders[7] != nullptr
                    && sliders[0]->getBounds().getRight() <= 242
                    && sliders[7]->getBounds().getRight() <= 242
                    && sliders[7]->getBounds().getY() > sliders[0]->getBounds().getBottom(),
                "the trim knob and the EQ's first band stack inside column 1, one under the other");
}

void testCostesStripEq()
{
    beginTest ("v2.61.0: the Costes strip's four-band SSL-style EQ - bells, shelves, octave bandwidths, the global plate");

    juce::MidiBuffer noMidi;

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    proc.toggleCostesStrip();

    auto* strip = dynamic_cast<CostesStrip*> (proc.getHostedStrip().getActivePlugin());
    expectTrue (strip != nullptr, "the strip is hosted");

    if (strip == nullptr)
        return;

    // the same coherent lock-in amplitude the v2.59 audio test uses: a
    // peak reading of a tone is grid-quantized, the lock-in correlates
    // the block against the probe's own cos/sin over an exact integer
    // number of cycles. Every probe here is a multiple of 100 Hz, so one
    // 480-sample block at 48 kHz holds a whole number of cycles
    const int lockinBlocks = 24;

    auto measureAmplitude = [&proc, &noMidi] (double frequencyHz,
                                              double probeHz) -> double
    {
        SineSource src (frequencyHz, 48000.0, 0.25f);
        juce::AudioBuffer<float> block (2, 480);

        for (int i = 0; i < lockinBlocks; ++i)
        {
            src.fill (block);
            proc.processBlock (block, noMidi);
        }

        src.fill (block);
        proc.processBlock (block, noMidi);

        const double probeOmega = juce::MathConstants<double>::twoPi * probeHz / 48000.0;
        const double phase0 = probeOmega * (double) (lockinBlocks * 480);

        double c = 0.0, s = 0.0;

        for (int i = 0; i < 480; ++i)
        {
            const double phase = phase0 + probeOmega * (double) i;
            const double sample = block.getSample (0, i);
            c += sample * std::cos (phase);
            s += sample * std::sin (phase);
        }

        return 2.0 * std::sqrt (c * c + s * s) / 480.0;
    };

    // the unity reference with the EQ parked (every band at 0 dB - the
    // bit-exact identity - and the plate ON): the parked cuts ride both
    // the reference and every probe below, so they cancel in the ratios
    const double unity1k = measureAmplitude (1000.0, 1000.0);

    // the global plate at rest: OFF runs zero biquads, ON runs the four
    // identity bands - both read the parked chain to within float dust
    strip->getApvts().getParameterAsValue ("eqOn").setValue (false);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0) / unity1k), 0.0, 0.02,
                "EQ OFF at rest is transparent against the unity reference");
    strip->getApvts().getParameterAsValue ("eqOn").setValue (true);

    // band 1 (LF): a bell reads EXACTLY its gain at its centre - an RBJ
    // property the whole design leans on. Expected values come from the
    // float64 simulation of the exact parked chain
    strip->getApvts().getParameterAsValue ("eq1Gain").setValue (6.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (100.0, 100.0) / unity1k), 6.0, 0.3,
                "band 1 bell +6 dB reads +6 dB at its 100 Hz centre");
    strip->getApvts().getParameterAsValue ("eq1Gain").setValue (-6.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (100.0, 100.0) / unity1k), -6.0, 0.3,
                "band 1 bell -6 dB reads -6 dB at its 100 Hz centre");
    strip->getApvts().getParameterAsValue ("eq1Gain").setValue (0.0f);

    // band 1's shelf: the same corner knob drives the low shelf. fc 400
    // at +6 dB has flattened to +5.97 dB by 100 Hz (the simulation)
    strip->getApvts().getParameterAsValue ("eq1Bell").setValue (false);
    strip->getApvts().getParameterAsValue ("eq1Hz").setValue (400.0f);
    strip->getApvts().getParameterAsValue ("eq1Gain").setValue (6.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (100.0, 100.0) / unity1k), 5.97, 0.4,
                "band 1 low shelf +6 dB at 400 Hz reads +5.97 dB at 100 Hz");
    strip->getApvts().getParameterAsValue ("eq1Gain").setValue (0.0f);
    strip->getApvts().getParameterAsValue ("eq1Hz").setValue (100.0f);
    strip->getApvts().getParameterAsValue ("eq1Bell").setValue (true);

    // band 2 (LMF): the bell centre, then the bandwidth sweep - the
    // same +6 dB bell probed one octave up reads +0.12 dB narrow
    // (0.3 oct) and +3.00 dB wide (2.0 oct): the octave readout moves
    // the bell exactly the way it says it does
    strip->getApvts().getParameterAsValue ("eq2Gain").setValue (6.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (400.0, 400.0) / unity1k), 6.0, 0.3,
                "band 2 bell +6 dB reads +6 dB at its 400 Hz centre");
    strip->getApvts().getParameterAsValue ("eq2Oct").setValue (0.4f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (800.0, 800.0) / unity1k), 0.21, 0.4,
                "a 0.4-octave band 2 bell reaches +0.21 dB one octave above its centre");
    strip->getApvts().getParameterAsValue ("eq2Oct").setValue (2.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (800.0, 800.0) / unity1k), 3.0, 0.4,
                "a 2.0-octave band 2 bell reaches +3.00 dB one octave above its centre");

    // v2.62.0 (Clement's remark 1): a narrow bell still reads
    // exactly its gain at its centre - the widest and the narrowest
    // legal widths behave the same, an RBJ property. The dip Clement
    // saw reading ~-3 dB instead of -6 dB on the analyser is the
    // ANALYSER's own 31-bin frequency smoothing diluting dips narrower
    // than about half an octave - the filter itself is exact at any
    // width, and this lock-in probe (immune to that smearing) proves it.
    // v2.63.0 (remark 1): 0.4 octaves is the narrowest legal width now
    strip->getApvts().getParameterAsValue ("eq2Oct").setValue (0.4f);
    strip->getApvts().getParameterAsValue ("eq2Gain").setValue (-6.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (400.0, 400.0) / unity1k), -6.0, 0.3,
                "a 0.4-octave band 2 bell at -6 dB still reads EXACTLY -6 dB at its centre");
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (800.0, 800.0) / unity1k), -0.21, 0.15,
                "the same 0.4-octave dip is nearly closed one octave above its centre");
    strip->getApvts().getParameterAsValue ("eq2Gain").setValue (0.0f);
    strip->getApvts().getParameterAsValue ("eq2Oct").setValue (1.0f);

    // band 3 (HMF): the centre, then a wide -6 dB bell probed an
    // octave BELOW its centre (the mirrored side of the band 2 check)
    strip->getApvts().getParameterAsValue ("eq3Gain").setValue (6.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1600.0, 1600.0) / unity1k), 6.0, 0.3,
                "band 3 bell +6 dB reads +6 dB at its 1600 Hz centre");
    strip->getApvts().getParameterAsValue ("eq3Gain").setValue (-6.0f);
    strip->getApvts().getParameterAsValue ("eq3Oct").setValue (2.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (800.0, 800.0) / unity1k), -3.01, 0.4,
                "a 2.0-octave band 3 bell at -6 dB reads -3.01 dB one octave below its centre");
    strip->getApvts().getParameterAsValue ("eq3Gain").setValue (0.0f);
    strip->getApvts().getParameterAsValue ("eq3Oct").setValue (1.0f);

    // band 4 (HF): the centre, then the high shelf. fc 7200 at +6 dB has
    // reached +3.74 dB of its own by 16 kHz and the parked 24 kHz cut
    // eats another 0.79 dB there, so the chain reads +2.95 dB
    strip->getApvts().getParameterAsValue ("eq4Gain").setValue (6.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (7200.0, 7200.0) / unity1k), 6.0, 0.3,
                "band 4 bell +6 dB reads +6 dB at its 7200 Hz centre");
    strip->getApvts().getParameterAsValue ("eq4Bell").setValue (false);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (16000.0, 16000.0) / unity1k), 2.95, 0.4,
                "band 4 high shelf +6 dB at 7200 Hz reads +2.95 dB at 16 kHz");
    strip->getApvts().getParameterAsValue ("eq4Bell").setValue (true);
    strip->getApvts().getParameterAsValue ("eq4Gain").setValue (0.0f);

    // the global plate with a live band: OFF must silence it exactly -
    // zero sections run, the +6 dB bell never touches the signal
    strip->getApvts().getParameterAsValue ("eq1Gain").setValue (6.0f);
    strip->getApvts().getParameterAsValue ("eqOn").setValue (false);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (100.0, 100.0) / unity1k), 0.0, 0.02,
                "EQ OFF silences a live +6 dB band exactly (the true bypass)");
    strip->getApvts().getParameterAsValue ("eqOn").setValue (true);
    strip->getApvts().getParameterAsValue ("eq1Gain").setValue (0.0f);

    // the readouts' own parse plumbing (v2.61.0): a typed "0.50 oct"
    // lands on the nearest legal width, and the shape plate parses its
    // words - "shelf" switches band 1 off its bell, "bell" back on
    auto* eq2OctParam = dynamic_cast<juce::RangedAudioParameter*> (
        strip->getApvts().getParameter ("eq2Oct"));
    expectTrue (eq2OctParam != nullptr,
                "the band 2 bandwidth parameter is a ranged parameter");

    if (eq2OctParam != nullptr)
    {
        expectNear (eq2OctParam->convertFrom0to1 (eq2OctParam->getValueForText ("0.50 oct")), 0.4, 0.01,
                    "a typed 0.50 oct parses to the nearest legal width (0.4 oct)");

        // v2.62.0 (Clement's remarks 2-3) / v2.63.0 (remark 1): the
        // bandwidth sweep runs 0.4 to 4.0 octaves and holds exactly
        // seven legal widths, spread EQUALLY across the knob travel -
        // the positions are i/6, not linear in value - and every legal
        // width round-trips through its normalised step with no drift,
        // so sessions load exactly as they were saved
        const float legalWidths[] = { 0.4f, 0.7f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };

        for (int i = 0; i < 7; ++i)
        {
            strip->getApvts().getParameterAsValue ("eq2Oct").setValue (legalWidths[i]);

            expectNear (eq2OctParam->getValue(), (float) i / 6.0f, 0.001,
                        "the seven legal widths sit at seven EQUAL knob positions (spacing 1/6)");
            expectNear (strip->getApvts().getRawParameterValue ("eq2Oct")->load(),
                        legalWidths[i], 0.0001,
                        "every legal width round-trips through its knob step without drift");
        }

        // a session value outside the legal set snaps to the nearest
        // legal width on load - and a tie resolves to the NARROWER width
        strip->getApvts().getParameterAsValue ("eq2Oct").setValue (0.2f);
        expectNear (strip->getApvts().getRawParameterValue ("eq2Oct")->load(), 0.4, 0.0001,
                    "an old 0.2-octave width snaps up to the 0.4-octave step");
        // 0.85 octaves is NOT an exact tie in float arithmetic
        // (0.85f sits 6e-8 closer to 1.0f than to 0.7f), so the tie is
        // proven with 1.25 instead: an EXACT float tie between the
        // 1.0 and 1.5 steps that must resolve to the NARROWER 1.0
        strip->getApvts().getParameterAsValue ("eq2Oct").setValue (1.25f);
        expectNear (strip->getApvts().getRawParameterValue ("eq2Oct")->load(), 1.0, 0.0001,
                    "a tie (1.25 oct) snaps to the NARROWER 1.0-octave step");
        strip->getApvts().getParameterAsValue ("eq2Oct").setValue (2.5f);
        expectNear (strip->getApvts().getRawParameterValue ("eq2Oct")->load(), 2.0, 0.0001,
                    "a tie (2.5 oct) snaps to the NARROWER 2.0-octave step");

        // park the bandwidth back on its default
        strip->getApvts().getParameterAsValue ("eq2Oct").setValue (1.0f);
    }

    auto* eq1BellParam = dynamic_cast<juce::RangedAudioParameter*> (
        strip->getApvts().getParameter ("eq1Bell"));
    expectTrue (eq1BellParam != nullptr,
                "the band 1 shape parameter is a ranged parameter");

    if (eq1BellParam != nullptr)
    {
        expectNear (eq1BellParam->convertFrom0to1 (eq1BellParam->getValueForText ("shelf")), 0.0, 0.01,
                    "a typed shelf parses to the shelf shape");
        expectNear (eq1BellParam->convertFrom0to1 (eq1BellParam->getValueForText ("bell")), 1.0, 0.01,
                    "a typed bell parses back to the bell shape");
    }

    // ---- v2.70.0 (Clement's remark 4): the discrete knob feel ----
    // the EQ gains move in exact 0.5 dB steps and the frequencies in
    // two significant digits - the range itself quantizes both ways,
    // so every path in (a drag, a typed value, a session, an
    // automation) lands on the same grid ----
    {
        strip->getApvts().getParameterAsValue ("eq1Gain").setValue (3.3f);
        expectNear (strip->getApvts().getRawParameterValue ("eq1Gain")->load(), 3.5, 0.0001,
                    "an EQ gain set to 3.3 dB lands on the 0.5 dB grid at 3.5");
        strip->getApvts().getParameterAsValue ("eq1Gain").setValue (3.2f);
        expectNear (strip->getApvts().getRawParameterValue ("eq1Gain")->load(), 3.0, 0.0001,
                    "an EQ gain set to 3.2 dB lands on the 0.5 dB grid at 3.0");
        strip->getApvts().getParameterAsValue ("eq1Gain").setValue (0.0f);

        auto* eq2HzParam = dynamic_cast<juce::RangedAudioParameter*> (
            strip->getApvts().getParameter ("eq2Hz"));
        expectTrue (eq2HzParam != nullptr,
                    "the band 2 frequency parameter is a ranged parameter");

        if (eq2HzParam != nullptr)
        {
            strip->getApvts().getParameterAsValue ("eq2Hz").setValue (737.0f);
            expectNear (strip->getApvts().getRawParameterValue ("eq2Hz")->load(), 740.0, 0.0001,
                        "an EQ frequency set to 737 Hz snaps to two significant digits (740)");
            expectNear (eq2HzParam->convertFrom0to1 (eq2HzParam->getValueForText ("737 Hz")), 740.0, 0.01,
                        "a typed 737 Hz parses onto the same two-significant-digit grid");
            strip->getApvts().getParameterAsValue ("eq2Hz").setValue (400.0f);
        }
    }
}

// ---------------------------------------------------------------------------
// v2.64.0 (Clement's spec, roadmap C3): the dynamics section - the
// gate/expander and the classic compressor. Every expected value is the
// float64 simulation of the exact DSP (the 10 ms RMS power detector,
// the static curves, the attack/release one-poles), measured the
// harness way: the coherent lock-in on a settled tone. The settle runs
// 240 blocks (2.4 s) - the expander's release reaches 3 s, so the audio
// test's short 24-block settle would leave the gain a full
// time-constant short of its steady state
// ---------------------------------------------------------------------------
void testCostesStripDynamics()
{
    beginTest ("v2.64.0: the dynamics - expander range/ratio/mix, compressor ratio/gain/mix, the FAST ATTACK plate, the sidechain HPF");

    juce::MidiBuffer noMidi;

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    proc.toggleCostesStrip();

    auto* strip = dynamic_cast<CostesStrip*> (proc.getHostedStrip().getActivePlugin());
    expectTrue (strip != nullptr, "the strip is hosted");

    if (strip == nullptr)
        return;

    const int lockinBlocks = 240;

    auto measureAmplitude = [&] (double frequencyHz, double probeHz,
                                  double amplitude) -> double
    {
        SineSource src (frequencyHz, 48000.0, (float) amplitude);
        juce::AudioBuffer<float> block (2, 480);

        for (int i = 0; i < lockinBlocks; ++i)
        {
            src.fill (block);
            proc.processBlock (block, noMidi);
        }

        src.fill (block);
        proc.processBlock (block, noMidi);

        // the source phase at the measured block start: omega times
        // the total number of samples filled before it
        const double probeOmega = juce::MathConstants<double>::twoPi * probeHz / 48000.0;
        const double phase0 = probeOmega * (double) (lockinBlocks * 480);

        double c = 0.0, s = 0.0;

        for (int i = 0; i < 480; ++i)
        {
            const double phase = phase0 + probeOmega * (double) i;
            const double sample = block.getSample (0, i);
            c += sample * std::cos (phase);
            s += sample * std::sin (phase);
        }

        return 2.0 * std::sqrt (c * c + s * s) / 480.0;
    };

    auto setParam = [strip] (const char* id, juce::var value)
    {
        strip->getApvts().getParameterAsValue (id).setValue (value);
    };

    // the parked defaults are the strip's own identity: the expander
    // parks at range 0 and the compressor at threshold 0, both halves
    // are skipped wholesale, and a 1 kHz tone comes out at its own
    // amplitude (the parked cuts and the identity EQ move 1 kHz by
    // less than a thousandth of a dB)
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.25) / 0.25), 0.0, 0.2,
                "the parked dynamics section is a true bypass (bit-skipped at range 0 and threshold 0)");

    // the bypassed references the ratios below read against
    const double flatRef1k  = measureAmplitude (1000.0, 1000.0, 0.25);
    const double flatRef05  = measureAmplitude (1000.0, 1000.0, 0.05);
    const double flatRef100 = measureAmplitude (100.0, 100.0, 0.25);

    // ---- the EXPANDER: a loud tone (-15.05 dB RMS) rides above a -35
    // dB threshold and must pass untouched even at the deepest gate
    // settings; a quiet one (-29.03 dB RMS) sits 9.03 dB under the -20
    // dB threshold, and the expansion reads the ratio arithmetic, the
    // range clamp and the mix blend straight off the static curve ----
    setParam ("expThreshold", -35.0f);
    setParam ("expRange", 36.0f);
    setParam ("expRatio", 12.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.25) / flatRef1k), 0.0, 0.3,
                "the expander stays fully open above its threshold");

    setParam ("expThreshold", -20.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.05) / flatRef05), -36.0, 1.0,
                "deep under the threshold the expander floors at its RANGE (36 dB)");

    setParam ("expRatio", 2.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.05) / flatRef05), -9.0, 0.6,
                "a 2:1 expansion turns 9 dB under the threshold into 9 dB down");

    setParam ("expRatio", 4.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.05) / flatRef05), -27.0, 0.8,
                "a 4:1 expansion turns 9 dB under the threshold into 27 dB down");

    setParam ("expRatio", 1.2f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.05) / flatRef05), -1.8, 0.4,
                "the gentlest 1.2:1 slope still expands (1.8 dB down)");

    setParam ("expRatio", 2.0f);
    setParam ("expRange", 5.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.05) / flatRef05), -5.0, 0.5,
                "the RANGE clamps the expansion at its own 5 dB floor");

    setParam ("expRange", 36.0f);
    setParam ("expMix", 50.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.05) / flatRef05), -3.4, 0.5,
                "a 50 % mix halves the expansion in the blend (the sim says -3.38 dB)");
    setParam ("expMix", 100.0f);

    setParam ("expRatio", 12.0f);
    setParam ("expOn", false);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.05) / flatRef05), 0.0, 0.3,
                "the expander's OFF plate bypasses the whole section");
    setParam ("expOn", true);

    // back to the parked expander: range 0 is the true bypass
    setParam ("expThreshold", -50.0f);
    setParam ("expRange", 0.0f);
    setParam ("expRatio", 2.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.25) / flatRef1k), 0.0, 0.2,
                "the parked expander (range 0) is a true bypass");

    // ---- the expander's own sidechain pair (v2.66.0, Clement's
    // remarks D and E): the SC HPF and SC LPF shape what the gate's
    // detector hears. At their 16 Hz / 24000 Hz rest positions the
    // pair is hard-bypassed and the detector hears the flat band; a
    // 1600 Hz SC HPF lifts the 100 Hz bass out of the detector so the
    // gate floors at its RANGE, and a 100 Hz SC LPF lifts the 1 kHz
    // tone the same way ----
    setParam ("expThreshold", -20.0f);
    setParam ("expRange", 36.0f);
    setParam ("expRatio", 12.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (100.0, 100.0, 0.25) / flatRef100), 0.0, 0.5,
                "a 100 Hz tone above the gate threshold holds it open (the flat sidechain)");

    setParam ("expScHpf", 1600.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (100.0, 100.0, 0.25) / flatRef100), -36.0, 1.5,
                "a 1600 Hz SC HPF lifts the bass out of the detector - the gate floors at its RANGE");
    setParam ("expScHpf", 16.0f);

    setParam ("expScLpf", 100.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.25) / flatRef1k), -36.0, 1.5,
                "a 100 Hz SC LPF lifts the tone out of the detector - the gate floors at its RANGE");
    setParam ("expScLpf", 24000.0f);

    // ---- the expander's FAST ATTACK plate (v2.67.0, Clement's
    // spec): 120 blocks of silence park the detector at its floor and
    // close the gate at its RANGE, then a single 480-sample tone block
    // lands. The block's RMS reads how far the gate re-opened inside
    // its own 10 ms: the sim says -0.44 dB with the 0.3 ms fast attack
    // (the gate snaps open almost before the tone is through) and
    // -6.15 dB with the 3 ms slow one ----
    setParam ("expThreshold", -35.0f);
    setParam ("expRange", 36.0f);
    setParam ("expRatio", 12.0f);
    setParam ("expRelease", 0.1f);
    setParam ("expMix", 100.0f);

    auto expOnsetRmsDb = [&] (bool fastAttack) -> double
    {
        setParam ("expFastAttack", fastAttack);

        juce::AudioBuffer<float> silence (2, 480);
        silence.clear();

        for (int i = 0; i < 120; ++i)
            proc.processBlock (silence, noMidi);

        SineSource src (1000.0, 48000.0, 0.25f);
        juce::AudioBuffer<float> block (2, 480);
        src.fill (block);
        proc.processBlock (block, noMidi);

        double energy = 0.0;

        for (int i = 0; i < 480; ++i)
            energy += (double) block.getSample (0, i) * block.getSample (0, i);

        return juce::Decibels::gainToDecibels (std::sqrt (energy / 480.0)
                                              / (0.25 / std::sqrt (2.0)));
    };

    expectNear (expOnsetRmsDb (true), -0.4, 1.2,
                "the expander's FAST attack (0.3 ms) re-opens the gate almost inside the first 10 ms (the sim says -0.44 dB)");
    expectNear (expOnsetRmsDb (false), -6.2, 1.2,
                "the expander's SLOW attack (3 ms) lets the tone through half-closed (the sim says -6.15 dB)");
    setParam ("expRelease", 0.3f);

    // back to the parked expander: range 0 is the true bypass
    setParam ("expThreshold", -50.0f);
    setParam ("expRange", 0.0f);
    setParam ("expRatio", 2.0f);

    // ---- the COMPRESSOR: the loud tone (-15.05 dB RMS) sits 9.95 dB
    // over a -25 dB threshold, and the reduction reads the ratio
    // arithmetic straight off the static curve ----
    setParam ("compThreshold", -25.0f);
    setParam ("compRatio", 2.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.25) / flatRef1k), -5.0, 0.6,
                "a 2:1 compressor turns 9.95 dB over threshold into 4.97 dB down");

    setParam ("compRatio", 20.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.25) / flatRef1k), -9.5, 0.6,
                "a 20:1 compressor limits 9.95 dB over threshold to 9.48 dB down");

    setParam ("compRatio", 10.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.25) / flatRef1k), -9.0, 0.6,
                "a 10:1 compressor reads between the two");

    setParam ("compRatio", 2.0f);
    setParam ("compGain", 6.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.25) / flatRef1k), 1.0, 0.7,
                "the make-up gain rides the wet half (+6 dB on a -4.97 dB reduction)");
    setParam ("compGain", 0.0f);

    setParam ("compMix", 50.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.25) / flatRef1k), -2.1, 0.5,
                "a 50 % mix halves the reduction in the blend (the sim says -2.14 dB)");
    setParam ("compMix", 100.0f);

    setParam ("compRatio", 20.0f);
    setParam ("compOn", false);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.25) / flatRef1k), 0.0, 0.3,
                "the compressor's OFF plate bypasses the whole section");
    setParam ("compOn", true);

    // ---- the FAST ATTACK plate (Clement's spec): 120 blocks of
    // silence park the detector at its floor and the gain open, then a
    // single 480-sample tone block lands. The block's RMS reads how
    // much of the reduction engaged inside its own 10 ms: the sim says
    // -4.6 dB with the 3 ms fast attack and -1.1 dB with the classic
    // 30 ms - the FAST plate clamps the transient, the SLOW one lets
    // its point through ----
    setParam ("compThreshold", -30.0f);
    setParam ("compRatio", 20.0f);
    setParam ("compHpf", 16.0f);

    auto onsetRmsDb = [&] (bool fastAttack) -> double
    {
        setParam ("compFastAttack", fastAttack);

        juce::AudioBuffer<float> silence (2, 480);
        silence.clear();

        for (int i = 0; i < 120; ++i)
            proc.processBlock (silence, noMidi);

        SineSource src (1000.0, 48000.0, 0.25f);
        juce::AudioBuffer<float> block (2, 480);
        src.fill (block);
        proc.processBlock (block, noMidi);

        double energy = 0.0;

        for (int i = 0; i < 480; ++i)
            energy += (double) block.getSample (0, i) * block.getSample (0, i);

        return juce::Decibels::gainToDecibels (std::sqrt (energy / 480.0)
                                              / (0.25 / std::sqrt (2.0)));
    };

    expectNear (onsetRmsDb (true), -4.6, 1.2,
                "the FAST attack engages most of the reduction inside the first 10 ms (the sim says -4.6 dB)");
    expectNear (onsetRmsDb (false), -1.1, 1.2,
                "the SLOW attack lets most of the transient through (the sim says -1.1 dB)");

    // both plates converge on the same steady reduction: 14.95 dB over
    // threshold at 20:1 is 14.2 dB down (the sim says -14.23)
    setParam ("compFastAttack", true);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.25) / flatRef1k), -14.2, 0.8,
                "the fast attack settles on the full 14.2 dB reduction");
    setParam ("compFastAttack", false);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.25) / flatRef1k), -14.2, 0.8,
                "the slow attack settles on the same 14.2 dB reduction");

    // ---- the sidechain HPF (Clement's spec): a 100 Hz tone at -25 dB
    // threshold and 20:1 compresses with the corner at its 16 Hz rest
    // position (the honest "off", a flat band since v2.66.0), but a
    // 1000 Hz corner removes it from the
    // detector and the compressor opens. The sim pins the exact
    // reading, Jensen ripple included: -9.86 dB ----
    setParam ("compThreshold", -25.0f);
    setParam ("compHpf", 16.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (100.0, 100.0, 0.25) / flatRef100), -9.9, 0.9,
                "the sidechain HPF at its 16 Hz rest position leaves the bass in the detector");

    setParam ("compHpf", 1000.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (100.0, 100.0, 0.25) / flatRef100), 0.0, 0.5,
                "a 1000 Hz sidechain corner frees the bass from the detector");

    // the compressor's own SC LPF (v2.66.0, remark E): a 100 Hz corner
    // lifts the 1 kHz tone out of the detector and the compressor
    // opens - the LPF is the HPF's mirror, its 24000 Hz rest position
    // a flat band
    setParam ("compScLpf", 100.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.25) / flatRef1k), 0.0, 0.5,
                "a 100 Hz SC LPF lifts the tone out of the compressor's detector - it opens");
    setParam ("compScLpf", 24000.0f);

    // ---- the honest IN ladders (v2.67.0, Clement's remark): the
    // sidechain corners shape the detector and its GR curve, never
    // the IN meter - it reads the TRUE full-band input in its own
    // 10 ms one-pole. A corner that lifts the tone out of the
    // detector (the old detector-view IN reading sat ~48 dB down)
    // leaves the IN ladder at the tone's own level ----
    {
        setParam ("expScHpf", 1600.0f);

        juce::AudioBuffer<float> tone (2, 480);
        SineSource (100.0, 48000.0, 0.25f).fill (tone);

        for (int i = 0; i < 10; ++i)
            proc.processBlock (tone, noMidi);

        expectNear (strip->getExpInDb(), -15.05, 0.6,
                    "the expander's IN ladder reads the true full-band input - the SC corner never touches it");
        setParam ("expScHpf", 16.0f);

        setParam ("compScLpf", 100.0f);

        SineSource (1000.0, 48000.0, 0.25f).fill (tone);

        for (int i = 0; i < 10; ++i)
            proc.processBlock (tone, noMidi);

        expectNear (strip->getCompInDb(), -15.05, 0.6,
                    "the compressor's IN ladder reads the true full-band input too");
        setParam ("compScLpf", 24000.0f);
    }

    // ---- the dry/wet mix is a linear, phase-coherent interpolation
    // (v2.67.0, Clement's remark): dry and wet are the SAME signal in
    // phase, so the blend is plain gain-domain arithmetic - out = dry
    // * (1 - m) + wet * m - and with the wet untouched (the gate open
    // above its threshold, the compressor below its own) the blend
    // collapses to exactly 1 at ANY mix position. The proof rides the
    // harness's own lock-in: the settled amplitude at mix 50 reads
    // against the same tone at mix 100, and an equal-power law would
    // sit a full +3.01 dB up instead of the 0.00 dB the linear blend
    // pins ----
    {
        // the expander's half: the loud tone holds the gate open, the
        // compressor's plate is off so only the expander's blend runs
        setParam ("compOn", false);
        setParam ("expThreshold", -35.0f);
        setParam ("expRange", 36.0f);
        setParam ("expRatio", 12.0f);
        setParam ("expMix", 100.0f);

        const double wetRef1k = measureAmplitude (1000.0, 1000.0, 0.25);

        setParam ("expMix", 50.0f);

        expectNear (juce::Decibels::gainToDecibels (
                        measureAmplitude (1000.0, 1000.0, 0.25) / wetRef1k),
                    0.0, 0.01,
                    "the expander's 50 % dry/wet mix leaves the level mathematically constant with the gate open - no equal-power bump");

        // the compressor's half: a quiet tone sits below the
        // threshold, the reduction is zero, and the same arithmetic
        // holds on the compressor's own blend
        setParam ("expRange", 0.0f);
        setParam ("expMix", 100.0f);

        setParam ("compOn", true);
        setParam ("compThreshold", -30.0f);
        setParam ("compRatio", 2.0f);
        setParam ("compMix", 100.0f);

        const double quietRef1k = measureAmplitude (1000.0, 1000.0, 0.005);

        setParam ("compMix", 50.0f);

        expectNear (juce::Decibels::gainToDecibels (
                        measureAmplitude (1000.0, 1000.0, 0.005) / quietRef1k),
                    0.0, 0.01,
                    "the compressor's 50 % dry/wet mix leaves the level mathematically constant below its threshold - the phase-coherent level never moves");
    }

    // ---- the strip's own GR reaches Studio One (v2.67.0, Clement's
    // remark): the forwarder hands the host the console's value PLUS
    // the strip compressor's per-block snapshot - with no console
    // hosted the strip's own value is the whole answer, and a reset
    // to 0 never lets a stale reading leak across a voice swap ----
    {
        GRForwarder forwarder;
        expectNear (forwarder.pullGainReductionDb(), 0.0, 0.0001,
                    "a fresh forwarder reports silence (the PreSonus contract)");
        forwarder.reportStripGainReductionDb (-5.5f);
        expectNear (forwarder.pullGainReductionDb(), -5.5, 0.0001,
                    "the strip compressor's gain reduction is forwarded to the host");
        forwarder.reportStripGainReductionDb (0.0f);
        expectNear (forwarder.pullGainReductionDb(), 0.0, 0.0001,
                    "the report resets with the voice - a stale reduction never leaks");
    }

    // ---- the LISTEN plates (v2.66.0, Clement's remark A): while a
    // plate is lit the strip's voice becomes its own sidechain - the
    // filtered signal the detector hears - handed to the output in
    // place of the programme. The compressor's plate wins when both
    // are lit: the later stage, the last one you reached for ----
    setParam ("compThreshold", 0.0f);
    setParam ("compHpf", 16.0f);
    setParam ("compListen", true);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.25) / flatRef1k), 0.0, 0.4,
                "the lit LISTEN plate hands the strip's flat sidechain to the output");

    setParam ("compHpf", 1600.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (100.0, 100.0, 0.25) / flatRef100), -48.0, 3.0,
                "the monitored sidechain carries its SC HPF's corner (a 2nd-order attenuation)");
    setParam ("compListen", false);

    setParam ("expListen", true);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.25) / flatRef1k), 0.0, 0.4,
                "the expander's LISTEN plate speaks the same language on its own stage");
    setParam ("expListen", false);
    setParam ("compHpf", 16.0f);

    // ---- the readouts' own parse plumbing: the knob speaks its format
    // and a typed value lands through the parameter's text functions ----
    {
        auto* ratioParam = dynamic_cast<juce::RangedAudioParameter*> (
            strip->getApvts().getParameter ("compRatio"));
        expectTrue (ratioParam != nullptr,
                    "the compressor ratio parameter is a ranged parameter");

        if (ratioParam != nullptr)
            expectNear (ratioParam->convertFrom0to1 (ratioParam->getValueForText ("8 : 1")), 8.0, 0.01,
                        "a typed 8 : 1 ratio parses to 8.0");

        auto* releaseParam = dynamic_cast<juce::RangedAudioParameter*> (
            strip->getApvts().getParameter ("compRelease"));
        expectTrue (releaseParam != nullptr,
                    "the compressor release parameter is a ranged parameter");

        if (releaseParam != nullptr)
            expectNear (releaseParam->convertFrom0to1 (releaseParam->getValueForText ("0.50 s")), 0.5, 0.001,
                        "a typed 0.50 s release parses to half a second");

        auto* expFastParam = dynamic_cast<juce::RangedAudioParameter*> (
            strip->getApvts().getParameter ("expFastAttack"));
        expectTrue (expFastParam != nullptr,
                    "the expander's FAST ATTACK plate is a ranged parameter");

        if (expFastParam != nullptr)
            expectNear (expFastParam->convertFrom0to1 (expFastParam->getValueForText ("fast")), 1.0, 0.001,
                        "a typed fast attack parses to the plate's lit position");

        auto* hpfParam = dynamic_cast<juce::RangedAudioParameter*> (
            strip->getApvts().getParameter ("compHpf"));
        expectTrue (hpfParam != nullptr,
                    "the sidechain HPF parameter is a ranged parameter");

        if (hpfParam != nullptr)
            expectNear (hpfParam->convertFrom0to1 (hpfParam->getValueForText ("400 Hz")), 400.0, 0.01,
                        "a typed 400 Hz sidechain corner parses to 400 Hz");

        auto* mixParam = dynamic_cast<juce::RangedAudioParameter*> (
            strip->getApvts().getParameter ("expMix"));
        expectTrue (mixParam != nullptr,
                    "the expander mix parameter is a ranged parameter");

        if (mixParam != nullptr)
            expectNear (mixParam->convertFrom0to1 (mixParam->getValueForText ("45 %")), 45.0, 0.01,
                        "a typed 45 % mix parses to 45");

        auto* thresholdParam = dynamic_cast<juce::RangedAudioParameter*> (
            strip->getApvts().getParameter ("expThreshold"));
        expectTrue (thresholdParam != nullptr,
                    "the expander threshold parameter is a ranged parameter");

        if (thresholdParam != nullptr)
            expectNear (thresholdParam->convertFrom0to1 (thresholdParam->getValueForText ("-12 dB")), -12.0, 0.01,
                        "a typed -12 dB threshold parses to -12 dB");
    }

    // ---- v2.69.0 (Clement's remark): the sidechain corners are
    // discrete cranks - a set value snaps to its nearest legal
    // corner, an equidistant tie lands on the gentler filter (the
    // lower HPF, the higher LPF), and the "off" rest positions parse
    // back and forth through the selector's own text ----
    setParam ("expScHpf", 500.0f);
    expectNear (strip->getApvts().getRawParameterValue ("expScHpf")->load(), 400.0, 0.01,
                "an expander SC HPF set between corners snaps to the nearest one (500 Hz -> 400 Hz, the gentler tie)");
    setParam ("expScHpf", 200.0f);
    expectNear (strip->getApvts().getRawParameterValue ("expScHpf")->load(), 200.0, 0.01,
                "a legal expander SC HPF corner (200 Hz) lands exactly on its own crank");
    setParam ("expScHpf", 16.0f);

    setParam ("expScLpf", 2500.0f);
    expectNear (strip->getApvts().getRawParameterValue ("expScLpf")->load(), 3200.0, 0.01,
                "an expander SC LPF set between corners snaps to the nearest one (2500 Hz -> 3200 Hz)");
    setParam ("expScLpf", 12000.0f);
    expectNear (strip->getApvts().getRawParameterValue ("expScLpf")->load(), 12800.0, 0.01,
                "a bright expander SC LPF corner snaps up to 12800 Hz");
    setParam ("expScLpf", 24000.0f);

    {
        auto* scHpfParam = dynamic_cast<juce::RangedAudioParameter*> (
            strip->getApvts().getParameter ("expScHpf"));
        expectTrue (scHpfParam != nullptr,
                    "the expander SC HPF parameter is a ranged parameter");

        if (scHpfParam != nullptr)
        {
            expectNear (scHpfParam->convertFrom0to1 (scHpfParam->getValueForText ("90 Hz")), 90.0, 0.01,
                        "a typed 90 Hz SC corner parses to its own legal crank");
            expectNear (scHpfParam->convertFrom0to1 (scHpfParam->getValueForText ("off")), 16.0, 0.01,
                        "a typed off parses to the flat-band rest position (16 Hz)");
        }

        auto* scLpfParam = dynamic_cast<juce::RangedAudioParameter*> (
            strip->getApvts().getParameter ("compScLpf"));
        expectTrue (scLpfParam != nullptr,
                    "the compressor SC LPF parameter is a ranged parameter");

        if (scLpfParam != nullptr)
            expectNear (scLpfParam->convertFrom0to1 (scLpfParam->getValueForText ("off")), 24000.0, 0.01,
                        "a typed off on the SC LPF parses to its own flat-band rest (24000 Hz)");
    }

    setParam ("compHpf", 1000.0f);
    expectNear (strip->getApvts().getRawParameterValue ("compHpf")->load(), 800.0, 0.01,
                "the compressor SC HPF tie at 1000 Hz lands on the gentler lower corner (800 Hz)");
    setParam ("compHpf", 16.0f);

    // ---- back to the parked defaults: threshold 0, ratio 2, mix 100,
    // the SC pair at rest (16 Hz / 24000 Hz) - the compressor is
    // skipped wholesale again ----
    setParam ("compThreshold", 0.0f);
    setParam ("compRatio", 2.0f);
    setParam ("compHpf", 16.0f);
    expectNear (juce::Decibels::gainToDecibels (measureAmplitude (1000.0, 1000.0, 0.25) / flatRef1k), 0.0, 0.2,
                "the parked compressor (threshold 0) is a true bypass");
}

void testCostesStripDynamicsMeters()
{
    beginTest ("v2.65.0 (grown in v2.66.0): the dynamics meters - the IN/OUT/GR ladders read the stages' truth, parked or working");

    juce::MidiBuffer noMidi;

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    proc.toggleCostesStrip();

    auto* strip = dynamic_cast<CostesStrip*> (proc.getHostedStrip().getActivePlugin());
    expectTrue (strip != nullptr, "the strip is hosted");

    if (strip == nullptr)
        return;

    // the meter readings settle with the same discipline as the audio
    // checks: 2.4 s of lock-in rides out the 0.3 s releases, and the
    // meters snapshot the state at the very last sample of the final
    // block - the sim pins each reading, ripple included
    const int lockinBlocks = 240;

    auto runTone = [&] (double frequencyHz, double amplitude)
    {
        SineSource src (frequencyHz, 48000.0, (float) amplitude);
        juce::AudioBuffer<float> block (2, 480);

        for (int i = 0; i < lockinBlocks; ++i)
        {
            src.fill (block);
            proc.processBlock (block, noMidi);
        }

        src.fill (block);
        proc.processBlock (block, noMidi);
    };

    auto setParam = [strip] (const char* id, juce::var value)
    {
        strip->getApvts().getParameterAsValue (id).setValue (value);
    };

    // ---- at rest: the IN ladders read the detector's floor (an
    // EMPTY ladder, never a lying 0 dB) and the GR ladders read unity
    {
        juce::AudioBuffer<float> silence (2, 480);
        silence.clear();
        proc.processBlock (silence, noMidi);

        expectNear ((double) strip->getExpInDb(),  -120.0, 0.01,
                    "a fresh strip's IN ladder reads the detector's floor, never a lying 0 dB");
        expectNear ((double) strip->getCompInDb(), -120.0, 0.01,
                    "the compressor's IN ladder starts empty too");
        expectNear ((double) strip->getExpGrDb(),    0.0, 0.01,
                    "a fresh strip's GR ladder reads unity");
        expectNear ((double) strip->getCompGrDb(),   0.0, 0.01,
                    "the compressor's GR ladder starts at unity");
        expectNear ((double) strip->getExpOutDb(),  -120.0, 0.01,
                    "a fresh strip's OUT ladder reads the floor, never a lying 0 dB");
        expectNear ((double) strip->getCompOutDb(), -120.0, 0.01,
                    "the compressor's OUT ladder starts empty too");
    }

    // ---- the PARKED defaults: the detectors run even while both
    // halves are skipped, so the IN ladders track the -15 dB RMS tone
    // while the GR ladders stay honestly at unity (the sim pins
    // -15.05 dB, the 10 Hz sidechain's ripple included)
    runTone (1000.0, 0.25);

    expectNear ((double) strip->getExpInDb(),  -15.05, 0.2,
                "the IN ladder tracks the RMS level even while the section is parked");
    expectNear ((double) strip->getCompInDb(), -15.05, 0.2,
                "the compressor's IN ladder hears the same level through its flat sidechain");
    expectNear ((double) strip->getExpGrDb(),    0.0, 0.01,
                "a parked expander reports no reduction");
    expectNear ((double) strip->getCompGrDb(),   0.0, 0.01,
                "a parked compressor reports no reduction");
    expectNear ((double) strip->getExpOutDb(),  -15.05, 0.2,
                "the OUT ladder tracks the level the parked stage leaves behind");
    expectNear ((double) strip->getCompOutDb(), -15.05, 0.2,
                "the compressor's OUT ladder hears the same untouched level");

    // ---- the expander working: a -29 dB RMS tone sits 9 dB under
    // the -20 dB threshold, the 2:1 expansion pulls 9 dB down, and the
    // COMPRESSOR's IN ladder hears the gated signal - the stages
    // cascade in the metering exactly as they do in the audio path
    setParam ("expThreshold", -20.0f);
    setParam ("expRange", 36.0f);
    setParam ("expRatio", 2.0f);
    runTone (1000.0, 0.05);

    expectNear ((double) strip->getExpInDb(), -29.03, 0.2,
                "the IN ladder reads the quiet tone's RMS level");
    expectNear ((double) strip->getExpGrDb(),  -9.0, 0.25,
                "the GR ladder reads the 2:1 expansion working on a signal 9 dB under the threshold");
    expectNear ((double) strip->getCompInDb(), -38.03, 0.25,
                "the compressor's IN ladder hears the GATED signal - the stages cascade");
    expectNear ((double) strip->getCompGrDb(), 0.0, 0.01,
                "the parked compressor still reports no reduction while it listens");
    expectNear ((double) strip->getExpOutDb(), -38.03, 0.25,
                "the expander's OUT ladder reads the level after its 9 dB gate");
    expectNear ((double) strip->getCompOutDb(), -38.03, 0.25,
                "the compressor's OUT ladder hears the same gated level (it is parked)");

    // ---- the expander's OFF plate: the audio bypasses the section
    // and the GR ladder snaps open with it - no stale reading
    setParam ("expOn", false);
    runTone (1000.0, 0.05);

    expectNear ((double) strip->getExpGrDb(), 0.0, 0.01,
                "the OFF plate snaps the GR ladder open - no stale reduction");
    setParam ("expOn", true);
    setParam ("expRange", 0.0f);

    // ---- the compressor working: 9.95 dB over a -25 dB threshold at
    // 2:1 is a 4.98 dB reduction (the sim pins -4.98, the detector's
    // 2f ripple included)
    setParam ("compThreshold", -25.0f);
    setParam ("compRatio", 2.0f);
    runTone (1000.0, 0.25);

    expectNear ((double) strip->getCompGrDb(), -4.98, 0.25,
                "the GR ladder reads the 2:1 reduction");
    expectNear ((double) strip->getCompInDb(), -15.05, 0.2,
                "the IN ladder still reads the level at the detector");
    expectNear ((double) strip->getExpOutDb(), -15.05, 0.25,
                "the parked expander's OUT ladder still reads the untouched tone");
    expectNear ((double) strip->getCompOutDb(), -20.03, 0.25,
                "the compressor's OUT ladder reads the level after its 4.98 dB reduction");

    // ---- the compressor's OFF plate: the same snap to unity
    setParam ("compOn", false);
    runTone (1000.0, 0.25);

    expectNear ((double) strip->getCompGrDb(), 0.0, 0.01,
                "the compressor's OFF plate snaps its GR ladder to unity");
    setParam ("compOn", true);

    // ---- back to the parked defaults ----
    setParam ("compThreshold", 0.0f);
    setParam ("compRatio", 2.0f);
}

void testCostesStripCompLinkAndGrTiers()
{
    beginTest ("v2.72.0: the compressor's LINK plate, the expander's 36 dB range, the mid bands' Width readout");

    juce::MidiBuffer noMidi;

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    proc.toggleCostesStrip();

    auto* strip = dynamic_cast<CostesStrip*> (proc.getHostedStrip().getActivePlugin());
    expectTrue (strip != nullptr, "the strip is hosted");

    if (strip == nullptr)
        return;

    // ---- the compLink parameter itself (Clement's request): four
    // positions - dual, 50% link, 80% link, link - parked at "link",
    // the v2.64.0 behaviour every existing session already knows ----
    expectNear (strip->getApvts().getRawParameterValue ("compLink")->load(), 3.0, 0.0001,
                "the compLink parameter parks at link (3) - the classic stereo-linked detector");

    auto* linkParam = dynamic_cast<juce::RangedAudioParameter*> (
        strip->getApvts().getParameter ("compLink"));
    expectTrue (linkParam != nullptr,
                "the compLink parameter is a ranged parameter");

    if (linkParam != nullptr)
    {
        expectNear (linkParam->convertFrom0to1 (linkParam->getValueForText ("80% link")), 2.0, 0.01,
                    "a typed 80% link parses to the third position");

        expectTrue (linkParam->getText (linkParam->getValue(), 100) == "link",
                    "the readout speaks the plate's own words - \"link\" at rest");
    }

    // ---- the expander's RANGE tops at 36 dB (Clement's request: the
    // GR ladder's three 12 dB tiers read 0-6, 6-18, 18-36) ----
    {
        auto* expRangeParam = dynamic_cast<juce::RangedAudioParameter*> (
            strip->getApvts().getParameter ("expRange"));

        expectTrue (expRangeParam != nullptr
                        && std::abs (expRangeParam->convertFrom0to1 (1.0f) - 36.0) < 0.01,
                    "the expander's RANGE knob tops at 36 dB now");
    }

    // ---- the mid bands' width readout (v2.79.0, Clement's remark 2):
    // the sweep moved into its band's margin column, so the readout is
    // the bare number and its unit, parsed as happily as ever ----
    {
        strip->getApvts().getParameterAsValue ("eq2Oct").setValue (1.0f);

        auto* eq2OctParam = dynamic_cast<juce::RangedAudioParameter*> (
            strip->getApvts().getParameter ("eq2Oct"));

        expectTrue (eq2OctParam != nullptr
                        && eq2OctParam->getText (eq2OctParam->getValue(), 100)
                               == "1.0 oct",
                    "the mid bands' width readout speaks its bare \"1.0 oct\"");

        if (eq2OctParam != nullptr)
            expectNear (eq2OctParam->convertFrom0to1 (
                            eq2OctParam->getValueForText ("Width : 1.4 octave")),
                        1.5, 0.01,
                        "the typed full sentence parses, then snaps to the nearest legal width (1.4 -> 1.5)");
    }

    // ---- the LINK plate's DSP (Clement's request): each channel
    // carries its own detector and gain, and the plate blends each
    // channel's detector level towards the stereo-linked one. The
    // proof rides a differential tone - the left channel loud (-15.05
    // dB RMS), the right quiet (-23.01 dB) - under a -25 dB threshold
    // at 4:1. The left channel rides the linked detector at every
    // position (it IS the louder one), the right one moves the whole
    // way from its own gentle -1.49 dB (dual) to the full linked
    // -7.46 dB (link), and the two partial plates land between ----
    {
        auto setParam = [strip] (const char* id, juce::var value)
        {
            strip->getApvts().getParameterAsValue (id).setValue (value);
        };

        setParam ("compThreshold", -25.0f);
        setParam ("compRatio", 4.0f);
        setParam ("compMix", 100.0f);
        setParam ("compGain", 0.0f);

        const double omega = juce::MathConstants<double>::twoPi * 1000.0 / 48000.0;

        // a settled differential tone, then the lock-in amplitude of
        // the block the strip leaves behind, per channel
        auto measureChannelDb = [&] (double amplitudeL, double amplitudeR,
                                     int channel) -> double
        {
            double phase = 0.0;
            juce::AudioBuffer<float> block (2, 480);

            for (int i = 0; i < 240; ++i)
            {
                for (int j = 0; j < 480; ++j)
                {
                    const double sample = std::sin (phase);
                    block.setSample (0, j, (float) (amplitudeL * sample));
                    block.setSample (1, j, (float) (amplitudeR * sample));
                    phase += omega;
                }

                proc.processBlock (block, noMidi);
            }

            const double phase0 = omega * (double) (240 * 480);
            double c = 0.0, s = 0.0;

            for (int j = 0; j < 480; ++j)
            {
                const double p = phase0 + omega * (double) j;
                const double sample = block.getSample (channel, j);
                c += sample * std::cos (p);
                s += sample * std::sin (p);
            }

            return 2.0 * std::sqrt (c * c + s * s) / 480.0;
        };

        const double inRight = 0.10;
        const double inLeft = 0.25;

        setParam ("compLink", 0.0f);   // dual
        const double rightDualDb = juce::Decibels::gainToDecibels (
            measureChannelDb (inLeft, inRight, 1) / inRight);
        const double leftDualDb = juce::Decibels::gainToDecibels (
            measureChannelDb (inLeft, inRight, 0) / inLeft);

        setParam ("compLink", 1.0f);   // 50% link
        const double rightHalfDb = juce::Decibels::gainToDecibels (
            measureChannelDb (inLeft, inRight, 1) / inRight);

        setParam ("compLink", 2.0f);   // 80% link
        const double rightEightyDb = juce::Decibels::gainToDecibels (
            measureChannelDb (inLeft, inRight, 1) / inRight);

        setParam ("compLink", 3.0f);   // link
        const double rightLinkDb = juce::Decibels::gainToDecibels (
            measureChannelDb (inLeft, inRight, 1) / inRight);
        const double leftLinkDb = juce::Decibels::gainToDecibels (
            measureChannelDb (inLeft, inRight, 0) / inLeft);

        expectNear (rightDualDb, -1.49, 0.6,
                    "at DUAL the quiet channel answers only for itself - its own gentle reduction");

        expectNear (rightHalfDb, -4.48, 0.6,
                    "at 50% LINK the quiet channel's detector sits halfway to the linked one");

        expectNear (rightEightyDb, -6.27, 0.6,
                    "at 80% LINK the quiet channel leans most of the way to the linked one");

        expectNear (rightLinkDb, -7.46, 0.6,
                    "at LINK the quiet channel rides the loud one's full reduction");

        expectNear (leftDualDb, -7.46, 0.6,
                    "the loud channel rides the linked detector at EVERY position - dual leaves it untouched");

        expectTrue (std::abs (leftLinkDb - leftDualDb) < 0.1,
                    "the loud channel's reduction never depends on the plate - it IS the linked detector");

        // and the GR meter reads the AVERAGE of the two channels'
        // reductions whatever the plate says: both at -7.46 at link,
        // half-depth (-4.48) at dual where only one side works
        setParam ("compLink", 3.0f);
        measureChannelDb (inLeft, inRight, 1);
        expectNear ((double) strip->getCompGrDb(), -7.46, 0.3,
                    "the GR meter reads the two identical reductions' average at LINK");

        setParam ("compLink", 0.0f);
        measureChannelDb (inLeft, inRight, 1);
        expectNear ((double) strip->getCompGrDb(), -4.48, 0.3,
                    "the GR meter reads the average at DUAL - half-depth, the mean of the two sides");
    }
}

void testCostesStripKneeAndLayoutTightening()
{
    beginTest ("v2.73/v2.74: the expander's KNEE plate, the compressor's 18 dB ceiling, the tightened dynamics layout");

    juce::MidiBuffer noMidi;

    TheChannelStripAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    proc.toggleCostesStrip();

    auto* strip = dynamic_cast<CostesStrip*> (proc.getHostedStrip().getActivePlugin());
    expectTrue (strip != nullptr, "the strip is hosted");

    if (strip == nullptr)
        return;

    // ---- the expander's KNEE parameter (Clement's choice, option B):
    // six even plates - "hard" at rest, the corner every session born
    // before v2.73 already knows, then 4 / 8 / 12 / 16 / 20 dB ----
    expectNear (strip->getApvts().getRawParameterValue ("expKnee")->load(), 0.0, 0.0001,
                "the knee parks at hard (0 dB) - the pre-2.73 corner, bit-exact");

    auto* kneeParam = dynamic_cast<juce::RangedAudioParameter*> (
        strip->getApvts().getParameter ("expKnee"));
    expectTrue (kneeParam != nullptr,
                "the expKnee parameter is a ranged parameter");

    if (kneeParam != nullptr)
    {
        expectNear (kneeParam->convertFrom0to1 (1.0f), 20.0, 0.01,
                    "the knee's last plate tops at 20 dB");

        expectTrue (kneeParam->getText (kneeParam->getValue(), 100) == "KNEE hard",
                    "the readout says its own name at rest - \"KNEE hard\"");

        expectNear (kneeParam->convertFrom0to1 (kneeParam->getValueForText ("hard")), 0.0, 0.01,
                    "a typed hard parses back to the parked corner");

        expectNear (kneeParam->convertFrom0to1 (kneeParam->getValueForText ("16 dB")), 16.0, 0.01,
                    "a typed 16 dB parses to its plate");

        strip->getApvts().getParameterAsValue ("expKnee").setValue (16.0f);
        expectTrue (kneeParam->getText (kneeParam->getValue(), 100) == "16 dB",
                    "the readout speaks the plate's number once it leaves hard");
        strip->getApvts().getParameterAsValue ("expKnee").setValue (0.0f);
    }

    // ---- the sidechain readouts identify themselves now (v2.79.0,
    // Clement's remark 4): the SC selector line lost its captions, so
    // each readout says its own name at rest - once you move one, its
    // own hertz speak for it ----
    {
        auto* scHpfParam = dynamic_cast<juce::RangedAudioParameter*> (
            strip->getApvts().getParameter ("compHpf"));
        auto* scLpfParam = dynamic_cast<juce::RangedAudioParameter*> (
            strip->getApvts().getParameter ("compScLpf"));

        expectTrue (scHpfParam != nullptr && scLpfParam != nullptr
                        && scHpfParam->getText (scHpfParam->getValue(), 100)
                               == "SC HPF off"
                        && scLpfParam->getText (scLpfParam->getValue(), 100)
                               == "SC LPF off",
                    "the sidechain readouts speak their own name at rest - \"SC HPF off\" and \"SC LPF off\"");

        if (scHpfParam != nullptr)
            expectNear (scHpfParam->convertFrom0to1 (
                            scHpfParam->getValueForText ("SC HPF off")),
                        16.0, 0.01,
                        "the named \"SC HPF off\" parses back to its own rest corner");
    }

    // ---- the compressor's GAIN rides the same 18 dB ceiling as its
    // GR ladder's new full scale (Clement's remark 4) ----
    {
        auto* compGainParam = dynamic_cast<juce::RangedAudioParameter*> (
            strip->getApvts().getParameter ("compGain"));

        expectTrue (compGainParam != nullptr
                        && std::abs (compGainParam->convertFrom0to1 (1.0f) - 18.0) < 0.01,
                    "the compressor's GAIN knob tops at 18 dB now");

        if (compGainParam != nullptr)
            expectNear (compGainParam->convertFrom0to1 (
                            compGainParam->getValueForText ("19 dB")),
                        18.0, 0.01,
                        "a typed 19 dB clamps to the new 18 dB ceiling");
    }

    // ---- the KNEE's DSP (Clement's choice): the static curve bends
    // through the threshold's neighbourhood instead of folding at it,
    // and never touches the deep floor. A 2:1 expansion under a -25 dB
    // threshold, the range parked wide open: 4 dB under the threshold
    // the softest knee's parabola reads -4.92 where the hard corner
    // reads -4.03; above the threshold the soft knee still speaks
    // 1.60 dB where the hard one is silent; and 18 dB under, past the
    // whole knee zone, both plates read the same floor ----
    {
        auto setParam = [strip] (const char* id, juce::var value)
        {
            strip->getApvts().getParameterAsValue (id).setValue (value);
        };

        setParam ("expThreshold", -25.0f);
        setParam ("expRatio", 2.0f);
        setParam ("expRange", 36.0f);
        setParam ("expMix", 100.0f);

        const int lockinBlocks = 240;

        auto measureAmplitude = [&] (double amplitude) -> double
        {
            SineSource src (1000.0, 48000.0, (float) amplitude);
            juce::AudioBuffer<float> block (2, 480);

            for (int i = 0; i < lockinBlocks; ++i)
            {
                src.fill (block);
                proc.processBlock (block, noMidi);
            }

            src.fill (block);
            proc.processBlock (block, noMidi);

            const double probeOmega = juce::MathConstants<double>::twoPi * 1000.0 / 48000.0;
            const double phase0 = probeOmega * (double) (lockinBlocks * 480);

            double c = 0.0, s = 0.0;

            for (int i = 0; i < 480; ++i)
            {
                const double phase = phase0 + probeOmega * (double) i;
                const double sample = block.getSample (0, i);
                c += sample * std::cos (phase);
                s += sample * std::sin (phase);
            }

            return 2.0 * std::sqrt (c * c + s * s) / 480.0;
        };

        // the tone 4 dB under the threshold (-29.03 dB RMS at 0.05):
        // the softest knee bends the corner instead of folding it
        setParam ("expKnee", 20.0f);
        expectNear (juce::Decibels::gainToDecibels (measureAmplitude (0.05) / 0.05), -4.92, 0.6,
                    "a 20 dB knee bends the corner - 4 dB under the threshold reads -4.92, not the hard -4.03");

        setParam ("expKnee", 0.0f);
        expectNear (juce::Decibels::gainToDecibels (measureAmplitude (0.05) / 0.05), -4.03, 0.5,
                    "the parked hard knee keeps the classic corner arithmetic");

        // the tone above the threshold (-23.01 dB RMS at 0.10): the
        // soft knee is already at work where the hard one is silent
        setParam ("expKnee", 20.0f);
        expectNear (juce::Decibels::gainToDecibels (measureAmplitude (0.10) / 0.10), -1.60, 0.5,
                    "the soft knee starts working before the threshold - 1.60 dB down above it");

        setParam ("expKnee", 0.0f);
        expectNear (juce::Decibels::gainToDecibels (measureAmplitude (0.10) / 0.10), 0.0, 0.3,
                    "the hard corner is silent above the threshold");

        // deep under the whole knee zone (-43.01 dB RMS at 0.01): the
        // knee hands the curve back to the hard branch untouched
        setParam ("expKnee", 20.0f);
        const double deepSoftDb = juce::Decibels::gainToDecibels (measureAmplitude (0.01) / 0.01);

        setParam ("expKnee", 0.0f);
        const double deepHardDb = juce::Decibels::gainToDecibels (measureAmplitude (0.01) / 0.01);

        expectNear (deepSoftDb, -18.0, 0.6,
                    "18 dB under the threshold the soft knee reads the same expansion as the hard one");

        expectNear (deepHardDb, -18.0, 0.6,
                    "the deep floor is the hard curve's own -18 dB");

        expectTrue (std::abs (deepSoftDb - deepHardDb) < 0.3,
                    "the knee never touches the deep floor");
    }

    // ---- the tightened layout (Clement's remarks 1, 2, 3 and 5), on
    // a fresh strip and editor so the pass is the user's own ----
    CostesStrip stripForLayout;
    stripForLayout.prepareToPlay (48000.0, 512);

    CostesStripEditor editor (stripForLayout);

    // the click probes below ask the editor who sits at a point - and
    // a bare editor built in the harness carries JUCE's default
    // INVISIBLE flag, so getComponentAt answers nullptr for every
    // point and every probe reads as "swallowed". Show it first: the
    // probes then read exactly the component the peer would hand a
    // real click to when the strip runs in a host
    editor.setVisible (true);

    const auto meters    = editor.getDynMeters();
    const auto plates    = editor.getDynPlateButtons();
    const auto cutPlates = editor.getCutPlateButtons();
    const auto sliders  = editor.getStripSliders();

    // v2.73.0 tightened the trio; v2.74.0 (Clement's remark 2) took
    // the IN ladder's numbers column down to the OUT ladder's own
    // width - the pair reads as one meter, its ONE engraved scale
    // reading between the two frames
    expectTrue (meters[0] != nullptr && meters[0]->getWidth() == 40
                    && meters[1] != nullptr && meters[1]->getWidth() == 40
                    && meters[2] != nullptr && meters[2]->getWidth() == 52,
                "the expander's meters tightened - IN and OUT both at 40 px, the GR at 52");

    expectTrue (meters[3] != nullptr && meters[3]->getWidth() == 40
                    && meters[4] != nullptr && meters[4]->getWidth() == 40
                    && meters[5] != nullptr && meters[5]->getWidth() == 52,
                "the compressor's meters tightened the same way");

    expectTrue (meters[0] != nullptr && meters[1] != nullptr && meters[2] != nullptr
                    && meters[3] != nullptr && meters[4] != nullptr && meters[5] != nullptr
                    && meters[1]->getBounds().getX() - meters[0]->getBounds().getRight() == 4
                    && meters[2]->getBounds().getX() - meters[1]->getBounds().getRight() == 4
                    && meters[4]->getBounds().getX() - meters[3]->getBounds().getRight() == 4
                    && meters[5]->getBounds().getX() - meters[4]->getBounds().getRight() == 4,
                "the three ladders of each stage sit one even 4 px seam apart - close enough to read as one meter");

    // v2.74.0 (Clement's remark 2): the frame-to-frame air reads EVEN
    // now - each engraved frame sits 3 px inside its meter and spans
    // 20 px, so the air between two frames is the component seam plus
    // six: the IN-to-OUT air equals the OUT-to-GR air, 24 px each
    expectTrue (meters[0] != nullptr && meters[1] != nullptr && meters[2] != nullptr
                    && meters[3] != nullptr && meters[4] != nullptr && meters[5] != nullptr
                    && meters[1]->getBounds().getX() - meters[0]->getBounds().getX() - 20
                        == meters[2]->getBounds().getX() - meters[1]->getBounds().getX() - 20
                    && meters[4]->getBounds().getX() - meters[3]->getBounds().getX() - 20
                        == meters[5]->getBounds().getX() - meters[4]->getBounds().getX() - 20
                    && meters[1]->getBounds().getX() - meters[0]->getBounds().getX() - 20 == 24,
                "the ladders' frames sit one even distance apart - the IN-to-OUT air equals the OUT-to-GR air, 24 px");

    // v2.74.0 (Clement's remark 3): the shared IN/OUT scale engraves
    // every TEN dB now, 0 down to the -70 floor (was every 20)
    expectTrue (meters[0] != nullptr && meters[3] != nullptr
                    && meters[0]->markStep == 10.0f
                    && meters[3]->markStep == 10.0f,
                "the IN ladders engrave every ten dB - 0 down to the -70 floor");

    // v2.73.0 (Clement's remark 2): FAST ATK and LISTEN stack as one
    // column - v2.74.0 (Clement's remark 1) moved the pair UP a row,
    // into the middle row's third cell, centred as one plate unit
    expectTrue (plates[0] != nullptr && plates[1] != nullptr
                    && plates[1]->getBounds().getY() - plates[0]->getBounds().getBottom() >= 0
                    && plates[1]->getBounds().getY() - plates[0]->getBounds().getBottom() <= 2,
                "the expander's FAST ATK and LISTEN plates stack with barely a seam between them");

    expectTrue (plates[2] != nullptr && plates[3] != nullptr
                    && plates[3]->getBounds().getY() - plates[2]->getBounds().getBottom() >= 0
                    && plates[3]->getBounds().getY() - plates[2]->getBounds().getBottom() <= 2,
                "the compressor's FAST ATK and LISTEN plates stack the same way");

    // v2.74.0 (Clement's remark 1): with FAST ATK and LISTEN moved up
    // into the middle row, the LINK and KNEE selectors took the SC
    // row's third cell - sitting EXACTLY level with the SC LPF slider
    // at their left, wearing the very same 76 x 36 selector geometry
    expectTrue (sliders[31] != nullptr && sliders[28] != nullptr && plates[3] != nullptr
                    && sliders[31]->getBounds().getY() == sliders[28]->getBounds().getY()
                    && sliders[31]->getBounds().getX() > sliders[28]->getBounds().getX()
                    && sliders[31]->getWidth() == sliders[28]->getWidth()
                    && sliders[31]->getHeight() == sliders[28]->getHeight()
                    && plates[3]->getBounds().getBottom() <= sliders[31]->getBounds().getY(),
                "the compressor's LINK slider sits exactly level with its SC LPF, LISTEN clear above it");

    expectTrue (sliders[32] != nullptr && sliders[30] != nullptr && plates[1] != nullptr
                    && sliders[32]->getBounds().getY() == sliders[30]->getBounds().getY()
                    && sliders[32]->getBounds().getX() > sliders[30]->getBounds().getX()
                    && sliders[32]->getWidth() == sliders[30]->getWidth()
                    && sliders[32]->getHeight() == sliders[30]->getHeight()
                    && plates[1]->getBounds().getBottom() <= sliders[32]->getBounds().getY(),
                "the expander's KNEE slider sits exactly level with its SC LPF, LISTEN clear above it");

    // and both take their own clicks on their track - the v2.72 LINK
    // bug was a section label covering the slider and swallowing the
    // mouse; nothing may sit above either selector now (the probe is
    // the track's own centre, the half above the value box)
    if (sliders[31] != nullptr && sliders[32] != nullptr)
    {
        expectTrue (editor.getComponentAt (
                        sliders[31]->getBounds().removeFromTop (18).getCentre()) == sliders[31],
                    "the LINK slider takes its own clicks on its track - nothing covers it (the v2.72 fix)");

        expectTrue (editor.getComponentAt (
                        sliders[32]->getBounds().removeFromTop (18).getCentre()) == sliders[32],
                    "the KNEE slider takes its own clicks on its track too");
    }

    // remark 3 + v2.78.0 (Clement's remark 8): the cut ON/OFF plates
    // moved to their knob's TOP-RIGHT corner - the plate rides the
    // right edge of its knob, flush with its cell, and sits ABOVE the
    // disc so the corner's clicks switch the cut instead of dragging
    // the knob behind it
    expectTrue (cutPlates[0] != nullptr && sliders[1] != nullptr
                    && cutPlates[0]->getBounds().getRight() == sliders[1]->getBounds().getRight()
                    && cutPlates[0]->getBounds().getBottom() <= sliders[1]->getBounds().getY(),
                "the HPF's ON/OFF plate rides its knob's top-right corner - above the disc, flush with its cell");

    expectTrue (cutPlates[1] != nullptr && sliders[2] != nullptr
                    && cutPlates[1]->getBounds().getRight() == sliders[2]->getBounds().getRight()
                    && cutPlates[1]->getBounds().getBottom() <= sliders[2]->getBounds().getY(),
                "the LPF's ON/OFF plate rides its knob's top-right corner too");

    if (cutPlates[0] != nullptr && cutPlates[1] != nullptr)
    {
        expectTrue (editor.getComponentAt (cutPlates[0]->getBounds().getCentre()) == cutPlates[0],
                    "the HPF's ON/OFF plate takes its own clicks - it sits above its knob, not under it");

        expectTrue (editor.getComponentAt (cutPlates[1]->getBounds().getCentre()) == cutPlates[1],
                    "the LPF's ON/OFF plate takes its own clicks too");
    }

    // v2.79.0 (Clement's remark 3): the cut glyphs ride UNDER their
    // captions now - the words above, the curve at the knob square's
    // empty top-left corner, the corner the rotary disc never reaches
    {
        const auto icons = editor.getSectionIcons();
        expectTrue (icons[(size_t) 1] != nullptr && sliders[1] != nullptr
                        && icons[(size_t) 1]->getBounds().getX()
                            == sliders[1]->getBounds().getX() + 2
                        && icons[(size_t) 1]->getBounds().getY()
                            == sliders[1]->getBounds().getY() + 2,
                    "the HPF's glyph stands right under its caption, at its knob square's top-left corner");

        expectTrue (icons[(size_t) 2] != nullptr && sliders[2] != nullptr
                        && icons[(size_t) 2]->getBounds().getX()
                            == sliders[2]->getBounds().getX() + 2
                        && icons[(size_t) 2]->getBounds().getY()
                            == sliders[2]->getBounds().getY() + 2,
                    "the LPF's glyph stands right under its caption too");
    }

    // v2.79.0 (Clement's remark 5): the dynamics ON plates own the
    // room their own widest words need - "COMP OFF" at 11 px bold never
    // clips into "COMP O..." again (the LED's 24 plus the margins' 2
    // stay inside the plate's own 88)
    {
        const auto onButtons = editor.getDynOnButtons();
        expectTrue (onButtons[(size_t) 1] != nullptr
                        && onButtons[(size_t) 1]->getWidth() == 88
                        && juce::Font (juce::FontOptions (11.0f, juce::Font::bold))
                               .getStringWidth ("COMP OFF") + 26 <= 88,
                    "the COMP plate owns its own widest words - its OFF never clips");
    }
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

    std::cout << "TheChannelStrip offline test harness - v" TCS_VERSION << std::endl;
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
    testEqVeriteCurve();
    testConsoleSlotStatus();
    testPresetManager();
    testCurrentPresetTracking();
    testSilentConsoleWarning();
    testHoverReadout();
    testStateRoundtrip();
    testAnalyserStripButtons();
    testAnalyserZoomButtons();
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
    testTortureAdversarialAudio();
    testTortureStateCornerCases();
    testTortureDisplayStress();

    // v2.51.1 hotfix (the Studio Pro crash): the guarded instantiation
    testSupersededInstantiationGuard();
    testDuplicateRestoreSkip();
    testRelayThreadSafety();

    // v2.55.0 (roadmap C1): the internal Costes strip
    testCostesStripBasics();
    testCostesStripAudio();
    testCostesStripSession();
    testCostesStripMigration();
    testCostesStripSessionDeep();
    testHostDirtyNotification();
    testCostesStripSectionIcons();
    testCostesStripKnobDefaultsAndCaps();
    testCostesStripEq();
    testCostesStripDynamics();
    testCostesStripDynamicsMeters();
    testCostesStripCompLinkAndGrTiers();
    testCostesStripKneeAndLayoutTightening();

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
