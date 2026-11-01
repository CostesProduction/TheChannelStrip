// ---------------------------------------------------------------------------
// src/CostesStrip.h - v2.56.0 (roadmap C2)
//
// The shell's OWN channel strip: a small juce::AudioProcessor compiled INTO
// the plugin and hosted on the very same HostedStrip as the third-party
// consoles (TheChannelStripAudioProcessor::selectCostesStrip). Signal path:
// trim in -> 12 dB/oct low cut -> 12 dB/oct high cut -> tilt (two mirrored
// shelves around a 1 kHz pivot, +/- 12 dB) -> drive (tanh saturation with an
// invisible auto-makeup calibrated on a -12 dBFS sine) -> optional 2x
// oversampling around the drive stage. Because it rides the exact same
// hosting path as the consoles, it inherits the click-free 50 ms crossfade,
// the per-ident settings memory, the session save/restore and the parameter
// relay without a single extra wire.
//
// The cut corners and the tilt are plain RBJ-style biquads (the shelves use
// juce::dsp::ArrayCoefficients, so JUCE itself owns the shelf formulas), the
// oversampler is juce::dsp::Oversampling's polyphase IIR halfband. The
// polyphase IIR latency is a fraction of a sample, which a plug-in cannot
// report: the strip stays at zero reported latency (and the harness asserts
// it).
//
// Roadmap: C3 will add the compressor, C4 a transient designer.
// ---------------------------------------------------------------------------

#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>

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

    // The editor (and the offline harness) reach the six parameters
    // through this - everything else is private DSP plumbing.
    juce::AudioProcessorValueTreeState& getApvts() { return apvts; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // One biquad section per channel per stage: direct form I, plain
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

        // adopts a { b0, b1, b2, a0, a1, a2 } coefficient array from
        // juce::dsp::ArrayCoefficients and normalises it by a0
        void setShelfCoefficients (const std::array<float, 6>& c);

        float processSample (float input);
    };

    void refreshCutCoefficients();
    void refreshTiltCoefficients();

    juce::AudioProcessorValueTreeState apvts;

    // raw pointers into the APVTS parameters (lock-free reads)
    std::atomic<float>* trimInDb          = nullptr;
    std::atomic<float>* lowCutHz          = nullptr;
    std::atomic<float>* highCutHz         = nullptr;
    std::atomic<float>* tiltDb            = nullptr;
    std::atomic<float>* driveAmount       = nullptr;
    std::atomic<float>* driveOversampled  = nullptr; // 0.0 / 1.0

    double currentSampleRate = 48000.0;

    // the trim moves as a smoothed linear gain (20 ms); the cut corners
    // and the tilt move per block - a knob turn is already a slow ramp,
    // and each block only steps the response a fraction of a dB
    juce::SmoothedValue<float> trimInGain;

    Biquad lowCut[2];
    Biquad highCut[2];
    Biquad tiltLow[2];
    Biquad tiltHigh[2];

    float lastLowCutHz  = -1.0f;
    float lastHighCutHz = -1.0f;
    float lastTiltDb    = -999.0f;

    // 2x oversampling for the drive stage: polyphase IIR halfband, built
    // in prepareToPlay (allocation stays off the audio thread) and fed
    // sub-blocks no longer than the prepared maximum
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int preparedMaxSamples = 512;
};

// ---------------------------------------------------------------------------
// v2.56.0: the cream SSL-style mini panel - five knobs in a row (TRIM IN,
// LOW CUT, HIGH CUT, TILT, DRIVE) grouped under engraved section captions
// (TRIM / FILTERS / TONE / DRIVE), an "OS 2x" toggle under the drive knob
// and the COSTES CHANNEL STRIP name plate. The knobs are built for the
// mouse: horizontal-or-vertical drag, a wide 320 px travel, double-click
// back to default, an editable readout under each cap. The shell embeds
// the panel through the standard hosted-editor window, so nothing here
// knows anything about the shell: it is a plain plugin editor.
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

    juce::Slider trimInKnob, lowCutKnob, highCutKnob, tiltKnob, driveKnob;
    juce::Label trimInLabel, lowCutLabel, highCutLabel, tiltLabel, driveLabel;
    juce::ToggleButton oversampleButton;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> trimInAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tiltAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> oversampleAttachment;

    // SSL-style knobs: cream bezel, dark disc, white pointer line, tick
    // marks around the cap and a warm value arc that shows the position
    struct StripLookAndFeel : public juce::LookAndFeel_V4
    {
        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                               float sliderPos, float rotaryStartAngle,
                               float rotaryEndAngle, juce::Slider&) override;
    };

    StripLookAndFeel look;
};
