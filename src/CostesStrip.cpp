// ---------------------------------------------------------------------------
// src/CostesStrip.cpp - v2.59.0 (roadmap C2)
//
// The internal Costes channel strip's DSP and its cream SSL-style editor.
// See CostesStrip.h for the design notes.
// ---------------------------------------------------------------------------

#include "CostesStrip.h"

#include <array>
#include <cmath>

namespace
{
    // Butterworth quality factors for the cascaded cut sections. An order-N
    // Butterworth factors into second-order sections whose Q values are
    // 1 / (2 cos ((2j - 1) pi / 2N)) when N is even and
    // 1 / (2 cos (j pi / N)) when N is odd (one first-order section carries
    // the odd pole); the values below are the textbook ones (N = 2: 0.7071,
    // N = 4: 0.5412 / 1.3066, N = 16 tops at 5.1011 - comfortably stable in
    // float32 direct form I). The cascades sit at exactly -3.01 dB at the
    // corner for every order, the maximally-flat Butterworth property.
    constexpr float shelfQ = 0.7071067811865476f;

    // the Q of the second-order section with the given 0-based index in an
    // order-N Butterworth cascade (the first-order section of an odd order
    // is NOT one of these - it is built separately, prewarped)
    double butterworthQ (int order, int sectionIndex)
    {
        const int j = sectionIndex + 1;
        const double angle = (order % 2 == 0)
            ? (2.0 * (double) j - 1.0) * juce::MathConstants<double>::pi / (2.0 * (double) order)
            : (double) j * juce::MathConstants<double>::pi / (double) order;

        return 1.0 / (2.0 * std::cos (angle));
    }

    // v2.59.0: the slope knobs offer only the legal Butterworth orders
    // 1, 2, 3, 4, 6, 8 and 12 (6/12/18/24/36/48/72 dB/oct - Clement's
    // remark 3, nothing in between). This snaps any raw order value to
    // the nearest legal one, ties resolving to the gentler slope. It
    // guards both the parameter's own snap and the DSP refresh, so a
    // stale or hand-edited session value can never build an illegal
    // cascade.
    float snapCutOrderToLegal (float v)
    {
        static const float legal[] = { 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f };

        float best = legal[0];
        float bestDist = std::abs (v - legal[0]);

        for (int i = 1; i < 7; ++i)
        {
            const float d = std::abs (v - legal[i]);

            // strict < keeps the earlier (gentler) entry on a tie:
            // 5 -> 4, 7 -> 6, 9 and 10 -> 8, 11 -> 12
            if (d < bestDist)
            {
                best = legal[i];
                bestDist = d;
            }
        }

        return best;
    }

    // the drive: the knob is decibels of input gain into the tanh, 0 to
    // +15.6 dB (the old normalised "amount" mapped the same sweep onto
    // g = 1 + slope * amount, up to 6x - and 20 log10 (6) = 15.56 dB, so
    // the top of the new knob is the top of the old one). amount below is
    // the normalised blend the stage has always used, recovered from the
    // dB reading: exactly 0 at 0 dB (bit-clean) and 1 at the top.
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
    // range spans the same knob travel, and the optional snap lambda lands
    // every knob position on a legal value (whole Hz for the low cut, tens
    // of Hz for the high cut - the host-facing conversion snaps on every
    // set, drag, typed value and automation alike)
    juce::NormalisableRange<float> makeLogRange (
        float minValue, float maxValue,
        juce::NormalisableRange<float>::ValueRemapFunction snapToLegalValue = {})
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
            },
            snapToLegalValue);
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
    lowCutOrder      = apvts.getRawParameterValue ("lowCutOrder");
    lowCutOn         = apvts.getRawParameterValue ("lowCutOn");
    highCutHz        = apvts.getRawParameterValue ("highCutHz");
    highCutOrder     = apvts.getRawParameterValue ("highCutOrder");
    highCutOn        = apvts.getRawParameterValue ("highCutOn");
    tiltDb           = apvts.getRawParameterValue ("tiltDb");
    driveDb          = apvts.getRawParameterValue ("driveDb");
    driveOversampled = apvts.getRawParameterValue ("driveOversampled");

    jassert (trimInDb != nullptr && lowCutHz != nullptr && lowCutOrder != nullptr
             && lowCutOn != nullptr && highCutHz != nullptr && highCutOrder != nullptr
             && highCutOn != nullptr && tiltDb != nullptr && driveDb != nullptr
             && driveOversampled != nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout CostesStrip::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // the shared dB text plumbing: one decimal place and a " dB" suffix on
    // the way out, and a parser that tolerates the French decimal comma on
    // the way in. The slider attachments wire these straight into the
    // editable readouts, so every knob shows and accepts the same format
    const auto dbText = [] (float value, int)
    {
        return juce::String (value, 1) + " dB";
    };

    const auto dbParse = [] (const juce::String& text)
    {
        return text.trim()
                   .replaceCharacter (',', '.')
                   .upToFirstOccurrenceOf (" ", false, true)
                   .getFloatValue();
    };

    // the trim: -24 to +12 dB in 0.1 dB steps, unity at the default
    // (v2.59.0, Clement's remark 4: the old -20/+10 sweep widened)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "trimInDb", 1 }, "Trim In",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (dbText)
            .withValueFromStringFunction (dbParse)));

    // the low cut corner: [16, 1600] Hz, logarithmic, snapped to whole Hz
    // by the range's own snap lambda (every drag position, typed value,
    // automation point and session restore land on an integer frequency)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lowCutHz", 1 }, "Low Cut",
        makeLogRange (16.0f, 1600.0f,
                      [] (float start, float end, float v)
                      {
                          return juce::jlimit (start, end, std::round (v));
                      }),
        16.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (juce::roundToInt (value)) + " Hz";
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                auto t = text.trim();

                if (t.endsWithIgnoreCase ("hz"))
                    t = t.dropLastCharacters (2).trim();

                return t.replaceCharacter (',', '.').getFloatValue();
            })));

    // the shared slope plumbing (v2.59.0): the knob speaks and accepts
    // dB/oct, and its legal values are only the Butterworth orders
    // 1, 2, 3, 4, 6, 8 and 12 - Clement's remark 3, nothing in between.
    // "off" is no longer a slope: the on/off plate owns the bypass, so a
    // typed "off" simply parks the slope at its 12 dB/oct default
    const auto slopeText = [] (float value, int)
    {
        return juce::String (juce::roundToInt (value * 6.0f)) + " dB/oct";
    };

    const auto slopeParse = [] (const juce::String& text)
    {
        if (text.trim().equalsIgnoreCase ("off"))
            return 2.0f;

        const double slope = text.trim()
                                    .replaceCharacter (',', '.')
                                    .upToFirstOccurrenceOf (" ", false, true)
                                    .getDoubleValue();

        return snapCutOrderToLegal ((float) (slope / 6.0));
    };

    // a linear 1..12 range that snaps every position to a legal order:
    // drags, typed values, automation points and session restores all
    // land on one of the seven legal cascades
    const auto slopeRange = []
    {
        return juce::NormalisableRange<float> (
            1.0f, 12.0f,
            [] (float start, float end, float normalised)
            {
                return juce::jlimit (start, end, normalised * (end - start) + start);
            },
            [] (float start, float end, float value)
            {
                return juce::jlimit (0.0f, 1.0f, (value - start) / (end - start));
            },
            [] (float start, float end, float value)
            {
                return snapCutOrderToLegal (juce::jlimit (start, end, value));
            });
    };

    // the low cut slope: a Butterworth order from 1 (6 dB/oct) to 12
    // (72 dB/oct), default 2. Sessions saved by v2.58.x held plain integer
    // orders here - the denormalised value matches one-to-one, so they
    // load unchanged (the one exception, order 0 "off", is migrated by
    // setStateInformation)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lowCutOrder", 1 }, "Low Cut Slope",
        slopeRange(), 2.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (slopeText)
            .withValueFromStringFunction (slopeParse)));

    // the low cut on/off plate (v2.59.0, Clement's remark 2): true when the
    // filter processes, false when it is fully bypassed. Default on
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "lowCutOn", 1 }, "Low Cut On", true,
        juce::AudioParameterBoolAttributes()
            .withStringFromValueFunction ([] (bool value, int)
            {
                return value ? juce::String ("on") : juce::String ("off");
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                const auto t = text.trim();

                return ! (t.equalsIgnoreCase ("off") || t == "0");
            })));

    // the high cut corner: [240, 24000] Hz, logarithmic, snapped to whole
    // tens of Hz (0.01 kHz); the readout is kHz with two decimals, and a
    // typed number without a unit is read as kHz - the unit the knob
    // itself displays
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "highCutHz", 1 }, "High Cut",
        makeLogRange (240.0f, 24000.0f,
                      [] (float start, float end, float v)
                      {
                          return juce::jlimit (start, end, 10.0f * std::round (v / 10.0f));
                      }),
        24000.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (value * 0.001f, 2) + " kHz";
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                auto t = text.trim();
                const bool kHz = t.endsWithIgnoreCase ("khz");

                if (t.endsWithIgnoreCase ("hz"))
                    t = t.dropLastCharacters (2).trim();

                const float parsed = t.replaceCharacter (',', '.').getFloatValue();

                return kHz ? 1000.0f * parsed : parsed;
            })));

    // the high cut slope: the same 6 to 72 dB/oct Butterworth selector
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "highCutOrder", 1 }, "High Cut Slope",
        slopeRange(), 2.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (slopeText)
            .withValueFromStringFunction (slopeParse)));

    // the high cut on/off plate (v2.59.0, Clement's remark 2): default on
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "highCutOn", 1 }, "High Cut On", true,
        juce::AudioParameterBoolAttributes()
            .withStringFromValueFunction ([] (bool value, int)
            {
                return value ? juce::String ("on") : juce::String ("off");
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                const auto t = text.trim();

                return ! (t.equalsIgnoreCase ("off") || t == "0");
            })));

    // the tilt (v2.57.0, Clement's remark 1): a full-band tilt, the
    // FabFilter style - a straight line of adjustable slope in the
    // log(f)/dB plane. The single knob is the +/- 12 dB gained or lost
    // BETWEEN 20 Hz AND 20 kHz; 0 dB at the default. The line pivots at
    // 632.4555 Hz, the geometric centre of the band (see
    // refreshTiltCoefficients for how the cascade draws it). 0.1 dB steps
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tiltDb", 1 }, "Tilt",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (dbText)
            .withValueFromStringFunction (dbParse)));

    // the drive (v2.58.0): decibels of input gain into the tanh, 0 (clean,
    // bit-transparent) to +15.6 dB (the full 6x saturation of the old
    // normalised knob - 20 log10 (6) = 15.56 dB), in 0.1 dB steps. A NEW
    // parameter id: sessions saved with the old "driveAmount" simply find
    // no value here and park at the 0 dB default
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "driveDb", 1 }, "Drive",
        juce::NormalisableRange<float> (0.0f, 15.6f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (dbText)
            .withValueFromStringFunction (dbParse)));

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

    lastLowCutHz    = -1.0f;
    lastHighCutHz   = -1.0f;
    lastLowCutOrder  = -1;
    lastHighCutOrder = -1;
    lastTiltDb      = -999.0f;

    for (auto& row : lowCut)
        for (auto& section : row)
            section.reset();

    for (auto& row : highCut)
        for (auto& section : row)
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

    const float low  = juce::jlimit (16.0f,   nyquistGuard, lowCutHz->load());
    const float high = juce::jlimit (240.0f, nyquistGuard, highCutHz->load());

    const int lowOrder  = (int) snapCutOrderToLegal (juce::jlimit (1.0f, 12.0f, lowCutOrder->load()));
    const int highOrder = (int) snapCutOrderToLegal (juce::jlimit (1.0f, 12.0f, highCutOrder->load()));

    // v2.59.0: the on/off plates. False parks the section count at zero,
    // which the process loop reads as a full bypass - the filter states
    // keep their coefficients but no sample passes through
    const bool lowOn  = lowCutOn->load()  > 0.5f;
    const bool highOn = highCutOn->load() > 0.5f;

    // cache guard: a step smaller than 1e-4 Hz between blocks does not
    // refresh the coefficients (a direct float compare used to trip
    // -Wfloat-equal; the epsilon keeps the cache behaviour identical).
    // The orders and the on/off plates ride the cache too: a change in
    // either alone must rebuild
    if (std::abs (low - lastLowCutHz) < 1.0e-4f
        && std::abs (high - lastHighCutHz) < 1.0e-4f
        && lowOrder == lastLowCutOrder && highOrder == lastHighCutOrder
        && lowOn == lastLowCutOn && highOn == lastHighCutOn)
        return;

    lastLowCutHz     = low;
    lastHighCutHz    = high;
    lastLowCutOrder  = lowOrder;
    lastHighCutOrder = highOrder;
    lastLowCutOn     = lowOn;
    lastHighCutOn    = highOn;

    // how many sections the process loop runs: an odd order's first-order
    // stage occupies one section slot, every remaining pole pair another
    // (and a plate switched off runs none at all)
    activeLowCutSections  = lowOn  ? (lowOrder  + 1) / 2 : 0;
    activeHighCutSections = highOn ? (highOrder + 1) / 2 : 0;

    // RBJ high-pass (the low cut): b0 = (1 + cos w0) / 2 ... For an odd
    // order the first section is a prewarped bilinear first-order stage
    // (t = tan (w0 / 2), |H (w0)| = 1 / sqrt (2) exactly) whose pole maps
    // the analog corner precisely; the remaining sections are RBJ
    // biquads with the Butterworth Q of their slot
    for (int j = 0; j < activeLowCutSections; ++j)
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;

        if (lowOrder % 2 == 1 && j == 0)
        {
            const double w0 = juce::MathConstants<double>::twoPi
                                  * (double) low / currentSampleRate;
            const double t = std::tan (w0 * 0.5);
            const double d = 1.0 + t;

            b0 = (float) (1.0 / d);
            b1 = (float) (-1.0 / d);
            a1 = (float) ((t - 1.0) / d);
        }
        else
        {
            const double q = (lowOrder % 2 == 1) ? butterworthQ (lowOrder, j - 1)
                                                 : butterworthQ (lowOrder, j);
            const double w0 = juce::MathConstants<double>::twoPi
                                  * (double) low / currentSampleRate;
            const double cs = std::cos (w0);
            const double alpha = std::sin (w0) / (2.0 * q);
            const double a0 = 1.0 + alpha;

            b0 = (float) ((1.0 + cs) * 0.5 / a0);
            b1 = (float) (-(1.0 + cs) / a0);
            b2 = (float) ((1.0 + cs) * 0.5 / a0);
            a1 = (float) (-2.0 * cs / a0);
            a2 = (float) ((1.0 - alpha) / a0);
        }

        for (auto& section : lowCut[j])
            section.setCoefficients (b0, b1, b2, a1, a2);
    }

    // RBJ low-pass (the high cut): b0 = (1 - cos w0) / 2 ... same odd-order
    // first-order treatment (t = tan (w0 / 2): b0 = b1 = t / d)
    for (int j = 0; j < activeHighCutSections; ++j)
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;

        if (highOrder % 2 == 1 && j == 0)
        {
            const double w0 = juce::MathConstants<double>::twoPi
                                  * (double) high / currentSampleRate;
            const double t = std::tan (w0 * 0.5);
            const double d = 1.0 + t;

            b0 = (float) (t / d);
            b1 = (float) (t / d);
            a1 = (float) ((t - 1.0) / d);
        }
        else
        {
            const double q = (highOrder % 2 == 1) ? butterworthQ (highOrder, j - 1)
                                                  : butterworthQ (highOrder, j);
            const double w0 = juce::MathConstants<double>::twoPi
                                  * (double) high / currentSampleRate;
            const double cs = std::cos (w0);
            const double alpha = std::sin (w0) / (2.0 * q);
            const double a0 = 1.0 + alpha;

            b0 = (float) ((1.0 - cs) * 0.5 / a0);
            b1 = (float) ((1.0 - cs) / a0);
            b2 = (float) ((1.0 - cs) * 0.5 / a0);
            a1 = (float) (-2.0 * cs / a0);
            a2 = (float) ((1.0 - alpha) / a0);
        }

        for (auto& section : highCut[j])
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

    // the drive constants, evaluated once per block. The knob reads in dB
    // of input gain; the normalised blend the stage has always used is
    // recovered from it (exactly 0 at 0 dB, exactly 1 at the top)
    const float driveDbValue = driveDb->load();
    const float drive = juce::jlimit (
        0.0f, 1.0f,
        (juce::Decibels::decibelsToGain (driveDbValue) - 1.0f) / driveSlope);
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

            for (int k = 0; k < activeLowCutSections; ++k)
                sample = lowCut[k][ch].processSample (sample);

            for (int k = 0; k < activeHighCutSections; ++k)
                sample = highCut[k][ch].processSample (sample);

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
        {
            auto tree = juce::ValueTree::fromXml (*xml);

            // v2.59.0 migration: sessions saved by v2.58.x and earlier hold
            // a plain integer cut order (0 meant "off" - the filter was
            // bypassed) and no on/off plate children. On the new slope
            // parameter the order range starts at 1, so a stored 0 would
            // load as an active 6 dB/oct filter. Migrate on the raw tree,
            // before it reaches the parameters: order 0 becomes a switched
            // OFF plate at the 12 dB/oct default slope, any other order
            // snaps to a legal one, and a fresh on/off child is appended
            // (the parameter's own snap also guards the orders - this
            // rewrites the stored values so a quick save keeps them)
            if (! tree.getChildWithProperty ("id", "lowCutOn").isValid())
            {
                for (int i = 0; i < 2; ++i)
                {
                    const bool isLow = (i == 0);
                    const juce::Identifier orderId (isLow ? "lowCutOrder" : "highCutOrder");
                    const juce::Identifier onId     (isLow ? "lowCutOn"     : "highCutOn");

                    auto orderChild = tree.getChildWithProperty ("id", orderId.toString());

                    if (! orderChild.isValid())
                        continue;

                    const float rawOrder = (float) (double) orderChild.getProperty ("value");
                    const bool wasOff = rawOrder < 0.5f;
                    const float migratedOrder = wasOff ? 2.0f
                        : snapCutOrderToLegal (juce::jlimit (1.0f, 12.0f, rawOrder));

                    orderChild.setProperty ("value", migratedOrder, nullptr);

                    auto onChild = juce::ValueTree (orderChild.getType());
                    onChild.setProperty ("id", onId.toString(), nullptr);
                    onChild.setProperty ("value", wasOff ? 0.0f : 1.0f, nullptr);
                    tree.appendChild (onChild, nullptr);
                }
            }

            apvts.replaceState (tree);
        }
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
        // 76 x 18 (v2.59.0, Clement's remark 1): the old 74 x 16 box sat
        // its descenders on the clip - the wider, taller box keeps every
        // readout fully inside the panel
        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 76, 18);
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
    setUpKnob (lowCutKnob,  lowCutLabel,  "LOW CUT",  16.0f);
    setUpKnob (highCutKnob, highCutLabel, "HIGH CUT", 24000.0f);
    setUpKnob (tiltKnob,    tiltLabel,    "TILT",     0.0f);
    setUpKnob (driveKnob,   driveLabel,   "DRIVE",    0.0f);

    // the slope selectors under the cut knobs (v2.58.0, Clement's remark;
    // v2.59.0): a horizontal slider from 6 to 72 dB/oct. "off" moved to
    // the on/off plate on the left, so the sweep holds only legal
    // Butterworth slopes - the readout is the parameter's own text
    // ("6 dB/oct" ... "72 dB/oct"), so the label never drifts from the DSP
    const auto setUpSlope = [this] (juce::Slider& slope)
    {
        slope.setSliderStyle (juce::Slider::LinearHorizontal);
        slope.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 72, 16);
        slope.setColour (juce::Slider::trackColourId, knobBezel);
        slope.setColour (juce::Slider::thumbColourId, valueArc);
        slope.setColour (juce::Slider::textBoxTextColourId, engraving);
        slope.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0x00000000));
        slope.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0x00000000));
        addAndMakeVisible (slope);
    };

    setUpSlope (lowCutSlope);
    setUpSlope (highCutSlope);

    // the oversampling switch, an SSL-style illuminated plate under the
    // drive knob (v2.57.0, Clement's remark 2): dark plate with a dead
    // LED and a dim caption when off, glowing amber plate with a lit LED
    // and the caption spelled "OS 2x ON" when on - the state reads at a
    // glance, where the old default checkbox tick was too quiet to see
    oversampleButton.setButtonText ("OS 2x");
    addAndMakeVisible (oversampleButton);

    // the cut on/off plates (v2.59.0, Clement's remark 2): the same SSL
    // plate language as the oversampling switch, vertical, one per cut.
    // They sit left of each cut's readout band and own the bypass - the
    // slope sweep itself starts at 6 dB/oct
    addAndMakeVisible (lowCutOnButton);
    addAndMakeVisible (highCutOnButton);

    trimInAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "trimInDb", trimInKnob);
    lowCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "lowCutHz", lowCutKnob);
    lowCutSlopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "lowCutOrder", lowCutSlope);
    highCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "highCutHz", highCutKnob);
    highCutSlopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "highCutOrder", highCutSlope);
    tiltAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "tiltDb", tiltKnob);
    driveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "driveDb", driveKnob);
    oversampleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        owner.getApvts(), "driveOversampled", oversampleButton);
    lowCutOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        owner.getApvts(), "lowCutOn", lowCutOnButton);
    highCutOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        owner.getApvts(), "highCutOn", highCutOnButton);

    setSize (880, 200);
}

CostesStripEditor::~CostesStripEditor()
{
    // detach the controls before the LookAndFeel member goes away
    trimInAttachment.reset();
    lowCutAttachment.reset();
    lowCutSlopeAttachment.reset();
    highCutAttachment.reset();
    highCutSlopeAttachment.reset();
    tiltAttachment.reset();
    driveAttachment.reset();
    oversampleAttachment.reset();
    lowCutOnAttachment.reset();
    highCutOnAttachment.reset();

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

    // five equal columns: knob above, caption below; the cut columns also
    // carry their slope slider under the caption, and the drive column
    // hosts the OS toggle under its caption
    const int columnWidth = area.getWidth() / 5;

    for (int i = 0; i < 5; ++i)
    {
        auto column = area.removeFromLeft (columnWidth).reduced (8, 2);

        auto* knob = &trimInKnob;
        auto* caption = &trimInLabel;
        juce::Slider* slope = nullptr;

        if (i == 1)      { knob = &lowCutKnob;  caption = &lowCutLabel;  slope = &lowCutSlope; }
        else if (i == 2) { knob = &highCutKnob; caption = &highCutLabel; slope = &highCutSlope; }
        else if (i == 3) { knob = &tiltKnob;    caption = &tiltLabel; }
        else if (i == 4) { knob = &driveKnob;   caption = &driveLabel; }

        const int knobHeight = (i == 4) ? column.getHeight() - 48
                             : (slope != nullptr) ? column.getHeight() - 44
                                                  : column.getHeight() - 20;

        // the cut on/off plate (v2.59.0): a 34 px strip on the left of
        // the readout band - the knob's own frequency readout and the
        // caption row - above the slope slider. The readout box stays
        // centred, so the plate never collides with it
        if (slope != nullptr)
        {
            auto plateArea = column;
            plateArea = plateArea.removeFromBottom (64).removeFromTop (40);

            auto plate = plateArea.removeFromLeft (34).reduced (2);
            (i == 1 ? lowCutOnButton : highCutOnButton).setBounds (plate);
        }

        if (slope != nullptr)
        {
            auto slopeBounds = column.removeFromBottom (24).reduced (6, 0);
            slope->setBounds (slopeBounds);
        }

        // the drive column hosts the switch below its caption
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

void CostesStripEditor::CutOnToggle::paintButton (juce::Graphics& g,
                                                  bool highlighted,
                                                  bool /*down*/)
{
    // the vertical sibling of the oversampling plate (v2.59.0, Clement's
    // remark 2): an SSL-style bar beside each cut's readout - the LED on
    // top, the state spelled out below. Amber and lit when the filter
    // processes, dark with a dead LED when it is bypassed
    const bool on = getToggleState();
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (on ? valueArc.withMultipliedAlpha (0.85f)
                    : plateFill);
    g.fillRoundedRectangle (bounds, 4.0f);

    g.setColour (on ? valueArc.brighter (0.12f)
                    : (highlighted ? knobBezel : engravingDim));
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    // the LED at the top: a lit amber dot with a soft glow when on, a
    // dark socket sunk into the plate when off
    const auto ledArea = bounds.removeFromTop (bounds.getHeight() * 0.45f);
    const auto led = ledArea.withSizeKeepingCentre (8.0f, 8.0f);

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

    // the caption spells the state: "ON" / "OFF", dark engraving on the
    // lit plate, dim cream on the dark one
    g.setColour (on ? engraving : engravingDim);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText (on ? "ON" : "OFF", bounds,
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
