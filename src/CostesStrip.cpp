// ---------------------------------------------------------------------------
// src/CostesStrip.cpp - v2.64.0 (roadmap C3)
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

    // v2.60.0 (Clement's remark 1): the same seven legal orders as an
    // array, shared by the parameter's equal-step range below. The
    // index of the nearest legal order uses the same tie rule as the
    // snap - strict < keeps the earlier (gentler) entry - so the knob
    // normalisation and the value snap always agree
    constexpr int numLegalCutOrders = 7;
    const float legalCutOrders[numLegalCutOrders] =
        { 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f };

    int nearestCutOrderIndex (float v)
    {
        int best = 0;
        float bestDist = std::abs (v - legalCutOrders[0]);

        for (int i = 1; i < numLegalCutOrders; ++i)
        {
            const float d = std::abs (v - legalCutOrders[i]);

            if (d < bestDist)
            {
                best = i;
                bestDist = d;
            }
        }

        return best;
    }

    // v2.69.0 (Clement's remark): the sidechain corners are discrete
    // cranks, evenly spaced like the slope selectors - the HPF reads
    // off / 40 / 60 / 90 / 120 / 150 / 200 / 250 / 300 / 400 / 600 /
    // 800 / 1200 / 1600 Hz and the LPF 120 / 150 / 200 / 250 / 300 /
    // 400 / 600 / 800 / 1200 / 1600 / 3200 / 6400 / 12800 Hz / off.
    // The two "off" rest positions are the honest flat-band corners
    // the DSP already knows (16 Hz / 24000 Hz). An equidistant tie
    // lands on the gentler corner - the lower HPF, the higher LPF -
    // so a slip of the mouse never sharpens a filter
    constexpr int numScHpfCorners = 14;
    const float scHpfCorners[numScHpfCorners] =
    {
        16.0f, 40.0f, 60.0f, 90.0f, 120.0f, 150.0f, 200.0f,
        250.0f, 300.0f, 400.0f, 600.0f, 800.0f, 1200.0f, 1600.0f
    };

    constexpr int numScLpfCorners = 14;
    const float scLpfCorners[numScLpfCorners] =
    {
        120.0f, 150.0f, 200.0f, 250.0f, 300.0f, 400.0f, 600.0f,
        800.0f, 1200.0f, 1600.0f, 3200.0f, 6400.0f, 12800.0f, 24000.0f
    };

    int nearestScCornerIndex (const float* corners, int n, float v, bool higherOnTie)
    {
        int best = 0;
        float bestDist = std::abs (v - corners[0]);

        for (int i = 1; i < n; ++i)
        {
            const float d = std::abs (v - corners[i]);

            // strict < keeps the earlier entry on a tie unless the
            // gentler corner is the higher one (the LPF's rule)
            if (d < bestDist || (d == bestDist && higherOnTie))
            {
                best = i;
                bestDist = d;
            }
        }

        return best;
    }

    // v2.62.0 (Clement's remarks 2-3) / v2.63.0 (Clement's remark 1):
    // the mid bells' bandwidth knob offers exactly seven legal widths,
    // 0.4 / 0.7 / 1.0 / 1.5 / 2.0 / 3.0 / 4.0 octaves. This snaps any
    // raw width to the nearest legal one, ties resolving to the
    // narrower width (the same rule as the cut orders), so a stale or
    // hand-edited session value can never build an illegal width:
    // 0.2 -> 0.4, 0.85 -> 0.7, 2.5 -> 2.0
    constexpr int numLegalEqOctaves = 7;
    const float legalEqOctaves[numLegalEqOctaves] =
        { 0.4f, 0.7f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };

    int nearestEqOctaveIndex (float v)
    {
        int best = 0;
        float bestDist = std::abs (v - legalEqOctaves[0]);

        for (int i = 1; i < numLegalEqOctaves; ++i)
        {
            const float d = std::abs (v - legalEqOctaves[i]);

            // strict < keeps the earlier (narrower) entry on a tie
            if (d < bestDist)
            {
                best = i;
                bestDist = d;
            }
        }

        return best;
    }

    float snapEqOctavesToLegal (float v)
    {
        return legalEqOctaves[nearestEqOctaveIndex (v)];
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

    // v2.62.0 (Clement's remark 4): the EQ band caps speak SSL - the LF
    // band keeps its black aluminium disc (knobDisc above), the LO MID
    // band wears a pastel blue cap, the HI MID an off-white and the HF
    // a pastel red. Each knob carries its cap colour on the slider's
    // backgroundColourId, and drawRotarySlider builds the disc's sheen
    // around it (the pointer flips dark on the bright caps). v2.69.0
    // (Clement's remark): the pastel hues sat washed-out against the
    // cream front - the three caps speak at full saturation now
    const juce::Colour eqLoMidDisc (0xff6f9fd4);
    const juce::Colour eqHiMidDisc (0xffefe0bc);
    const juce::Colour eqHfDisc    (0xffcd7f6d);
    const juce::Colour valueArc     (0xffe2a14f);
    const juce::Colour plateFill    (0xff26221b);
    const juce::Colour plateText    (0xffe8dcbf);
    const juce::Colour screwColour  (0xff8a7d63);

    // v2.65.0: the dynamics meters' LEDs. The IN ladders speak the
    // console's green with an amber top (the over-region of a level
    // meter); the GR ladders speak the classic reduction orange - the
    // colour a real SSL's GR meter glows
    const juce::Colour meterGreen   (0xff4d9c55);
    const juce::Colour meterGrOrange (0xffd97a2b);

    // v2.68.0 (Clement's remark): the dynamics caps speak the EQ's own
    // SSL language - the expander wears the HI MID band's off-white,
    // the compressor the LO MID band's pastel blue, so the two stages
    // identify by colour at a glance. v2.69.0: they follow the EQ's
    // own saturated retint
    const juce::Colour expKnobDisc  (0xffefe0bc);
    const juce::Colour compKnobDisc (0xff6f9fd4);

    // a true logarithmic sweep for the cut corners: every octave of the
    // range spans the same knob travel, and the optional snap lambda lands
    // every knob position on a legal value (whole Hz for the low cut, tens
    // of Hz for the high cut - the host-facing conversion snaps on every
    // set, drag, typed value and automation alike)
    // v2.70.0 (Clement's remarks 4 and 5): the stepped range helpers.
    // twoSigFigHz snaps a frequency to the TWO-SIGNIFICANT-FIGURE grid
    // - 20, 21 ... 99, then 100, 110 ... 990, 1.0k, 1.1k ... 9.9k,
    // 10k, 11k ... - so every Hz knob on the panel speaks no more than
    // two significant digits, the classic switched-frequency feel of
    // a real console. Every range on the strip starts on the grid, so
    // the bounds and defaults never move
    float twoSigFigHz (float v)
    {
        if (v >= 10000.0f)  return 1000.0f * std::round (v / 1000.0f);
        if (v >= 1000.0f)   return 100.0f  * std::round (v / 100.0f);
        if (v >= 100.0f)    return 10.0f   * std::round (v / 10.0f);
        return std::round (v);
    }

    // the snap form the Hz ranges pass around: clamped to its range,
    // quantised to the two-significant-figure grid
    juce::NormalisableRange<float>::ValueRemapFunction twoSigFigSnap()
    {
        return [] (float start, float end, float v)
        {
            return juce::jlimit (start, end, twoSigFigHz (v));
        };
    }

    // the stepped linear dB range (v2.70.0, Clement's remarks 4 and 5):
    // the EQ gains step 0.5 dB, the tilt and the drive whole dBs. The
    // step quantises EVERY path - the drag (the slider snaps the value
    // through the range's own snap lambda), a typed readout, a session
    // restore and an automation point all land on the grid, because
    // both conversion directions quantise and the round trip through
    // them cannot leave it. A session saved by an earlier version
    // simply snaps to the nearest step on load
    juce::NormalisableRange<float> makeSteppedRange (float minValue, float maxValue,
                                                     float step)
    {
        const auto snapStep = [minValue, maxValue, step] (float v)
        {
            const float clamped = juce::jlimit (minValue, maxValue, v);
            const float snapped = minValue + step
                * std::round ((clamped - minValue) / step);
            return juce::jlimit (minValue, maxValue, snapped);
        };

        return juce::NormalisableRange<float> (
            minValue, maxValue,
            [snapStep, minValue, maxValue] (float, float, float normalised)
            {
                return snapStep (minValue + (maxValue - minValue) * normalised);
            },
            [snapStep, minValue, maxValue] (float, float, float value)
            {
                return (snapStep (value) - minValue) / (maxValue - minValue);
            },
            [snapStep] (float, float, float value)
            {
                return snapStep (value);
            });
    }

    juce::NormalisableRange<float> makeLogRange (
        float minValue, float maxValue,
        juce::NormalisableRange<float>::ValueRemapFunction snapToLegalValue = {})
    {
        // v2.70.0: a snapping log range quantises BOTH directions, so a
        // typed value, a session restore or an automation point lands on
        // the legal grid exactly like a drag does - the same guarantee
        // the discrete selectors (the SC corners, the slopes, the
        // octave widths) have always had through their own two-way maps
        return juce::NormalisableRange<float> (
            minValue, maxValue,
            [snapToLegalValue] (float start, float end, float normalised)
            {
                float v = start * std::pow (end / start, normalised);
                return (snapToLegalValue != nullptr) ? snapToLegalValue (start, end, v) : v;
            },
            [snapToLegalValue] (float start, float end, float value)
            {
                const float v = (snapToLegalValue != nullptr)
                    ? snapToLegalValue (start, end, value) : value;
                return std::log (juce::jmax (v, start) / start)
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

    eqOn    = apvts.getRawParameterValue ("eqOn");
    eq1Hz   = apvts.getRawParameterValue ("eq1Hz");
    eq1Gain = apvts.getRawParameterValue ("eq1Gain");
    eq1Bell = apvts.getRawParameterValue ("eq1Bell");
    eq2Hz   = apvts.getRawParameterValue ("eq2Hz");
    eq2Gain = apvts.getRawParameterValue ("eq2Gain");
    eq2Oct  = apvts.getRawParameterValue ("eq2Oct");
    eq3Hz   = apvts.getRawParameterValue ("eq3Hz");
    eq3Gain = apvts.getRawParameterValue ("eq3Gain");
    eq3Oct  = apvts.getRawParameterValue ("eq3Oct");
    eq4Hz   = apvts.getRawParameterValue ("eq4Hz");
    eq4Gain = apvts.getRawParameterValue ("eq4Gain");
    eq4Bell = apvts.getRawParameterValue ("eq4Bell");

    expOn          = apvts.getRawParameterValue ("expOn");
    expThreshold   = apvts.getRawParameterValue ("expThreshold");
    expRange       = apvts.getRawParameterValue ("expRange");
    expRelease     = apvts.getRawParameterValue ("expRelease");
    expRatio       = apvts.getRawParameterValue ("expRatio");
    expKnee        = apvts.getRawParameterValue ("expKnee");
    expMix         = apvts.getRawParameterValue ("expMix");
    expFastAttack  = apvts.getRawParameterValue ("expFastAttack");
    expScHpf       = apvts.getRawParameterValue ("expScHpf");
    expScLpf       = apvts.getRawParameterValue ("expScLpf");
    expListen      = apvts.getRawParameterValue ("expListen");
    compOn         = apvts.getRawParameterValue ("compOn");
    compThreshold  = apvts.getRawParameterValue ("compThreshold");
    compRatio      = apvts.getRawParameterValue ("compRatio");
    compRelease    = apvts.getRawParameterValue ("compRelease");
    compFastAttack = apvts.getRawParameterValue ("compFastAttack");
    compGain       = apvts.getRawParameterValue ("compGain");
    compMix        = apvts.getRawParameterValue ("compMix");
    compHpf        = apvts.getRawParameterValue ("compHpf");
    compScLpf     = apvts.getRawParameterValue ("compScLpf");
    compListen    = apvts.getRawParameterValue ("compListen");
    compLink      = apvts.getRawParameterValue ("compLink");

    jassert (trimInDb != nullptr && lowCutHz != nullptr && lowCutOrder != nullptr
             && lowCutOn != nullptr && highCutHz != nullptr && highCutOrder != nullptr
             && highCutOn != nullptr && tiltDb != nullptr && driveDb != nullptr
             && driveOversampled != nullptr
             && eqOn != nullptr && eq1Hz != nullptr && eq1Gain != nullptr
             && eq1Bell != nullptr && eq2Hz != nullptr && eq2Gain != nullptr
             && eq2Oct != nullptr && eq3Hz != nullptr && eq3Gain != nullptr
             && eq3Oct != nullptr && eq4Hz != nullptr && eq4Gain != nullptr
             && eq4Bell != nullptr
             && expOn != nullptr && expThreshold != nullptr && expRange != nullptr
             && expFastAttack != nullptr && expRelease != nullptr && expRatio != nullptr
             && expKnee != nullptr
             && expMix != nullptr && expScHpf != nullptr && expScLpf != nullptr
             && expListen != nullptr
             && compOn != nullptr && compThreshold != nullptr && compRatio != nullptr
             && compRelease != nullptr && compFastAttack != nullptr
             && compGain != nullptr && compMix != nullptr && compHpf != nullptr
             && compScLpf != nullptr && compListen != nullptr
             && compLink != nullptr);
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

    // the low cut corner: [16, 1600] Hz, logarithmic. v2.70.0
    // (Clement's remark 5): the knob steps on the TWO-SIGNIFICANT-
    // FIGURE grid - the discrete-knob feel of a real console's
    // switched HPF - and every drag position, typed value, automation
    // point and session restore land on it
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lowCutHz", 1 }, "Low Cut",
        makeLogRange (16.0f, 1600.0f, twoSigFigSnap()), 16.0f,
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

    // v2.60.0 (Clement's remark 1): the knob's seven settings are EQUALLY
    // SPACED. The range maps the legal orders 1, 2, 3, 4, 6, 8, 12 onto
    // the seven equal knob positions 0, 1/6 ... 1 - a value-linear 1..12
    // sweep used to huddle the weak slopes at the left end (1 and 2 sit
    // a tenth of the travel apart, 8 and 12 nearly a third). Ties snap
    // to the gentler slope exactly like the value snap, and sessions
    // stay exact: the APVTS tree stores the DENORMALISED order, and
    // every legal order round-trips through its normalised step with
    // no drift (a v2.59 session holding 4.0 loads back as 4.0)
    const auto slopeRange = []
    {
        return juce::NormalisableRange<float> (
            1.0f, 12.0f,
            [] (float /*start*/, float /*end*/, float normalised)
            {
                const int index = juce::jlimit (0, numLegalCutOrders - 1,
                                                juce::roundToInt (normalised
                                                    * (float) (numLegalCutOrders - 1)));
                return legalCutOrders[index];
            },
            [] (float /*start*/, float /*end*/, float value)
            {
                return (float) nearestCutOrderIndex (juce::jlimit (1.0f, 12.0f, value))
                         / (float) (numLegalCutOrders - 1);
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

    // the high cut corner: [240, 24000] Hz, logarithmic, on the same
    // two-significant-figure grid (v2.70.0, Clement's remark 5); the
    // readout is kHz with two decimals, and a typed number without a
    // unit is read as kHz - the unit the knob itself displays
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "highCutHz", 1 }, "High Cut",
        makeLogRange (240.0f, 24000.0f, twoSigFigSnap()), 24000.0f,
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

    // -----------------------------------------------------------------------
    // the EQ section (v2.61.0, Clement's spec): four SSL-style bands between
    // the cuts and the tilt. Band 1 (LF): bell or low shelf, 20-600 Hz,
    // default 100 Hz. Band 2 (LO MID): bell with a variable bandwidth in
    // octaves, 60-3000 Hz, default 400 Hz. Band 3 (HI MID): the same,
    // 500 Hz-12 kHz, default 1600 Hz. Band 4 (HF): bell or high shelf,
    // 600 Hz-18 kHz, default 7200 Hz. The bells sweep +/- 15 dB and read
    // exactly their gain at their centre; the mid bells' bandwidths run
    // 0.4 to 4.0 octaves in seven legal widths (v2.62.0, v2.63.0:
    // Clement's remarks); the shelves use the RBJ Q form with Q = 1/sqrt(2)
    // -----------------------------------------------------------------------

    // the global on/off plate: the section's true bypass. Sessions saved
    // before v2.61 find no EQ children at all and load these defaults
    // (the tree stores denormalised values, so a default IS the value)
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "eqOn", 1 }, "EQ On", true,
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

    // band 1 (LF): the corner sweeps 20 to 600 Hz, logarithmic, on the
    // two-significant-figure grid (v2.70.0, Clement's remark 4); the
    // readout speaks Hz and accepts it
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "eq1Hz", 1 }, "EQ 1 Frequency",
        makeLogRange (20.0f, 600.0f, twoSigFigSnap()), 100.0f,
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

    // the EQ gains: +/- 15 dB in 0.5 dB steps (v2.70.0, Clement's
    // remark 4 - a stepped gain knob, the SSL way), unity at the
    // default. A session saved by an earlier version snaps its 0.1 dB
    // values to the nearest half-dB on load
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "eq1Gain", 1 }, "EQ 1 Gain",
        makeSteppedRange (-15.0f, 15.0f, 0.5f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (dbText)
            .withValueFromStringFunction (dbParse)));

    // the band-1 shape: a plain bell (the default, lit plate) or the low
    // shelf - the corner knob drives either shape
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "eq1Bell", 1 }, "EQ 1 Mode", true,
        juce::AudioParameterBoolAttributes()
            .withStringFromValueFunction ([] (bool value, int)
            {
                return value ? juce::String ("bell") : juce::String ("shelf");
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                const auto t = text.trim();

                return ! (t.equalsIgnoreCase ("shelf") || t == "0");
            })));

    // band 2 (LO MID): the same two-significant-figure plumbing over
    // 60 Hz to 3 kHz
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "eq2Hz", 1 }, "EQ 2 Frequency",
        makeLogRange (60.0f, 3000.0f, twoSigFigSnap()), 400.0f,
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

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "eq2Gain", 1 }, "EQ 2 Gain",
        makeSteppedRange (-15.0f, 15.0f, 0.5f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (dbText)
            .withValueFromStringFunction (dbParse)));

    // the bell's bandwidth in octaves (Clement's spec: the Q is presented
    // as the number of octaves concerned). v2.62.0 (Clement's remarks
    // 2-3) / v2.63.0 (remark 1): the sweep now runs 0.4 to 4.0 octaves
    // and holds exactly seven legal widths, spread EQUALLY across the
    // knob travel - the positions are i/6, not linear in value - the
    // same equal-step construction the cut slopes use. A session width
    // outside the set snaps to the nearest legal one on load (ties to
    // the narrower)
    const auto eqOctaveRange = []
    {
        return juce::NormalisableRange<float> (
            0.4f, 4.0f,
            [] (float /*start*/, float /*end*/, float normalised)
            {
                const int index = juce::jlimit (0, numLegalEqOctaves - 1,
                                                juce::roundToInt (normalised
                                                    * (float) (numLegalEqOctaves - 1)));
                return legalEqOctaves[index];
            },
            [] (float /*start*/, float /*end*/, float value)
            {
                return (float) nearestEqOctaveIndex (juce::jlimit (0.4f, 4.0f, value))
                         / (float) (numLegalEqOctaves - 1);
            },
            [] (float start, float end, float value)
            {
                return snapEqOctavesToLegal (juce::jlimit (start, end, value));
            });
    };

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "eq2Oct", 1 }, "EQ 2 Width",
        eqOctaveRange(), 1.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                // v2.72.0 (Clement's remark 1): the readout speaks its
                // own sentence - the words never clip and the bell
                // glyph sits before the "Width"
                return "Width : " + juce::String (value, 1) + " octave";
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                auto t = text.trim();

                // v2.72.0: the parser reads the full sentence ("Width :
                // 1.4 octave") as happily as the bare "1.4" or the old
                // "1.40 oct" readout
                if (t.startsWithIgnoreCase ("width"))
                {
                    t = t.fromFirstOccurrenceOf (":", true, false)
                         .fromFirstOccurrenceOf (" ", true, false).trim();
                }

                if (t.endsWithIgnoreCase ("octave"))
                    t = t.dropLastCharacters (7).trim();
                else if (t.endsWithIgnoreCase ("oct"))
                    t = t.dropLastCharacters (3).trim();

                return t.replaceCharacter (',', '.').getFloatValue();
            })));

    // band 3 (HI MID): the same pair, kHz-spoken like the high cut,
    // both knobs on the two-significant-figure grid
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "eq3Hz", 1 }, "EQ 3 Frequency",
        makeLogRange (500.0f, 12000.0f, twoSigFigSnap()), 1600.0f,
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

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "eq3Gain", 1 }, "EQ 3 Gain",
        makeSteppedRange (-15.0f, 15.0f, 0.5f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (dbText)
            .withValueFromStringFunction (dbParse)));

    // band 3's own width (v2.62.0): the same seven equal-step widths,
    // and (v2.72.0) the same full-sentence readout as band 2's
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "eq3Oct", 1 }, "EQ 3 Width",
        eqOctaveRange(), 1.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                return "Width : " + juce::String (value, 1) + " octave";
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                auto t = text.trim();

                if (t.startsWithIgnoreCase ("width"))
                {
                    t = t.fromFirstOccurrenceOf (":", true, false)
                         .fromFirstOccurrenceOf (" ", true, false).trim();
                }

                if (t.endsWithIgnoreCase ("octave"))
                    t = t.dropLastCharacters (7).trim();
                else if (t.endsWithIgnoreCase ("oct"))
                    t = t.dropLastCharacters (3).trim();

                return t.replaceCharacter (',', '.').getFloatValue();
            })));

    // band 4 (HF): bell or high shelf, 600 Hz to 18 kHz, the knobs on
    // the same two-significant-figure grid
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "eq4Hz", 1 }, "EQ 4 Frequency",
        makeLogRange (600.0f, 18000.0f, twoSigFigSnap()), 7200.0f,
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

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "eq4Gain", 1 }, "EQ 4 Gain",
        makeSteppedRange (-15.0f, 15.0f, 0.5f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (dbText)
            .withValueFromStringFunction (dbParse)));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "eq4Bell", 1 }, "EQ 4 Mode", true,
        juce::AudioParameterBoolAttributes()
            .withStringFromValueFunction ([] (bool value, int)
            {
                return value ? juce::String ("bell") : juce::String ("shelf");
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                const auto t = text.trim();

                return ! (t.equalsIgnoreCase ("shelf") || t == "0");
            })));

    // -----------------------------------------------------------------------
    // the DYNAMICS section (v2.64.0, Clement's spec, roadmap C3;
    // reorganised in v2.66.0, Clement's remarks A to F): the
    // gate/expander and the classic compressor, between the EQ and the
    // tilt, both stages carrying the SAME control set. The expander:
    // threshold -70 to 0 dB, range 0 to 36 dB (the maximum attenuation
    // - 0 is a true bypass), the FAST ATTACK plate (1 ms lit / 30 ms
    // dark, since v2.66.0), release 0.1 to 3 s, ratio 1.2 to 12, mix 0
    // to 100 % wet, the sidechain pair (SC HPF 16 to 1600 Hz, SC LPF
    // 100 to 24000 Hz, their rest positions a true bypass) and the
    // LISTEN plate. The compressor: threshold -60 to 0 dB, ratio 1 to
    // 20, release 0.05 to 1.5 s, the FAST ATTACK plate (3 ms lit /
    // 30 ms dark), make-up gain 0 to +20 dB, mix, its own sidechain
    // pair and LISTEN. The DEFAULTS are neutral (range 0, threshold 0,
    // every sidechain at rest): the process loop skips both halves
    // wholesale, so a parked strip is bit-for-bit v2.63. Sessions
    // saved by earlier versions find no dynamics children and load these
    // defaults (the APVTS tree stores denormalised values)
    // -----------------------------------------------------------------------

    // the shared on/off plate plumbing: the cuts' and the EQ's own
    // language ("on" / "off", typed values tolerated both ways)
    const auto onOffText = [] (bool value, int)
    {
        return value ? juce::String ("on") : juce::String ("off");
    };

    const auto onOffParse = [] (const juce::String& text)
    {
        const auto t = text.trim();

        return ! (t.equalsIgnoreCase ("off") || t == "0");
    };

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "expOn", 1 }, "Expander On", true,
        juce::AudioParameterBoolAttributes()
            .withStringFromValueFunction (onOffText)
            .withValueFromStringFunction (onOffParse)));

    // the expander's threshold: -70 to 0 dB of detector level, 0.1 dB
    // steps, the classic gate starting point at -50
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "expThreshold", 1 }, "Expander Threshold",
        juce::NormalisableRange<float> (-70.0f, 0.0f, 0.1f), -50.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (dbText)
            .withValueFromStringFunction (dbParse)));

    // the range: the deepest attenuation the expander may apply, 0 to
    // 36 dB (v2.72.0, Clement's request: the range tops at 36 dB so the
    // GR ladder's three 12 dB tiers read 0-6, 6-18 and 18-36). 0 dB is
    // the neutral default - the section is skipped
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "expRange", 1 }, "Expander Range",
        juce::NormalisableRange<float> (0.0f, 36.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (dbText)
            .withValueFromStringFunction (dbParse)));

    // the shared time plumbing: the releases speak seconds ("0.30 s"),
    // and the parser tolerates either unit - the French decimal comma
    // included. (v2.66.0: the millisecond plumbing retired with the
    // expander's attack knob, which became the FAST/SLOW plate)
    const auto sText = [] (float value, int)
    {
        return juce::String (value, 2) + " s";
    };

    const auto sParse = [] (const juce::String& text)
    {
        auto t = text.trim();
        float scale = 1.0f;      // a bare number reads as the knob's own s

        if (t.endsWithIgnoreCase ("ms"))
        {
            t = t.dropLastCharacters (2).trim();
            scale = 1000.0f;
        }
        else if (t.endsWithIgnoreCase ("s"))
        {
            t = t.dropLastCharacters (1).trim();
        }

        return t.replaceCharacter (',', '.').getFloatValue() / scale;
    };

    // the expander's FAST ATTACK plate (v2.66.0, Clement's remark A):
    // the gate's OPENING speed as the J's own two-position switch - 1 ms
    // lit (the old knob's default: a transient lands whole), 30 ms dark
    // (the compressor's classic slow fade-in). FAST is the default, so
    // the strip breathes exactly as it did; a session saved with the old
    // "expAttack" knob simply finds no value here and parks on it
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "expFastAttack", 1 }, "Expander Fast Attack", true,
        juce::AudioParameterBoolAttributes()
            .withStringFromValueFunction ([] (bool value, int)
            {
                return value ? juce::String ("fast") : juce::String ("slow");
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                return text.trim().equalsIgnoreCase ("slow") ? false : true;
            })));

    // the expander's release: the gate's CLOSING speed, 0.1 to 3 s -
    // the sweep Clement asked for
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "expRelease", 1 }, "Expander Release",
        makeLogRange (0.1f, 3.0f), 0.3f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (sText)
            .withValueFromStringFunction (sParse)));

    // the shared ratio plumbing: the knob speaks "2.0 : 1", and a typed
    // value tolerates the colon or a bare number
    const auto ratioText = [] (float value, int)
    {
        return juce::String (value, 1) + " : 1";
    };

    const auto ratioParse = [] (const juce::String& text)
    {
        return text.trim()
                   .upToFirstOccurrenceOf (":", false, true)
                   .trim()
                   .replaceCharacter (',', '.')
                   .getFloatValue();
    };

    // the expander's ratio: 1.2 (a gentle slope) to 12 (a gate in all
    // but name), logarithmic like every ratio knob worth turning.
    // Default 2:1
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "expRatio", 1 }, "Expander Ratio",
        makeLogRange (1.2f, 12.0f), 2.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (ratioText)
            .withValueFromStringFunction (ratioParse)));

    // the expander's KNEE (v2.73.0, Clement's choice - option B): how
    // softly the static curve turns at its threshold. Six even plates
    // - "hard" (the corner every session born before v2.73 already
    // knows, bit-exact) then 4 / 8 / 12 / 16 / 20 dB - the same
    // interval-snapped selector pattern the SC corners use
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "expKnee", 1 }, "Expander Knee",
        juce::NormalisableRange<float> (0.0f, 20.0f, 4.0f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                return value < 0.5f ? juce::String ("hard")
                                    : juce::String (juce::roundToInt (value)) + " dB";
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                const auto t = text.trim();

                if (t.equalsIgnoreCase ("hard") || t.equalsIgnoreCase ("off"))
                    return 0.0f;

                return t.replaceCharacter (',', '.').getFloatValue();
            })));

    // the shared mix plumbing: 0 to 100 % wet, a bare number reads as %
    const auto mixText = [] (float value, int)
    {
        return juce::String (juce::roundToInt (value)) + " %";
    };

    const auto mixParse = [] (const juce::String& text)
    {
        return text.trim()
                   .upToFirstOccurrenceOf (" ", false, true)
                   .trim()
                   .replaceCharacter (',', '.')
                   .getFloatValue();
    };

    // the expander's mix: parallel expansion, 0 to 100 % wet
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "expMix", 1 }, "Expander Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (mixText)
            .withValueFromStringFunction (mixParse)));

    // v2.69.0 (Clement's remark): the sidechain selectors' own discrete
    // range - one legal corner per step, the same equal-step geometry
    // the slope and octave selectors ride, so the SC pairs feel like
    // the rest of the console. The range bounds keep the old
    // continuous spans, so a pre-2.69 session lands on its nearest
    // legal corner instead of jumping
    const auto makeScCornerRange = [] (float minValue, float maxValue,
                                       const float* corners, int n, bool higherOnTie)
    {
        return juce::NormalisableRange<float> (
            minValue, maxValue,
            [corners, n] (float, float, float normalised)
            {
                const int index = juce::jlimit (0, n - 1,
                    juce::roundToInt (normalised * (float) (n - 1)));
                return corners[index];
            },
            [corners, n, higherOnTie] (float, float, float value)
            {
                const int index = nearestScCornerIndex (corners, n,
                    juce::jlimit (corners[0], corners[n - 1], value), higherOnTie);
                return (float) index / (float) (n - 1);
            },
            [corners, n, higherOnTie] (float start, float end, float value)
            {
                const int index = nearestScCornerIndex (corners, n,
                    juce::jlimit (start, end, value), higherOnTie);
                return corners[index];
            });
    };

    // the sidechain readouts speak "off" at their rest positions and
    // plain hertz elsewhere, and a typed "off" parses back to rest
    const auto scHpfText = [] (float value, int)
    {
        if (value < 16.5f)
            return juce::String ("off");

        return juce::String (juce::roundToInt (value)) + " Hz";
    };

    const auto scHpfParse = [] (const juce::String& text)
    {
        auto t = text.trim();

        if (t.equalsIgnoreCase ("off"))
            return 16.0f;

        if (t.endsWithIgnoreCase ("hz"))
            t = t.dropLastCharacters (2).trim();

        return t.replaceCharacter (',', '.').getFloatValue();
    };

    const auto scLpfText = [] (float value, int)
    {
        if (value > 23999.5f)
            return juce::String ("off");

        return juce::String (juce::roundToInt (value)) + " Hz";
    };

    const auto scLpfParse = [] (const juce::String& text)
    {
        auto t = text.trim();

        if (t.equalsIgnoreCase ("off"))
            return 24000.0f;

        if (t.endsWithIgnoreCase ("hz"))
            t = t.dropLastCharacters (2).trim();

        return t.replaceCharacter (',', '.').getFloatValue();
    };

    // the shared LISTEN plumbing (v2.66.0, Clement's remark A): the
    // plate speaks "listen" when lit and "off" when dark, and a typed
    // value parses the way the cuts' on/off plates do
    const auto listenText = [] (bool value, int)
    {
        return value ? juce::String ("listen") : juce::String ("off");
    };

    const auto listenParse = [] (const juce::String& text)
    {
        return ! text.trim().equalsIgnoreCase ("off");
    };

    // the expander's own sidechain pair (v2.66.0, Clement's remarks A,
    // D and E): the same two selectors the compressor owns, feeding
    // only the gate's detector - 12 dB/oct biquads like the
    // compressor's own HPF. The rest positions (16 Hz / 24000 Hz)
    // leave the detector's band whole: a knob at rest never filters, so
    // the default strip hears exactly as v2.65 did
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "expScHpf", 1 }, "Expander SC HPF",
        makeScCornerRange (16.0f, 1600.0f, scHpfCorners, numScHpfCorners, false), 16.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (scHpfText)
            .withValueFromStringFunction (scHpfParse)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "expScLpf", 1 }, "Expander SC LPF",
        makeScCornerRange (100.0f, 24000.0f, scLpfCorners, numScLpfCorners, true), 24000.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (scLpfText)
            .withValueFromStringFunction (scLpfParse)));

    // the expander's LISTEN plate (v2.66.0, Clement's remark A): the
    // strip's voice becomes the gate's own sidechain - the filtered
    // signal its detector hears - while the plate is lit
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "expListen", 1 }, "Expander Listen", false,
        juce::AudioParameterBoolAttributes()
            .withStringFromValueFunction (listenText)
            .withValueFromStringFunction (listenParse)));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "compOn", 1 }, "Compressor On", true,
        juce::AudioParameterBoolAttributes()
            .withStringFromValueFunction (onOffText)
            .withValueFromStringFunction (onOffParse)));

    // the compressor's threshold: -60 to 0 dB. The 0 dB default is
    // neutral - nothing in a [-1, 1] signal can rise above it, so the
    // section is skipped
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compThreshold", 1 }, "Compressor Threshold",
        juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (dbText)
            .withValueFromStringFunction (dbParse)));

    // the compressor's ratio: 1 (the identity) to 20, logarithmic.
    // Default 2:1
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compRatio", 1 }, "Compressor Ratio",
        makeLogRange (1.0f, 20.0f), 2.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (ratioText)
            .withValueFromStringFunction (ratioParse)));

    // the compressor's release: 0.05 to 1.5 s, the classic sweep.
    // Default 0.25 s
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compRelease", 1 }, "Compressor Release",
        makeLogRange (0.05f, 1.5f), 0.25f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (sText)
            .withValueFromStringFunction (sParse)));

    // the FAST ATTACK plate: 3 ms when lit (a transient clamped almost
    // before it lands), the classic 30 ms when dark (a drum's point
    // rides through untouched). Dark is the default
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "compFastAttack", 1 }, "Compressor Fast Attack", false,
        juce::AudioParameterBoolAttributes()
            .withStringFromValueFunction ([] (bool value, int)
            {
                return value ? juce::String ("fast") : juce::String ("slow");
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                return text.trim().equalsIgnoreCase ("slow") ? false : true;
            })));

    // the make-up gain: 0 to +18 dB on the wet path only, so the mix
    // knob's dry half stays honest (v2.73.0, Clement's remark 4: the
    // ceiling rides the compressor's new 18 dB full-scale reduction -
    // a session saved above 18 dB clamps to it)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compGain", 1 }, "Compressor Gain",
        juce::NormalisableRange<float> (0.0f, 18.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (dbText)
            .withValueFromStringFunction (dbParse)));

    // the compressor's mix: parallel compression, 0 to 100 % wet
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compMix", 1 }, "Compressor Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (mixText)
            .withValueFromStringFunction (mixParse)));

    // the compressor's LINK selector (v2.72.0, Clement's request): a
    // four-position plate - dual, 50 % link, 80 % link, link. Each
    // channel carries its own detector, and the position blends its
    // level towards the stereo-linked one (0 = fully independent
    // detectors AND gains, 1 = the classic single linked detector, the
    // strip's behaviour since v2.64.0 - the default, so every existing
    // session loads exactly as it was saved). The GR meter always
    // reads the average of the two channels' reductions
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compLink", 1 }, "Compressor Link",
        juce::NormalisableRange<float> (0.0f, 3.0f, 1.0f), 3.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                const int index = juce::jlimit (0, 3, juce::roundToInt (value));

                static const char* const words[4] =
                    { "dual", "50% link", "80% link", "link" };

                return juce::String (words[index]);
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                auto t = text.trim();

                if (t.containsIgnoreCase ("dual"))
                    return 0.0f;
                if (t.contains ("50"))
                    return 1.0f;
                if (t.contains ("80"))
                    return 2.0f;
                if (t.containsIgnoreCase ("link"))
                    return 3.0f;

                // a bare number speaks the position index
                return juce::jlimit (0.0f, 3.0f,
                                     t.replaceCharacter (',', '.').getFloatValue());
            })));

    // the compressor's sidechain HPF (v2.66.0, Clement's remark D): the
    // range grows to the same 16 to 1600 Hz the expander's own selector
    // rides, and the flat 16 Hz rest position is the honest "off" the
    // old 10 Hz default approximated - a knob at rest never filters, so
    // the detector hears the whole band and the parked strip stays
    // bit-exact. Only the detector ever hears it - the audio path is
    // untouched
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compHpf", 1 }, "Compressor SC HPF",
        makeScCornerRange (16.0f, 1600.0f, scHpfCorners, numScHpfCorners, false), 16.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (scHpfText)
            .withValueFromStringFunction (scHpfParse)));

    // the compressor's SC LPF (v2.66.0, Clement's remark E): 100 to
    // 24000 Hz, darkening the detector's top end - a bright cymbal wash
    // stops pumping the gain. The 24000 Hz rest position leaves the
    // band whole
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compScLpf", 1 }, "Compressor SC LPF",
        makeScCornerRange (100.0f, 24000.0f, scLpfCorners, numScLpfCorners, true), 24000.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (scLpfText)
            .withValueFromStringFunction (scLpfParse)));

    // the compressor's LISTEN plate (v2.66.0, Clement's remark A): the
    // strip's voice becomes the compressor's sidechain while lit - and
    // when both plates are lit the compressor's wins (the later stage,
    // the last one you reached for)
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "compListen", 1 }, "Compressor Listen", false,
        juce::AudioParameterBoolAttributes()
            .withStringFromValueFunction (listenText)
            .withValueFromStringFunction (listenParse)));

    // the tilt (v2.57.0, Clement's remark 1): a full-band tilt, the
    // FabFilter style - a straight line of adjustable slope in the
    // log(f)/dB plane. The single knob is the +/- 12 dB gained or lost
    // BETWEEN 20 Hz AND 20 kHz; 0 dB at the default. The line pivots at
    // 632.4555 Hz, the geometric centre of the band (see
    // refreshTiltCoefficients for how the cascade draws it). v2.70.0
    // (Clement's remark 5): whole-dB steps - a switched tilt, the SSL
    // way; a session's finer value snaps to the nearest dB on load
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tiltDb", 1 }, "Tilt",
        makeSteppedRange (-12.0f, 12.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (dbText)
            .withValueFromStringFunction (dbParse)));

    // the drive (v2.58.0): decibels of input gain into the tanh, 0 (clean,
    // bit-transparent) to +15.6 dB (the full 6x saturation of the old
    // normalised knob - 20 log10 (6) = 15.56 dB). v2.70.0 (Clement's
    // remark 5): whole-dB steps, the top bound 15.6 itself stays the
    // knob's very last position. A NEW parameter id: sessions saved with
    // the old "driveAmount" simply find no value here and park at the
    // 0 dB default
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "driveDb", 1 }, "Drive",
        makeSteppedRange (0.0f, 15.6f, 1.0f), 0.0f,
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

    for (int j = 0; j < numEqBands; ++j)
    {
        lastEqHz[j]   = -1.0f;
        lastEqGain[j] = -999.0f;
        lastEqN[j]    = -1.0f;
        lastEqBell[j] = false;
    }
    lastEqOn = -1;

    for (auto& row : lowCut)
        for (auto& section : row)
            section.reset();

    for (auto& row : highCut)
        for (auto& section : row)
            section.reset();

    for (auto& row : tiltShelves)
        for (auto& section : row)
            section.reset();

    for (auto& row : eqBands)
        for (auto& section : row)
            section.reset();

    // the dynamics states (v2.64.0): both detectors park at the -120 dB
    // floor and both gains park OPEN, so a fresh strip never breathes on
    // its first block; the detector coefficient is the 10 ms RMS
    // ballistics; and the sidechain biquads start from silence with the
    // corner cache forced to rebuild
    expDetectorPower   = 0.0f;
    expGainDb          = 0.0f;
    compDetectorPowerL = 0.0f;
    compDetectorPowerR = 0.0f;
    compGainDbL        = 0.0f;
    compGainDbR        = 0.0f;
    detectorCoef      = 1.0f - std::exp (-1.0f / (0.010f * (float) currentSampleRate));

    // v2.65.0: the meters start at the detector's floor (an empty IN
    // ladder) and at unity (an empty GR ladder) - a fresh strip never
    // lies about the silence it has not heard yet
    expMeterInDb.store  (-120.0f, std::memory_order_relaxed);
    expMeterGrDb.store  (0.0f,    std::memory_order_relaxed);
    compMeterInDb.store (-120.0f, std::memory_order_relaxed);
    compMeterGrDb.store (0.0f,    std::memory_order_relaxed);

    // v2.66.0: the OUT ladders start at the floor like the IN ladders,
    // and every sidechain biquad - the compressor's pair and the
    // expander's own - starts from silence with its corner cache
    // forced to rebuild
    expMeterOutDb.store  (-120.0f, std::memory_order_relaxed);
    compMeterOutDb.store (-120.0f, std::memory_order_relaxed);
    expOutPower  = 0.0f;
    compOutPower = 0.0f;

    // v2.67.0: the honest IN one-poles start from silence like the OUT
    // states - a fresh strip paints empty IN ladders
    expInPower  = 0.0f;
    compInPower = 0.0f;

    for (auto& section : compSidechain)
        section.reset();

    for (auto& section : compSidechainLp)
        section.reset();

    for (auto& section : expSidechainHp)
        section.reset();

    for (auto& section : expSidechainLp)
        section.reset();

    lastCompHpf   = -1.0f;
    lastCompScLpf = -1.0f;
    lastExpScHpf  = -1.0f;
    lastExpScLpf  = -1.0f;

    // the oversampler is built here, never on the audio thread. A
    // generous maximum (any sane host block, then some) lets the whole
    // buffer ride a single up/down pass; a hostile block larger than
    // this degrades to the non-oversampled drive path for that block.
    preparedMaxSamples = juce::jmax (512, samplesPerBlock * 2, 8192);
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    oversampler->initProcessing ((size_t) preparedMaxSamples);

    refreshCutCoefficients();
    refreshEqCoefficients();
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

void CostesStrip::refreshEqCoefficients()
{
    // clamp all four corners below Nyquist with the same headroom as
    // the cuts: a hostile rate change must never feed RBJ a w0 past pi
    const float nyquistGuard = (float) (currentSampleRate * 0.45);

    const float hz[4] =
    {
        juce::jlimit (20.0f,  nyquistGuard, eq1Hz->load()),
        juce::jlimit (60.0f,  nyquistGuard, eq2Hz->load()),
        juce::jlimit (500.0f, nyquistGuard, eq3Hz->load()),
        juce::jlimit (600.0f, nyquistGuard, eq4Hz->load())
    };
    const float gainDb[4] = { eq1Gain->load(), eq2Gain->load(),
                              eq3Gain->load(), eq4Gain->load() };
    // the bells of bands 1/4 ride a fixed one-octave bandwidth; bands
    // 2/3 sweep theirs; the shelf modes use the RBJ Q form instead
    const float octaves[4] = { 1.0f, eq2Oct->load(), eq3Oct->load(), 1.0f };
    const bool bell[4] = { eq1Bell->load() > 0.5f, true, true,
                           eq4Bell->load() > 0.5f };
    const int on = (eqOn->load() > 0.5f) ? 1 : 0;

    // the cache guard: any change among the thirteen parameters rebuilds,
    // a step smaller than 1e-4 does not (same discipline as the cuts)
    bool unchanged = (on == lastEqOn);

    for (int j = 0; j < numEqBands; ++j)
        unchanged = unchanged
            && std::abs (hz[j] - lastEqHz[j]) < 1.0e-4f
            && std::abs (gainDb[j] - lastEqGain[j]) < 1.0e-4f
            && std::abs (octaves[j] - lastEqN[j]) < 1.0e-4f
            && bell[j] == lastEqBell[j];

    if (unchanged)
        return;

    for (int j = 0; j < numEqBands; ++j)
    {
        lastEqHz[j]   = hz[j];
        lastEqGain[j] = gainDb[j];
        lastEqN[j]    = octaves[j];
        lastEqBell[j] = bell[j];
    }
    lastEqOn = on;

    // the global plate parks the section count at zero: the process loop
    // reads that as a full bypass - the filter states keep their
    // coefficients but no sample passes through
    activeEqSections = (on == 1) ? numEqBands : 0;

    // the RBJ formulas in double, cast to float once, normalised by a0 -
    // exactly the cuts' discipline. The bell uses the bandwidth-in-
    // octaves form (alpha = sin w0 * sinh (ln2/2 * N * w0 / sin w0)),
    // so a bell reads EXACTLY its gain at its centre whatever N; the
    // shelves use the Q form with Q = 1/sqrt(2). At 0 dB (A == 1) the
    // numerator coefficients equal the denominators coefficient-for-
    // coefficient, so a parked band is the bit-exact identity
    for (int j = 0; j < activeEqSections; ++j)
    {
        const double w0 = juce::MathConstants<double>::twoPi
                              * (double) hz[j] / currentSampleRate;
        const double cs = std::cos (w0);
        const double sn = std::sin (w0);
        const double A  = std::pow (10.0, (double) gainDb[j] / 40.0);

        double b0, b1, b2, a0, a1, a2;

        if (bell[j])
        {
            const double alpha = sn
                * std::sinh ((std::log (2.0) * 0.5 * (double) octaves[j] * w0) / sn);

            a0 = 1.0 + alpha / A;
            b0 = (1.0 + alpha * A) / a0;
            b1 = -2.0 * cs / a0;
            b2 = (1.0 - alpha * A) / a0;
            a1 = -2.0 * cs / a0;
            a2 = (1.0 - alpha / A) / a0;
        }
        else
        {
            // the RBJ shelf, Q form with Q = 1/sqrt(2): alpha =
            // sin w0 / (2 Q) = sin w0 * sqrt(2) / 2
            const double alpha = sn * std::sqrt (2.0) * 0.5;
            const double sqA  = std::sqrt (A);

            if (j == 0)
            {
                // the low shelf of band 1
                a0 = (A + 1.0) + (A - 1.0) * cs + 2.0 * sqA * alpha;
                b0 = A * ((A + 1.0) - (A - 1.0) * cs + 2.0 * sqA * alpha) / a0;
                b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cs) / a0;
                b2 = A * ((A + 1.0) - (A - 1.0) * cs - 2.0 * sqA * alpha) / a0;
                a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cs) / a0;
                a2 = ((A + 1.0) + (A - 1.0) * cs - 2.0 * sqA * alpha) / a0;
            }
            else
            {
                // the high shelf of band 4
                a0 = (A + 1.0) - (A - 1.0) * cs + 2.0 * sqA * alpha;
                b0 = ((A + 1.0) + (A - 1.0) * cs + 2.0 * sqA * alpha) / a0;
                b1 = -2.0 * ((A - 1.0) + (A + 1.0) * cs) / a0;
                b2 = ((A + 1.0) + (A - 1.0) * cs - 2.0 * sqA * alpha) / a0;
                a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cs) / a0;
                a2 = ((A + 1.0) - (A - 1.0) * cs - 2.0 * sqA * alpha) / a0;
            }
        }

        const float fb0 = (float) b0, fb1 = (float) b1, fb2 = (float) b2;
        const float fa1 = (float) a1, fa2 = (float) a2;

        for (auto& section : eqBands[j])
            section.setCoefficients (fb0, fb1, fb2, fa1, fa2);
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
    refreshEqCoefficients();
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

    // ------------------------------------------------------------------
    // the dynamics section (v2.64.0): all the per-block constants are
    // evaluated here, once - the process loop below only runs plain
    // arithmetic per sample. A neutral half (its plate off, or range /
    // threshold / mix at a value that mathematically cannot move a
    // sample) applies not a single multiply to the audio, so the parked
    // strip is bit-for-bit the strip of v2.63. v2.65.0: the DETECTORS
    // now run unconditionally - the meters must tell the truth even
    // while the plates are off - but they only read the samples, never
    // write them, so the parked strip still processes sample for
    // sample exactly as before
    // ------------------------------------------------------------------
    const float expThresholdDb = expThreshold->load();
    const float expRangeDb     = expRange->load();
    const float expRatioValue  = expRatio->load();
    const float expKneeDb      = expKnee->load();
    const float expMixNorm     = expMix->load() * 0.01f;
    const bool  expActive = expOn->load() > 0.5f
                            && expRangeDb > 0.0f
                            && expMixNorm > 0.0f;
    const float expAttackCoef  = 1.0f - std::exp (-1.0f
        / ((expFastAttack->load() > 0.5f ? 0.0003f : 0.003f)
           * (float) currentSampleRate));
    const float expReleaseCoef = 1.0f - std::exp (-1.0f
        / (juce::jmax (0.1f, expRelease->load()) * (float) currentSampleRate));
    const float expCurveSlope  = expRatioValue - 1.0f;

    const float compThresholdDb = compThreshold->load();
    const float compRatioValue  = juce::jmax (1.0f, compRatio->load());
    const float compMixNorm     = compMix->load() * 0.01f;
    const bool  compActive = compOn->load() > 0.5f
                             && compThresholdDb < 0.0f
                             && compRatioValue > 1.0f
                             && compMixNorm > 0.0f;
    const float compAttackCoef  = 1.0f - std::exp (-1.0f
        / ((compFastAttack->load() > 0.5f ? 0.003f : 0.030f) * (float) currentSampleRate));
    const float compReleaseCoef = 1.0f - std::exp (-1.0f
        / (juce::jmax (0.05f, compRelease->load()) * (float) currentSampleRate));
    const float compMakeupGain  = juce::Decibels::decibelsToGain (compGain->load());
    const float compCurveSlope  = 1.0f - 1.0f / compRatioValue;

    // v2.72.0 (Clement's request): how far each channel's detector
    // leans towards the stereo-linked one - 0 (dual) to 1 (the full
    // link, the strip's behaviour since v2.64.0)
    const float compLinkWeight  = compLinkWeights[juce::jlimit (0, 3,
                                     juce::roundToInt (compLink->load()))];

    // the sidechain corners ride the same per-block cache discipline as
    // the cuts: the biquad coefficients rebuild only when a knob moves,
    // and a section standing on its rest position is hard-bypassed - a
    // reset biquad and a forced cache, so the detector hears the whole
    // band and the parked strip stays bit-exact. v2.65.0: the rebuild
    // left the compActive gate - the detectors always listen through
    // their sidechains now, so the IN meters show the levels the
    // sections will actually act on, corner or no corner. v2.66.0: the
    // expander owns its own pair and the SC LPFs join both stages; the
    // LPF corners also stand down above 0.45 of the sample rate, so no
    // Nyquist edge is ever crossed at any rate
    const auto rebuildSidechain = [this] (Biquad (&sections)[2], float corner,
                                          float& cache, bool highPass)
    {
        if (std::abs (corner - cache) > 1.0e-4f)
        {
            cache = corner;

            const double w0 = juce::MathConstants<double>::twoPi
                                  * (double) corner / currentSampleRate;
            const double cs = std::cos (w0);
            const double alpha = std::sin (w0) / (2.0 * shelfQ);
            const double a0 = 1.0 + alpha;

            const float b0 = highPass ? (float) ((1.0 + cs) * 0.5 / a0)
                                      : (float) ((1.0 - cs) * 0.5 / a0);
            const float b1 = highPass ? (float) (-(1.0 + cs) / a0)
                                      : (float) ((1.0 - cs) / a0);
            const float b2 = b0;
            const float a1 = (float) (-2.0 * cs / a0);
            const float a2 = (float) ((1.0 - alpha) / a0);

            for (auto& section : sections)
                section.setCoefficients (b0, b1, b2, a1, a2);
        }
    };

    const auto restSidechain = [] (Biquad (&sections)[2], float& cache)
    {
        for (auto& section : sections)
            section.reset();

        cache = -1.0f;
    };

    const float compScHpfHz = juce::jlimit (16.0f, 1600.0f, compHpf->load());
    const bool  compScHpfOn = compScHpfHz > 16.5f;
    const float compScLpfHz = juce::jlimit (100.0f, 24000.0f, compScLpf->load());
    const bool  compScLpfOn = compScLpfHz < 23999.5f
                              && compScLpfHz < 0.45f * (float) currentSampleRate;
    const float expScHpfHz  = juce::jlimit (16.0f, 1600.0f, expScHpf->load());
    const bool  expScHpfOn  = expScHpfHz > 16.5f;
    const float expScLpfHz  = juce::jlimit (100.0f, 24000.0f, expScLpf->load());
    const bool  expScLpfOn  = expScLpfHz < 23999.5f
                              && expScLpfHz < 0.45f * (float) currentSampleRate;

    if (compScHpfOn)
        rebuildSidechain (compSidechain, compScHpfHz, lastCompHpf, true);
    else
        restSidechain (compSidechain, lastCompHpf);

    if (compScLpfOn)
        rebuildSidechain (compSidechainLp, compScLpfHz, lastCompScLpf, false);
    else
        restSidechain (compSidechainLp, lastCompScLpf);

    if (expScHpfOn)
        rebuildSidechain (expSidechainHp, expScHpfHz, lastExpScHpf, true);
    else
        restSidechain (expSidechainHp, lastExpScHpf);

    if (expScLpfOn)
        rebuildSidechain (expSidechainLp, expScLpfHz, lastExpScLpf, false);
    else
        restSidechain (expSidechainLp, lastExpScLpf);

    // the LISTEN plates, evaluated once per block like every other knob
    // (the monitor itself is written per sample, in the stage loops)
    const bool compListenOn = compListen->load() > 0.5f;
    const bool expListenOn  = expListen->load() > 0.5f;

    const bool useOS = drive > 0.0f && driveOversampled->load() > 0.5f
                        && numSamples <= preparedMaxSamples;

    float* channels[2] = { nullptr, nullptr };

    for (int ch = 0; ch < numChannels; ++ch)
        channels[ch] = buffer.getWritePointer (ch);

    // the linear stages at the base rate: trim -> low cut -> high cut ->
    // EQ -> the dynamics -> tilt. Each sample makes three passes: the
    // per-channel chain up to the EQ, the stereo-linked dynamics on the
    // parked result (one detector for L and R, so the image never
    // shifts), then the per-channel tilt - the exact v2.63 arithmetic
    // when the dynamics are parked, sample for sample
    for (int i = 0; i < numSamples; ++i)
    {
        // one smoothed gain per sample, shared by every channel
        const float gainIn = trimInGain.getNextValue();

        // v2.66.0: the LISTEN monitor's per-sample carry - whichever
        // sidechain plate is lit writes its filtered signal here, and
        // the tilt stage hands it to the output in place of the voice
        float monitor[2] = { 0.0f, 0.0f };

        // stage 1: the per-channel chain up to and including the EQ
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float sample = channels[ch][i] * gainIn;

            for (int k = 0; k < activeLowCutSections; ++k)
                sample = lowCut[k][ch].processSample (sample);

            for (int k = 0; k < activeHighCutSections; ++k)
                sample = highCut[k][ch].processSample (sample);

            // the four-band EQ (v2.61.0): between the cuts and the tilt,
            // exactly where the SSL channel strips put it
            for (int k = 0; k < activeEqSections; ++k)
                sample = eqBands[k][ch].processSample (sample);

            channels[ch][i] = sample;
        }

        // stage 2, the EXPANDER: the 10 ms RMS detector rides the louder
        // channel (one detector for L and R, so the image never
        // shifts), the static curve (below the threshold expand by
        // over * (ratio - 1), never past the range floor) is evaluated
        // in the dB domain, and the gain one-pole opens on the attack
        // and closes on the release. v2.65.0: the detector runs even
        // when the section is parked - the IN meter must read the level
        // that sits at the gate - and a parked gain snaps open so the
        // GR meter never carries a stale reading. A NaN sample fails
        // the rectify compare and leaves the detector untouched - the
        // documented limit of the whole shell, no worse here
        {
            // v2.66.0: the detector (and the LISTEN monitor) hear the
            // stage's own sidechain - the signal through the SC HPF and
            // SC LPF when their knobs sit off their rest positions, the
            // raw sample otherwise. v2.67.0 (Clement's remark): the IN
            // ladder no longer shares the detector's view - it reads
            // the TRUE full-band input in its own one-pole below, so a
            // corner that shaves the detector's band can never make an
            // IN ladder read below the OUT ladder beside it
            float rectified = 0.0f;
            float rawIn = 0.0f;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float rawSample = channels[ch][i];
                rawIn = juce::jmax (rawIn, std::abs (rawSample));

                float sc = rawSample;

                if (expScHpfOn)
                    sc = expSidechainHp[ch].processSample (sc);

                if (expScLpfOn)
                    sc = expSidechainLp[ch].processSample (sc);

                if (expListenOn)
                    monitor[ch] = sc;

                rectified = juce::jmax (rectified, std::abs (sc));
            }

            expDetectorPower += (rectified * rectified - expDetectorPower) * detectorCoef;

            // v2.67.0: the IN ladder's own honest level - the raw
            // full-band input the stage receives, whatever the
            // sidechain corners do to the detector's copy
            expInPower += (rawIn * rawIn - expInPower) * detectorCoef;

            if (expActive)
            {
                const float levelDb = 10.0f * std::log10 (
                    juce::jmax (expDetectorPower, detectorFloorPower));

                float targetDb = 0.0f;

                // v2.73.0 (Clement's choice, option B): the KNEE plate
                // softens the corner. The zone spans a half-width either
                // side of the threshold; a parabola leaves the unity
                // line at the zone's top edge with slope zero and
                // rejoins the hard curve at its bottom edge, so the
                // transition bends instead of folding. A knee parked
                // at 0 takes the classic corner branch untouched -
                // bit-exact the pre-2.73 behaviour
                if (expKneeDb > 0.0f)
                {
                    const float kneeHalfDb = expKneeDb * 0.5f;

                    if (levelDb < expThresholdDb + kneeHalfDb)
                    {
                        // the hard curve's own value at the zone's lower
                        // edge (range-clamped) anchors the parabola
                        const float edgeDb = juce::jmin (kneeHalfDb * expCurveSlope,
                                                        expRangeDb);

                        if (levelDb <= expThresholdDb - kneeHalfDb)
                            targetDb = -juce::jmin ((expThresholdDb - levelDb) * expCurveSlope,
                                                    expRangeDb);
                        else
                        {
                            const float ramp = (expThresholdDb + kneeHalfDb - levelDb)
                                               / expKneeDb;
                            targetDb = -edgeDb * ramp * ramp;
                        }
                    }
                }
                else if (levelDb < expThresholdDb)
                    targetDb = -juce::jmin ((expThresholdDb - levelDb) * expCurveSlope,
                                            expRangeDb);

                expGainDb += (targetDb - expGainDb)
                    * ((targetDb > expGainDb) ? expAttackCoef : expReleaseCoef);

                const float wet = std::pow (10.0f, expGainDb * 0.05f);
                const float blend = 1.0f - expMixNorm + expMixNorm * wet;

                for (int ch = 0; ch < numChannels; ++ch)
                    channels[ch][i] *= blend;
            }
            else
            {
                expGainDb = 0.0f;
            }

            // v2.66.0: the OUT ladder reads the level this half leaves
            // behind - the blend applied or the untouched sample,
            // whatever sits in the buffer now. The detector only reads,
            // so the parked strip still processes bit-for-bit
            float expOutLevel = 0.0f;

            for (int ch = 0; ch < numChannels; ++ch)
                expOutLevel = juce::jmax (expOutLevel, std::abs (channels[ch][i]));

            expOutPower += (expOutLevel * expOutLevel - expOutPower) * detectorCoef;
        }

        // stage 2, the COMPRESSOR: the same RMS ballistics on the
        // sidechain-filtered signal (the biquad runs on the detector
        // path only - the audio never hears it), the static curve
        // above the threshold, the gain one-pole ENGAGING on the attack
        // and RECOVERING on the release, and the make-up riding the wet
        // half only so the mix knob stays honest. v2.65.0: the detector
        // (and its sidechain) runs even when the section is parked, and
        // a parked gain snaps back to unity for the GR meter.
        // v2.72.0 (Clement's request): each channel carries its OWN
        // detector and its OWN gain, and the compLink plate blends each
        // channel's detector level towards the stereo-linked one - at
        // "link" the two blends read identically and the two gains
        // evolve as one, the bit-exact v2.64.0 behaviour; at "dual"
        // the channels answer for themselves. The GR meter reads the
        // AVERAGE of the two reductions whatever the plate says
        {
            // the same sidechain discipline for the compressor (its SC
            // HPF since v2.64, its SC LPF since v2.66.0) - and its
            // LISTEN monitor wins over the expander's when both plates
            // are lit: the later stage, the last one you reached for.
            // v2.67.0: the IN ladder reads the true full-band input
            // here too, in its own one-pole below
            float rectified[2] = { 0.0f, 0.0f };
            float rawIn = 0.0f;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float rawSample = channels[ch][i];
                rawIn = juce::jmax (rawIn, std::abs (rawSample));

                float sc = rawSample;

                if (compScHpfOn)
                    sc = compSidechain[ch].processSample (sc);

                if (compScLpfOn)
                    sc = compSidechainLp[ch].processSample (sc);

                if (compListenOn)
                    monitor[ch] = sc;

                // the same NaN discipline as the expander's detector:
                // a NaN fails the compare and leaves the rectified
                // level untouched - the documented limit of the shell
                const int slot = juce::jmin (ch, 1);
                rectified[slot] = juce::jmax (rectified[slot], std::abs (sc));
            }

            // a mono stream only ever feeds channel 0's detector; the
            // idle one parks at its -120 dB floor
            compDetectorPowerL += (rectified[0] * rectified[0] - compDetectorPowerL)
                                    * detectorCoef;
            compDetectorPowerR += (rectified[1] * rectified[1] - compDetectorPowerR)
                                    * detectorCoef;

            // v2.67.0: the same honest IN level for the compressor -
            // the full-band sample entering the stage (the expander's
            // output), not the detector's filtered band
            compInPower += (rawIn * rawIn - compInPower) * detectorCoef;

            if (compActive)
            {
                const float levelDbL = 10.0f * std::log10 (
                    juce::jmax (compDetectorPowerL, detectorFloorPower));
                const float levelDbR = 10.0f * std::log10 (
                    juce::jmax (compDetectorPowerR, detectorFloorPower));

                // the stereo-linked level - the louder detector, the
                // one the v2.64.0 strip always rode - and each
                // channel's blend towards it
                const float linkedDb = juce::jmax (levelDbL, levelDbR);
                const float effDbL = compLinkWeight * linkedDb
                                       + (1.0f - compLinkWeight) * levelDbL;
                const float effDbR = compLinkWeight * linkedDb
                                       + (1.0f - compLinkWeight) * levelDbR;

                float targetDbL = 0.0f;
                float targetDbR = 0.0f;

                if (effDbL > compThresholdDb)
                    targetDbL = -(effDbL - compThresholdDb) * compCurveSlope;

                if (effDbR > compThresholdDb)
                    targetDbR = -(effDbR - compThresholdDb) * compCurveSlope;

                compGainDbL += (targetDbL - compGainDbL)
                    * ((targetDbL < compGainDbL) ? compAttackCoef : compReleaseCoef);
                compGainDbR += (targetDbR - compGainDbR)
                    * ((targetDbR < compGainDbR) ? compAttackCoef : compReleaseCoef);

                const float wetL = compMakeupGain * std::pow (10.0f, compGainDbL * 0.05f);
                const float wetR = compMakeupGain * std::pow (10.0f, compGainDbR * 0.05f);
                const float blendL = 1.0f - compMixNorm + compMixNorm * wetL;
                const float blendR = 1.0f - compMixNorm + compMixNorm * wetR;

                for (int ch = 0; ch < numChannels; ++ch)
                    channels[ch][i] *= (ch == 0 ? blendL : blendR);
            }
            else
            {
                compGainDbL = 0.0f;
                compGainDbR = 0.0f;
            }

            float compOutLevel = 0.0f;

            for (int ch = 0; ch < numChannels; ++ch)
                compOutLevel = juce::jmax (compOutLevel, std::abs (channels[ch][i]));

            compOutPower += (compOutLevel * compOutLevel - compOutPower) * detectorCoef;
        }

        // stage 3: the tilt cascade and its flat centring companion -
        // or the LISTEN monitor (v2.66.0, Clement's remark A): while a
        // plate is lit, the strip's voice becomes its own sidechain
        // instead, the compressor's plate winning when both are lit
        const bool listening = compListenOn || expListenOn;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (listening)
            {
                channels[ch][i] = monitor[ch];
                continue;
            }

            float sample = channels[ch][i];

            for (int k = 0; k < numTiltShelves; ++k)
                sample = tiltShelves[k][ch].processSample (sample);

            sample *= tiltCompensation;

            channels[ch][i] = sample;
        }
    }

    // v2.65.0: the meters' per-block snapshot, stored after the loop so
    // the ladders read the state at the block's last sample. The IN
    // meters speak the TRUE input level (v2.67.0, Clement's remark: the
    // full-band RMS, not the detector's filtered view - the floor parks
    // an empty ladder), the GR meters speak the gain each half applies
    // right now - 0 by construction when the half is parked. v2.66.0:
    // the OUT meters speak the level each half leaves behind
    expMeterInDb.store (10.0f * std::log10 (
                           juce::jmax (expInPower, detectorFloorPower)),
                       std::memory_order_relaxed);
    expMeterGrDb.store (expGainDb, std::memory_order_relaxed);
    expMeterOutDb.store (10.0f * std::log10 (
                            juce::jmax (expOutPower, detectorFloorPower)),
                        std::memory_order_relaxed);
    compMeterInDb.store (10.0f * std::log10 (
                            juce::jmax (compInPower, detectorFloorPower)),
                        std::memory_order_relaxed);
    // v2.72.0: the GR ladder always reads the AVERAGE of the two
    // channels' reductions - identical at "link", half-depth when the
    // plate sits at "dual" and only one side works
    compMeterGrDb.store (0.5f * (compGainDbL
                             + (numChannels > 1 ? compGainDbR : compGainDbL)),
                         std::memory_order_relaxed);
    compMeterOutDb.store (10.0f * std::log10 (
                             juce::jmax (compOutPower, detectorFloorPower)),
                         std::memory_order_relaxed);

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
    // editable readout under the cap for exact typed values. v2.62.0
    // (Clement's remark 4): each knob also carries its cap colour on
    // the backgroundColourId. v2.63.0 (Clement's remarks 2-4): a
    // COMMAND-CLICK parks the knob on its default too (CostesSlider),
    // and the HIGH CUT knob joins the HF band on the pastel red cap
    const auto setUpKnob = [this] (CostesSlider& knob, juce::Label& label,
                                   const juce::String& labelText, float defaultValue,
                                   const juce::Colour& capColour)
    {
        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);

        juce::Slider::RotaryParameters rotary;
        rotary.startAngleRadians = juce::MathConstants<float>::pi * -0.75f;
        rotary.endAngleRadians   = juce::MathConstants<float>::pi *  0.75f;
        rotary.stopAtEnd = true;
        knob.setRotaryParameters (rotary);

        knob.setMouseDragSensitivity (320);
        knob.setDoubleClickReturnValue (true, defaultValue);
        knob.cmdClickDefault = defaultValue;
        // 76 x 18 (v2.59.0, Clement's remark 1): the old 74 x 16 box sat
        // its descenders on the clip - the wider, taller box keeps every
        // readout fully inside the panel
        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 76, 18);
        knob.setColour (juce::Slider::backgroundColourId, capColour);
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

    setUpKnob (trimInKnob,  trimInLabel,  "TRIM IN",  0.0f,    knobDisc);
    setUpKnob (lowCutKnob,  lowCutLabel,  "LOW CUT",  16.0f,   knobDisc);
    // v2.63.0 (Clement's remark 4): the HIGH CUT knob wears the same
    // pastel red cap as the EQ's HF band
    setUpKnob (highCutKnob, highCutLabel, "HIGH CUT", 24000.0f, eqHfDisc);
    setUpKnob (tiltKnob,    tiltLabel,    "TILT",     0.0f,    knobDisc);
    setUpKnob (driveKnob,   driveLabel,   "DRIVE",    0.0f,    knobDisc);

    // the EQ knobs (v2.61.0): the same ergonomics, one FREQ and one GAIN
    // per band - double-click parks each band on its SSL-style starting
    // point (100 Hz, 400 Hz, 1600 Hz, 7200 Hz) or 0 dB. v2.62.0 (remark
    // 4): the caps speak the SSL band colours - LF keeps the black
    // aluminium, LO MID pastel blue, HI MID off-white, HF pastel red
    setUpKnob (eq1HzKnob,   eq1HzLabel,   "FREQ", 100.0f,  knobDisc);
    setUpKnob (eq1GainKnob, eq1GainLabel, "GAIN", 0.0f,    knobDisc);
    setUpKnob (eq2HzKnob,   eq2HzLabel,   "FREQ", 400.0f,  eqLoMidDisc);
    setUpKnob (eq2GainKnob, eq2GainLabel, "GAIN", 0.0f,    eqLoMidDisc);
    setUpKnob (eq3HzKnob,   eq3HzLabel,   "FREQ", 1600.0f, eqHiMidDisc);
    setUpKnob (eq3GainKnob, eq3GainLabel, "GAIN", 0.0f,    eqHiMidDisc);
    setUpKnob (eq4HzKnob,   eq4HzLabel,   "FREQ", 7200.0f, eqHfDisc);
    setUpKnob (eq4GainKnob, eq4GainLabel, "GAIN", 0.0f,    eqHfDisc);

    // the section glyphs (v2.60.0, Clement's remark): each icon sits at
    // the left of its caption and paints itself - nothing to add here
    // beyond making them visible
    addAndMakeVisible (trimIcon);
    addAndMakeVisible (lowCutIcon);
    addAndMakeVisible (highCutIcon);
    addAndMakeVisible (tiltIcon);
    addAndMakeVisible (driveIcon);
    addAndMakeVisible (eqIcon);   // v2.61.0: the EQ header's bell glyph

    // the two mid bands' bell glyphs (v2.62.0; re-cut in v2.70.0,
    // Clement's remark 9): the outer bands' shapes moved INTO their
    // BELL/SHELF plates, so only the two bells remain as free icons,
    // each sitting at the right of its Q readout's own "oct"
    addAndMakeVisible (eqBand2Icon);
    addAndMakeVisible (eqBand3Icon);

    // the EQ section header (v2.61.0): the caption is a label so the
    // icon can hug its left edge with the same geometry the knob
    // captions use - the engraved silkscreen look, drawn as a component
    eqSectionLabel.setText ("EQ", juce::dontSendNotification);
    eqSectionLabel.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    eqSectionLabel.setJustificationType (juce::Justification::centred);
    eqSectionLabel.setColour (juce::Label::textColourId, engravingDim);
    addAndMakeVisible (eqSectionLabel);

    // v2.70.0 (Clement's remarks 5 and 7): the input conditioning block
    // (TRIM, the cuts, TILT, DRIVE) carries its own written name -
    // INPUT, the SSL idiom for the channel's front end - centred in
    // the top band over the left half, naming the block the way the
    // EQ header names the right one
    inputSectionLabel.setText ("INPUT", juce::dontSendNotification);
    inputSectionLabel.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    inputSectionLabel.setJustificationType (juce::Justification::centred);
    inputSectionLabel.setColour (juce::Label::textColourId, engravingDim);
    addAndMakeVisible (inputSectionLabel);

    // the four band captions (v2.61.0): LF, LO MID, HI MID, HF - the
    // SSL E-series naming, each above its band's column
    const auto setUpBandCaption = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, engraving);
        addAndMakeVisible (label);
    };

    setUpBandCaption (eqBand1Label, "LF");
    setUpBandCaption (eqBand2Label, "LO MID");
    setUpBandCaption (eqBand3Label, "HI MID");
    setUpBandCaption (eqBand4Label, "HF");

    // the slope selectors under the cut knobs (v2.58.0, Clement's remark;
    // v2.59.0): a horizontal slider from 6 to 72 dB/oct. "off" moved to
    // the on/off plate on the left, so the sweep holds only legal
    // Butterworth slopes - the readout is the parameter's own text
    // ("6 dB/oct" ... "72 dB/oct"), so the label never drifts from the DSP.
    // v2.63.0 (Clement's remark 2): the linear selectors join the knobs
    // on the default-on-command-click behaviour, and gain the
    // double-click default they never had
    const auto setUpSlope = [this] (CostesSlider& slope, float defaultValue,
                                   int boxWidth = 76)
    {
        slope.setSliderStyle (juce::Slider::LinearHorizontal);
        // 76 x 18 (v2.60.0, Clement's remark 3): the 72 x 16 box clipped
        // the bottom of "24 dB/oct" - the knob readouts' own 18 px height
        // fix, applied to the slopes too. v2.72.0: the width became a
        // parameter - the mid bells' width sweeps grew their own wider
        // readout ("Width : 1.0 octave")
        slope.setTextBoxStyle (juce::Slider::TextBoxBelow, true, boxWidth, 18);
        slope.setDoubleClickReturnValue (true, defaultValue);
        slope.cmdClickDefault = defaultValue;
        slope.setColour (juce::Slider::trackColourId, knobBezel);
        slope.setColour (juce::Slider::thumbColourId, valueArc);
        slope.setColour (juce::Slider::textBoxTextColourId, engraving);
        slope.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0x00000000));
        slope.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0x00000000));
        addAndMakeVisible (slope);
    };

    setUpSlope (lowCutSlope, 2.0f);
    setUpSlope (highCutSlope, 2.0f);

    // the bandwidth sweeps of EQ bands 2/3 (v2.61.0): the same horizontal
    // selector the cuts use for their slopes, reading the bell's width in
    // octaves - and (v2.72.0, Clement's remark 1) the readout speaks its
    // own sentence now, "Width : 1.0 octave", wide enough that the words
    // never clip and the bell glyph sits before the "Width"
    setUpSlope (eq2OctSlider, 1.0f, 150);
    setUpSlope (eq3OctSlider, 1.0f, 150);

    // ------------------------------------------------------------------
    // the DYNAMICS row (v2.64.0, Clement's spec, reorganised in
    // v2.66.0 and re-cut into two vertical blocks in v2.68.0): the
    // two stages carry the SAME control set, and each stage's knobs
    // wear its own cap colour (v2.68.0, Clement's remark) - the HI
    // MID band's off-white on the expander, the LO MID band's pastel
    // blue on the compressor, the two stages echoing the EQ's own
    // palette. Double-click and command-click park every one
    // on its neutral default (threshold -50 / range 0 on the
    // expander, threshold 0 / ratio 2 on the compressor, mix 100 %,
    // every sidechain at rest)
    // ------------------------------------------------------------------
    setUpKnob (expThresholdKnob, expThresholdLabel, "THRESH", -50.0f, expKnobDisc);
    setUpKnob (expRangeKnob,     expRangeLabel,     "RANGE",    0.0f, expKnobDisc);
    setUpKnob (expReleaseKnob,  expReleaseLabel,   "RELEASE",   0.3f, expKnobDisc);
    setUpKnob (expRatioKnob,    expRatioLabel,      "RATIO",    2.0f, expKnobDisc);
    setUpKnob (expMixKnob,      expMixLabel,        "MIX",     100.0f, expKnobDisc);

    setUpKnob (compThresholdKnob, compThresholdLabel, "THRESH",  0.0f, compKnobDisc);
    setUpKnob (compRatioKnob,     compRatioLabel,     "RATIO",   2.0f, compKnobDisc);
    setUpKnob (compReleaseKnob,   compReleaseLabel,  "RELEASE", 0.25f, compKnobDisc);
    setUpKnob (compGainKnob,      compGainLabel,      "GAIN",    0.0f, compKnobDisc);
    setUpKnob (compMixKnob,       compMixLabel,      "MIX",     100.0f, compKnobDisc);

    // the sidechain pair of each stage rides the cuts' own horizontal
    // selector - the SC HPF parks at 16 Hz, the SC LPF at 24000 Hz,
    // and both rest positions leave the detector a flat band
    setUpSlope (compHpfSlider, 16.0f);
    setUpSlope (compScLpfSlider, 24000.0f);
    setUpSlope (expScHpfSlider, 16.0f);
    setUpSlope (expScLpfSlider, 24000.0f);

    // v2.72.0 (Clement's request): the compressor's LINK selector - a
    // four-position horizontal slider (dual / 50% link / 80% link /
    // link) parked at "link" (the v2.64.0 behaviour every session
    // already knows). v2.73.0 (Clement's remark 2): it reads its mode
    // UNDER the slider now, the same selector geometry the SC corners
    // use - it moved out of the block's header, where the section
    // label's full-width bounds swallowed every one of its clicks
    setUpSlope (compLinkSlider, 3.0f);

    // v2.73.0 (Clement's choice, option B): the expander's KNEE
    // selector - the same selector geometry, parked at "hard" (0 dB)
    setUpSlope (expKneeSlider, 0.0f);

    setUpBandCaption (compHpfLabel, "SC HPF");
    setUpBandCaption (compScLpfLabel, "SC LPF");
    setUpBandCaption (expScHpfLabel, "SC HPF");
    setUpBandCaption (expScLpfLabel, "SC LPF");

    // v2.74.0 (Clement's remark 1): the KNEE / LINK selectors sit in
    // the SC row's third cell now - their captions read under them
    // the same way the SC selectors' do
    setUpBandCaption (expKneeLabel, "KNEE");
    setUpBandCaption (compLinkLabel, "LINK");

    // v2.68.0 (Clement's remark): the dynamics sections carry no
    // glyphs any more - the stages read through their captions, their
    // knob colours and their ladders alone

    // the two section captions, engraved like the EQ header's own
    expSectionLabel.setText ("EXPANDER", juce::dontSendNotification);
    expSectionLabel.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    expSectionLabel.setJustificationType (juce::Justification::centred);
    expSectionLabel.setColour (juce::Label::textColourId, engravingDim);
    addAndMakeVisible (expSectionLabel);

    compSectionLabel.setText ("COMPRESSOR", juce::dontSendNotification);
    compSectionLabel.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    compSectionLabel.setJustificationType (juce::Justification::centred);
    compSectionLabel.setColour (juce::Label::textColourId, engravingDim);
    addAndMakeVisible (compSectionLabel);

    // the oversampling switch, an SSL-style illuminated plate under the
    // drive knob (v2.57.0, Clement's remark 2): dark plate with a dead
    // LED and a dim caption when off, glowing amber plate with a lit LED
    // and the caption spelled "OS 2x ON" when on - the state reads at a
    // glance, where the old default checkbox tick was too quiet to see.
    // v2.61.0: the caption moved into the plate painter, so the EQ's own
    // on/off plate speaks the same language
    oversampleButton.caption = "OS 2x";
    addAndMakeVisible (oversampleButton);

    // the EQ's global on/off plate (v2.61.0): beside the header, owning
    // the whole section's bypass - "EQ ON" lit amber, "EQ OFF" dark
    eqOnButton.caption = "EQ";
    addAndMakeVisible (eqOnButton);

    // the band-1/4 shape plates (v2.61.0): lit "BELL" or dark "SHELF",
    // one per outer band, under the band's knobs
    // v2.70.0 (Clement's remark 9): each outer plate carries its own
    // shelf shape inside the button - the low shelf on band 1, the
    // high shelf on band 4 (the default member suits band 1)
    eq4ModeButton.shelfGlyph = CostesStripEditor::SectionIcon::Icon::highShelf;
    addAndMakeVisible (eq1ModeButton);
    addAndMakeVisible (eq4ModeButton);

    // the cut on/off plates (v2.59.0, Clement's remark 2): the same SSL
    // plate language as the oversampling switch, vertical, one per cut.
    // They sit left of each cut's readout band and own the bypass - the
    // slope sweep itself starts at 6 dB/oct
    addAndMakeVisible (lowCutOnButton);
    addAndMakeVisible (highCutOnButton);

    // the dynamics' on/off plates (v2.64.0): "EXP ON" / "EXP OFF" and
    // "COMP ON" / "COMP OFF", the same plates as the EQ's global switch,
    // one per half - and each stage's FAST/SLOW attack plate (the
    // expander's own since v2.66.0, remark A: the two stages speak
    // the same language, 1 ms fast / 30 ms slow on the gate, 3 ms /
    // 30 ms on the compressor)
    expOnButton.caption = "EXP";
    addAndMakeVisible (expOnButton);
    compOnButton.caption = "COMP";
    addAndMakeVisible (compOnButton);
    addAndMakeVisible (compFastAttackButton);
    addAndMakeVisible (expFastAttackButton);

    // the LISTEN plates (v2.66.0, remark A): one push per stage, lit
    // amber while the strip's voice is its own sidechain
    addAndMakeVisible (expListenButton);
    addAndMakeVisible (compListenButton);

    // the dynamics meters (v2.65.0): the SSL J's own language - an IN
    // ladder and a GR ladder per stage, painted from the processor's
    // per-block snapshots
    addAndMakeVisible (expInMeter);
    addAndMakeVisible (expOutMeter);
    addAndMakeVisible (expGrMeter);
    addAndMakeVisible (compInMeter);
    addAndMakeVisible (compOutMeter);
    addAndMakeVisible (compGrMeter);

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

    eqOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        owner.getApvts(), "eqOn", eqOnButton);
    eq1HzAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "eq1Hz", eq1HzKnob);
    eq1GainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "eq1Gain", eq1GainKnob);
    eq1ModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        owner.getApvts(), "eq1Bell", eq1ModeButton);
    eq2HzAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "eq2Hz", eq2HzKnob);
    eq2GainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "eq2Gain", eq2GainKnob);
    eq2OctAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "eq2Oct", eq2OctSlider);
    eq3HzAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "eq3Hz", eq3HzKnob);
    eq3GainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "eq3Gain", eq3GainKnob);
    eq3OctAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "eq3Oct", eq3OctSlider);
    eq4HzAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "eq4Hz", eq4HzKnob);
    eq4GainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "eq4Gain", eq4GainKnob);
    eq4ModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        owner.getApvts(), "eq4Bell", eq4ModeButton);

    // the dynamics attachments (v2.64.0)
    expOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        owner.getApvts(), "expOn", expOnButton);
    expThresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "expThreshold", expThresholdKnob);
    expRangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "expRange", expRangeKnob);
    expFastAttackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        owner.getApvts(), "expFastAttack", expFastAttackButton);
    expReleaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "expRelease", expReleaseKnob);
    expRatioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "expRatio", expRatioKnob);
    expKneeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "expKnee", expKneeSlider);
    expMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "expMix", expMixKnob);
    expScHpfAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "expScHpf", expScHpfSlider);
    expScLpfAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "expScLpf", expScLpfSlider);
    expListenAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        owner.getApvts(), "expListen", expListenButton);
    compOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        owner.getApvts(), "compOn", compOnButton);
    compThresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "compThreshold", compThresholdKnob);
    compRatioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "compRatio", compRatioKnob);
    compReleaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "compRelease", compReleaseKnob);
    compFastAttackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        owner.getApvts(), "compFastAttack", compFastAttackButton);
    compGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "compGain", compGainKnob);
    compMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "compMix", compMixKnob);
    compHpfAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "compHpf", compHpfSlider);
    compScLpfAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "compScLpf", compScLpfSlider);
    compLinkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "compLink", compLinkSlider);
    compListenAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        owner.getApvts(), "compListen", compListenButton);

    // v2.70.0 (Clement's remark 1): the console front's own grain - a
    // 96 x 96 speckle tile baked ONCE, from a fixed xorshift seed so
    // the texture is the same pixels every launch. One dark speckle in
    // eight and one light one, each at a whisper of alpha (3 to 6):
    // the painted-steel finish of a real module, not a noise overlay.
    // The tile is a member image, the paint tiles it - no per-frame
    // cost beyond the blits the clipping region asks for
    {
        // JUCE 8 writes pixels through Image::BitmapData (the Image
        // itself carries no setPixelColour) - ONE lock around the
        // whole bake, taken before the loops, never per pixel
        juce::Image::BitmapData grainData (grainTexture,
                                           juce::Image::BitmapData::writeOnly);
        uint32_t grainState = 0x270119u;

        for (int y = 0; y < grainTexture.getHeight(); ++y)
        {
            for (int x = 0; x < grainTexture.getWidth(); ++x)
            {
                grainState ^= grainState << 13;
                grainState ^= grainState >> 17;
                grainState ^= grainState << 5;

                const uint32_t n = grainState & 0x7fffffffu;
                const uint32_t bucket = n & 15u;

                if (bucket < 2u)
                    grainData.setPixelColour (x, y, juce::Colour (0xff1c1a14)
                        .withAlpha ((juce::uint8) (3 + ((n >> 4) & 3u))));
                else if (bucket > 12u)
                    grainData.setPixelColour (x, y, juce::Colour (0xfffdf6e3)
                        .withAlpha ((juce::uint8) (3 + ((n >> 4) & 3u))));
            }
        }
    }

    // v2.68.0 (Clement's remark): the whole strip tightens - the row-1
    // band and the EQ lose a slice of height, the two dynamics stages
    // share ONE band as two vertical blocks side by side, and the
    // panel drops from 860 x 911 to 860 x 645 while keeping the
    // shell's own width
    setSize (860, 645);

    // the meters repaint at 30 Hz - the detectors are already smoothed
    // (a 10 ms RMS envelope, attack and release one-poles), so a
    // straight read paints a steady ladder without any display ballistics
    startTimerHz (30);
}

CostesStripEditor::~CostesStripEditor()
{
    // the meter timer stops before anything it repaints goes away
    stopTimer();

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

    eqOnAttachment.reset();
    eq1HzAttachment.reset();
    eq1GainAttachment.reset();
    eq1ModeAttachment.reset();
    eq2HzAttachment.reset();
    eq2GainAttachment.reset();
    eq2OctAttachment.reset();
    eq3HzAttachment.reset();
    eq3GainAttachment.reset();
    eq3OctAttachment.reset();
    eq4HzAttachment.reset();
    eq4GainAttachment.reset();
    eq4ModeAttachment.reset();

    expOnAttachment.reset();
    expThresholdAttachment.reset();
    expRangeAttachment.reset();
    expFastAttackAttachment.reset();
    expReleaseAttachment.reset();
    expRatioAttachment.reset();
    expKneeAttachment.reset();
    expMixAttachment.reset();
    expScHpfAttachment.reset();
    expScLpfAttachment.reset();
    expListenAttachment.reset();
    compOnAttachment.reset();
    compThresholdAttachment.reset();
    compRatioAttachment.reset();
    compReleaseAttachment.reset();
    compFastAttackAttachment.reset();
    compGainAttachment.reset();
    compMixAttachment.reset();
    compHpfAttachment.reset();
    compScLpfAttachment.reset();
    compListenAttachment.reset();

    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------
// CostesSlider - the command-click default (v2.63.0, Clement's remark 2).
// With the command key down, a click parks the control on its default and
// the drag never starts; every other gesture runs the normal JUCE path.
// The default itself travels through setValue with a synchronous
// notification, so the SliderAttachment carries it into the parameter
// tree immediately - the exact path a drag uses
// ---------------------------------------------------------------------------

void CostesStripEditor::CostesSlider::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isCommandDown())
    {
        cmdDefaultActive = true;
        applyCmdClickDefault();
        return;
    }

    juce::Slider::mouseDown (e);
}

void CostesStripEditor::CostesSlider::mouseDrag (const juce::MouseEvent& e)
{
    // a command-click swallowed the press, so the drag that follows must
    // not move the value either
    if (cmdDefaultActive)
        return;

    juce::Slider::mouseDrag (e);
}

void CostesStripEditor::CostesSlider::mouseUp (const juce::MouseEvent& e)
{
    if (cmdDefaultActive)
    {
        cmdDefaultActive = false;
        return;
    }

    juce::Slider::mouseUp (e);
}

void CostesStripEditor::CostesSlider::applyCmdClickDefault()
{
    setValue (cmdClickDefault, juce::sendNotificationSync);
}

void CostesStripEditor::paint (juce::Graphics& g)
{
    // the cream module front, slightly darker at the bottom like the
    // painted steel of an SSL 4000
    juce::ColourGradient gradient (panelTop, 0.0f, 0.0f,
                                   panelBottom, 0.0f, (float) getHeight(), false);
    g.setGradientFill (gradient);
    g.fillAll();

    // v2.70.0 (Clement's remark 1): the console front's own grain -
    // the 96 x 96 speckle tile baked in the constructor, tiled over
    // the cream. The alphas live inside the tile (3 to 6), so the
    // texture reads as paint grain; JUCE 8 retired drawImageTiled, so
    // the tiles are drawn directly - a few dozen tiny blits, and the
    // clipping region keeps the cost proportional to what actually
    // repaints
    for (int tileY = 0; tileY < getHeight(); tileY += grainTexture.getHeight())
        for (int tileX = 0; tileX < getWidth(); tileX += grainTexture.getWidth())
            g.drawImageAt (grainTexture, tileX, tileY);

    g.setColour (plateFill);
    g.drawRect (getLocalBounds(), 2);

    // v2.70.0 (Clement's remark 7): the engraved console signature
    // leaves the top band and reads along the LEFT EDGE - the way a
    // real console silkscreens its name down the side of its input
    // module. The trace, COSTES and CONSOLE are rotated a quarter
    // turn so they read bottom-up: the seismic line at the foot, the
    // name in dark engraving above it, CONSOLE in dim caps at the
    // head. In the rotated frame the +x axis points up the edge and
    // +y to the right, so the drawing below is the horizontal
    // signature re-used along the panel's own left margin (the top
    // band now belongs to the INPUT and EQ section names alone)
    const int width = getWidth();
    {
        const float trace[][2] =
        {
            {  0.0f, 10.0f }, {  5.0f, 10.0f }, {  8.0f,  2.5f },
            { 11.0f, 17.0f }, { 14.0f, 10.0f }, { 20.0f, 10.0f },
            { 23.0f,  5.0f }, { 26.0f, 15.0f }, { 29.0f, 10.0f },
            { 36.0f, 10.0f }, { 39.0f,  6.0f }, { 42.0f, 13.5f },
            { 45.0f, 10.0f }, { 50.0f, 10.0f }
        };

        g.saveState();
        g.addTransform (juce::AffineTransform::rotation (
            -juce::MathConstants<float>::halfPi, 17.0f, 384.0f));

        // the seismic line at the signature's foot, the logo's own
        // 50 x 20 trace scaled to 24 px - a heartbeat running up the
        // console's spine
        g.setColour (engraving);

        for (int i = 1; i < 14; ++i)
            g.drawLine (juce::Line<float> (17.0f + trace[i - 1][0] * 0.48f,
                                           377.0f + trace[i - 1][1] * 0.48f,
                                           17.0f + trace[i][0] * 0.48f,
                                           377.0f + trace[i][1] * 0.48f), 1.8f);

        // the name itself: COSTES in engraving, a small tick, CONSOLE
        // in dim caps - the same stacked wording as the old top band
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText ("COSTES", juce::Rectangle<int> (45, 377, 66, 14),
                    juce::Justification::centredLeft);
        g.setColour (engravingDim.withAlpha (0.7f));
        g.drawVerticalLine (115, 379.0f, 391.0f);
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        g.drawText ("CONSOLE", juce::Rectangle<int> (119, 378, 66, 14),
                    juce::Justification::centredLeft);

        g.restoreState();
    }

    // v2.70.0 (Clement's remarks 5 and 7): the MVP and the EQ stand
    // side by side as two vertical blocks and each carries its own
    // written name - INPUT heads the left one (a label, laid out in
    // resized), the EQ header names the right one. The engraved
    // silkscreen follows the blocks: a divider between the two
    // halves, a rule under the EQ's header, one between the MVP's
    // two sub-rows
    g.setColour (engravingDim.withAlpha (0.5f));

    g.drawVerticalLine (380, 32.0f, 388.0f);
    g.drawLine (394.0f, 55.0f, (float) width - 14.0f, 55.0f, 1.0f);
    g.drawLine (22.0f, 208.0f, 366.0f, 208.0f, 1.0f);

    // the dynamics band's own divider (v2.64.0): the same engraved
    // rule under the EQ block, so the third row reads as its own band
    // too. v2.70.0 (Clement's remark 8): the old horizontal rule that
    // crossed the row - a relic of the v2.65 stacked layout, cutting
    // straight through the two LISTEN plates - is gone; in its place
    // a VERTICAL rule separates the expander's block from the
    // compressor's, the mirror of the x = 380 divider above
    g.drawLine (14.0f, 394.0f, (float) width - 14.0f, 394.0f, 1.0f);
    g.drawVerticalLine (430, 402.0f, 632.0f);

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

void CostesStripEditor::timerCallback()
{
    // v2.65.0: the dynamics ladders repaint themselves - the detectors
    // are already smoothed, so the straight read of the per-block
    // snapshots is the display ballistics
    expInMeter.repaint();
    expOutMeter.repaint();
    expGrMeter.repaint();
    compInMeter.repaint();
    compOutMeter.repaint();
    compGrMeter.repaint();
}

void CostesStripEditor::resized()
{
    auto area = getLocalBounds();

    area.removeFromTop (26);   // the engraved signature band (v2.69.0)
    area.removeFromBottom (6);

    // v2.69.0 (Clement's remark): the MVP and the EQ share one band
    // as two vertical halves - the MVP's five sections stack as two
    // sub-rows in the left half, the EQ's four bands read as a 2 x 2
    // grid in the right one - and the dynamics band keeps its own
    // full row below, as in v2.68
    auto mvpEqBand = area.removeFromTop (368);
    auto dynArea   = area;

    // v2.70.0 (Clement's remarks 5 and 7): the top band belongs to the
    // two section names - INPUT centred over the left block, the EQ
    // header naming the right one below (its own 20 px row, as ever).
    // The engraved COSTES CONSOLE signature reads vertically along
    // the panel's left edge, so the MVP's own columns start clear of
    // it
    inputSectionLabel.setBounds (0, 0, 380, 26);

    auto mvpArea = mvpEqBand.removeFromLeft (380);
    mvpArea.removeFromLeft (28);   // the vertical signature's own margin
    auto eqArea  = mvpEqBand;

    // the MVP's top sub-row: TRIM, TONE and DRIVE, the same column
    // pattern as ever - knob above, caption below, the section glyph
    // hugging the caption's left edge - and the drive column keeps
    // its OS toggle under the caption
    auto mvpTop = mvpArea.removeFromTop (180);
    mvpArea.removeFromTop (4);          // the engraved rule's breath
    auto mvpBottom = mvpArea;

    const int mvpTopW = mvpTop.getWidth() / 3;

    for (int i = 0; i < 3; ++i)
    {
        auto column = mvpTop.removeFromLeft (mvpTopW).reduced (8, 2);

        auto* knob = &trimInKnob;
        auto* caption = &trimInLabel;
        SectionIcon* icon = &trimIcon;

        if (i == 1)      { knob = &tiltKnob;  caption = &tiltLabel;  icon = &tiltIcon; }
        else if (i == 2) { knob = &driveKnob; caption = &driveLabel; icon = &driveIcon; }

        // the drive column hosts the switch below its caption
        if (i == 2)
        {
            auto toggleBounds = column.removeFromBottom (26);
            toggleBounds = toggleBounds.withSizeKeepingCentre (juce::jmin (toggleBounds.getWidth(), 110), 26);
            oversampleButton.setBounds (toggleBounds);
        }

        auto captionBounds = column.removeFromBottom (18);
        caption->setBounds (captionBounds);

        // the section glyph (v2.60.0): a 16 px icon hugging the caption's
        // left edge - the caption stays centred in its column, the icon
        // sits exactly where the text begins
        const int textWidth = caption->getFont().getStringWidth (caption->getText());
        icon->setBounds (captionBounds.getCentreX() - textWidth / 2 - 21,
                         captionBounds.getY() + 1, 16, 16);

        knob->setBounds (column.removeFromTop (column.getHeight() - 20));
    }

    // the MVP's bottom sub-row: the two cuts side by side, each with
    // its slope selector and its on/off plate - the same per-column
    // geometry the cuts have carried since v2.59, one column wider
    const int mvpCutW = mvpBottom.getWidth() / 2;

    for (int i = 0; i < 2; ++i)
    {
        auto column = mvpBottom.removeFromLeft (mvpCutW).reduced (8, 2);

        auto* knob = &lowCutKnob;
        auto* caption = &lowCutLabel;
        juce::Slider* slope = &lowCutSlope;
        SectionIcon* icon = &lowCutIcon;

        if (i == 1) { knob = &highCutKnob; caption = &highCutLabel; slope = &highCutSlope; icon = &highCutIcon; }

        // the slope columns give their selector a 26 px band - the 18 px
        // readout box plus a slice of track above it (v2.60.0)
        const int knobHeight = column.getHeight() - 46;

        // the cut on/off plate (v2.59.0): v2.73.0 (Clement's remark 3)
        // moved it from the bottom-left of the knob's square to its
        // TOP-LEFT corner - the rotary disc leaves the corner empty,
        // so the plate reads as its knob's own switch at a glance and
        // steals none of the knob's room (the bounds below come from a
        // copy: the knob's own square stays untouched)
        auto plate = column;
        (i == 0 ? lowCutOnButton : highCutOnButton).setBounds (
            plate.removeFromTop (32).removeFromLeft (30));

        auto slopeBounds = column.removeFromBottom (26).reduced (6, 0);
        slope->setBounds (slopeBounds);

        auto captionBounds = column.removeFromBottom (18);
        caption->setBounds (captionBounds);

        const int textWidth = caption->getFont().getStringWidth (caption->getText());
        icon->setBounds (captionBounds.getCentreX() - textWidth / 2 - 21,
                         captionBounds.getY() + 1, 16, 16);

        knob->setBounds (column.removeFromTop (knobHeight));
    }

    // ---- the EQ section (v2.61.0, Clement's spec; v2.69.0: the
    // section stands beside the MVP as the right half of the band, and
    // its four bands read as a 2 x 2 grid - LF and LO MID across the
    // top, HI MID and HF across the bottom) ----
    // the header: the global on/off plate at the left and the section
    // caption centred, its bell glyph hugging the text's left edge
    // with the same geometry the knob captions use for their icons
    eqArea.removeFromTop (6);
    auto eqHeader = eqArea.removeFromTop (20);

    eqOnButton.setBounds (eqHeader.getX() + 14, eqHeader.getY() + 1, 76, 18);
    eqSectionLabel.setBounds (eqHeader);

    const int eqTextWidth = eqSectionLabel.getFont()
                                .getStringWidth (eqSectionLabel.getText());
    eqIcon.setBounds (eqHeader.getCentreX() - eqTextWidth / 2 - 21,
                      eqHeader.getY() + 2, 16, 16);

    // the four band cells: a caption above, the FREQ and GAIN knobs
    // side by side with their own small captions, the band's own
    // selector underneath - the bandwidth sweep for the mid bells, the
    // BELL/SHELF plate for the outer bands - and (v2.62.0, Clement's
    // remark 5) a 16 px glyph in a final row: the LF band's low-shelf
    // response, the two mid bells' bump and the HF band's high shelf
    const int eqBandH = eqArea.getHeight() / 2;

    for (int row = 0; row < 2; ++row)
    {
        auto eqRow = eqArea.removeFromTop (eqBandH);
        const int eqColumnWidth = eqRow.getWidth() / 2;

        for (int i = 0; i < 2; ++i)
        {
            const int band = row * 2 + i;
            auto column = eqRow.removeFromLeft (eqColumnWidth).reduced (4, 2);

            auto* bandLabel = &eqBand1Label;
            auto* freqKnob  = &eq1HzKnob;
            auto* freqLabel = &eq1HzLabel;
            auto* gainKnob  = &eq1GainKnob;
            auto* gainLabel = &eq1GainLabel;
            juce::Slider* octSlider   = nullptr;
            ShelfModeToggle* modeButton = &eq1ModeButton;

            // v2.70.0 (Clement's remark 9): the outer bands' glyphs live
            // inside their BELL/SHELF plates now, so the band icon is a
            // mid-band-only concern - it is only ever read where the
            // octSlider branch below runs
            SectionIcon* bandIcon = nullptr;

            if (band == 1)
            {
                bandLabel = &eqBand2Label; freqKnob = &eq2HzKnob;  freqLabel = &eq2HzLabel;
                gainKnob  = &eq2GainKnob;  gainLabel = &eq2GainLabel;
                octSlider = &eq2OctSlider; modeButton = nullptr;
                bandIcon  = &eqBand2Icon;
            }
            else if (band == 2)
            {
                bandLabel = &eqBand3Label; freqKnob = &eq3HzKnob;  freqLabel = &eq3HzLabel;
                gainKnob  = &eq3GainKnob;  gainLabel = &eq3GainLabel;
                octSlider = &eq3OctSlider; modeButton = nullptr;
                bandIcon  = &eqBand3Icon;
            }
            else if (band == 3)
            {
                bandLabel = &eqBand4Label; freqKnob = &eq4HzKnob;  freqLabel = &eq4HzLabel;
                gainKnob  = &eq4GainKnob;  gainLabel = &eq4GainLabel;
                modeButton = &eq4ModeButton;
            }

            bandLabel->setBounds (column.removeFromTop (16));
            column.removeFromTop (4);   // a breath under the band caption

            // v2.72.0 (Clement's remark 1): the mid bells' width sweep
            // grew a full-sentence readout ("Width : 1.0 octave") - it
            // owns the whole 44 px band now, the 18 px text box at its
            // bottom with air above, so the words never clip the way
            // the old 76 px box did. The outer bands' shapes still live
            // in their BELL/SHELF plates; every knob above stays put
            auto bottom = column.removeFromBottom (44);

            auto knobRow = column;

            const int half = knobRow.getWidth() / 2;
            auto freqCol = knobRow.removeFromLeft (half);
            auto gainCol = knobRow;

            freqLabel->setBounds (freqCol.removeFromBottom (14));
            freqKnob->setBounds (freqCol);
            gainLabel->setBounds (gainCol.removeFromBottom (14));
            gainKnob->setBounds (gainCol);

            if (octSlider != nullptr)
            {
                // the mid bells' bandwidth sweep - the cuts' own selector
                // geometry, one band width wide, riding the full band
                octSlider->setBounds (bottom.reduced (1, 1));

                // and the bell glyph BEFORE the words (v2.72.0,
                // Clement's remark 1): the text box is 150 px centred in
                // the selector, so the glyph hugs its left edge and
                // rides the same 18 px line the text sits in
                bandIcon->setBounds (bottom.getCentreX() - 75 - 18,
                                     bottom.getY() + 24, 16, 16);
            }
            else if (modeButton != nullptr)
            {
                // the outer bands' BELL/SHELF plate, centred under its
                // band - the top 26 px of the band, as ever
                auto control = bottom.removeFromTop (26);
                modeButton->setBounds (control.withSizeKeepingCentre (
                    juce::jmin (control.getWidth(), 76), 20));
            }
        }
    }

    // ---- the dynamics (v2.68.0, Clement's remark; re-cut in
    // v2.70.0, Clement's remark 2): the two stages stand side by side
    // as two VERTICAL blocks - the expander owns the LEFT half of the
    // band, the compressor the RIGHT one. Each block is one column:
    // the IN, OUT and GR ladders together at its left edge
    // (v2.72.0, Clement's request), and a three-by-three control grid
    // beside them - MIX,
    // THRESH and RELEASE across the top, RANGE (GAIN on the
    // compressor) and RATIO across the middle with the FAST ATK
    // plate beside them, the SC HPF and SC LPF selectors and the
    // LISTEN push across the bottom. The knobs sit smaller than the
    // row-1 and EQ columns - the SSL 500-series density - and the
    // whole band is one row tall. v2.70.0 (remark 6): the ATTACK and
    // LISTEN captions are gone - the plates say FAST ATK / SLOW ATK
    // and LISTEN on their own, so their cells stand full height
    auto expBlock  = dynArea.removeFromLeft (dynArea.getWidth() / 2);
    auto compBlock = dynArea;

    // one layout for both stages: the same control set in the same
    // order, the same ladders, so the two halves read as one design.
    // v2.74.0 (Clement's remark 1): FAST ATK and LISTEN moved up into
    // the middle row's third cell, and the lambda's SC row grew its
    // own third caption cell - the expander's KNEE, the compressor's
    // LINK, sitting level with the SC selectors
    auto layoutDynStage = [] (juce::Rectangle<int> block,
                                 PlateToggle& onButton,
                                 juce::Label& sectionLabel,
                                 DynamicsMeter& inMeter,
                                 DynamicsMeter& outMeter,
                                 DynamicsMeter& grMeter,
                                 CostesSlider* knobs[5],
                                 juce::Label* captions[5],
                                 FastAtkToggle& attackButton,
                                 CostesSlider& hpfSlider,
                                 juce::Label& hpfLabel,
                                 CostesSlider& lpfSlider,
                                 juce::Label& lpfLabel,
                                 ListenToggle& listenButton,
                                 juce::Label& selectorLabel,
                                 CostesSlider& plateSelector)
    {
        block = block.reduced (4, 2);
        block.removeFromTop (6);      // a breath under the band above
        auto header = block.removeFromTop (20);

        onButton.setBounds (header.getX() + 8, header.getY() + 1, 76, 18);
        sectionLabel.setBounds (header);

        // the three ladders: IN, OUT and GR together at the block's
        // left edge (v2.72.0, Clement's request - the GR meter found
        // its place between the OUT meter and the knobs), the full
        // height of the stage. v2.73.0 (Clement's remark 1): the trio
        // tightened - the IN ladder keeps the ONE engraved scale the
        // OUT ladder beside it shares (its own copy is gone), so the
        // pair reads as one meter with one series of numbers.
        // v2.74.0 (Clement's remark 2): the IN ladder gave its numbers
        // column the same width the OUT ladder gives its air - the
        // frame-to-frame distance reads EVEN now, IN to OUT exactly
        // the OUT-to-GR seam
        auto meterIn  = block.removeFromLeft (44);
        auto meterOut = block.removeFromLeft (44);
        auto meterGr  = block.removeFromLeft (56);
        inMeter.setBounds (meterIn.reduced (2, 2));
        outMeter.setBounds (meterOut.reduced (2, 2));
        grMeter.setBounds (meterGr.reduced (2, 2));

        // every cell sits its caption at the bottom and centres its
        // control in the room that is left
        const auto captionCell = [] (juce::Rectangle<int> cell, juce::Label& caption)
        {
            caption.setBounds (cell.removeFromBottom (14));
            return cell;
        };

        const int cellH = block.getHeight() / 3;

        // the top row: MIX, THRESH and RATIO
        auto knobRow = block.removeFromTop (cellH);
        const int knobW = knobRow.getWidth() / 3;

        for (int i = 0; i < 3; ++i)
        {
            auto cell = captionCell (knobRow.removeFromLeft (knobW).reduced (2, 0),
                                     *captions[(size_t) i]);
            knobs[(size_t) i]->setBounds (cell.withSizeKeepingCentre (
                juce::jmin (cell.getWidth(), 72), cell.getHeight()));
        }

        // the middle row: RANGE (GAIN on the compressor) and RATIO,
        // with the FAST ATK / SLOW ATK plate beside them - v2.70.0
        // (Clement's remark 2): RELEASE moved up beside MIX and THRESH,
        // RATIO dropped down, so the two stages read MIX, THRESH,
        // RELEASE across the top
        auto midRow = block.removeFromTop (cellH);
        const int midW = midRow.getWidth() / 3;

        for (int i = 0; i < 2; ++i)
        {
            auto cell = captionCell (midRow.removeFromLeft (midW).reduced (2, 0),
                                     *captions[(size_t) (3 + i)]);
            knobs[(size_t) (3 + i)]->setBounds (cell.withSizeKeepingCentre (
                juce::jmin (cell.getWidth(), 72), cell.getHeight()));
        }

        // v2.70.0 (Clement's remark 6): no caption under the plate any
        // more - the button owns its whole cell, wide enough for the
        // FAST ATK wording. v2.74.0 (Clement's remark 1): the FAST ATK
        // and LISTEN plates moved UP into this cell, stacked as one
        // centred column - the room below frees the SC row's third
        // cell for the stage's own selector
        auto plateCell = midRow.reduced (2, 0);
        plateCell = plateCell.withSizeKeepingCentre (plateCell.getWidth(),
                                                     juce::jmin (plateCell.getHeight(), 56));

        attackButton.setBounds (plateCell.removeFromTop (28).withSizeKeepingCentre (
            juce::jmin (plateCell.getWidth(), 76), 26));
        listenButton.setBounds (plateCell.removeFromTop (28).withSizeKeepingCentre (
            juce::jmin (plateCell.getWidth(), 76), 26));

        // the bottom row: the SC HPF, SC LPF and the stage's own
        // selector - three caption cells of the same geometry, so
        // (v2.74.0, Clement's remark 1) the KNEE / LINK selector sits
        // EXACTLY level with the SC selectors at its left, its value
        // reading under the slider the same way
        const int scW = block.getWidth() / 3;

        auto hpfCell = captionCell (block.removeFromLeft (scW).reduced (2, 0), hpfLabel);
        hpfSlider.setBounds (hpfCell.withSizeKeepingCentre (76, 36));

        auto lpfCell = captionCell (block.removeFromLeft (scW).reduced (2, 0), lpfLabel);
        lpfSlider.setBounds (lpfCell.withSizeKeepingCentre (76, 36));

        auto selectorCell = captionCell (block.reduced (2, 0), selectorLabel);
        plateSelector.setBounds (selectorCell.withSizeKeepingCentre (76, 36));
    };

    {
        // v2.70.0 (Clement's remark 2): MIX, THRESH, RELEASE across the
        // top, then RANGE and RATIO - the ratio reads as the section's
        // last word, the deepest of its controls
        CostesSlider* expKnobs[5] =
        {
            &expMixKnob, &expThresholdKnob, &expReleaseKnob,
            &expRangeKnob, &expRatioKnob
        };
        juce::Label* expCaptions[5] =
        {
            &expMixLabel, &expThresholdLabel, &expReleaseLabel,
            &expRangeLabel, &expRatioLabel
        };

        layoutDynStage (expBlock, expOnButton, expSectionLabel,
                        expInMeter, expOutMeter, expGrMeter,
                        expKnobs, expCaptions,
                        expFastAttackButton,
                        expScHpfSlider, expScHpfLabel,
                        expScLpfSlider, expScLpfLabel,
                        expListenButton,
                        expKneeLabel,
                        expKneeSlider);
    }

    {
        // v2.70.0 (Clement's remark 2): MIX, THRESH, RELEASE across the
        // top, then GAIN and RATIO
        CostesSlider* compKnobs[5] =
        {
            &compMixKnob, &compThresholdKnob, &compReleaseKnob,
            &compGainKnob, &compRatioKnob
        };
        juce::Label* compCaptions[5] =
        {
            &compMixLabel, &compThresholdLabel, &compReleaseLabel,
            &compGainLabel, &compRatioLabel
        };

        layoutDynStage (compBlock, compOnButton, compSectionLabel,
                        compInMeter, compOutMeter, compGrMeter,
                        compKnobs, compCaptions,
                        compFastAttackButton,
                        compHpfSlider, compHpfLabel,
                        compScLpfSlider, compScLpfLabel,
                        compListenButton,
                        compLinkLabel,
                        compLinkSlider);
    }
}
void CostesStripEditor::DynamicsMeter::paint (juce::Graphics& g)
{
    // v2.65.0: the SSL J's own meter language, painted straight from
    // the processor's per-block snapshots. An IN ladder lights from
    // the BOTTOM up on the stereo-linked RMS level its detector
    // hears (green, the top two segments amber - the over-region); a
    // GR ladder lights from the TOP down in the classic reduction
    // orange, 0 dB at the top. v2.66.0 (Clement's remarks A to C): an
    // OUT ladder joins each stage (the level the stage leaves
    // behind). v2.70.0 (Clement's remark 3): the GR ladders dropped
    // the J's two-half scale for a straight LINEAR one - one division
    // is ALWAYS one dB, only the number of divisions changes - 36
    // over the expander's 0 to 36 (v2.72.0), 18 over the compressor's
    // 0 to 18 (v2.73.0) -
    // and the engraved scale beside them grew from three anchors to
    // the full expGrMarks / compGrMarks lists, drawn positive.
    // v2.71.0 (Clement's request): the GR ladders went TWO-RATE -
    // one dB per division as before, but the first 6 dB own the TOP
    // HALF of the ladder in six fat divisions and everything below
    // squeezes into the bottom half in fine ones (34 divisions over
    // the expander's 6 to 40, 14 over the compressor's 6 to 20) -
    // most reduction lives between 0 and 6 dB, so that zone gets
    // the room. v2.72.0 (Clement's request): the THREE-TIER cut -
    // the expander reads 0-6, 6-18, 18-36 dB (the range itself tops
    // at 36 now), the compressor 0-3, 3-9, 9-18 (v2.73.0, Clement's
    // remark 4), each tier owning
    // exactly a THIRD of the ladder, one dB per division as ever.
    // The component holds no state of its
    // own: every paint reads the truth, and a degenerate resize
    // paints nothing but the caption - there is no size small enough
    // to divide by
    auto b = getLocalBounds();
    auto captionStrip = b.removeFromTop (14);

    auto body = b;
    auto ladderArea = body.removeFromLeft (26).reduced (3, 0);

    // v2.69.0 (Clement's remark): the caption reads straight up from
    // the LED column - IN, OUT and GR sit centred ABOVE the ladder
    // they name, not across the corner of the box
    g.setColour (engravingDim);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText (juce::String (caption),
                juce::Rectangle<int> (ladderArea.getX() - 8,
                                      captionStrip.getY(),
                                      ladderArea.getWidth() + 16,
                                      captionStrip.getHeight()),
                juce::Justification::centred);

    const float lx = (float) ladderArea.getX() + 1.0f;
    const float lw = (float) ladderArea.getWidth() - 2.0f;
    const float top = (float) ladderArea.getY() + 2.0f;
    const float bottom = (float) ladderArea.getBottom() - 2.0f;
    const float height = bottom - top;

    if (height < 8.0f || leds < 1 || lw < 2.0f)
        return;   // a degenerate resize never divides by anything

    // the meter window's engraved frame
    g.setColour (engravingDim.withAlpha (0.7f));
    g.drawRect (juce::Rectangle<float> (lx - 1.0f, top - 2.0f, lw + 2.0f, height + 4.0f), 1.0f);

    // the reading: the detector's RMS level for an IN ladder, the
    // level the stage leaves behind for an OUT ladder, the applied
    // gain (sign-flipped into its reduction) for a GR ladder
    float reading = 0.0f;

    if (mode == Mode::input)
        reading = expander ? owner.getExpInDb() : owner.getCompInDb();
    else if (mode == Mode::output)
        reading = expander ? owner.getExpOutDb() : owner.getCompOutDb();
    else
        reading = expander ? owner.getExpGrDb() : owner.getCompGrDb();

    // v2.67.0 (Clement's remark): the peak hold - the ladder remembers
    // the reading's own extreme (the loudest level on an IN or OUT
    // ladder, the deepest reduction on a GR ladder) for 3 s, then
    // glides back towards the live reading at 20 dB/s. The editor
    // repaints the ladders at 30 Hz, so the paint itself advances the
    // state; a degenerate resize returns above and never gets here
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        const double dt = (holdLastMs > 0.0)
            ? juce::jmin (0.2, (nowMs - holdLastMs) * 0.001) : 0.0;

        holdLastMs = nowMs;

        const bool newPeak = (mode == Mode::gainReduction)
            ? (reading <= holdReading) : (reading >= holdReading);

        if (! holdSet)
        {
            holdReading = reading;
            holdStampMs = nowMs;
            holdSet = true;
        }
        else if (newPeak)
        {
            holdReading = reading;
            holdStampMs = nowMs;
        }
        else if (nowMs - holdStampMs > 3000.0)
        {
            const float glide = 20.0f * (float) dt;

            if (mode == Mode::gainReduction)
                holdReading = juce::jmin (reading, holdReading + glide);
            else
                holdReading = juce::jmax (reading, holdReading - glide);

            if (holdReading == reading)
                holdStampMs = nowMs;
        }
    }

    const float span = highDb - lowDb;
    const float step = span / (float) leds;
    const float seg = height / (float) leds;

    // v2.72.0 (Clement's request): the GR ladders' three tier
    // boundaries - the expander 0-6-18-36, the compressor 0-3-9-18
    // (v2.73.0, Clement's remark 4: the compressor's full-scale
    // reduction is 18 dB now)
    const float grTier1Db = expander ? 6.0f : 3.0f;
    const float grTier2Db = expander ? 18.0f : 9.0f;

    // the LEDs: LED 0 is the lowest on an IN or OUT ladder, the
    // highest on a GR ladder - all three light towards the reading,
    // never past it. v2.71.0 (Clement's request): a GR rung now owns
    // its own height - the six fat divisions of the top half, the
    // fine rungs below - while the IN and OUT ladders stay uniform
    for (int j = 0; j < leds; ++j)
    {
        float y = 0.0f;
        float rung = seg;
        bool lit = false;
        juce::Colour colour (meterGreen);

        if (mode == Mode::gainReduction)
        {
            // v2.72.0: the rung boundaries ride the three-tier scale -
            // the border stays one dB per LED, exactly as in v2.70.0
            y = top + grFractionForDb ((float) j, span, grTier1Db, grTier2Db) * height;
            rung = (grFractionForDb ((float) j + 1.0f, span, grTier1Db, grTier2Db)
                    - grFractionForDb ((float) j, span, grTier1Db, grTier2Db)) * height;

            const float border = span * (float) j / (float) leds;

            lit = (-reading) > border;
            colour = meterGrOrange;
        }
        else
        {
            y = bottom - (float) (j + 1) * seg + 1.0f;
            lit = reading >= lowDb + (float) j * step;
            colour = (j >= leds - 2) ? valueArc : meterGreen;
        }

        // v2.71.0: the gap shrinks with the rung so the fine divisions
        // below the half still read as separate LEDs, never as a
        // solid bar
        const float gap = juce::jmin (2.0f, rung * 0.5f);
        const juce::Rectangle<float> led (lx + 1.0f, y + gap * 0.5f, lw - 2.0f,
                                          juce::jmax (1.0f, rung - gap));

        if (lit)
        {
            g.setColour (colour.withMultipliedAlpha (0.35f));
            g.fillRoundedRectangle (led.expanded (1.0f, 0.5f), 1.5f);
            g.setColour (colour);
            g.fillRoundedRectangle (led, 1.5f);
        }
        else
        {
            g.setColour (juce::Colour (0xff17150f));
            g.fillRoundedRectangle (led, 1.5f);
        }
    }

    // the peak-hold marker (v2.67.0): a cream hairline across the
    // ladder at the held extreme. v2.71.0: the GR ladder's marker
    // rides the same TWO-RATE geometry as its LEDs
    {
        float markerY = top;

        if (mode == Mode::gainReduction)
        {
            const float v = juce::jlimit (0.0f, span, -holdReading);
            markerY = top + grFractionForDb (v, span, grTier1Db, grTier2Db) * height;
        }
        else
        {
            const float v = juce::jlimit (lowDb, highDb, holdReading);
            markerY = bottom - (v - lowDb) / span * height;
        }

        markerY = juce::jlimit (top, bottom - 1.0f, markerY);

        // v2.68.0 (Clement's remark): the GR ladders hold their peak in
        // the panel's own pastel blue - the LO MID cap's colour - as a
        // thick bar that reads across the orange LEDs; v2.69.0
        // (Clement's remark): BOTH holds grew thicker - the GR bar and
        // the IN and OUT ladders' cream marker
        if (mode == Mode::gainReduction)
        {
            g.setColour (compKnobDisc);
            g.drawLine (juce::Line<float> (lx, markerY, lx + lw, markerY), 3.4f);
        }
        else
        {
            g.setColour (knobPointer);
            g.drawLine (juce::Line<float> (lx, markerY, lx + lw, markerY), 2.4f);
        }
    }

    // the dB scale, engraved beside the ladder: one mark every TEN dB
    // on an IN ladder now (v2.74.0, Clement's remark 3 - was 20).
    // v2.70.0 (Clement's remark 3): the GR ladder carries its full
    // value list instead of the old three anchors - the tiers and
    // their midpoints on each ladder (v2.74.0, Clement's request):
    // 0, 3, 6, 9, 12, 18 on the compressor, 0, 3, 6, 12, 18, 27, 36
    // on the expander - drawn POSITIVE at their tier rungs
    g.setColour (engravingDim);
    g.setFont (juce::Font (juce::FontOptions (8.0f)));

    const float scaleRight = (float) body.getX() + 2.0f;

    if (mode == Mode::gainReduction)
    {
        const int* marks = expander ? expGrMarks : compGrMarks;
        const int numMarks = expander ? 7 : 6;

        for (int m = 0; m < numMarks; ++m)
        {
            const float v = (float) marks[m];

            // v2.72.0: the engraved values sit at their three-tier
            // rungs - the tier boundaries at the thirds, the deep
            // values packed below
            const float y = top + grFractionForDb (v, span, grTier1Db, grTier2Db) * height;

            g.drawLine (lx + lw, y, lx + lw + 3.0f, y, 1.0f);
            g.drawText (juce::String (marks[m]),
                        juce::Rectangle<int> ((int) scaleRight, (int) (y - 5.0f),
                                              juce::jmax (1, body.getWidth() - 2), 10),
                        juce::Justification::centredLeft);
        }
    }
    else
    {
        // v2.73.0 (Clement's remark 1): the OUT ladder dropped its own
        // engraved scale - the IN ladder's series beside it serves the
        // tightened pair, one set of numbers for two ladders
        if (mode != Mode::input)
            return;

        for (int m = 0; (float) m * markStep <= span + 0.25f * markStep; ++m)
        {
            const float value = highDb - (float) m * markStep;
            const float y = top + (float) m * markStep / span * height;

            g.drawLine (lx + lw, y, lx + lw + 3.0f, y, 1.0f);

            // v2.74.0: the IN ladder's numbers column narrowed with the
            // tightened seam - the full remaining width keeps every
            // TEN readable right after its tick
            g.drawText (juce::String (juce::roundToInt (value)),
                        juce::Rectangle<int> ((int) ((float) body.getX()), (int) (y - 5.0f),
                                              juce::jmax (1, body.getWidth()), 10),
                        juce::Justification::centredLeft);
        }
    }
}

void CostesStripEditor::PlateToggle::paintButton (juce::Graphics& g,
                                                  bool highlighted,
                                                  bool /*down*/)
{
    const bool on = getToggleState();
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    // the plate itself carries the state: lit amber when the section
    // processes ("OS 2x" oversampling, the whole EQ), dark cream-panel
    // shadow when it is off
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

    // the caption says the state out loud: "EQ ON" / "EQ OFF",
    // dark engraving on the lit plate, and (v2.69.0, Clement's remark)
    // the plate's own cream text on the dark one - the dim grey was
    // unreadable against the plateFill
    g.setColour (on ? engraving : plateText);
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText (caption + (on ? " ON" : " OFF"), bounds,
                juce::Justification::centred);
}

void CostesStripEditor::ShelfModeToggle::paintButton (juce::Graphics& g,
                                                      bool highlighted,
                                                      bool /*down*/)
{
    // the band-1/4 shape switch (v2.61.0): the same SSL plate language
    // as the oversampling and EQ plates - lit amber with a live LED for
    // the bell, dark with a dead LED for the shelf. BELL is the resting
    // shape, so the plate lights whenever the band is a plain bell
    const bool on = getToggleState();
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (on ? valueArc.withMultipliedAlpha (0.85f)
                    : plateFill);
    g.fillRoundedRectangle (bounds, 5.0f);

    g.setColour (on ? valueArc.brighter (0.12f)
                    : (highlighted ? knobBezel : engravingDim));
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

    const auto ledArea = bounds.removeFromLeft (20.0f);
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

    // v2.70.0 (Clement's remark 9): the shape's own glyph rides INSIDE
    // the plate, beside the LED - the band's shelf plateau (low on band
    // 1, high on band 4, the shelfGlyph member) when the button reads
    // SHELF, the bell bump when it reads BELL - the same single-stroke
    // shapes the whole strip already speaks, one drawing, two owners
    auto glyphArea = bounds.removeFromLeft (16.0f);
    SectionIcon::drawGlyph (g,
                            glyphArea.withSizeKeepingCentre (13.0f, 13.0f),
                            on ? SectionIcon::Icon::eq : shelfGlyph,
                            on ? engraving : plateText);

    // the caption spells the shape: "BELL" / "SHELF", dark engraving on
    // the lit plate, and (v2.69.0) the plate's own cream on the dark one
    g.setColour (on ? engraving : plateText);
    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
    g.drawText (on ? "BELL" : "SHELF", bounds,
                juce::Justification::centred);
}

void CostesStripEditor::FastAtkToggle::paintButton (juce::Graphics& g,
                                                    bool highlighted,
                                                    bool /*down*/)
{
    // the compressor's attack plate (v2.64.0): the same SSL plate language
    // as the BELL/SHELF switch - lit amber with a live LED for the 3 ms
    // FAST attack that clamps a transient almost before it lands, dark
    // with a dead LED for the classic 30 ms SLOW that lets a drum's
    // point ride through
    const bool on = getToggleState();
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (on ? valueArc.withMultipliedAlpha (0.85f)
                    : plateFill);
    g.fillRoundedRectangle (bounds, 5.0f);

    g.setColour (on ? valueArc.brighter (0.12f)
                    : (highlighted ? knobBezel : engravingDim));
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

    const auto ledArea = bounds.removeFromLeft (20.0f);
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

    // v2.70.0 (Clement's remark 6): the plate says the whole word -
    // FAST ATK / SLOW ATK, no caption under it any more - so the type
    // comes down half a point to keep the wording comfortable on the
    // wider plate
    g.setColour (on ? engraving : plateText);
    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
    g.drawText (on ? "FAST ATK" : "SLOW ATK", bounds,
                juce::Justification::centred);
}

void CostesStripEditor::ListenToggle::paintButton (juce::Graphics& g,
                                                   bool highlighted,
                                                   bool /*down*/)
{
    // the LISTEN plate (v2.66.0, Clement's remark A): the same SSL
    // plate language as the attack plates - lit amber with a live
    // LED while the strip's voice is its own sidechain (the signal
    // the detector hears, through its SC HPF and SC LPF), dark with a
    // dead LED while the programme plays. The compressor's plate wins
    // over the expander's when both are lit
    const bool on = getToggleState();
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (on ? valueArc.withMultipliedAlpha (0.85f)
                    : plateFill);
    g.fillRoundedRectangle (bounds, 5.0f);

    g.setColour (on ? valueArc.brighter (0.12f)
                    : (highlighted ? knobBezel : engravingDim));
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

    const auto ledArea = bounds.removeFromLeft (20.0f);
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

    g.setColour (on ? engraving : plateText);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText ("LISTEN", bounds,
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
    // lit plate, and (v2.69.0) the plate's own cream on the dark one
    g.setColour (on ? engraving : plateText);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText (on ? "ON" : "OFF", bounds,
                juce::Justification::centred);
}

void CostesStripEditor::SectionIcon::drawGlyph (juce::Graphics& g,
                                               juce::Rectangle<float> b,
                                               Icon iconType,
                                               const juce::Colour& colour)
{
    // the glyphs (v2.60.0, Clement's remark; the EQ bell joined in
    // v2.61.0, the two shelf plateaus in v2.62.0; split out of paint()
    // in v2.70.0, Clement's remark 9): clean single-stroke vector shapes
    // drawn on a 16 x 16 design grid scaled to the GIVEN bounds so a
    // degenerate size can never divide by anything - and so the
    // BELL/SHELF plates can carry the very same shapes inside their
    // buttons. Every shape is drawn to be read instantly: a fader for
    // the trim, the two mirrored filter responses for the cuts, a
    // tilted line for the tilt, the soft saturation transfer curve
    // for the drive and the bell bump for the EQ

    const float sx = b.getWidth() / 16.0f;
    const float sy = b.getHeight() / 16.0f;
    const float stroke = 1.5f * (sx + sy) * 0.5f;

    const auto X = [&b, sx] (float v) { return b.getX() + v * sx; };
    const auto Y = [&b, sy] (float v) { return b.getY() + v * sy; };

    g.setColour (colour);

    juce::Path p;

    switch (iconType)
    {
        // TRIM: a fader - the vertical slot, a centred thumb and two
        // position ticks. The universal "level" reading
        case Icon::trim:
            p.startNewSubPath (X (8.0f), Y (2.6f));
            p.lineTo (X (8.0f), Y (13.4f));
            p.startNewSubPath (X (6.4f), Y (4.4f));
            p.lineTo (X (9.6f), Y (4.4f));
            p.startNewSubPath (X (6.4f), Y (11.6f));
            p.lineTo (X (9.6f), Y (11.6f));
            g.strokePath (p, juce::PathStrokeType (stroke));
            g.fillRoundedRectangle (juce::Rectangle<float> (
                X (5.0f), Y (6.6f), 6.0f * sx, 2.8f * sy), 1.2f * stroke);
            break;

        // LOW CUT: the high-pass response - flat at the bottom, then the
        // knee rising to the top right corner
        case Icon::lowCut:
            p.startNewSubPath (X (2.0f), Y (13.0f));
            p.lineTo (X (6.0f), Y (13.0f));
            p.quadraticTo (X (11.0f), Y (13.0f), X (14.0f), Y (3.0f));
            g.strokePath (p, juce::PathStrokeType (stroke));
            break;

        // HIGH CUT: the mirrored low-pass response - flat at the top,
        // the knee falling away to the bottom right
        case Icon::highCut:
            p.startNewSubPath (X (2.0f), Y (3.0f));
            p.lineTo (X (6.0f), Y (3.0f));
            p.quadraticTo (X (11.0f), Y (3.0f), X (14.0f), Y (13.0f));
            g.strokePath (p, juce::PathStrokeType (stroke));
            break;

        // TILT: a straight line pivoting over its centre dot - the
        // whole-band tilt in one stroke
        case Icon::tilt:
            p.startNewSubPath (X (3.0f), Y (13.0f));
            p.lineTo (X (13.0f), Y (3.0f));
            g.strokePath (p, juce::PathStrokeType (stroke));
            g.fillEllipse (juce::Rectangle<float> (X (6.6f), Y (6.6f),
                                                   2.8f * sx, 2.8f * sy));
            break;

        // DRIVE: the soft saturation transfer curve - flat, a straight
        // climb through the middle, flat again. The tanh the stage
        // actually applies, drawn as its own icon
        case Icon::drive:
            p.startNewSubPath (X (2.0f), Y (13.6f));
            p.cubicTo (X (7.0f), Y (13.2f), X (5.0f), Y (8.0f), X (8.0f), Y (8.0f));
            p.cubicTo (X (11.0f), Y (8.0f), X (9.0f), Y (2.8f), X (14.0f), Y (2.4f));
            g.strokePath (p, juce::PathStrokeType (stroke));
            break;

        // EQ (v2.61.0): the bell curve - the universal gain bump, two
        // mirrored quadratics over an implied baseline. One shape for
        // the whole section, beside the header caption
        case Icon::eq:
            p.startNewSubPath (X (2.0f), Y (12.5f));
            p.quadraticTo (X (4.5f), Y (3.0f), X (8.0f), Y (3.0f));
            p.quadraticTo (X (11.5f), Y (3.0f), X (14.0f), Y (12.5f));
            g.strokePath (p, juce::PathStrokeType (stroke));
            break;

        // LOW SHELF (v2.62.0, Clement's remark 5): the band-1 glyph under
        // the LF selector - flat at the bottom, the knee rising to the
        // high PLATEAU, flat at the top. Distinct from the low cut's
        // open-ended rise: the shelf settles, it does not climb away
        case Icon::lowShelf:
            p.startNewSubPath (X (2.0f), Y (12.5f));
            p.lineTo (X (5.0f), Y (12.5f));
            p.quadraticTo (X (9.0f), Y (12.5f), X (11.0f), Y (3.5f));
            p.lineTo (X (14.0f), Y (3.5f));
            g.strokePath (p, juce::PathStrokeType (stroke));
            break;

        // HIGH SHELF (v2.62.0, Clement's remark 5): the band-4 glyph, the
        // low shelf's mirror - flat at the top, the knee falling to the
        // low plateau, flat at the bottom
        case Icon::highShelf:
            p.startNewSubPath (X (2.0f), Y (3.5f));
            p.lineTo (X (5.0f), Y (3.5f));
            p.quadraticTo (X (7.0f), Y (3.5f), X (9.0f), Y (12.5f));
            p.lineTo (X (14.0f), Y (12.5f));
            g.strokePath (p, juce::PathStrokeType (stroke));
            break;

    }
}

void CostesStripEditor::SectionIcon::paint (juce::Graphics& g)
{
    // the component face of the glyph (v2.70.0, Clement's remark 9):
    // the whole drawing lives in drawGlyph now - paint just hands it
    // the local bounds and the strip's engraved silkscreen colour
    drawGlyph (g, getLocalBounds().toFloat(), type, engraving);
}

void CostesStripEditor::StripLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                                            int x, int y, int w, int h,
                                                            float sliderPos,
                                                            float rotaryStartAngle,
                                                            float rotaryEndAngle,
                                                            juce::Slider& slider)
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

    // the cap's own colour rides the backgroundColourId the knob setup
    // gives it (v2.62.0, Clement's remark 4): the row-1 and LF knobs
    // stay the black aluminium disc - the exact v2.60 gradient, kept
    // bit-for-bit - and the other EQ bands wear their SSL pastels,
    // with the same sheen built around their own base colour
    const juce::Colour discBase = slider.findColour (juce::Slider::backgroundColourId, true);
    const bool classicDisc = (discBase == knobDisc);
    const juce::Colour discLite = classicDisc ? knobDiscLite : discBase.brighter (0.18f);
    const juce::Colour discDark = classicDisc ? knobDisc     : discBase.darker (0.22f);

    const float inner = radius * 0.62f;
    juce::ColourGradient disc (discLite, centre.x - inner * 0.4f, centre.y - inner * 0.4f,
                               discDark, centre.x + inner * 0.4f, centre.y + inner * 0.4f, false);
    g.setGradientFill (disc);
    g.fillEllipse (centre.x - inner, centre.y - inner, inner * 2.0f, inner * 2.0f);

    // the pointer line, SSL style: white on the dark discs, a warm dark
    // on the bright pastel caps, so it always reads
    const float pointerLength = radius * 0.52f;
    const auto tip = juce::Point<float> (centre.x + std::sin (angle) * pointerLength,
                                         centre.y - std::cos (angle) * pointerLength);

    g.setColour (discBase.getPerceivedBrightness() > 0.55f ? engraving : knobPointer);
    g.drawLine (juce::Line<float> (centre, tip), radius * 0.09f);

    // the min/max skirt (v2.67.0, Clement's remark): the range's two
    // end values printed at the arc's own ends, the SSL silkscreen
    // language - the knob's whole span readable before it is touched.
    // The strings come from the slider's own value formatting, trimmed
    // to bare numbers ("+10.0 dB" -> "+10", "12.0:1" -> "12:1"), and
    // only the full-size knobs carry them - the little sidechain
    // selectors and the v2.68 dynamics mini-knobs have no skirt to
    // spare (the guard rose from 18 to 26 px for them)
    if (radius >= 26.0f)
    {
        const auto skirtText = [] (juce::Slider& s, double value)
        {
            juce::String t = s.getTextFromValue (value);

            // the skirt speaks bare numbers: drop the unit word
            if (const int space = t.indexOf (" "); space > 0)
                t = t.substring (0, space);

            // a ratio carries its ":1" suffix - trim the number
            // in front of it
            juce::String suffix;

            if (const int colon = t.indexOf (":"); colon > 0)
            {
                suffix = t.substring (colon);
                t = t.substring (0, colon);
            }

            // trim the decimal's trailing zeros ("24.00" -> "24",
            // "-60.0" -> "-60", "0.05" keeps its own digits)
            if (t.contains ("."))
            {
                while (t.endsWith ("0"))
                    t = t.dropLastCharacters (1);

                if (t.endsWith ("."))
                    t = t.dropLastCharacters (1);
            }

            return t + suffix;
        };

        g.setColour (engravingDim);
        g.setFont (juce::Font (juce::FontOptions (7.5f)));

        g.drawText (skirtText (slider, slider.getMinimum()),
                    juce::Rectangle<float> (bounds.getX(),
                                            bounds.getBottom() - 11.0f,
                                            (centre.x - radius * 0.55f)
                                                - bounds.getX(),
                                            11.0f),
                    juce::Justification::bottomLeft, false);

        g.drawText (skirtText (slider, slider.getMaximum()),
                    juce::Rectangle<float> (centre.x + radius * 0.55f,
                                            bounds.getBottom() - 11.0f,
                                            bounds.getRight()
                                                - (centre.x + radius * 0.55f),
                                            11.0f),
                    juce::Justification::bottomRight, false);
    }
}
