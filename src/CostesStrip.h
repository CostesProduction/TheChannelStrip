// ---------------------------------------------------------------------------
// src/CostesStrip.h - v2.65.0 (Clement's photos of a real SSL 9000 J)
//
// The shell's OWN channel strip: a small juce::AudioProcessor compiled INTO
// the plugin and hosted on the very same HostedStrip as the third-party
// consoles (TheChannelStripAudioProcessor::selectCostesStrip). Signal path:
// trim in -> variable-order Butterworth low cut -> variable-order Butterworth
// high cut -> the four-band EQ -> the expander -> the compressor -> tilt ->
// drive (tanh saturation with an invisible auto-makeup calibrated on a
// -12 dBFS sine) -> optional 2x oversampling around the drive stage. Because
// it rides the exact same hosting path as the consoles, it inherits the
// click-free 50 ms crossfade, the per-ident settings memory, the session
// save/restore and the parameter relay without a single extra wire.
//
// v2.65.0 (Clement's request, from his photos of the SSL J's dynamics
// section): the dynamics grow from one cramped thirteen-column row into
// TWO STAGES - the EXPANDER on its own row, the COMPRESSOR on its own -
// separated by an engraved divider like the J's own silkscreen, and each
// stage gets the SSL's two meters: an IN ladder at the LEFT (the
// stereo-linked RMS level the detector hears, scaled -70 to 0 dB, sitting
// right beside the THRESHOLD knob so the setting reads naturally) and a
// GR ladder at the RIGHT (0 dB at the top, growing down as the section
// works: 40 dB of span for the expander, 20 for the compressor, the
// classic orange). The meters ride four new atomics the process loop
// stores once per block: the detectors now ALWAYS run - metering never
// waits for a section to be engaged - while the gain arithmetic stays
// gated behind the activity flags, so a parked strip still processes
// sample-for-sample exactly as v2.63 did and the meters still tell the
// truth. A 30 Hz timer repaints the ladders; the panel grows to 1040x800.
//
// v2.64.0 (Clement's spec, roadmap C3): the DYNAMICS section - a
// gate/expander and a classic compressor between the EQ and the tilt,
// exactly where an SSL channel puts them. The expander: threshold
// (-70 to 0 dB), range (0 to 40 dB, the maximum attenuation - 0 dB is a
// true bypass), attack (0.5 to 100 ms, added: a gate needs its opening
// speed for click-free transients), release (0.1 to 3 s), ratio (1.2 to 12)
// and a mix knob (0 to 100 % wet). The compressor: threshold (-60 to 0 dB),
// ratio (1 to 20), release (0.05 to 1.5 s), a FAST ATTACK plate (3 ms lit /
// 30 ms dark), gain (0 to +20 dB of make-up), a mix knob and a sidechain
// HPF (10 to 1000 Hz) that filters only what the detector hears. Both
// halves sit behind their own on/off plates (the section language of the
// cuts and the EQ), both are stereo-linked (one detector for L and R, so
// the image never shifts), and both detectors are a 10 ms true-RMS power
// ballistics - the dbx-style smoothing that keeps low tones from pumping
// the gain at twice their frequency. At the DEFAULTS (range 0, threshold
// 0, ratio 2, mix 100) both halves are skipped wholesale: the strip is
// bit-for-bit the strip of v2.63. Sessions saved by earlier versions find
// no dynamics children and load those neutral defaults.
//
// v2.63.0 (Clement's remarks 1-2-4): the seven legal EQ widths are now
// 0.4 / 0.7 / 1.0 / 1.5 / 2.0 / 3.0 / 4.0 octaves (still seven equal
// knob positions; a stale v2.62 width snaps to the nearest legal one,
// ties to the narrower), and a COMMAND-CLICK on any knob or slider
// parks it back on its default - the same default as double-click,
// now wired into the four linear selectors too. The HIGH CUT knob
// joins the EQ's HF band on the pastel red cap.
//
// v2.62.0 (Clement's remarks 2-5): the mid bells' bandwidth sweeps now
// run 0.1 to 3.0 octaves and hold exactly seven legal widths (0.1, 0.3,
// 0.5, 1.0, 1.5, 2.0, 3.0 oct) at seven EQUAL knob positions, like the
// cut slopes - a stale v2.61 width snaps to the nearest legal one on
// load. The EQ band caps speak SSL colours (LF keeps its black disc,
// LO MID pastel blue, HI MID off-white, HF pastel red) with an
// adaptive pointer, and each band carries its own small glyph under
// its selector: a low-shelf response, two bells, a high shelf.
//
// v2.61.0 (Clement's spec): a four-band SSL-style EQ between the cuts and
// the tilt. Band 1 (LF): bell or low shelf, 20-600 Hz, default 100 Hz. Band
// 2 (LO MID): bell with a variable bandwidth in octaves, 60-3000 Hz,
// default 400 Hz. Band 3 (HI MID): the same, 500 Hz-12 kHz, default
// 1600 Hz. Band 4 (HF): bell or high shelf, 600 Hz-18 kHz, default
// 7200 Hz. The bells sweep +/- 15 dB; the bandwidths run 0.2 to 2.0
// octaves (a bell reads exactly its gain at its centre, an RBJ property
// the harness asserts), and the shelves use the RBJ Q form with
// Q = 1/sqrt(2). At 0 dB every band collapses to the exact identity
// (b == a coefficient-for-coefficient), so a parked EQ is bit-transparent,
// and the global EQ plate parks the whole section at zero active
// biquads - a true bypass, not a flat filter. Sessions saved by earlier
// versions simply find no EQ children and load the defaults (the APVTS
// tree stores denormalised values, so nothing to migrate).
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
// Roadmap: C4 will add a transient designer.
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

    // v2.65.0: the dynamics meters read these four once per repaint -
    // the stereo-linked RMS level each detector hears (an IN ladder)
    // and the smoothed gain each half applies (a GR ladder). They are
    // stored once per block at the end of processBlock, so a parked
    // section reports 0 dB of reduction while its IN ladder still
    // tells the truth - metering never waits for the plates
    float getExpInDb()  const { return expMeterInDb.load  (std::memory_order_relaxed); }
    float getExpGrDb()  const { return expMeterGrDb.load  (std::memory_order_relaxed); }
    float getCompInDb() const { return compMeterInDb.load (std::memory_order_relaxed); }
    float getCompGrDb() const { return compMeterGrDb.load (std::memory_order_relaxed); }

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
    void refreshEqCoefficients();
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

    // the EQ section (v2.61.0): thirteen parameters - the global plate,
    // then per band a corner, a gain and (band 1/4) a bell/shelf mode or
    // (band 2/3) a bandwidth in octaves
    std::atomic<float>* eqOn    = nullptr; // 0.0 / 1.0
    std::atomic<float>* eq1Hz   = nullptr;
    std::atomic<float>* eq1Gain = nullptr;
    std::atomic<float>* eq1Bell = nullptr; // 0.0 / 1.0 (shelf / bell)
    std::atomic<float>* eq2Hz   = nullptr;
    std::atomic<float>* eq2Gain = nullptr;
    std::atomic<float>* eq2Oct  = nullptr;
    std::atomic<float>* eq3Hz   = nullptr;
    std::atomic<float>* eq3Gain = nullptr;
    std::atomic<float>* eq3Oct  = nullptr;
    std::atomic<float>* eq4Hz   = nullptr;
    std::atomic<float>* eq4Gain = nullptr;
    std::atomic<float>* eq4Bell = nullptr; // 0.0 / 1.0 (shelf / bell)

    // the dynamics section (v2.64.0): the expander's seven parameters and
    // the compressor's eight. Thresholds and range read in dBFS of the
    // RMS detector, the attack/release times in seconds, the mix in %
    // wet, and the two on/off plates own each half's true bypass
    std::atomic<float>* expOn         = nullptr; // 0.0 / 1.0
    std::atomic<float>* expThreshold  = nullptr;
    std::atomic<float>* expRange      = nullptr;
    std::atomic<float>* expAttack     = nullptr;
    std::atomic<float>* expRelease    = nullptr;
    std::atomic<float>* expRatio      = nullptr;
    std::atomic<float>* expMix        = nullptr;
    std::atomic<float>* compOn        = nullptr; // 0.0 / 1.0
    std::atomic<float>* compThreshold = nullptr;
    std::atomic<float>* compRatio     = nullptr;
    std::atomic<float>* compRelease   = nullptr;
    std::atomic<float>* compFastAttack= nullptr; // 0.0 / 1.0
    std::atomic<float>* compGain      = nullptr;
    std::atomic<float>* compMix       = nullptr;
    std::atomic<float>* compHpf       = nullptr;

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

    // the four-band EQ (v2.61.0): [band][channel], one biquad per band.
    // The global plate parks activeEqSections at zero - the process loop
    // then skips the whole cascade (a true bypass, not a flat filter)
    static constexpr int numEqBands = 4;
    Biquad eqBands[numEqBands][2];
    int activeEqSections = 0;

    // the EQ cache (v2.61.0): the corner, the gain, the bandwidth and the
    // bell/shelf mode of every band plus the global plate all ride the
    // same per-block cache discipline as the cuts
    float lastEqHz[4]   = { -1.0f, -1.0f, -1.0f, -1.0f };
    float lastEqGain[4] = { -999.0f, -999.0f, -999.0f, -999.0f };
    float lastEqN[4]    = { -1.0f, -1.0f, -1.0f, -1.0f };
    bool  lastEqBell[4] = { false, false, false, false };
    int   lastEqOn      = -1;

    // the dynamics state (v2.64.0): one stereo-linked RMS power envelope
    // and one smoothed dB gain per half, plus the compressor's sidechain
    // high-pass (a biquad the AUDIO never hears - only the detector). The
    // 10 ms detector coefficient is rebuilt in prepareToPlay; the attack
    // and release one-poles are per-block constants from the parameters.
    // The gains live in the dB domain so the static curves stay plain
    // arithmetic, and a neutral section (range 0, threshold 0) is skipped
    // before a single multiply touches the samples
    static constexpr float detectorFloorPower = 1.0e-12f;  // -120 dB
    float expDetectorPower = 0.0f;
    float expGainDb        = 0.0f;    // 0 = open, negative = expanded
    float compDetectorPower = 0.0f;
    float compGainDb       = 0.0f;    // 0 = unity, negative = compressed
    float detectorCoef     = 0.0f;    // 10 ms RMS ballistics, per sample
    Biquad compSidechain[2] = {};     // the SC HPF, one per channel
    float lastCompHpf      = -1.0f;   // the sidechain corner's cache

    // the dynamics meters (v2.65.0): the per-block snapshots the four
    // SSL ladders read. The IN meters start at the detector's floor
    // (-120 dB - a fresh strip paints an empty ladder, never a lying
    // 0 dB), the GR meters start at unity
    std::atomic<float> expMeterInDb   { -120.0f };
    std::atomic<float> expMeterGrDb   { 0.0f };
    std::atomic<float> compMeterInDb  { -120.0f };
    std::atomic<float> compMeterGrDb  { 0.0f };

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
//
// v2.61.0 (Clement's spec): the panel grows a second row - the four-band
// SSL-style EQ. An "EQ" plate on the left of the header owns the global
// bypass, the section caption carries its own bell-curve glyph, and four
// band columns (LF, LO MID, HI MID, HF) each hold a FREQ and a GAIN knob
// side by side. Bands 1/4 switch between bell and shelf with a dedicated
// BELL/SHELF plate, bands 2/3 sweep their bandwidth in octaves with the
// same horizontal slider the cuts use for their slopes.
//
// v2.64.0 (Clement's spec, roadmap C3): the panel grows a third row - the
// DYNAMICS, twice thirteen columns wide. The left half is the EXPANDER
// (THRESH, RANGE, ATTACK, RELEASE, RATIO, MIX), the right half the
// COMPRESSOR (THRESH, RATIO, RELEASE, the FAST/SLOW attack plate, GAIN,
// MIX and the SC HPF slider). Each half owns its on/off plate and its
// transfer-curve glyph beside its caption, every knob keeps the
// command-click default and the caps stay the black aluminium of the
// row-1 knobs - the SSL dynamics modules wear no colour.
// ---------------------------------------------------------------------------
class CostesStripEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit CostesStripEditor (CostesStrip& ownerProcessor);
    ~CostesStripEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // v2.65.0: the dynamics meters repaint at 30 Hz - the detectors are
    // already smoothed, so the straight read of the per-block
    // snapshots is the display ballistics
    void timerCallback() override;

    // v2.63.0 (Clement's remark 2): a command-click parks any knob or
    // slider back on its DEFAULT - the same default double-click
    // already used, now wired into the linear selectors too. Public so
    // the offline test harness can name the type and drive the path
    struct CostesSlider : public juce::Slider
    {
        // the value a command-click restores (setUpKnob/setUpSlope set
        // it from the same constant as the double-click default)
        float cmdClickDefault = 0.0f;

        // with the command key down the click jumps straight to the
        // default and the drag never starts; without it the normal JUCE
        // drag runs untouched
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;

        // what a command-click does - public so the offline harness can
        // exercise the exact path the mouse takes
        void applyCmdClickDefault();

    private:
        bool cmdDefaultActive = false;
    };

    // every knob and slider of the strip, in a stable order for the
    // offline test harness: the five row-1 knobs, the two cut slopes,
    // the EQ's FREQ/GAIN knobs and width sweeps band by band, then the
    // expander's six knobs and the compressor's five knobs plus its
    // sidechain HPF slider (the attack plate is a button, not a slider)
    std::array<CostesSlider*, 29> getStripSliders()
    {
        return { &trimInKnob, &lowCutKnob, &highCutKnob, &tiltKnob, &driveKnob,
                 &lowCutSlope, &highCutSlope,
                 &eq1HzKnob, &eq1GainKnob, &eq2HzKnob, &eq2GainKnob, &eq2OctSlider,
                 &eq3HzKnob, &eq3GainKnob, &eq3OctSlider, &eq4HzKnob, &eq4GainKnob,
                 &expThresholdKnob, &expRangeKnob, &expAttackKnob,
                 &expReleaseKnob, &expRatioKnob, &expMixKnob,
                 &compThresholdKnob, &compRatioKnob, &compReleaseKnob,
                 &compGainKnob, &compMixKnob, &compHpfSlider };
    }

    // v2.60.0 (Clement's remark): one clean vector glyph per knob, painted
    // to the left of its caption in the engraved silkscreen colour - the
    // five shapes read at a glance without any text. Public so the offline
    // test harness can name the types and count the instances
    struct SectionIcon : public juce::Component
    {
        enum class Icon { trim, lowCut, highCut, tilt, drive, eq, lowShelf, highShelf,
                           expander, compressor };

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

    // the EQ section's own bell-curve glyph (v2.61.0), beside the header
    // caption - the harness reaches it through this
    SectionIcon* getEqIcon() { return &eqIcon; }

    // the four band glyphs (v2.62.0, Clement's remark 5), one small icon
    // per band under its selector - LF and HF speak their shelf-capable
    // shapes, the two mids reuse the bell bump. In band order for the
    // offline test harness
    std::array<SectionIcon*, 4> getEqBandIcons()
    {
        return { &eqBand1Icon, &eqBand2Icon, &eqBand3Icon, &eqBand4Icon };
    }

    // the two dynamics glyphs (v2.64.0): the expander's and the
    // compressor's transfer curves, beside their section captions - for
    // the offline test harness, in half order
    std::array<SectionIcon*, 2> getDynIcons()
    {
        return { &expIcon, &compIcon };
    }

    // the dynamics meters (v2.65.0): the SSL J's own language, one IN
    // ladder and one GR ladder per stage. The IN ladder lights from the
    // bottom up on the stereo-linked RMS level its detector hears; the
    // GR ladder lights from the top down as the section works. The
    // component polls the processor in its paint, so the meters never
    // hold state of their own - a fresh paint is always the truth.
    // Public so the offline harness can name the modes and drive the
    // degenerate-size paint path
    struct DynamicsMeter : public juce::Component
    {
        enum class Mode { input, gainReduction };

        DynamicsMeter (CostesStrip& source, Mode meterMode, bool expanderSide,
                       float lowScaleDb, float highScaleDb, int numLeds,
                       float scaleMarkStep, const char* captionText)
            : owner (source), mode (meterMode), expander (expanderSide),
              lowDb (lowScaleDb), highDb (highScaleDb), leds (numLeds),
              markStep (scaleMarkStep), caption (captionText) {}

        void paint (juce::Graphics& g) override;

        CostesStrip& owner;
        Mode mode;
        bool expander;
        float lowDb;
        float highDb;
        int leds;
        float markStep;
        const char* caption;
    };

    // the four ladders in stage order - the expander's IN/GR pair then
    // the compressor's - for the offline test harness
    std::array<DynamicsMeter*, 4> getDynMeters()
    {
        return { &expInMeter, &expGrMeter, &compInMeter, &compGrMeter };
    }

private:
    CostesStrip& owner;

    CostesSlider trimInKnob, lowCutKnob, highCutKnob, tiltKnob, driveKnob;
    CostesSlider lowCutSlope, highCutSlope;
    juce::Label trimInLabel, lowCutLabel, highCutLabel, tiltLabel, driveLabel;

    SectionIcon trimIcon   { SectionIcon::Icon::trim };
    SectionIcon lowCutIcon { SectionIcon::Icon::lowCut };
    SectionIcon highCutIcon{ SectionIcon::Icon::highCut };
    SectionIcon tiltIcon   { SectionIcon::Icon::tilt };
    SectionIcon driveIcon  { SectionIcon::Icon::drive };
    SectionIcon eqIcon    { SectionIcon::Icon::eq };   // v2.61.0

    // the four band glyphs (v2.62.0, Clement's remark 5): the LF band's
    // low-shelf response, the two mid bells' shared bump and the HF
    // band's high shelf, one under each band's selector
    SectionIcon eqBand1Icon { SectionIcon::Icon::lowShelf };
    SectionIcon eqBand2Icon { SectionIcon::Icon::eq };
    SectionIcon eqBand3Icon { SectionIcon::Icon::eq };
    SectionIcon eqBand4Icon { SectionIcon::Icon::highShelf };

    // the EQ section (v2.61.0): eight knobs (a FREQ and a GAIN per band,
    // each with its own caption label), four band captions, two bandwidth
    // sliders (bands 2/3) and two bell/shelf plates (bands 1/4)
    CostesSlider eq1HzKnob,   eq1GainKnob;
    CostesSlider eq2HzKnob,   eq2GainKnob;
    CostesSlider eq3HzKnob,   eq3GainKnob;
    CostesSlider eq4HzKnob,   eq4GainKnob;
    CostesSlider eq2OctSlider, eq3OctSlider;

    juce::Label eq1HzLabel, eq1GainLabel;
    juce::Label eq2HzLabel, eq2GainLabel;
    juce::Label eq3HzLabel, eq3GainLabel;
    juce::Label eq4HzLabel, eq4GainLabel;
    juce::Label eqBand1Label, eqBand2Label, eqBand3Label, eqBand4Label;
    juce::Label eqSectionLabel;

    // the dynamics section (v2.64.0, Clement's spec): eleven knobs and one
    // sidechain slider - the expander's THRESH, RANGE, ATTACK, RELEASE,
    // RATIO and MIX, then the compressor's THRESH, RATIO, RELEASE, GAIN
    // and MIX plus its SC HPF - each with its own caption label, the two
    // section captions and the two transfer-curve glyphs
    CostesSlider expThresholdKnob, expRangeKnob, expAttackKnob;
    CostesSlider expReleaseKnob,  expRatioKnob,  expMixKnob;
    CostesSlider compThresholdKnob, compRatioKnob, compReleaseKnob;
    CostesSlider compGainKnob,     compMixKnob,   compHpfSlider;

    juce::Label expThresholdLabel, expRangeLabel, expAttackLabel;
    juce::Label expReleaseLabel,   expRatioLabel,  expMixLabel;
    juce::Label compThresholdLabel, compRatioLabel, compReleaseLabel;
    juce::Label compAttackLabel, compGainLabel, compMixLabel, compHpfLabel;
    juce::Label expSectionLabel, compSectionLabel;

    SectionIcon expIcon  { SectionIcon::Icon::expander };    // v2.64.0
    SectionIcon compIcon { SectionIcon::Icon::compressor }; // v2.64.0

    // the oversampling switch: a ToggleButton that paints its own plate
    // (lit amber + LED when on, dark + dim LED when off) - the default
    // checkbox tick was too quiet to read (v2.57.0, Clement's remark 2).
    // v2.61.0: the caption is a member now, so the EQ's global on/off
    // plate speaks the same plate language ("EQ ON" / "EQ OFF")
    struct PlateToggle : public juce::ToggleButton
    {
        juce::String caption;

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    };

    PlateToggle oversampleButton;
    PlateToggle eqOnButton;   // v2.61.0

    // the band-1/4 shape switch (v2.61.0): lit "BELL" or dark "SHELF",
    // the same plate language as the cut on/off bars
    struct ShelfModeToggle : public juce::ToggleButton
    {
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    };

    ShelfModeToggle eq1ModeButton, eq4ModeButton;

    // the compressor's attack plate (v2.64.0): lit amber "FAST" (a 3 ms
    // attack that clamps a transient almost before it lands) or dark
    // "SLOW" (the classic 30 ms that lets a drum's point through) - the
    // same plate language as the BELL/SHELF switch
    struct FastAtkToggle : public juce::ToggleButton
    {
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    };

    FastAtkToggle compFastAttackButton;

    // the cut on/off plates (v2.59.0, Clement's remark 2): one ToggleButton
    // per cut, painted like the oversampling switch but vertical - a
    // horizontal bar with an LED at the top and an ON/OFF caption below.
    // It replaces "off" in the slope slider, which now starts at 6 dB/oct.
    struct CutOnToggle : public juce::ToggleButton
    {
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    };

    CutOnToggle lowCutOnButton, highCutOnButton;

    // the dynamics' own on/off plates (v2.64.0): the same "NAME ON" /
    // "NAME OFF" plates as the EQ's global switch, one per half
    PlateToggle expOnButton, compOnButton;

    // the dynamics meters' instances (v2.65.0) - the public struct and
    // the harness accessor sit above; the expander's pair spans 70 dB
    // of IN (marks every 20) and 40 of GR (marks every 10 - its RANGE
    // runs 0 to 40), the compressor's 70 of IN and 20 of GR (marks
    // every 5)
    DynamicsMeter expInMeter  { owner, DynamicsMeter::Mode::input,
                                true, -70.0f, 0.0f, 14, 20.0f, "IN" };
    DynamicsMeter expGrMeter { owner, DynamicsMeter::Mode::gainReduction,
                                true, -40.0f, 0.0f, 20, 10.0f, "GR" };
    DynamicsMeter compInMeter { owner, DynamicsMeter::Mode::input,
                                false, -70.0f, 0.0f, 14, 20.0f, "IN" };
    DynamicsMeter compGrMeter { owner, DynamicsMeter::Mode::gainReduction,
                                false, -20.0f, 0.0f, 10, 5.0f, "GR" };

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

    // the EQ attachments (v2.61.0): thirteen controls wired to the APVTS
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> eqOnAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eq1HzAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eq1GainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> eq1ModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eq2HzAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eq2GainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eq2OctAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eq3HzAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eq3GainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eq3OctAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eq4HzAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eq4GainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> eq4ModeAttachment;

    // the dynamics attachments (v2.64.0): twelve sliders and three
    // plates wired to the APVTS
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> expOnAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> expThresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> expRangeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> expAttackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> expReleaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> expRatioAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> expMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> compOnAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> compThresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> compRatioAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> compReleaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> compFastAttackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> compGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> compMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> compHpfAttachment;

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
