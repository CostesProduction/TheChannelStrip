// ---------------------------------------------------------------------------
// src/CostesStrip.cpp - v2.55.0 (roadmap C1)
//
// The internal Costes channel strip's DSP and its cream SSL-style editor.
// See CostesStrip.h for the design notes.
// ---------------------------------------------------------------------------

#include "CostesStrip.h"

#include <cmath>

namespace
{
    // Butterworth corner for both cuts: maximally flat passband, the exact
    // textbook 12 dB/oct single-section slope the harness asserts against.
    constexpr double cutQ = 0.7071067811865476;

    // the palette of the mini panel: warm SSL cream, dark engraving
    const juce::Colour panelTop     (0xffe3d9c4);
    const juce::Colour panelBottom  (0xffc8bc9f);
    const juce::Colour engraving    (0xff2b2620);
    const juce::Colour knobBezel    (0xffb0a380);
    const juce::Colour knobDisc     (0xff23232a);
    const juce::Colour knobPointer  (0xfff5f2e8);
    const juce::Colour plateFill    (0xff26221b);
    const juce::Colour plateText    (0xffe8dcbf);
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
    trimInDb  = apvts.getRawParameterValue ("trimInDb");
    lowCutHz = apvts.getRawParameterValue ("lowCutHz");
    highCutHz = apvts.getRawParameterValue ("highCutHz");
    trimOutDb = apvts.getRawParameterValue ("trimOutDb");

    jassert (trimInDb != nullptr && lowCutHz != nullptr
             && highCutHz != nullptr && trimOutDb != nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout CostesStrip::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    using Range = juce::NormalisableRange<float>;

    // the two trims: +/- 24 dB, unity at the default
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "trimInDb", 1 }, "Trim In",
        Range (-24.0f, 24.0f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "trimOutDb", 1 }, "Trim Out",
        Range (-24.0f, 24.0f), 0.0f));

    // the cuts: parked wide open by default (20 Hz / 20 kHz), skewed so
    // the musically useful corner region spreads across the knob travel
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lowCutHz", 1 }, "Low Cut",
        Range (20.0f, 500.0f, 0.0f, 0.35f), 20.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "highCutHz", 1 }, "High Cut",
        Range (1000.0f, 20000.0f, 0.0f, 0.35f), 20000.0f));

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

void CostesStrip::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    // defensive floor: the RBJ formulas need a rate high enough for the
    // whole cut range (a hostile host can hand over anything)
    currentSampleRate = juce::jmax (8000.0, sampleRate);

    trimInGain.reset (currentSampleRate, 0.02);
    trimOutGain.reset (currentSampleRate, 0.02);
    trimInGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (trimInDb->load()));
    trimOutGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (trimOutDb->load()));

    lastLowCutHz = -1.0f;
    lastHighCutHz = -1.0f;

    for (auto& section : lowCut)
        section.reset();

    for (auto& section : highCut)
        section.reset();

    refreshCutCoefficients();
}

void CostesStrip::releaseResources()
{
    // the biquad states survive a pause; a fresh prepareToPlay resets them
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
    trimOutGain.setTargetValue (juce::Decibels::decibelsToGain (trimOutDb->load()));

    refreshCutCoefficients();

    float* channels[2] = { nullptr, nullptr };

    for (int ch = 0; ch < numChannels; ++ch)
        channels[ch] = buffer.getWritePointer (ch);

    for (int i = 0; i < numSamples; ++i)
    {
        // one smoothed gain pair per sample, shared by every channel
        const float gainIn = trimInGain.getNextValue();
        const float gainOut = trimOutGain.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float sample = channels[ch][i] * gainIn;

            sample = lowCut[ch].processSample (sample);
            sample = highCut[ch].processSample (sample);

            channels[ch][i] = sample * gainOut;
        }
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

    const auto setUpKnob = [this] (juce::Slider& knob, juce::Label& label,
                                   const juce::String& labelText)
    {
        knob.setSliderStyle (juce::Slider::Rotary);
        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 76, 18);
        knob.setColour (juce::Slider::rotarySliderFillColourId, plateText);
        knob.setColour (juce::Slider::rotarySliderOutlineColourId, knobBezel);
        knob.setColour (juce::Slider::textBoxTextColourId, engraving);
        knob.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0x00000000));
        knob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0x00000000));
        addAndMakeVisible (knob);

        label.setText (labelText, juce::dontSendNotification);
        label.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, engraving);
        addAndMakeVisible (label);
    };

    setUpKnob (trimInKnob, trimInLabel, "TRIM IN");
    setUpKnob (lowCutKnob, lowCutLabel, "LOW CUT");
    setUpKnob (highCutKnob, highCutLabel, "HIGH CUT");
    setUpKnob (trimOutKnob, trimOutLabel, "TRIM OUT");

    trimInAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "trimInDb", trimInKnob);
    lowCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "lowCutHz", lowCutKnob);
    highCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "highCutHz", highCutKnob);
    trimOutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "trimOutDb", trimOutKnob);

    setSize (640, 170);
}

CostesStripEditor::~CostesStripEditor()
{
    // detach the knobs before the LookAndFeel member goes away
    trimInAttachment.reset();
    lowCutAttachment.reset();
    highCutAttachment.reset();
    trimOutAttachment.reset();

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
    auto plate = getLocalBounds().removeFromTop (24).reduced (170, 3);
    g.setColour (plateFill);
    g.fillRoundedRectangle (plate.toFloat(), 4.0f);
    g.setColour (plateText);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.drawText ("COSTES CHANNEL STRIP", plate, juce::Justification::centred);
}

void CostesStripEditor::resized()
{
    auto area = getLocalBounds();

    area.removeFromTop (24);  // the name plate
    area.removeFromBottom (4);

    // four equal columns: knob above, caption below
    const int columnWidth = area.getWidth() / 4;

    for (int i = 0; i < 4; ++i)
    {
        auto column = area.removeFromLeft (columnWidth).reduced (6, 2);

        auto* knob = &trimInKnob;
        auto* caption = &trimInLabel;

        if (i == 1)      { knob = &lowCutKnob;  caption = &lowCutLabel; }
        else if (i == 2) { knob = &highCutKnob; caption = &highCutLabel; }
        else if (i == 3) { knob = &trimOutKnob; caption = &trimOutLabel; }

        auto captionBounds = column.removeFromBottom (18);
        caption->setBounds (captionBounds);
        knob->setBounds (column);
    }
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

    // cream bezel ring
    g.setColour (knobBezel);
    g.fillEllipse (centre.x - radius, centre.y - radius,
                   radius * 2.0f, radius * 2.0f);

    // dark aluminium disc
    const float inner = radius * 0.80f;
    g.setColour (knobDisc);
    g.fillEllipse (centre.x - inner, centre.y - inner,
                   inner * 2.0f, inner * 2.0f);

    // white pointer line, SSL style
    const float pointerLength = radius * 0.62f;
    const auto tip = juce::Point<float> (centre.x + std::sin (angle) * pointerLength,
                                         centre.y - std::cos (angle) * pointerLength);

    g.setColour (knobPointer);
    g.drawLine (juce::Line<float> (centre, tip), radius * 0.10f);
}
