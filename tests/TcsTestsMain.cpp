// ---------------------------------------------------------------------------
// tests/TcsTestsMain.cpp - v2.22.0 offline test harness (roadmap point 8)
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
//   - hover readout mapping and formatting (v2.19, roadmap point 3)
//   - state save/restore roundtrip (display options + version tag)
//   - latency forwarding, static AND runtime changes (the v2.16.1 chain)
//   - click-free console crossfade (bounded slew + convergence)
//   - native mono negotiation vs stereo-bridge fallback (the v2.14
//     machinery, driven by a mock that accepts / refuses the mono layout)
//   - stereo <-> mono hot switching without re-creating the instance
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
#include <functional>
#include <iostream>
#include <memory>
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
    expectTrue (xml != nullptr && xml->getStringAttribute ("version") == "2.22.0",
                "saved state carries the current version tag");

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

    std::cout << "TheChannelStrip offline test harness - v2.22.0" << std::endl;
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
    testHoverReadout();
    testStateRoundtrip();
    testLatencyForwarding();
    testCrossfadeClickFree();
    testMonoNative();
    testMonoBridge();
    testFormatHotSwitch();

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
