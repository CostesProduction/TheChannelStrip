// ---------------------------------------------------------------------------
// src/CostesStrip.h - v2.55.0 (roadmap C1)
//
// The shell's OWN channel strip: a small juce::AudioProcessor compiled INTO
// the plugin and hosted on the very same HostedStrip as the third-party
// consoles (TheChannelStripAudioProcessor::selectCostesStrip). MVP signal
// path: trim in -> 12 dB/oct low cut -> 12 dB/oct high cut -> trim out, two
// RBJ biquads per channel with per-block coefficient refresh and per-sample
// smoothed trims. Because it rides the exact same hosting path as the
// consoles, it inherits the click-free 50 ms crossfade, the per-ident
// settings memory, the session save/restore and the parameter relay without
// a single extra wire.
//
// The editor (CostesStripEditor) is the cream SSL-style mini panel embedded
// in the shell's window through the usual hosted-editor slot.
//
// Roadmap: C2 will add compression and saturation, C3 a transient designer.
// ---------------------------------------------------------------------------

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class CostesStripEditor;

class CostesStrip : public juce::AudioProcessor
{
public:
    CostesStrip();

    // The shell-side ident, in the same "Format|Path|Name" shape as a
    // scanned console's. No file backs it - the strip lives in the binary,
    // so it never appears in the KnownPluginList and the shell hosts it
    // directly instead of instantiating it from a description. The Name
    // part must match getName() (the preset loader's "hosted right now"
    // path compares the two).
    static juce::String shellIdent() { return "Internal|Costes|Costes Strip"; }

    const juce::String getName() const override { return "Costes Strip"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return juce::String(); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // The editor (and the offline harness) reach the four parameters
    // through this - everything else is private DSP plumbing.
    juce::AudioProcessorValueTreeState& getApvts() { return apvts; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // One RBJ biquad section per channel per cut: direct form I, plain
    // float state, coefficients swapped in one shot. No allocation ever
    // happens in processBlock (the shell's realtime-safety discipline
    // applies to the internal strip as much as to the shell itself).
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

        void reset() { x1 = x2 = y1 = y2 = 0.0f; }

        void setCoefficients (float newB0, float newB1, float newB2,
                              float newA1, float newA2)
        {
            b0 = newB0; b1 = newB1; b2 = newB2; a1 = newA1; a2 = newA2;
        }

        float processSample (float input);
    };

    void refreshCutCoefficients();

    juce::AudioProcessorValueTreeState apvts;

    // raw pointers into the APVTS parameters (lock-free reads)
    std::atomic<float>* trimInDb  = nullptr;
    std::atomic<float>* lowCutHz  = nullptr;
    std::atomic<float>* highCutHz = nullptr;
    std::atomic<float>* trimOutDb = nullptr;

    double currentSampleRate = 48000.0;

    // the trims move as smoothed linear gains (20 ms); the cut corners
    // move per block - a knob turn is already a slow ramp, and each
    // block only steps the response a fraction of a dB
    juce::SmoothedValue<float> trimInGain;
    juce::SmoothedValue<float> trimOutGain;

    Biquad lowCut[2];
    Biquad highCut[2];

    float lastLowCutHz  = -1.0f;
    float lastHighCutHz = -1.0f;
};

// ---------------------------------------------------------------------------
// v2.55.0: the cream SSL-style mini panel - four knobs in a row (TRIM IN,
// LOW CUT, HIGH CUT, TRIM OUT) under the COSTES CHANNEL STRIP name plate.
// The shell embeds it through the standard hosted-editor window, so nothing
// here knows anything about the shell: it is a plain plugin editor.
// ---------------------------------------------------------------------------
class CostesStripEditor : public juce::AudioProcessorEditor
{
public:
    explicit CostesStripEditor (CostesStrip& ownerProcessor);
    ~CostesStripEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    CostesStrip& owner;

    juce::Slider trimInKnob, lowCutKnob, highCutKnob, trimOutKnob;
    juce::Label trimInLabel, lowCutLabel, highCutLabel, trimOutLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> trimInAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> trimOutAttachment;

    // SSL-style knobs: cream bezel, dark disc, white pointer line
    struct StripLookAndFeel : public juce::LookAndFeel_V4
    {
        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                               float sliderPos, float rotaryStartAngle,
                               float rotaryEndAngle, juce::Slider&) override;
    };

    StripLookAndFeel look;
};
