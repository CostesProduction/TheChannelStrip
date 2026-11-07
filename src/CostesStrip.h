// ---------------------------------------------------------------------------
// src/CostesStrip.h - v2.66.0 (Clement's remarks A to F)
//
// v2.66.0 (Clement's remarks after the SSL J session): the two dynamics
// stages adopt the SAME control order the J's modules share - the IN and
// OUT ladders side by side at the left, then MIX, THRESH, RATIO and
// RELEASE, a vertical block holding the FAST/SLOW attack plate, the two
// sidechain selectors (SC HPF and SC LPF, 12 dB/oct biquads, 16 to 1600 Hz
// and 100 to 24000 Hz - the flat rest positions leave the detector's band
// whole) and the LISTEN plate, then RANGE on the expander (make-up GAIN on
// the compressor), and the GR ladder beside them (at the block's left
// since v2.72.0, Clement's request). The expander's attack
// knob became a FAST/SLOW plate like the compressor's (1 ms lit / 30 ms
// dark); each stage carries its own sidechain pair and its own LISTEN
// monitor (the filtered signal its detector hears, the compressor's plate
// winning when both are lit); two new OUT ladders read the level each
// stage leaves behind; and the GR ladders speak the J's two-half scales
// (the expander tops at 36 dB since v2.72.0, the compressor at 20). The knobs
// wear the section colours Clement asked for in v2.68.0: the HI MID
// band's off-white cap on the expander's knobs, the LO MID band's
// pastel blue on the compressor's.
//
// v2.65.0 (Clement's request, from his photos of the SSL J's dynamics
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
// GR ladder (0 dB at the top, growing down as the section
// works: 36 dB of span for the expander since v2.72.0, 20 for the
// compressor, the
// classic orange). The meters ride four new atomics the process loop
// stores once per block: the detectors now ALWAYS run - metering never
// waits for a section to be engaged - while the gain arithmetic stays
// gated behind the activity flags, so a parked strip still processes
// sample-for-sample exactly as v2.63 did and the meters still tell the
// truth. A 30 Hz timer repaints the ladders; the panel grows to 1040x800
// (v2.67.0: 860x911, the shell's own minimum footprint).
//
// v2.64.0 (Clement's spec, roadmap C3): the DYNAMICS section - a
// gate/expander and a classic compressor between the EQ and the tilt,
// exactly where an SSL channel puts them. The expander: threshold
// (-70 to 0 dB), range (0 to 36 dB since v2.72.0, the maximum
// attenuation - 0 dB is a
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
// Roadmap: C4 (v2.88.0) added the transient designer.
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
    float getExpOutDb()  const { return expMeterOutDb.load (std::memory_order_relaxed); }
    float getCompOutDb() const { return compMeterOutDb.load (std::memory_order_relaxed); }

    // v2.90.0: the transient designer's meters are slimmed down to a
    // single bipolar GR bar - the old IN and OUT ladders read the
    // same samples the expander's OUT and the compressor's IN
    // ladders already show (the chain runs expander -> transient ->
    // compressor), so they were structural duplicates.  The GR bar
    // reads the smoothed envelope gain itself, signed: a boost
    // reads positive (filling right), a cut negative (filling left)
    float getTransGrDb() const { return transMeterGrDb.load (std::memory_order_relaxed); }

    // v2.90.0: the CLIPPER plate's LED - true while the wet signal
    // has touched the rail within the last second (peak-hold
    // window), so the LED stays lit long enough to be seen
    bool getTransClipActive() const
    {
        return juce::Time::getMillisecondCounterHiRes() - transClipLastMs.load (std::memory_order_relaxed) < 1000.0;
    }

    // v2.91.0 (Clement's request): the WET GAIN balance readout -
    // the section's level delta, OUT minus IN in dB, from the same
    // 10 ms RMS one-poles the honest meters ride. Unity (zero)
    // means the section changes the CONTRAST without changing the
    // loudness, so the makeup can be set by ear and eye: the editor
    // engraves the reading beside the GR bar
    float getTransBalanceDb() const { return transBalanceDb.load (std::memory_order_relaxed); }

    // v2.95.0 (Clement's request): the AVERAGED balance - a ~1 s
    // one-pole on the per-block reading, the number the ADD digits
    // engrave and the click folds - the settled truth, not the
    // instant's spike
    float getTransBalanceAvgDb() const { return transBalanceAvgDb.load (std::memory_order_relaxed); }

    // v2.93.0 (Clement's request): the compressor's ADD readout - the
    // stage's own level delta, OUT minus IN, for the editor's face
    // beside the COMP ON plate and the offline harness
    float getCompBalanceDb() const { return compBalanceDb.load (std::memory_order_relaxed); }

    // v2.95.0: the compressor's own averaged ADD reading
    float getCompBalanceAvgDb() const { return compBalanceAvgDb.load (std::memory_order_relaxed); }

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
    std::atomic<float>* trimOutDb         = nullptr;   // v2.86.0
    std::atomic<float>* lowCutHz          = nullptr;
    std::atomic<float>* lowCutOrder       = nullptr;
    std::atomic<float>* lowCutOn          = nullptr; // 0.0 / 1.0 (v2.59.0)
    std::atomic<float>* highCutHz         = nullptr;
    std::atomic<float>* highCutOrder      = nullptr;
    std::atomic<float>* highCutOn         = nullptr; // 0.0 / 1.0 (v2.59.0)
    std::atomic<float>* tiltDb            = nullptr;
    std::atomic<float>* driveDb           = nullptr;

    // the IMAGE section (v2.84.0, Clement's remarks 3 to 6): the
    // phase plate, the stereo balance, the stereo width and the mono
    // maker - four controls over the stereo image itself, each parked
    // at a fully neutral default
    std::atomic<float>* phaseInvert   = nullptr; // 0.0 / 1.0
    std::atomic<float>* stereoBalance = nullptr; // -100 .. +100, 0 = centre
    std::atomic<float>* stereoWidth   = nullptr; // 0 .. 200 (percent), 100 = honest
    std::atomic<float>* monoMakerHz   = nullptr; // 0 = off, else the corner

    // the image stages' own state: the balance and width gains ride
    // the trim's own 20 ms one-pole (a ridden knob is a slow ramp),
    // and the mono maker keeps a 2nd-order Butterworth lowpass pair,
    // one per channel, with the same corner-cache discipline as the
    // sidechains
    juce::SmoothedValue<float> balanceGainL, balanceGainR;
    juce::SmoothedValue<float> widthAmount;
    Biquad monoMakerLp[2] {};
    float lastMonoMakerHz = -1.0f;

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
    std::atomic<float>* expFastAttack = nullptr; // 0.0 / 1.0 (v2.66.0)
    std::atomic<float>* expRelease    = nullptr;
    std::atomic<float>* expRatio      = nullptr;
    std::atomic<float>* expKnee       = nullptr;  // 0-20 dB in 4 dB plates (v2.73.0)
    std::atomic<float>* expMix        = nullptr;
    std::atomic<float>* expScHpf      = nullptr;  // v2.66.0
    std::atomic<float>* expScLpf      = nullptr;  // v2.66.0
    std::atomic<float>* expListen     = nullptr;  // 0.0 / 1.0 (v2.66.0)
    std::atomic<float>* compOn        = nullptr; // 0.0 / 1.0
    std::atomic<float>* compThreshold = nullptr;
    std::atomic<float>* compRatio     = nullptr;
    std::atomic<float>* compRelease   = nullptr;
    std::atomic<float>* compFastAttack= nullptr; // 0.0 / 1.0
    std::atomic<float>* compGain      = nullptr;
    std::atomic<float>* compMix       = nullptr;
    std::atomic<float>* compHpf       = nullptr;
    std::atomic<float>* compScLpf     = nullptr;  // v2.66.0
    std::atomic<float>* compListen    = nullptr;  // 0.0 / 1.0 (v2.66.0)
    std::atomic<float>* compLink      = nullptr;  // 0-3: dual/50/80/link (v2.72.0)
    std::atomic<float>* compStyle     = nullptr;  // 0-3: FET/VCA/GLUE/OPTO (v2.97.0)

    // the transient designer (v2.88.0, Clement's spec, roadmap C4):
    // the attack and sustain knobs read in dB of envelope lead, the
    // PUNCH plate (v2.89.0) is the section's own master switch, lit
    // by default like EXP and COMP, the gain is the wet half's own
    // make-up, the mix the dry/wet blend in % wet, and the clip
    // plate bounds the wet half at the rail. The SC corners are
    // gone (v2.89.0, Clement's verdict) - the detector hears the
    // full band, the -60 dBFS dead zone keeps it honest on its own
    std::atomic<float>* transAttack  = nullptr;  // -15 to +15 dB
    std::atomic<float>* transSustain = nullptr;  // -15 to +15 dB
    std::atomic<float>* transOn      = nullptr;  // v2.89.0: the PUNCH plate, lit by default
    std::atomic<float>* transGain    = nullptr;   // -18 to +18 dB, the wet half only
    std::atomic<float>* transMix     = nullptr;   // 0-100 % wet
    std::atomic<float>* transClipper = nullptr;  // 0.0 / 1.0

    double currentSampleRate = 48000.0;

    // the trim moves as a smoothed linear gain (20 ms); the cut corners
    // and the tilt move per block - a knob turn is already a slow ramp,
    // and each block only steps the response a fraction of a dB
    juce::SmoothedValue<float> trimInGain;
    juce::SmoothedValue<float> trimOutGain;   // v2.86.0: the last gain, 20 ms as ever

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
    // before a single multiply touches the samples.
    // v2.72.0 (Clement's request): the compressor carries its own
    // detector AND gain per channel - the compLink plate blends each
    // channel's detector level towards the stereo-linked one (0 = fully
    // dual, 1 = the classic single-detector link), so at "link" the two
    // gains evolve identically and the strip is bit-for-bit its old self
    static constexpr float detectorFloorPower = 1.0e-12f;  // -120 dB
    static constexpr float compLinkWeights[4] { 0.0f, 0.5f, 0.8f, 1.0f };
    float expDetectorPower = 0.0f;
    float expGainDb        = 0.0f;    // 0 = open, negative = expanded
    float compDetectorPowerL = 0.0f;  // the compressor's per-channel RMS (v2.72.0)
    float compDetectorPowerR = 0.0f;
    float compGainDbL       = 0.0f;    // 0 = unity, negative = compressed
    float compGainDbR       = 0.0f;
    float detectorCoef     = 0.0f;    // 10 ms RMS ballistics, per sample
    Biquad compSidechain[2] = {};     // the SC HPF, one per channel
    float lastCompHpf      = -1.0f;   // the sidechain corner's cache
    Biquad compSidechainLp[2] = {};   // the SC LPF, one per channel (v2.66.0)
    float lastCompScLpf    = -1.0f;
    // v2.97.0 (Clement's request, the GLUE character): the feedback
    // detector's own state - the GLUE's sidechain listens to the
    // stage's OUTPUT, so it carries its own mirrored biquads (and
    // their own caches - a shared cache would never see a rebuild)
    float compFbPowerL     = 0.0f;
    float compFbPowerR     = 0.0f;
    // v2.99.0 (Clement's request, the FET character): the fast stage's
    // own detector - two cascaded one-poles (3 ms each, v2.99.1)
    // fed by the same rectified word the VCA's single 10 ms window
    // reads
    float compFetPower1L    = 0.0f;
    float compFetPower1R    = 0.0f;
    float compFetPower2L    = 0.0f;
    float compFetPower2R    = 0.0f;
    Biquad compFbSidechainHp[2] = {};
    Biquad compFbSidechainLp[2] = {};
    float lastCompFbHpf   = -1.0f;
    float lastCompFbScLpf = -1.0f;
    Biquad expSidechainHp[2] = {};    // the expander's own pair (v2.66.0)
    Biquad expSidechainLp[2] = {};
    float lastExpScHpf     = -1.0f;
    float lastExpScLpf     = -1.0f;
    float expOutPower      = 0.0f;    // the OUT ladders' 10 ms RMS state
    float compOutPower     = 0.0f;
    // v2.67.0 (Clement's remark): the IN ladders' own 10 ms RMS state -
    // the TRUE level the section receives, full band, whatever the
    // sidechain corners do to the detector's copy. The detector still
    // hears its filtered sidechain (that is what the GR curve acts on),
    // but the IN meter reads the honest input, so an IN ladder can
    // never sit below its OUT twin just because a corner shaved the
    // detector's band
    float expInPower       = 0.0f;
    float compInPower     = 0.0f;

    // the transient designer's state (v2.88.0, Clement's spec): two
    // envelope followers ride the stereo-linked rectified level -
    // full band since v2.89.0, the SC corners are gone (Clement's
    // verdict) - a FAST one (0.5 ms up / 20 ms down) that
    // leads on the attacks, a SLOW one (30 ms up / 150 ms down) that
    // carries the body - and their dB difference IS the section's
    // control signal: positive (the fast envelope leading) an attack
    // is landing, negative (the fast envelope fallen behind) the
    // body sustains. The attack and sustain knobs scale each half of
    // that difference, the smoothed dB gain tracks it at 2 ms, and a
    // -60 dBFS dead zone on the detector's input keeps a sidechain
    // corner that lifts a band out really lifted out - the envelope
    // lead is scale-invariant, so without the dead zone a
    // filtered-away note would drive the section just as hard.
    // v2.88.0 build 2: a PEAK-HOLD pre-envelope (the rectified peak
    // held on a 20 ms decay) feeds the dead zone and BOTH followers
    // - without it the fast follower rode the rectified wave's own
    // peaks while the slow one smoothed to its mean (2/pi), and
    // every steady tone read as a false +3.5 dB attack; the hold
    // re-latches at every wave peak, so on a steady tone both
    // followers settle on the same held peak and the lead reads
    // zero. A NaN sample fails the jmax compare and leaves the held
    // peak untouched
    float transPeakHold         = 0.0f;   // the held detector peak
    float transEnvFast = 0.0f;
    float transEnvSlow = 0.0f;
    float transGainDb  = 0.0f;    // the smoothed dB gain, 0 = unity
    float transFastAttackCoef  = 0.0f;   // the fast envelope, 0.5 ms up
    float transFastReleaseCoef = 0.0f;   // the fast envelope, 20 ms down
    float transSlowAttackCoef  = 0.0f;   // the slow envelope, 30 ms up
    float transSlowReleaseCoef = 0.0f;   // the slow envelope, 150 ms down
    // the peak hold's 20 ms decay as its per-sample retention
    // MULTIPLIER (NOT an update coefficient - processBlock multiplies
    // the held peak by it directly)
    float transPeakDecayFactor = 0.0f;
    float transGainCoef        = 0.0f;   // the gain one-pole, 2 ms
    static constexpr float transDetectorDeadZone = 1.0e-3f;   // -60 dBFS
    // v2.91.0: the section's own OUT level - a 10 ms RMS one-pole fed
    // every sample, parked or not, so the WET GAIN balance readout
    // (out minus in, in dB) tells the truth even while the PUNCH
    // plate is dark. The IN side needs no state of its own: the
    // expander's OUT one-pole reads the very same sample
    float transOutPower        = 0.0f;

    // the dynamics meters (v2.65.0): the per-block snapshots the four
    // SSL ladders read. The IN meters start at the detector's floor
    // (-120 dB - a fresh strip paints an empty ladder, never a lying
    // 0 dB), the GR meters start at unity
    std::atomic<float> expMeterInDb   { -120.0f };
    std::atomic<float> expMeterGrDb   { 0.0f };
    std::atomic<float> compMeterInDb  { -120.0f };
    std::atomic<float> compMeterGrDb  { 0.0f };
    std::atomic<float> expMeterOutDb  { -120.0f };   // v2.66.0
    std::atomic<float> compMeterOutDb { -120.0f };
    // v2.90.0: the transient designer keeps only the bipolar GR
    // reading - the smoothed envelope gain itself, signed, shown
    // on the section's horizontal GR bar.  The IN and OUT ladders
    // are gone with their one-poles (the neighbouring sections
    // already meter those samples)
    std::atomic<float> transMeterGrDb { 0.0f };
    // v2.91.0: the WET GAIN balance readout - the section's OUT
    // level (a one-pole fed every sample, parked or not) minus its
    // IN level (the expander's own OUT one-pole), both floored at
    // the detector's silence so a silent section reads a clean 0.0
    std::atomic<float> transBalanceDb { 0.0f };

    // v2.95.0: the averaged WET GAIN balance - a ~1 s one-pole the
    // ADD face engraves and its click folds
    float transBalanceAvg = 0.0f;
    std::atomic<float> transBalanceAvgDb { 0.0f };

    // v2.93.0: the compressor's ADD readout - the stage's OUT-minus-IN
    // one-pole delta, snapped per block beside the transient's
    std::atomic<float> compBalanceDb { 0.0f };

    // v2.95.0: the compressor's own averaged balance
    float compBalanceAvg = 0.0f;
    std::atomic<float> compBalanceAvgDb { 0.0f };
    // v2.90.0: last wall-clock moment the wet signal touched the
    // CLIPPER's rail (-1e12 = never); the plate's LED peak-holds
    // for one second from this timestamp
    std::atomic<double> transClipLastMs { -1.0e12 };

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
// moved to the cut on/off plate on the left of each cut's readout
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
    // v2.60.0 (Clement's remark): one clean vector glyph per knob, painted
    // to the left of its caption in the engraved silkscreen colour - the
    // five shapes read at a glance without any text. Public so the offline
    // test harness can name the types and count the instances.
    // v2.80.0: the struct stands BEFORE CostesSlider now - a slider
    // may carry one of these glyphs inside its own readout (the SC
    // selectors' curve beside the value), and the member needs the
    // complete type
    struct ShelfModeToggle;   // the outer bands' glyphs shadow their plates

    struct SectionIcon : public juce::Component
    {
        enum class Icon { trim, lowCut, highCut, tilt, drive, eq, lowShelf, highShelf };

        explicit SectionIcon (Icon iconType) : type (iconType) {}

        void paint (juce::Graphics& g) override;

        // v2.70.0 (Clement's remark 9): the glyph drawing itself, split
        // out of paint() so the BELL/SHELF plates can carry the very
        // same shapes INSIDE their buttons - one drawing, two owners
        static void drawGlyph (juce::Graphics& g, juce::Rectangle<float> bounds,
                               Icon iconType, const juce::Colour& colour);

        Icon type;

        // v2.78.0 (Clement's remark 4): the outer bands' glyphs stand
        // under their band names now and FOLLOW their BELL/SHELF
        // plates - the icon picks its shape at paint time, the bell
        // while the plate reads BELL, the band's own shelf when it
        // reads SHELF. Null on every other icon, which keeps its type
        const ShelfModeToggle* follows = nullptr;
    };

    // v2.87.0 (Clement's remark 4, the AMEK 9099's pastel zones): a
    // LOCAL background tile - a rounded rectangle in one of the two
    // warm beiges, worn behind a zone's knobs, plates and words. It
    // never intercepts a click, so the knobs keep their own drag and
    // the empty streets stay empty; it carries the panel's own grain
    // so it reads as a shade of the cream front, not a sticker
    struct ZoneTile : public juce::Component
    {
        ZoneTile() { setInterceptsMouseClicks (false, false); }

        void paint (juce::Graphics& g) override;

        juce::Colour tint { 0xfff9f3e3 };
        const juce::Image* grain = nullptr;
    };

    struct CostesSlider : public juce::Slider
    {
        // the value a command-click restores (setUpKnob/setUpSlope set
        // it from the same constant as the double-click default)
        float cmdClickDefault = 0.0f;

        // v2.80.0 (Clement's remark 6): a slider may carry a glyph in
        // its own readout - the SC selectors draw their HPF / LPF
        // curve beside the value ("off" at rest, "90 Hz" moved)
        // instead of spelling "SC HPF" out. hasReadoutGlyph switches
        // it on and the look-and-feel's text box paints it
        SectionIcon::Icon readoutGlyph = SectionIcon::Icon::lowCut;
        bool hasReadoutGlyph = false;

        // v2.81.0 (Clement's remark 3): a slider may also speak its
        // readout SMALLER than JUCE's own - the cut slopes' own
        // "12 dB/oct" rides at 10 px inside the EQ selectors' own
        // 52 px box now. The look-and-feel's factory applies it every
        // time the box is (re)created; 0 keeps the default size
        float readoutFontHeight = 0.0f;

        // v2.92.0 (Clement's request): a knob may carry a DETENT step
        // - the drag and the wheel snap the value to whole steps, the
        // way a cranked pot notches under your fingers. 1 dB on the
        // two THRESH knobs and the transient's ATTACK and SUSTAIN;
        // 0 keeps the free sweep
        float detentStep = 0.0f;

        // with the command key down the click jumps straight to the
        // default and the drag never starts; without it the normal JUCE
        // drag runs untouched
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseWheelMove (const juce::MouseEvent& e,
                             const juce::MouseWheelDetails& wheel) override;

        // what a command-click does - public so the offline harness can
        // exercise the exact path the mouse takes
        void applyCmdClickDefault();

        // v2.92.0: what a detented drag does at every step - public so
        // the offline harness can exercise the exact path the mouse
        // takes, the same discipline as applyCmdClickDefault
        void applyDetent();

    private:
        bool cmdDefaultActive = false;
    };

    // every knob and slider of the strip, in a stable order for the
    // offline test harness: the five row-1 knobs, the two cut slopes,
    // the EQ's FREQ/GAIN knobs and width sweeps band by band, then the
    // expander's five knobs and the compressor's five (both attack
    // plates are buttons, not sliders, since v2.66.0), the four
    // sidechain selectors - the compressor's SC HPF and SC LPF, the
    // expander's own pair - the compressor's LINK selector (v2.72.0)
    // as index 31, the expander's KNEE selector (v2.73.0) as
    // index 32, the trim out (v2.86.0) as index 33, then the
    // transient designer's own six (v2.88.0): its ATTACK, SUSTAIN,
    // GAIN and MIX knobs as 34 to 37, its SC HPF and SC LPF
    // selectors as 38 and 39
    std::array<CostesSlider*, 38> getStripSliders()
    {
        return { &trimInKnob, &lowCutKnob, &highCutKnob, &tiltKnob, &driveKnob,
                 &lowCutSlope, &highCutSlope,
                 &eq1HzKnob, &eq1GainKnob, &eq2HzKnob, &eq2GainKnob, &eq2OctSlider,
                 &eq3HzKnob, &eq3GainKnob, &eq3OctSlider, &eq4HzKnob, &eq4GainKnob,
                 &expThresholdKnob, &expRangeKnob, &expReleaseKnob,
                 &expRatioKnob, &expMixKnob,
                 &compThresholdKnob, &compRatioKnob, &compReleaseKnob,
                 &compGainKnob, &compMixKnob,
                 &compHpfSlider, &compScLpfSlider,
                 &expScHpfSlider, &expScLpfSlider,
                 &compLinkSlider, &expKneeSlider,
                 &trimOutKnob,   // v2.86.0: the trim out closes the dynamics as the 34th
                 &transAttackKnob, &transSustainKnob,   // v2.88.0: the transient
                 &transGainKnob, &transMixKnob };        // designer's own four (the SC pair is gone, v2.89.0)
    }

    // v2.60.0 (Clement's remark): one clean vector glyph per knob -
    // the SectionIcon struct itself now stands ABOVE CostesSlider
    // (v2.80.0), where the sliders' readout glyphs read it

    // the icons in column order (trim, low cut, high cut, tilt, drive),
    // for the offline test harness
    // v2.78.0 (Clement's remark 5): the strip's own five glyphs, for
    // the harness. v2.86.0: the trim out carries the fader glyph too
    // (the trim's own), so the sixth entry rides at the END - the
    // indices of the five never moved (v2.87.0: the harness pins the
    // glyph breaths of all four caption units)
    std::array<SectionIcon*, 6> getSectionIcons()
    {
        return { &trimIcon, &lowCutIcon, &highCutIcon, &tiltIcon, &driveIcon,
                 &trimOutIcon };
    }

    // v2.78.0 (Clement's remarks 4 and 6): every band's glyph stands
    // under its band's name now, in the row's margin column - the two
    // mid bells; the two outer bands' dynamic shapes, which follow
    // their BELL/SHELF plates, live in their own getter below
    // (v2.81.0). In band order (LMF then HMF) for the offline test
    // harness
    std::array<SectionIcon*, 2> getEqBandIcons()
    {
        return { &eqBand2Icon, &eqBand3Icon };
    }

    // the outer bands' BELL/SHELF plates (band 1 then band 4) - the
    // harness reads their shelfGlyph to prove each plate carries its
    // own shape inside the button. The struct itself is defined below,
    // with the other painted plates (the forward declaration at the
    // top of the class, v2.80.0, serves the whole editor now - this
    // duplicate went with it)

    std::array<ShelfModeToggle*, 2> getEqModeButtons()
    {
        return { &eq1ModeButton, &eq4ModeButton };
    }

    // v2.81.0 (Clement's remark 4): the outer bands' OWN glyphs - the
    // two SectionIcons that follow their BELL/SHELF plates (the
    // follows member). Public so the offline harness can paint them
    // and prove the shape tracks the plate, bell for bell, shelf for
    // shelf
    std::array<SectionIcon*, 2> getEqOuterBandIcons()
    {
        return { &eqBand1Icon, &eqBand4Icon };
    }

    // v2.81.0: the four SC selectors' readout labels. JUCE parents
    // each slider's value box to the slider itself (its one child
    // component), so the harness can reach the label and cast it to
    // the glyph-aware type - proving the look-and-feel's factory
    // really built it, the proof the v2.80.0 build could not give
    // (the DEFAULT look-and-feel had been building a plain label)
    std::array<juce::Label*, 4> getScReadoutLabels()
    {
        return { dynamic_cast<juce::Label*> (compHpfSlider.getChildComponent (0)),
                 dynamic_cast<juce::Label*> (compScLpfSlider.getChildComponent (0)),
                 dynamic_cast<juce::Label*> (expScHpfSlider.getChildComponent (0)),
                 dynamic_cast<juce::Label*> (expScLpfSlider.getChildComponent (0)) };
    }

    // v2.80.0 (Clement's remark 6): the SC selectors' readout label -
    // the slider's own curve glyph drawn at the left of the value, so
    // the readout reads [curve] "off" / [curve] "90 Hz" and the words
    // "SC HPF" are gone. v2.81.0: the struct stands in the PUBLIC
    // section now so the harness can name the type and prove the
    // look-and-feel's factory really built the box - the v2.80.0
    // build never showed the glyph, because a parentless slider still
    // answers JUCE's own DEFAULT factory at setTextBoxStyle time; the
    // SC sliders carry `look` themselves now (see the constructor)
    struct GlyphReadoutLabel : public juce::Label
    {
        explicit GlyphReadoutLabel (SectionIcon::Icon glyphType) : glyph (glyphType) {}

        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds();

            // the curve at the readout's left, 12 px in an 18 px strip -
            // the same engraved silkscreen colour the value speaks
            auto glyphArea = bounds.removeFromLeft (16);
            SectionIcon::drawGlyph (g, glyphArea.toFloat().reduced (2.0f),
                                    glyph, findColour (juce::Label::textColourId));

            // then the value, centred in the room that is left - the
            // readout never grows: the glyph rides INSIDE the old box
            g.setFont (getFont());
            g.setColour (findColour (juce::Label::textColourId));
            g.drawText (getText(), bounds, juce::Justification::centred);
        }

        SectionIcon::Icon glyph;
    };

    // v2.77.0 (Clement's remark): the four band names stand in a slim
    // margin column at their rows' left now, level with their knobs,
    // instead of owning caption rows above them. Public so the
    // offline harness can pin their new address
    std::array<juce::Label*, 4> getEqBandLabels()
    {
        return { &eqBand1Label, &eqBand2Label, &eqBand3Label, &eqBand4Label };
    }

    // v2.75.0 (Clement's request): the reserved slots, public so the
    // offline harness can pin the three-column grid - the TRANSIENT
    // slot waits between the two dynamics stages, and the five
    // named frames stack down column 3 in the signal's own order:
    // v2.86.0 (Clement's request): TWO blocks now - IMAGE (the phase
    // plate, the balance, the width, the mono maker) at the top of
    // the column, SHAPE OUT (the tilt, the drive, the trim out) under
    // it
    const juce::Rectangle<int>& getTransientFrame() const { return transientFrame; }
    const std::array<juce::Rectangle<int>, 2>& getReservedFrames() const { return reservedFrames; }

    // v2.88.0 (Clement's spec, roadmap C4): the transient designer's
    // own controls, for the offline test harness - the four knobs in
    // panel order (v2.90.0: ATTACK, SUSTAIN then WET GAIN, MIX, one
    // rank of four - character first, dosage after, Clement's call),
    // the four caption words, the CLIPPER plate and the PUNCH plate
    std::array<CostesSlider*, 4> getTransientKnobs()
    {
        return { &transAttackKnob, &transSustainKnob, &transGainKnob, &transMixKnob };
    }

    std::array<juce::Label*, 4> getTransientCaptions()
    {
        return { &transAttackLabel, &transSustainLabel, &transGainLabel, &transMixLabel };
    }

    juce::Button* getTransClipButton()
    {
        return &transClipButton;
    }

    // v2.89.0: the PUNCH plate - the section's own master switch, the
    // harness reads its lit state through the toggle itself
    juce::Button* getPunchButton()
    {
        return &punchOnButton;
    }

    // v2.70.0 (Clement's remark 1): the panel front's own grain - a
    // 96 x 96 deterministic speckle tile baked once at construction
    // and tiled over the cream at paint time. Public for the harness
    const juce::Image& getGrainTexture() const { return grainTexture; }

    // the dynamics meters (v2.65.0, grown in v2.66.0): the SSL J's own
    // language, one IN, one OUT and one GR ladder per stage. The IN and
    // OUT ladders light from the bottom up on the stereo-linked RMS
    // levels their stages hear and leave behind; the GR ladder lights
    // from the top down as the section works. v2.70.0 (Clement's
    // remark 3): the GR scale is a plain LINEAR dB ladder - ONE DECIBEL
    // PER DIVISION on both stages, only the number of divisions
    // changes (36 on the expander, whose RANGE runs 0 to 36 since v2.72.0,
    // 20 on the
    // compressor) - and the engraved scale beside it carries the full
    // value lists below. The component polls the processor in its
    // paint, so the meters never hold state of their own - a fresh
    // paint is always the truth. Public so the offline harness can
    // name the modes and drive the degenerate-size paint path
    struct DynamicsMeter : public juce::Component
    {
        enum class Mode { input, output, gainReduction };

        // the ladder's stage - the expander and the compressor.
        // v2.90.0: the transient designer has left the ladder family:
        // its old BIPOLAR GR ladder lives on as the section's own
        // horizontal GrBarMeter, and the IN/OUT duplicates are gone
        enum class Stage { expander, compressor };

        DynamicsMeter (CostesStrip& source, Mode meterMode, Stage stageSide,
                       float lowScaleDb, float highScaleDb,
                       int numLeds, float scaleMarkStep, const char* captionText)
            : owner (source), mode (meterMode), stage (stageSide),
              lowDb (lowScaleDb), highDb (highScaleDb),
              leds (numLeds), markStep (scaleMarkStep), caption (captionText) {}

        void paint (juce::Graphics& g) override;

        CostesStrip& owner;
        Mode mode;
        Stage stage;
        float lowDb;
        float highDb;
        int leds;
        float markStep;
        const char* caption;

        // v2.80.0 (Clement's remarks 4 and 5): the THRESHOLD bar - an
        // IN ladder may carry its stage's own threshold corner as a
        // red bar (the same 3.4 px weight as the GR ladder's peak-hold
        // bar), reading the parameter its THRESH knob drives. Null on
        // the OUT and GR ladders, which carry no threshold. Public so
        // the offline harness can ask a ladder where its bar reads
        std::function<float()> thresholdDb;

        // v2.70.0 (Clement's remark 3): the GR scales' engraved values -
        // more numbers than the old three anchors, drawn positive
        // beside the ladder. One list per stage, public for the harness
        // v2.74.0 (Clement's request): the value lists read at the
        // ladders' own milestones now - the expander's tiers and their
        // midpoints (0, 3, 6, 12, 18, 27, 36), the compressor's the
        // same idea (0, 3, 6, 9, 12, 18)
        static constexpr int expGrMarks[7]  { 0, 3, 6, 12, 18, 27, 36 };
        static constexpr int compGrMarks[6] { 0, 3, 6, 9, 12, 18 };

        // v2.72.0 (Clement's request): the GR ladders' THREE-TIER
        // geometry - each tier owns exactly a THIRD of the ladder. The
        // expander reads 0-6, 6-18 and 18-36 dB (the range itself now
        // tops at 36 dB), the compressor 0-3, 3-9 and 9-18 (v2.73.0,
        // Clement's request - the compressor's full-scale reduction is
        // 18 dB now). The
        // divisions stay one dB - only their heights change, so the
        // working zone at the top keeps the room. Pure and public so
        // the offline harness can pin the geometry: 0 dB is fraction
        // 0, the first tier boundary exactly 1/3, the second 2/3, and
        // the ladder's full scale 1.0
        static float grFractionForDb (float reductionDb, float spanDb,
                                      float tier1Db, float tier2Db)
        {
            const float third = 1.0f / 3.0f;

            if (reductionDb <= tier1Db)
                return third * reductionDb / tier1Db;

            if (reductionDb <= tier2Db)
                return third + third * (reductionDb - tier1Db)
                       / (tier2Db - tier1Db);

            const float below = juce::jmax (1.0e-4f, spanDb - tier2Db);
            return (2.0f / 3.0f) + third * (reductionDb - tier2Db) / below;
        }

        // v2.67.0 (Clement's remark): the peak hold - the reading's own
        // extreme (the loudest level on an IN or OUT ladder, the
        // deepest reduction on a GR ladder) held for 3 s, then a gentle
        // glide back towards the live reading. The editor repaints the
        // ladders at 30 Hz, so the state lives here and the paint
        // itself advances it. Public so the offline harness can ask a
        // ladder what it is holding
        float getPeakHoldDb() const { return holdReading; }

        float holdReading = 0.0f;
        double holdStampMs = 0.0;
        double holdLastMs = 0.0;
        bool holdSet = false;
    };

    // v2.90.0 (Clement's request): the transient designer's own
    // HORIZONTAL bipolar GR bar.  The section's IN and OUT ladders
    // are gone - the chain runs expander -> transient -> compressor,
    // so the neighbours already meter those samples - and the one
    // reading that belongs to the section alone, the signed gain
    // MODIFICATION, now owns the full width: a boost fills RIGHT of
    // centre in the meter green, a cut fills LEFT in the reduction
    // orange, the engraved scale runs -15 / +15 dB at five dB a
    // mark with zero dead centre, and a peak-hold mark rides the
    // track with the ladders' own discipline (3 s hold, 20 dB/s
    // glide back).  The component polls the processor in its paint,
    // so it holds only the peak-hold state - a fresh paint is
    // always the truth.  Public so the offline harness can pin its
    // bounds
    struct GrBarMeter : public juce::Component
    {
        explicit GrBarMeter (CostesStrip& source) : owner (source) {}

        void paint (juce::Graphics& g) override;

        // v2.94.0 (Clement's request): the ADD face is a BUTTON now -
        // one click on the digits folds their reading straight into
        // the WET GAIN knob, the gain staging in one move. The zone
        // is the digits' own strip - the plates' line right of the
        // PUNCH plate; the GR bar below keeps its own face
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseMove (const juce::MouseEvent& e) override;
        void applyTheAdd();

        CostesStrip& owner;

        // the peak-hold mark's state: the mark holds its magnitude
        // for three seconds from holdStampMs, then glides back
        // towards the live reading at 20 dB/s - the editor repaints
        // at 30 Hz, so the state lives here and the paint itself
        // advances it
        float holdReading = 0.0f;
        double holdStampMs = 0.0;
        double holdLastMs = 0.0;
        bool holdSet = false;

        // v2.91.0: the WET GAIN balance readout's display state.
        // v2.92.0 (Clement's request): the reading stopped dancing -
        // the display value LATCHES once a second (1 Hz), off the
        // paint's own 30 Hz pass. v2.95.0: the strip's own ~1 s
        // average feeds the latch - the smoothing moved to the
        // processor, so the digits and the click read the same
        // number
        float balLatch = 0.0f;
        double balLastLatchMs = 0.0;
    };

    // v2.93.0 (Clement's request): the compressor's ADD face - the
    // stage's own OUT-minus-IN delta, shown as the ACTION to take
    // (v2.93.1: the digits carry the balance's negation), engraved
    // just right of the COMP ON plate, level with it on the plates'
    // line, in the same 1 Hz-latched discipline as the PUNCH side's
    // own ADD face. Public so the offline harness can pin its bounds
    struct CompAddFace : public juce::Component
    {
        explicit CompAddFace (CostesStrip& source) : owner (source) {}

        void paint (juce::Graphics& g) override;

        // v2.94.0 (Clement's request): the same one-click gain
        // staging as the PUNCH side's face - a click folds the
        // digits into the compressor's own make-up knob. The whole
        // face is the button
        void mouseDown (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override;
        void applyTheAdd();

        CostesStrip& owner;

        // the same display state as the GrBarMeter's own readout:
        // the strip's own ~1 s average, LATCHED once a second so
        // the digits stand still instead of dancing (v2.95.0: the
        // smoothing lives in the processor now)
        float balLatch = 0.0f;
        double balLastLatchMs = 0.0;
    };

    // the six ladders in stage order - the expander's IN/OUT/GR trio
    // then the compressor's - for the offline test harness
    std::array<DynamicsMeter*, 6> getDynMeters()
    {
        return { &expInMeter, &expOutMeter, &expGrMeter,
                 &compInMeter, &compOutMeter, &compGrMeter };
    }

    // v2.90.0: the transient's horizontal GR bar for the offline harness
    GrBarMeter* getTransientGrBar()  { return &transGrBar; }

    // v2.93.0: the compressor's ADD face for the offline harness
    CompAddFace* getCompAddFace()  { return &compAddFace; }

    // v2.97.0 (Clement's request): the compressor's CHARACTER plates -
    // four engraved names riding the plates' line (FET, VCA, GLUE,
    // OPTO), one lit lamp between them. The strip's own VCA and the
    // GLUE feedback style are live; FET (v2.98.0) and OPTO (v2.99.0)
    // are engraved but not built yet, so their plates sit inert (no
    // click, a dimmed paint) - a promise, not a ghost button.
    // Public like ShelfModeToggle further down, so the harness can read
    // the caption - and the accessor's return type needs the full name
    // declared here, a return type is not inside the complete-class
    // context
    struct CompStyleToggle : public juce::ToggleButton
    {
        juce::String caption;

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    };

    // v2.97.0: the compressor's character plates for the offline harness
    CompStyleToggle* getCompStylePlate (int index)
    {
        return &compStylePlates[juce::jlimit (0, 3, index)];
    }

    // v2.73.0: the dynamics plates in stable order - the expander's
    // FAST ATK and LISTEN, then the compressor's - so the offline
    // harness can pin their stacked geometry
    std::array<juce::Button*, 4> getDynPlateButtons()
    {
        return { &expFastAttackButton, &expListenButton,
                 &compFastAttackButton, &compListenButton };
    }

    // v2.73.0 / v2.80.0 (Clement's remark 1): the two merged HPF / LPF
    // buttons, for the harness to pin their top-left corner beside the
    // knob they switch
    std::array<juce::Button*, 2> getCutPlateButtons()
    {
        return { &lowCutOnButton, &highCutOnButton };
    }

    // v2.79.0 (Clement's remark 5): the two dynamics ON/OFF plates -
    // the harness pins their width against their own widest words
    // ("COMP OFF" at 11 px bold), so the plate can never clip its
    // state into "COMP O..." again (a juce::Button*, like the cut
    // plates above - PlateToggle itself is defined below, past the
    // public section, so the name is not in reach here)
    std::array<juce::Button*, 2> getDynOnButtons()
    {
        return { &expOnButton, &compOnButton };
    }

    // v2.84.0 (Clement's remark 3): the phase-invert button, for the
    // harness (a juce::Button* - PhaseToggle is defined below)
    juce::Button* getPhaseButton()
    {
        return &phaseButton;
    }

    // v2.84.0 (Clement's remarks 4-6): the three image-section knobs,
    // in panel order - balance, width, mono maker
    std::array<CostesSlider*, 3> getImageKnobs()
    {
        return { &balanceKnob, &widthKnob, &monoMakerKnob };
    }

    // v2.84.0 (Clement's remark 1): the front-end captions, for the
    // harness' "TILT must be written" pin. v2.86.0: the tilt rides
    // the SHAPE OUT block now, its own caption word under its knob
    // again
    std::array<juce::Label*, 2> getGainCaptions()
    {
        return { &trimInLabel, &tiltLabel };
    }

    // v2.86.0 (Clement's request): the output column's own caption
    // words, in block order - the IMAGE block's three (balance,
    // width, mono maker), then the SHAPE OUT block's trim out
    std::array<juce::Label*, 4> getOutputColumnCaptions()
    {
        return { &balanceLabel, &widthLabel, &monoMakerLabel, &trimOutLabel };
    }

    // v2.87.0 (Clement's remark 4): the zone tiles in panel order -
    // the front end's three (trim in, LPF, HPF), the IMAGE block's
    // four (phase, balance, width, mono maker), the SHAPE OUT block's
    // three (tilt, drive with its OS plate, trim out) - for the
    // offline harness to pin their bounds, their tones and their
    // click-through
    // v2.87.0: the AMEK-style zone tiles. v2.89.0 (Clement's
    // verdict): the front end's three are gone - only the right
    // column's seven remain
    std::array<ZoneTile*, 7> getZoneTiles()
    {
        return { &phaseZone, &balanceZone, &widthZone, &monoMakerZone,
                 &tiltZone, &driveZone, &trimOutZone };
    }

private:
    CostesStrip& owner;

    CostesSlider trimInKnob, lowCutKnob, highCutKnob, tiltKnob, driveKnob;
    CostesSlider lowCutSlope, highCutSlope;
    CostesSlider trimOutKnob;   // v2.86.0 (Clement's request): the TRIM OUT
    juce::Label trimInLabel, tiltLabel, driveLabel;
    juce::Label balanceLabel, widthLabel, monoMakerLabel;   // v2.86.0: the IMAGE block's own words
    juce::Label trimOutLabel;                                // v2.86.0

    SectionIcon trimIcon   { SectionIcon::Icon::trim };
    SectionIcon lowCutIcon { SectionIcon::Icon::lowCut };
    SectionIcon highCutIcon{ SectionIcon::Icon::highCut };
    SectionIcon tiltIcon   { SectionIcon::Icon::tilt };
    SectionIcon driveIcon  { SectionIcon::Icon::drive };
    SectionIcon trimOutIcon { SectionIcon::Icon::trim };   // v2.86.0: the fader glyph, the trim's own

    // v2.87.0 (Clement's remark 4, the AMEK 9099's pastel zones): the
    // ten LOCAL background tiles - tone A behind the trim in, the
    // phase plate, the stereo width, the tilt and the trim out; tone
    // B behind the two cuts, the stereo balance, the mono maker and
    // the drive with its OS plate. Added first in the editor, so they
    // paint behind everything (highCut is the LPF, lowCut the HPF -
    // the knobs' own names)
    ZoneTile phaseZone;
    ZoneTile balanceZone;
    ZoneTile widthZone;
    ZoneTile monoMakerZone;
    ZoneTile tiltZone;
    ZoneTile driveZone;
    ZoneTile trimOutZone;

    // the EQ band glyphs (v2.62.0; re-cut in v2.70.0 and again in
    // v2.78.0, Clement's remarks 4 and 6): all four bands keep a free
    // glyph now, standing under the band's name in the row's margin
    // column - the two mid bells, and the two outer bands' own shapes,
    // which follow their BELL/SHELF plates (the follows member) and
    // repaint with them
    SectionIcon eqBand1Icon { SectionIcon::Icon::lowShelf };
    SectionIcon eqBand2Icon { SectionIcon::Icon::eq };
    SectionIcon eqBand3Icon { SectionIcon::Icon::eq };
    SectionIcon eqBand4Icon { SectionIcon::Icon::highShelf };

    // the EQ section (v2.61.0; v2.78.0, Clement's remarks 5 and 9):
    // eight knobs - a GAIN at the left of each band's row, a FREQ at
    // its right, no caption words under them any more - four band
    // captions, two bandwidth sliders (bands 2/3) and two bell/shelf
    // plates (bands 1/4, living in the margin column now)
    CostesSlider eq1HzKnob,   eq1GainKnob;
    CostesSlider eq2HzKnob,   eq2GainKnob;
    CostesSlider eq3HzKnob,   eq3GainKnob;
    CostesSlider eq4HzKnob,   eq4GainKnob;
    CostesSlider eq2OctSlider, eq3OctSlider;

    juce::Label eqBand1Label, eqBand2Label, eqBand3Label, eqBand4Label;

    // v2.70.0 (Clement's remark 1): the panel front's grain - a small
    // deterministic speckle tile, baked once in the constructor (the
    // same pixels every launch, a fixed xorshift seed) and tiled over
    // the cream at paint time. v2.80.0 (Clement's remark 8): the
    // alphas climbed from the 3-to-6 whisper to a visible felt finish
    // - the painted-steel grain of a real module, still fine noise.
    // Public for the harness
    juce::Image grainTexture { juce::Image::ARGB, 96, 96, true };

    // the dynamics section (v2.64.0, Clement's spec; reordered in
    // v2.66.0 and v2.70.0): ten knobs and four sidechain selectors -
    // the expander's THRESH, RANGE, RELEASE, RATIO and MIX, then the
    // compressor's THRESH, RATIO, RELEASE, GAIN and MIX, plus each
    // stage's SC HPF/SC LPF pair - each with its own caption label
    // and the two section captions. v2.70.0 (Clement's remark 6): the
    // ATTACK and LISTEN captions are gone - the plates say FAST ATK /
    // SLOW ATK and LISTEN loud enough on their own
    CostesSlider expThresholdKnob, expRangeKnob;
    CostesSlider expReleaseKnob,   expRatioKnob, expMixKnob;
    CostesSlider compThresholdKnob, compRatioKnob, compReleaseKnob;
    CostesSlider compGainKnob,     compMixKnob;
    CostesSlider expScHpfSlider, expScLpfSlider;
    CostesSlider compHpfSlider, compScLpfSlider;

    // v2.72.0 (Clement's request): the compressor's LINK selector - a
    // four-position slider (dual / 50% link / 80% link / link) that
    // blends the two channels' detectors towards the stereo-linked
    // one. v2.73.0 (Clement's remark 2): it moved out of the block's
    // header (the section label swallowed its clicks there) into the
    // bottom row's third cell. v2.74.0 (Clement's remark 1): with
    // FAST ATK and LISTEN moved up a row, the selector sits level
    // with the SC selectors - horizontally aligned with the SC LPF
    // at its left, its LINK caption under it the same way
    CostesSlider compLinkSlider;

    // v2.73.0 (Clement's choice, option B): the expander's KNEE
    // selector - six plates (hard / 4 / 8 / 12 / 16 / 20 dB) that
    // soften the static curve's corner at the threshold, parked at
    // "hard" so every session born before v2.73 keeps its classic
    // corner untouched. v2.74.0 (Clement's remark 1): it sits level
    // with the SC selectors now, horizontally aligned with the
    // expander's SC LPF at its left
    CostesSlider expKneeSlider;

    // v2.88.0 (Clement's spec, roadmap C4): the TRANSIENT DESIGNER's
    // controls - the ATTACK, SUSTAIN, GAIN and MIX knobs with their
    // four caption words, and the section's own SC HPF / SC LPF
    // selectors (the same legal-corner selectors the two dynamics
    // stages ride). v2.89.0: the SC pair is gone (Clement's verdict -
    // a bad idea); the detector hears the full band. The PUNCH plate
    // (the section's master switch) joins the CLIPPER plate at the
    // top of the block
    CostesSlider transAttackKnob, transSustainKnob;
    CostesSlider transGainKnob, transMixKnob;
    juce::Label transAttackLabel, transSustainLabel;
    juce::Label transGainLabel, transMixLabel;

    juce::Label expThresholdLabel, expRangeLabel;
    juce::Label expReleaseLabel,   expRatioLabel,  expMixLabel;
    juce::Label compThresholdLabel, compRatioLabel, compReleaseLabel;
    juce::Label compGainLabel, compMixLabel;
    // v2.80.0 (Clement's remark 7): the EXPANDER and COMPRESSOR
    // section titles are gone - the stages' own EXP / COMP plates
    // name them, and the header band's height belongs to the knobs

    // v2.75.0 (Clement's request): the reserved slots the future
    // builds will fill - the TRANSIENT stage's slot between the two
    // dynamics halves, and the output chain's sections down column
    // 3. v2.86.0 (Clement's request): column 3 reads as TWO engraved
    // blocks now - IMAGE at the top (the phase plate and the image
    // knobs), SHAPE OUT under it (the tilt, the drive and the new
    // trim out) - the five little reserved frames are gone
    juce::Rectangle<int> transientFrame;
    std::array<juce::Rectangle<int>, 2> reservedFrames {};

    // v2.68.0 (Clement's remark): every glyph of the dynamics sections
    // is gone - the two stages read through their captions, their knob
    // colours and their ladders alone

    // the oversampling switch: a ToggleButton that paints its own plate
    // (lit amber + LED when on, dark + dim LED when off) - the default
    // checkbox tick was too quiet to read (v2.57.0, Clement's remark 2).
    // v2.61.0: the caption is a member now, so the EQ's global on/off
    // plate speaks the same plate language ("EQ ON" / "EQ OFF")
    struct PlateToggle : public juce::ToggleButton
    {
        juce::String caption;

        // v2.90.0: an optional clip flag for the plate's LED - when
        // the plate is ON and the flag returns true the LED burns
        // saturated red with a red glow (the CLIPPER plate's one
        // second peak-hold) instead of the steady amber
        std::function<bool()> clipSignal;

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    };

    // v2.97.0: the four character plates themselves - the engraved
    // CompStyleToggle moved up to the public harness section (the
    // accessor's return type needs the declared name up there)
    CompStyleToggle compStylePlates[4];
    PlateToggle eqOnButton;   // v2.61.0
    PlateToggle transClipButton;   // v2.88.0: the transient's CLIP plate
    PlateToggle punchOnButton;   // v2.89.0: the PUNCH plate, lit by default

public:
    // the band-1/4 shape switch (v2.61.0): lit "BELL" or dark "SHELF",
    // the same plate language as the cut on/off bars. v2.70.0
    // (Clement's remark 9): the plate carries its shape INSIDE the
    // button - the bell glyph beside the word BELL, the band's own
    // shelf glyph beside SHELF. shelfGlyph says which shelf the band
    // speaks (lowShelf on band 1, highShelf on band 4). Public like
    // SectionIcon and DynamicsMeter, so the harness can read the
    // shelfGlyph the way it reads a glyph type or a ladder's LEDs.
    // v2.80.0 (Clement's remark 1): the cuts' merged HPF / LPF button
    // speaks through this same plate - cutWord pins the word the
    // button spells in BOTH states ("HPF" / "LPF"), and only the
    // light follows the state, exactly the BELL/SHELF language
    struct ShelfModeToggle : public juce::ToggleButton
    {
        SectionIcon::Icon shelfGlyph = SectionIcon::Icon::lowShelf;

        // v2.80.0: empty on the EQ's own BELL/SHELF plates (the word
        // still follows the state there), "HPF" / "LPF" on the cuts'
        // merged buttons (the word never changes, only the light)
        juce::String cutWord;

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    };

private:
    ShelfModeToggle eq1ModeButton, eq4ModeButton;

    // the compressor's attack plate (v2.64.0): lit amber "FAST ATK" (a
    // 3 ms attack that clamps a transient almost before it lands) or
    // dark "SLOW ATK" (the classic 30 ms that lets a drum's point
    // through) - the same plate language as the BELL/SHELF switch.
    // v2.70.0 (Clement's remark 6): the word ATTACK left the caption
    // under the plate and joined the plate's own text
    struct FastAtkToggle : public juce::ToggleButton
    {
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    };

    FastAtkToggle compFastAttackButton;
    FastAtkToggle expFastAttackButton;   // v2.66.0: the gate's own plate

    // the LISTEN plates (v2.66.0, Clement's spec): one push per stage -
    // lit amber with a live LED while the strip's voice is its own
    // sidechain (the filtered signal the detector hears), dark with a
    // dead LED while the programme plays. The same plate language as
    // the BELL/SHELF and FAST/SLOW switches
    struct ListenToggle : public juce::ToggleButton
    {
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    };

    ListenToggle expListenButton, compListenButton;

    // the cuts' merged HPF / LPF buttons (v2.80.0, Clement's remark 1):
    // the old ON/OFF plate and the old caption word are ONE button
    // now - a ShelfModeToggle that says "HPF" / "LPF" in both states,
    // lit amber with a live LED when the filter processes, black with
    // a dead LED when it is bypassed, riding the cell's top-left
    // corner where the caption word used to stand
    ShelfModeToggle lowCutOnButton, highCutOnButton;

    // the IMAGE section's own plate (v2.84.0, Clement's remark 3):
    // the phase button carries the empty-set symbol - the polarity
    // sign, drawn as the vector circle-and-slash itself - lit amber
    // with a live LED while the programme is inverted, black with a
    // dead LED while it plays normal, the same plate language as
    // every other switch on the panel
    struct PhaseToggle : public juce::ToggleButton
    {
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    };

    PhaseToggle phaseButton;

    // the IMAGE section's knobs (v2.84.0, Clement's remarks 4 to 6):
    // the stereo balance, the stereo width and the mono maker, each
    // riding under its engraved name inside its own reserved frame
    CostesSlider balanceKnob, widthKnob, monoMakerKnob;

    // the dynamics' own on/off plates (v2.64.0): the same "NAME ON" /
    // "NAME OFF" plates as the EQ's global switch, one per half
    PlateToggle expOnButton, compOnButton;

    // the dynamics meters' instances (v2.65.0, grown in v2.66.0) - the
    // public struct and the harness accessor sit above. Each stage
    // carries its IN and OUT pair over 70 dB (marks every 20) and its
    // GR ladder on the THREE-TIER scale (v2.72.0, recut in v2.73.0):
    // one dB per division as ever, each tier owning exactly a third
    // - 36 LEDs over the expander's 0 to 36 range (v2.72.0), 18 over
    // the compressor's 0 to 18 (v2.73.0, Clement's request) - with
    // the full value lists engraved at the ladder's right
    // (expGrMarks / compGrMarks). v2.73.0 (Clement's remark 1): only
    // the IN ladder engraves the shared scale now - the OUT meter's
    // own copy is gone, the one series serves the tightened pair.
    // v2.74.0 (Clement's remark 3): the shared scale engraves EVERY
    // ten dB (step 10, was 20) - 0 down to the -70 floor
    DynamicsMeter expInMeter  { owner, DynamicsMeter::Mode::input,
                                DynamicsMeter::Stage::expander, -70.0f, 0.0f, 14, 10.0f, "IN" };
    DynamicsMeter expOutMeter { owner, DynamicsMeter::Mode::output,
                                DynamicsMeter::Stage::expander, -70.0f, 0.0f, 14, 10.0f, "OUT" };
    DynamicsMeter expGrMeter  { owner, DynamicsMeter::Mode::gainReduction,
                                DynamicsMeter::Stage::expander, -36.0f, 0.0f, 36, 1.0f, "GR" };
    DynamicsMeter compInMeter { owner, DynamicsMeter::Mode::input,
                                DynamicsMeter::Stage::compressor, -70.0f, 0.0f, 14, 10.0f, "IN" };
    DynamicsMeter compOutMeter { owner, DynamicsMeter::Mode::output,
                                DynamicsMeter::Stage::compressor, -70.0f, 0.0f, 14, 10.0f, "OUT" };
    DynamicsMeter compGrMeter { owner, DynamicsMeter::Mode::gainReduction,
                                DynamicsMeter::Stage::compressor, -18.0f, 0.0f, 18, 1.0f, "GR" };
    // v2.90.0: the transient designer's single meter - the
    // horizontal bipolar GR bar replacing the v2.89 trio
    GrBarMeter transGrBar { owner };
    CompAddFace compAddFace { owner };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> trimInAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> trimOutAttachment;   // v2.86.0
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> phaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> balanceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> widthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> monoMakerAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCutSlopeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highCutSlopeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tiltAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment;
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
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> expFastAttackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> expReleaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> expRatioAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> expMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> expScHpfAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> expScLpfAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> expListenAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> compOnAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> compThresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> compRatioAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> compReleaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> compFastAttackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> compGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> compMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> compHpfAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> compScLpfAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> compListenAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> compLinkAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> expKneeAttachment;

    // the transient designer's attachments (v2.88.0): six sliders and
    // the CLIP plate wired to the APVTS
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> transAttackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> transSustainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> transGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> transMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> transClipAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> punchAttachment;   // v2.89.0

    // SSL-style knobs: cream bezel, dark disc, white pointer line, tick
    // marks around the cap and a warm value arc that shows the position
    //
    // The glyph-aware readout label itself stands in the PUBLIC
    // section above (v2.81.0) - only this look-and-feel's factory
    // creates it
    struct StripLookAndFeel : public juce::LookAndFeel_V4
    {
        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                               float sliderPos, float rotaryStartAngle,
                               float rotaryEndAngle, juce::Slider&) override;

        // v2.80.0 (Clement's remark 6): a CostesSlider that carries a
        // readout glyph gets the glyph-aware label; every other slider
        // keeps JUCE's own. v2.81.0 (Clement's remark 3): a slider that
        // speaks a smaller readout gets its font applied HERE - the
        // factory is the one place every (re)creation of the box
        // passes through, so the size can never drift back
        juce::Label* createSliderTextBox (juce::Slider& slider) override
        {
            auto* asCostes = dynamic_cast<CostesSlider*> (&slider);

            if (asCostes != nullptr)
            {
                // v2.82.0 (Clement's remark 1): the glyph label now
                // copies the slider's OWN textBoxTextColourId - exactly
                // what JUCE's V2 factory does for its plain labels, and
                // the one thing our factory forgot: JUCE never copies
                // a slider colour onto the box itself (lookAndFeelChanged
                // just recreates it, bare), so a factory that stays
                // silent paints in the look-and-feel's DEFAULT - the
                // dark scheme's WHITE, the colour the SC values read
                // on screen in the v2.81.0 build. The colour is read
                // at EVERY (re)creation of the box, so a later
                // setColour lands too - colourChanged recreates the
                // box through this very factory
                juce::Label* box = nullptr;

                if (asCostes->hasReadoutGlyph)
                {
                    auto* glyphBox = new GlyphReadoutLabel (asCostes->readoutGlyph);
                    glyphBox->setColour (juce::Label::textColourId,
                                         slider.findColour (juce::Slider::textBoxTextColourId));
                    box = glyphBox;
                }
                else
                {
                    box = juce::LookAndFeel_V4::createSliderTextBox (slider);
                }

                if (asCostes->readoutFontHeight > 0.0f)
                    box->setFont (juce::Font (juce::FontOptions (asCostes->readoutFontHeight)));

                return box;
            }

            return juce::LookAndFeel_V4::createSliderTextBox (slider);
        }
    };

    StripLookAndFeel look;
};
