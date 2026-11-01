// ---------------------------------------------------------------------------
// src/CostesStrip.cpp - v2.57.0 (roadmap C2)
//
// The internal Costes channel strip's DSP and its cream SSL-style editor.
// See CostesStrip.h for the design notes.
// ---------------------------------------------------------------------------

#include "CostesStrip.h"

#include <array>
#include <cmath>

namespace
{
    // Butterworth corner for both cuts: maximally flat passband, the exact
    // textbook 12 dB/oct single-section slope the harness asserts against.
    // The tilt shelves share it: each octave step of the cascade is a
    // smooth, ripple-free shelf transition, and the octave spacing lets
    // the neighbours' transitions cancel the staircase (v2.57.0).
    constexpr double cutQ    = 0.7071067811865476;
    constexpr float  shelfQ  = 0.7071067811865476f;

    // the drive: input gain g = 1 + slope * amount (up to 6x at full), a
    // tanh soft clip, and an auto-makeup that keeps the loudness of a
    // -12 dBFS sine untouched (the calibration reference). The blend
    // y = x + amount * (wet - x) is exactly bit-clean at amount == 0.
    constexpr float driveSlope    = 5.0f;
    constexpr float driveMakeupRef = 0.25f;

    // the palette of the mini panel: warm SSL cream, dark engraving, a
    // warm amber accent for the value arcs (the shell's "strip active"
    // title colour, so the strip and its shell read as one design)
    const juce::Colour panelTop     (0xffe3d9c4);
    const juce::Colour panelBottom  (0xffc8bc9f);
    const juce::Colour engraving    (0xff2b2620);
    const juce::Colour engravingDim (0xff5a5044);
    const juce::Colour knobBezel    (0xffb0a380);
    const juce::Colour knobDisc     (0xff23232a);
    const juce::Colour knobDiscLite (0xff42424e);
    const juce::Colour knobPointer  (0xfff5f2e8);
    const juce::Colour valueArc     (0xffe2a14f);
    const juce::Colour plateFill    (0xff26221b);
    const juce::Colour plateText    (0xffe8dcbf);
    const juce::Colour screwColour  (0xff8a7d63);

    // a true logarithmic sweep for the cut corners: every octave of the
    // range spans the same knob travel (the old skew approximation was
    // close but not exact; the roadmap asked for logarithmic)
    juce::NormalisableRange<float> makeLogRange (float minValue, float maxValue)
    {
        return juce::NormalisableRange<float> (
            minValue, maxValue,
            [] (float start, float end, float normalised)
            {
                return start * std::pow (end / start, normalised);
            },
            [] (float start, float end, float value)
            {
                return std::log (juce::jmax (value, start) / start)
                       / std::log (end / start);
            });
    }
}

// ---------------------------------------------------------------------------
// CostesStrip - the processor
// ---------------------------------------------------------------------------

CostesStrip::CostesStrip()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Main", juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Main", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "costesStrip", createLayout())
{
    trimInDb         = apvts.getRawParameterValue ("trimInDb");
    lowCutHz         = apvts.getRawParameterValue ("lowCutHz");
    highCutHz        = apvts.getRawParameterValue ("highCutHz");
    tiltDb           = apvts.getRawParameterValue ("tiltDb");
    driveAmount      = apvts.getRawParameterValue ("driveAmount");
    driveOversampled = apvts.getRawParameterValue ("driveOversampled");

    jassert (trimInDb != nullptr && lowCutHz != nullptr && highCutHz != nullptr
             && tiltDb != nullptr && driveAmount != nullptr
             && driveOversampled != nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout CostesStrip::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // the trim: +/- 24 dB, unity at the default
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "trimInDb", 1 }, "Trim In",
        juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f));

    // the cuts: parked wide open by default (20 Hz / 20 kHz), swept
    // logarithmically so the musically useful corner region spreads
    // evenly across the knob travel
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lowCutHz", 1 }, "Low Cut",
        makeLogRange (20.0f, 500.0f), 20.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "highCutHz", 1 }, "High Cut",
        makeLogRange (1000.0f, 20000.0f), 20000.0f));

    // the tilt (v2.57.0, Clement's remark 1): a full-band tilt, the
    // FabFilter style - a straight line of adjustable slope in the
    // log(f)/dB plane. The single knob is the +/- 12 dB gained or lost
    // BETWEEN 20 Hz AND 20 kHz; 0 dB at the default. The line pivots at
    // 632.4555 Hz, the geometric centre of the band (see
    // refreshTiltCoefficients for how the cascade draws it)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tiltDb", 1 }, "Tilt",
        juce::NormalisableRange<float> (-12.0f, 12.0f), 0.0f));

    // the drive amount: 0 (clean) to 1 (full tanh saturation), blended
    // against the dry signal so 0 is bit-exactly transparent
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "driveAmount", 1 }, "Drive",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // the 2x oversampling toggle for the drive stage (polyphase IIR
    // halfband: sub-sample latency, which the strip simply keeps at
    // zero reported latency)
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "driveOversampled", 1 }, "Oversample 2x", false));

    return layout;
}

bool CostesStrip::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // stereo <-> stereo like the consoles; native mono <-> mono is
    // accepted too (the shell asks for it on a mono track before
    // falling back to the stereo bridge)
    const auto& inputSet = layouts.getMainInputChannelSet();
    const auto& outputSet = layouts.getMainOutputChannelSet();

    return (inputSet == juce::AudioChannelSet::stereo()
            || inputSet == juce::AudioChannelSet::mono())
        && inputSet == outputSet;
}

void CostesStrip::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // defensive floor: the RBJ formulas need a rate high enough for the
    // whole cut range (a hostile host can hand over anything)
    currentSampleRate = juce::jmax (8000.0, sampleRate);

    trimInGain.reset (currentSampleRate, 0.02);
    trimInGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (trimInDb->load()));

    lastLowCutHz  = -1.0f;
    lastHighCutHz = -1.0f;
    lastTiltDb    = -999.0f;

    for (auto& section : lowCut)
        section.reset();

    for (auto& section : highCut)
        section.reset();

    for (auto& row : tiltShelves)
        for (auto& section : row)
            section.reset();

    // the oversampler is built here, never on the audio thread. A
    // generous maximum (any sane host block, then some) lets the whole
    // buffer ride a single up/down pass; a hostile block larger than
    // this degrades to the non-oversampled drive path for that block.
    preparedMaxSamples = juce::jmax (512, samplesPerBlock * 2, 8192);
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    oversampler->initProcessing ((size_t) preparedMaxSamples);

    refreshCutCoefficients();
    refreshTiltCoefficients();
}

void CostesStrip::releaseResources()
{
    // the biquad and oversampler states survive a pause; a fresh
    // prepareToPlay resets them
}

void CostesStrip::refreshCutCoefficients()
{
    // clamp both corners below Nyquist with headroom: a hostile rate
    // change must never feed the RBJ formulas an w0 at or past pi
    const float nyquistGuard = (float) (currentSampleRate * 0.45);

    const float low  = juce::jlimit (20.0f,   nyquistGuard, lowCutHz->load());
    const float high = juce::jlimit (500.0f, nyquistGuard, highCutHz->load());

    // cache guard: a step smaller than 1e-4 Hz between blocks does not
    // refresh the coefficients (a direct float compare used to trip
    // -Wfloat-equal; the epsilon keeps the cache behaviour identical)
    if (std::abs (low - lastLowCutHz) < 1.0e-4f
        && std::abs (high - lastHighCutHz) < 1.0e-4f)
        return;

    lastLowCutHz = low;
    lastHighCutHz = high;

    // RBJ high-pass (the low cut): b0 = (1 + cos w0) / 2 ...
    {
        const double w0 = juce::MathConstants<double>::twoPi * (double) low / currentSampleRate;
        const double cs = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * cutQ);
        const double a0 = 1.0 + alpha;

        const float b0 = (float) ((1.0 + cs) * 0.5 / a0);
        const float b1 = (float) (-(1.0 + cs) / a0);
        const float b2 = (float) ((1.0 + cs) * 0.5 / a0);
        const float a1 = (float) (-2.0 * cs / a0);
        const float a2 = (float) ((1.0 - alpha) / a0);

        for (auto& section : lowCut)
            section.setCoefficients (b0, b1, b2, a1, a2);
    }

    // RBJ low-pass (the high cut): b0 = (1 - cos w0) / 2 ...
    {
        const double w0 = juce::MathConstants<double>::twoPi * (double) high / currentSampleRate;
        const double cs = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * cutQ);
        const double a0 = 1.0 + alpha;

        const float b0 = (float) ((1.0 - cs) * 0.5 / a0);
        const float b1 = (float) ((1.0 - cs) / a0);
        const float b2 = (float) ((1.0 - cs) * 0.5 / a0);
        const float a1 = (float) (-2.0 * cs / a0);
        const float a2 = (float) ((1.0 - alpha) / a0);

        for (auto& section : highCut)
            section.setCoefficients (b0, b1, b2, a1, a2);
    }
}

void CostesStrip::refreshTiltCoefficients()
{
    const float tilt = tiltDb->load();

    // same epsilon cache discipline as the cuts (no -Wfloat-equal)
    if (std::abs (tilt - lastTiltDb) < 1.0e-4f)
        return;

    lastTiltDb = tilt;

    // v2.57.0 (Clement's remark 1): the tilt is now a straight line of
    // adjustable slope in the log(f)/dB plane - the full FabFilter-style
    // band tilt. A single biquad cannot draw that line: every shelf
    // flattens outside its own corner, and a 10-octave-wide ramp needs
    // support the whole way up. So the line is built as a staircase of
    // ten octave-spaced high shelves, each carrying the slope (tilt/10 dB
    // per octave), whose gradual transitions smooth each other into the
    // line: shelf k sits at pivot * 2^(k - 4.5), a half octave off the
    // pivot on either side, so the cascade is antisymmetric around it.
    //
    // The knob's meaning follows the spec: +/- 12 dB is the gain
    // difference BETWEEN 20 Hz AND 20 kHz (1.2 dB/octave at full tilt,
    // so about +/- 6 dB at the band edges). processBlock adds the flat
    // -tilt/2 dB that centres the antisymmetric staircase on the pivot;
    // together the response tracks
    //     G(f) = (tilt / 10) * log2 (f / 632.4555)  dB
    // to within +/- 0.25 dB from 40 Hz to 16 kHz (simulated, and pinned
    // by the harness at four octaves of probe points).
    //
    // The pivot is the geometric mean of the band edges: the line's dB
    // average over any octave-symmetric spectrum is exactly zero there,
    // so an octave-uniform (pink-like) spectrum - most music - keeps its
    // loudness at any tilt. JUCE owns the shelf formulas (the arrays are
    // { b0, b1, b2, a0, a1, a2 }, normalised by a0 in setShelfCoefficients),
    // and the corners stay under the Nyquist guard so the formulas never
    // see an omega past pi. At tilt == 0 every shelf degenerates to an
    // exact unity biquad, so the whole cascade is bit-transparent.
    const float gain = juce::Decibels::decibelsToGain (tilt / 10.0f);
    const double nyquistGuard = currentSampleRate * 0.45;

    for (int k = 0; k < numTiltShelves; ++k)
    {
        const float corner = (float) juce::jmin (
            tiltPivotHz * std::pow (2.0, (double) k - 4.5), nyquistGuard);

        const auto shelf = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (
            currentSampleRate, corner, shelfQ, gain);

        for (auto& section : tiltShelves[k])
            section.setShelfCoefficients (shelf);
    }
}

void CostesStrip::Biquad::setShelfCoefficients (const std::array<float, 6>& c)
{
    const float inv = 1.0f / c[3];

    setCoefficients (c[0] * inv, c[1] * inv, c[2] * inv, c[4] * inv, c[5] * inv);
}

float CostesStrip::Biquad::processSample (float input)
{
    const float output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

    x2 = x1;
    x1 = input;
    y2 = y1;
    y1 = output;

    return output;
}

void CostesStrip::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    // the shell feeds at most 2 channels (mono is bridged to stereo unless
    // the native mono layout was accepted, in which case it is 1)
    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);

    trimInGain.setTargetValue (juce::Decibels::decibelsToGain (trimInDb->load()));

    refreshCutCoefficients();
    refreshTiltCoefficients();

    // the tilt's flat -tilt/2 dB companion: the shelf cascade of ten
    // octave steps is centred on the pivot by removing its own +5-step
    // staircase offset (see refreshTiltCoefficients). It rides the same
    // per-block discipline as the shelves - a knob turn is a slow ramp -
    // and is exactly unity at tilt == 0
    const float tilt = tiltDb->load();
    const float tiltCompensation = juce::Decibels::decibelsToGain (-tilt * 0.5f);

    // the drive constants, evaluated once per block: the input gain, and
    // the auto-makeup that holds the loudness of the calibration sine
    const float drive = driveAmount->load();
    const float driveGain = 1.0f + driveSlope * drive;
    const float driveMakeup = driveMakeupRef / std::tanh (driveMakeupRef * driveGain);

    const bool useOS = drive > 0.0f && driveOversampled->load() > 0.5f
                        && numSamples <= preparedMaxSamples;

    float* channels[2] = { nullptr, nullptr };

    for (int ch = 0; ch < numChannels; ++ch)
        channels[ch] = buffer.getWritePointer (ch);

    // linear stages at the base rate: trim -> low cut -> high cut -> tilt
    for (int i = 0; i < numSamples; ++i)
    {
        // one smoothed gain per sample, shared by every channel
        const float gainIn = trimInGain.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float sample = channels[ch][i] * gainIn;

            sample = lowCut[ch].processSample (sample);
            sample = highCut[ch].processSample (sample);

            for (int k = 0; k < numTiltShelves; ++k)
                sample = tiltShelves[k][ch].processSample (sample);

            sample *= tiltCompensation;

            channels[ch][i] = sample;
        }
    }

    if (drive <= 0.0f)
        return;   // the blend term vanishes: drive 0 is bit-exactly clean

    if (! useOS)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
            {
                float& sample = channels[ch][i];
                const float wet = std::tanh (sample * driveGain) * driveMakeup;
                sample += drive * (wet - sample);
            }
    }
    else
    {
        // 2x oversampled drive: upsample, saturate the doubled stream,
        // downsample. The polyphase IIR halfband holds the images well
        // below -75 dB, so the harmonics that fold back at the base rate
        // (the aliasing a plain sample-rate tanh suffers from) vanish.
        const auto up = oversampler->processSamplesUp (juce::dsp::AudioBlock<const float> (buffer));

        const size_t upSamples = up.getNumSamples();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = up.getChannelPointer ((size_t) ch);

            for (size_t i = 0; i < upSamples; ++i)
            {
                const float x = data[i];
                const float wet = std::tanh (x * driveGain) * driveMakeup;
                data[i] = x + drive * (wet - x);
            }
        }

        juce::dsp::AudioBlock<float> outputBlock (buffer);
        oversampler->processSamplesDown (outputBlock);
    }
}

juce::AudioProcessorEditor* CostesStrip::createEditor()
{
    return new CostesStripEditor (*this);
}

void CostesStrip::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void CostesStrip::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }
}

// ---------------------------------------------------------------------------
// CostesStripEditor - the cream mini panel
// ---------------------------------------------------------------------------

CostesStripEditor::CostesStripEditor (CostesStrip& ownerProcessor)
    : AudioProcessorEditor (&ownerProcessor), owner (ownerProcessor)
{
    // the LookAndFeel member outlives its registration by an explicit
    // reset in the destructor (the JUCE LookAndFeel ownership rule)
    setLookAndFeel (&look);

    // the ergonomic knob setup shared by all five: horizontal-or-vertical
    // rotary drag, a wide 320 px travel (fine adjustments without
    // touching the keyboard), double-click back to the default, and an
    // editable readout under the cap for exact typed values
    const auto setUpKnob = [this] (juce::Slider& knob, juce::Label& label,
                                   const juce::String& labelText, float defaultValue)
    {
        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);

        juce::Slider::RotaryParameters rotary;
        rotary.startAngleRadians = juce::MathConstants<float>::pi * -0.75f;
        rotary.endAngleRadians   = juce::MathConstants<float>::pi *  0.75f;
        rotary.stopAtEnd = true;
        knob.setRotaryParameters (rotary);

        knob.setMouseDragSensitivity (320);
        knob.setDoubleClickReturnValue (true, defaultValue);
        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 74, 16);
        knob.setColour (juce::Slider::rotarySliderFillColourId, plateText);
        knob.setColour (juce::Slider::rotarySliderOutlineColourId, knobBezel);
        knob.setColour (juce::Slider::textBoxTextColourId, engraving);
        knob.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0x00000000));
        knob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0x00000000));
        addAndMakeVisible (knob);

        label.setText (labelText, juce::dontSendNotification);
        label.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, engraving);
        addAndMakeVisible (label);
    };

    setUpKnob (trimInKnob,  trimInLabel,  "TRIM IN",  0.0f);
    setUpKnob (lowCutKnob,  lowCutLabel,  "LOW CUT",  20.0f);
    setUpKnob (highCutKnob, highCutLabel, "HIGH CUT", 20000.0f);
    setUpKnob (tiltKnob,    tiltLabel,    "TILT",     0.0f);
    setUpKnob (driveKnob,   driveLabel,   "DRIVE",    0.0f);

    // the oversampling switch, an SSL-style illuminated plate under the
    // drive knob (v2.57.0, Clement's remark 2): dark plate with a dead
    // LED and a dim caption when off, glowing amber plate with a lit LED
    // and the caption spelled "OS 2x ON" when on - the state reads at a
    // glance, where the old default checkbox tick was too quiet to see
    oversampleButton.setButtonText ("OS 2x");
    addAndMakeVisible (oversampleButton);

    trimInAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "trimInDb", trimInKnob);
    lowCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "lowCutHz", lowCutKnob);
    highCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "highCutHz", highCutKnob);
    tiltAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "tiltDb", tiltKnob);
    driveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "driveAmount", driveKnob);
    oversampleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        owner.getApvts(), "driveOversampled", oversampleButton);

    setSize (880, 200);
}

CostesStripEditor::~CostesStripEditor()
{
    // detach the controls before the LookAndFeel member goes away
    trimInAttachment.reset();
    lowCutAttachment.reset();
    highCutAttachment.reset();
    tiltAttachment.reset();
    driveAttachment.reset();
    oversampleAttachment.reset();

    setLookAndFeel (nullptr);
}

void CostesStripEditor::paint (juce::Graphics& g)
{
    // the cream module front, slightly darker at the bottom like the
    // painted steel of an SSL 4000
    juce::ColourGradient gradient (panelTop, 0.0f, 0.0f,
                                   panelBottom, 0.0f, (float) getHeight(), false);
    g.setGradientFill (gradient);
    g.fillAll();

    g.setColour (plateFill);
    g.drawRect (getLocalBounds(), 2);

    // the name plate across the top
    auto plate = getLocalBounds().removeFromTop (26).reduced (260, 3);
    g.setColour (plateFill);
    g.fillRoundedRectangle (plate.toFloat(), 4.0f);
    g.setColour (plateText);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.drawText ("COSTES CHANNEL STRIP", plate, juce::Justification::centred);

    // the engraved section captions: TRIM | FILTERS | TONE | DRIVE
    const int width = getWidth();
    const int columnWidth = width / 5;
    const juce::Rectangle<int> captionRow (0, 26, width, 18);

    auto drawSection = [&] (const juce::String& text, int x, int w)
    {
        g.setColour (engravingDim);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText (text, juce::Rectangle<int> (x, captionRow.getY(), w, captionRow.getHeight()),
                    juce::Justification::centred);
    };

    drawSection ("TRIM",    0,                columnWidth);
    drawSection ("FILTERS", columnWidth,     columnWidth * 2);
    drawSection ("TONE",    columnWidth * 3,  columnWidth);
    drawSection ("DRIVE",   columnWidth * 4,  columnWidth);

    // a thin engraved rule under the caption row, broken around the
    // section boundaries like a real silkscreen
    g.setColour (engravingDim.withAlpha (0.5f));

    for (int i = 0; i < 5; ++i)
    {
        const int x1 = columnWidth * i + 14;
        const int x2 = columnWidth * (i + 1) - 14;
        g.drawLine ((float) x1, 44.0f, (float) x2, 44.0f, 1.0f);
    }

    // the four corner screws of the module front
    for (int i = 0; i < 4; ++i)
    {
        const int x = (i % 2 == 0) ? 11 : width - 15;
        const int y = (i < 2) ? 32 : getHeight() - 15;

        g.setColour (screwColour);
        g.fillEllipse ((float) x, (float) y, 5.0f, 5.0f);
        g.setColour (engraving.withAlpha (0.6f));
        g.drawLine ((float) x, (float) y + 4.5f, (float) x + 5.0f, (float) y + 0.5f, 1.0f);
    }
}

void CostesStripEditor::resized()
{
    auto area = getLocalBounds();

    area.removeFromTop (44);   // the name plate and the section captions
    area.removeFromBottom (6);

    // five equal columns: knob above, caption below; the drive column
    // also carries the OS toggle under its caption
    const int columnWidth = area.getWidth() / 5;

    for (int i = 0; i < 5; ++i)
    {
        auto column = area.removeFromLeft (columnWidth).reduced (8, 2);

        auto* knob = &trimInKnob;
        auto* caption = &trimInLabel;

        if (i == 1)      { knob = &lowCutKnob;  caption = &lowCutLabel; }
        else if (i == 2) { knob = &highCutKnob; caption = &highCutLabel; }
        else if (i == 3) { knob = &tiltKnob;    caption = &tiltLabel; }
        else if (i == 4) { knob = &driveKnob;   caption = &driveLabel; }

        // the drive column hosts the switch below its caption
        const int knobHeight = (i == 4) ? column.getHeight() - 48
                                       : column.getHeight() - 20;

        if (i == 4)
        {
            auto toggleBounds = column.removeFromBottom (26);
            toggleBounds = toggleBounds.withSizeKeepingCentre (juce::jmin (toggleBounds.getWidth(), 130), 26);
            oversampleButton.setBounds (toggleBounds);
        }

        auto captionBounds = column.removeFromBottom (18);
        caption->setBounds (captionBounds);

        knob->setBounds (column.removeFromTop (knobHeight));
    }
}

void CostesStripEditor::OversampleToggle::paintButton (juce::Graphics& g,
                                                       bool highlighted,
                                                       bool /*down*/)
{
    const bool on = getToggleState();
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    // the plate itself carries the state: lit amber when the 2x
    // oversampling is on, dark cream-panel shadow when it is off
    g.setColour (on ? valueArc.withMultipliedAlpha (0.85f)
                    : plateFill);
    g.fillRoundedRectangle (bounds, 5.0f);

    g.setColour (on ? valueArc.brighter (0.12f)
                    : (highlighted ? knobBezel : engravingDim));
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

    // the LED: a lit amber dot with a soft glow when on, a dark socket
    // sunk into the plate when off
    const auto ledArea = bounds.removeFromLeft (24.0f);
    const auto led = ledArea.withSizeKeepingCentre (9.0f, 9.0f);

    if (on)
    {
        g.setColour (valueArc.withMultipliedAlpha (0.40f));
        g.fillEllipse (led.expanded (3.0f));
        g.setColour (valueArc.brighter (0.35f));
        g.fillEllipse (led);
    }
    else
    {
        g.setColour (juce::Colour (0xff17150f));
        g.fillEllipse (led);
    }

    // the caption says the state out loud: "OS 2x ON" / "OS 2x OFF",
    // dark engraving on the lit plate, dim cream on the dark one
    g.setColour (on ? engraving : engravingDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText (on ? "OS 2x ON" : "OS 2x OFF", bounds,
                juce::Justification::centred);
}

void CostesStripEditor::StripLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                                            int x, int y, int w, int h,
                                                            float sliderPos,
                                                            float rotaryStartAngle,
                                                            float rotaryEndAngle,
                                                            juce::Slider&)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y,
                                                (float) w, (float) h);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;

    if (radius < 4.0f)
        return;

    const float angle = rotaryStartAngle
                        + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // eleven tick marks around the sweep, engraved like a silkscreen scale
    const float tickInner = radius * 0.92f;
    const float tickOuter = radius * 1.00f;

    g.setColour (juce::Colour (0xff8a7d63));

    for (int t = 0; t < 11; ++t)
    {
        const float tickAngle = rotaryStartAngle
                                + (rotaryEndAngle - rotaryStartAngle) * (float) t / 10.0f;
        const auto inner = juce::Point<float> (
            centre.x + std::sin (tickAngle) * tickInner,
            centre.y - std::cos (tickAngle) * tickInner);
        const auto outer = juce::Point<float> (
            centre.x + std::sin (tickAngle) * tickOuter,
            centre.y - std::cos (tickAngle) * tickOuter);

        g.drawLine (juce::Line<float> (inner, outer), 1.2f);
    }

    // the warm value arc: the knob position at a glance, in the same
    // amber as the shell's "strip active" title
    const float arcRadius = radius * 0.80f;

    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                       rotaryStartAngle, angle, true);
    g.setColour (valueArc);
    g.strokePath (arc, juce::PathStrokeType (radius * 0.06f));

    // cream bezel ring with a darker rim
    const float bezel = radius * 0.72f;

    g.setColour (knobBezel);
    g.fillEllipse (centre.x - bezel, centre.y - bezel, bezel * 2.0f, bezel * 2.0f);
    g.setColour (juce::Colour (0xff6f6650));
    g.drawEllipse (centre.x - bezel, centre.y - bezel, bezel * 2.0f, bezel * 2.0f, 1.0f);

    // dark aluminium disc with a soft radial sheen
    const float inner = radius * 0.62f;
    juce::ColourGradient disc (knobDiscLite, centre.x - inner * 0.4f, centre.y - inner * 0.4f,
                               knobDisc, centre.x + inner * 0.4f, centre.y + inner * 0.4f, false);
    g.setGradientFill (disc);
    g.fillEllipse (centre.x - inner, centre.y - inner, inner * 2.0f, inner * 2.0f);

    // white pointer line, SSL style
    const float pointerLength = radius * 0.52f;
    const auto tip = juce::Point<float> (centre.x + std::sin (angle) * pointerLength,
                                         centre.y - std::cos (angle) * pointerLength);

    g.setColour (knobPointer);
    g.drawLine (juce::Line<float> (centre, tip), radius * 0.09f);
}
