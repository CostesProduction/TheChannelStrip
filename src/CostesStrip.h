// ---------------------------------------------------------------------------
// src/CostesStrip.h - v2.59.0 (roadmap C2)
//
// The shell's OWN channel strip: a small juce::AudioProcessor compiled INTO
// the plugin and hosted on the very same HostedStrip as the third-party
// consoles (TheChannelStripAudioProcessor::selectCostesStrip). Signal path:
// trim in -> variable-order Butterworth low cut -> variable-order Butterworth
// high cut -> tilt -> drive (tanh saturation with an invisible auto-makeup
// calibrated on a -12 dBFS sine) -> optional 2x oversampling around the drive
// stage. Because it rides the exact same hosting path as the consoles, it
// inherits the click-free 50 ms crossfade, the per-ident settings memory,
// the session save/restore and the parameter relay without a single extra
// wire.
//
// v2.59.0 (Clement's remarks): each cut owns an SSL-style on/off plate
// beside its readouts - the switch is the bypass now, so the slope slider
// holds only the seven legal Butterworth slopes 6 / 12 / 18 / 24 / 36 /
// 48 / 72 dB/oct (orders 1, 2, 3, 4, 6, 8, 12 - nothing in between, a
// snapping range on the parameter itself). The trim in widened to
// -24...+12 dB, the readout boxes grew to keep their descenders inside
// the panel, and sessions saved by v2.58.x migrate on load (an old
// "off" slope becomes a switched-off plate at its default 12 dB/oct).
//
// v2.58.0 (Clement's remarks): the cuts are full [16, 1600] Hz and
// [240, 24000] Hz logarithmic sweeps with a selectable Butterworth order -
// a horizontal slope slider from "off" (no filtering at all) to 16
// (96 dB/oct, eight cascaded second-order sections) - the trim reads
// -20 to +10 dB, and every knob displays and accepts 0.1 dB / whole-Hz /
// 0.01 kHz precision through the parameter's own text functions (the
// slider attachments wire them straight into the editable readouts). The
// drive knob moved to decibels of input gain: "driveDb", 0 (clean) to
// +15.6 dB (the full 6x input gain of the tanh), so the old "driveAmount"
// sessions simply park at 0 dB.
//
// v2.57.0 (Clement's remark 1): the tilt is now a FULL band tilt - a
// straight line of adjustable slope in the log(f)/dB plane, the FabFilter
// style - instead of the two mirrored shelves. The single knob is the
// +/- 12 dB gained or lost BETWEEN 20 Hz AND 20 kHz; the line pivots at
// 632.4555 Hz, the geometric mean of the band edges, so an octave-uniform
// (pink-like) spectrum - most music - keeps its loudness to within a
// quarter dB even at full tilt. One biquad cannot draw that line (any
// shelf flattens outside its corner), so it is built as a cascade of ten
// octave-spaced high shelves smoothed by their own transitions (see
// refreshTiltCoefficients).
//
// The cut corners and the tilt shelves are plain RBJ-style biquads (the
// shelves use juce::dsp::ArrayCoefficients, so JUCE itself owns the shelf
// formulas), the oversampler is juce::dsp::Oversampling's polyphase IIR
// halfband. The polyphase IIR latency is a fraction of a sample, which a
// plug-in cannot report: the strip stays at zero reported latency (and
// the harness asserts it).
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

    // The editor (and the offline harness) reach the eight parameters
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

    // the full-band tilt: ten octave-spaced high shelves draw the straight
    // log(f)/dB line (v2.57.0). The knob's +/- 12 dB is the gain difference
    // between the 20 Hz and 20 kHz band edges - 1.2 dB per octave at full -
    // and the line pivots at the geometric band centre: an octave-uniform
    // spectrum keeps its loudness there, whatever the slope
    static constexpr int numTiltShelves = 10;
    static constexpr double tiltPivotHz = 632.4555320336759;   // sqrt (20 * 20000)

    // the cuts: a Butterworth order from 0 ("off") to 16 is up to eight
    // cascaded second-order sections plus an optional first-order section
    // (v2.58.0)
    static constexpr int maxCutSections = 8;

    juce::AudioProcessorValueTreeState apvts;

    // raw pointers into the APVTS parameters (lock-free reads)
    std::atomic<float>* trimInDb          = nullptr;
    std::atomic<float>* lowCutHz          = nullptr;
    std::atomic<float>* lowCutOrder       = nullptr;
    std::atomic<float>* lowCutOn          = nullptr; // 0.0 / 1.0 (v2.59.0)
    std::atomic<float>* highCutHz         = nullptr;
    std::atomic<float>* highCutOrder      = nullptr;
    std::atomic<float>* highCutOn         = nullptr; // 0.0 / 1.0 (v2.59.0)
    std::atomic<float>* tiltDb            = nullptr;
    std::atomic<float>* driveDb           = nullptr;
    std::atomic<float>* driveOversampled  = nullptr; // 0.0 / 1.0

    double currentSampleRate = 48000.0;

    // the trim moves as a smoothed linear gain (20 ms); the cut corners
    // and the tilt move per block - a knob turn is already a slow ramp,
    // and each block only steps the response a fraction of a dB
    juce::SmoothedValue<float> trimInGain;

    // the cuts: [section][channel], the first (order + 1) / 2 sections
    // active (an odd order's first section is the first-order stage)
    Biquad lowCut[maxCutSections][2];
    Biquad highCut[maxCutSections][2];
    int activeLowCutSections  = 0;
    int activeHighCutSections = 0;

    // the tilt cascade: one shelf per octave around the pivot (indexed
    // [shelf][channel]) - see numTiltShelves above
    Biquad tiltShelves[numTiltShelves][2];

    float lastLowCutHz   = -1.0f;
    float lastHighCutHz  = -1.0f;
    int   lastLowCutOrder  = -1;
    int   lastHighCutOrder = -1;
    bool  lastLowCutOn   = true;   // v2.59.0: the on/off plates ride the cache
    bool  lastHighCutOn  = true;
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
// (TRIM / FILTERS / TONE / DRIVE), an "OS 2x" switch under the drive knob
// and the COSTES CHANNEL STRIP name plate. The knobs are built for the
// mouse: horizontal-or-vertical drag, a wide 320 px travel, double-click
// back to default, an editable readout under each cap. The shell embeds
// the panel through the standard hosted-editor window, so nothing here
// knows anything about the shell: it is a plain plugin editor.
//
// v2.57.0 (Clement's remark 2): the OS 2x switch paints itself as an
// illuminated SSL-style plate - dim and unlit with a dark LED when off,
// a glowing amber plate and a lit LED when on - so its state reads at a
// glance instead of hiding in a default checkbox tick.
//
// v2.58.0 (Clement's remarks): under each cut frequency knob sits a
// horizontal slope slider - "off" at the far left, then the Butterworth
// order stepped as 6 to 96 dB/oct - and every readout formats itself
// through its parameter's own text functions (integer Hz, 0.01 kHz,
// 0.1 dB), typed values included.
//
// v2.59.0 (Clement's remarks 1-3): the slope sliders keep only the seven
// legal slopes (6 to 72 dB/oct, snapped on the parameter), the bypass
// moved to the new CutOnToggle plate on the left of each cut's readout
// band, and the readout boxes grew (76 x 18 for the knobs, 72 x 16 for
// the slopes) so no descender clips at the panel's edge again.
//
// v2.60.0 (Clement's remarks): every knob caption carries a clean vector
// glyph on its left - a fader for the trim, the rising high-pass response
// for the low cut, the falling low-pass response for the high cut, a
// tilted line for the tilt and the soft saturation S-curve for the drive -
// drawn in the engraved silkscreen colour so the panel reads at a glance.
// The slope selectors are equally spaced across their seven legal
// settings (the range maps the orders to seven equal knob positions), and
// the slope readout box grew to the knobs' 76 x 18 so "24 dB/oct" no
// longer clips at its bottom edge.
// ---------------------------------------------------------------------------
class CostesStripEditor : public juce::AudioProcessorEditor
{
public:
    explicit CostesStripEditor (CostesStrip& ownerProcessor);
    ~CostesStripEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // v2.60.0 (Clement's remark): one clean vector glyph per knob, painted
    // to the left of its caption in the engraved silkscreen colour - the
    // five shapes read at a glance without any text. Public so the offline
    // test harness can name the types and count the instances
    struct SectionIcon : public juce::Component
    {
        enum class Icon { trim, lowCut, highCut, tilt, drive };

        explicit SectionIcon (Icon iconType) : type (iconType) {}

        void paint (juce::Graphics& g) override;

        Icon type;
    };

    // the icons in column order (trim, low cut, high cut, tilt, drive),
    // for the offline test harness
    std::array<SectionIcon*, 5> getSectionIcons()
    {
        return { &trimIcon, &lowCutIcon, &highCutIcon, &tiltIcon, &driveIcon };
    }

private:
    CostesStrip& owner;

    juce::Slider trimInKnob, lowCutKnob, highCutKnob, tiltKnob, driveKnob;
    juce::Slider lowCutSlope, highCutSlope;
    juce::Label trimInLabel, lowCutLabel, highCutLabel, tiltLabel, driveLabel;

    SectionIcon trimIcon   { SectionIcon::Icon::trim };
    SectionIcon lowCutIcon { SectionIcon::Icon::lowCut };
    SectionIcon highCutIcon{ SectionIcon::Icon::highCut };
    SectionIcon tiltIcon   { SectionIcon::Icon::tilt };
    SectionIcon driveIcon  { SectionIcon::Icon::drive };

    // the oversampling switch: a ToggleButton that paints its own plate
    // (lit amber + LED when on, dark + dim LED when off) - the default
    // checkbox tick was too quiet to read (v2.57.0, Clement's remark 2)
    struct OversampleToggle : public juce::ToggleButton
    {
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    };

    OversampleToggle oversampleButton;

    // the cut on/off plates (v2.59.0, Clement's remark 2): one ToggleButton
    // per cut, painted like the oversampling switch but vertical - a
    // horizontal bar with an LED at the top and an ON/OFF caption below.
    // It replaces "off" in the slope slider, which now starts at 6 dB/oct.
    struct CutOnToggle : public juce::ToggleButton
    {
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    };

    CutOnToggle lowCutOnButton, highCutOnButton;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> trimInAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCutSlopeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highCutSlopeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tiltAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> oversampleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lowCutOnAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> highCutOnAttachment;

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
