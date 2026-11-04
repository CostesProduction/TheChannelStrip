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

    // v2.87.0 (Clement's remark 4, the AMEK 9099's pastel tiles read
    // in two warm beiges): the zones wear a LOCAL background tile -
    // tone A a broken white, tone B a honey beige, both LIGHT like
    // the cream front so a tile reads as a shade of the panel itself,
    // and far enough apart to tell the two zones apart at a glance
    const juce::Colour zoneTileA (0xfff9f3e3);
    const juce::Colour zoneTileB (0xffe6d5a9);

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

    // the IMAGE section (v2.84.0)
    phaseInvert   = apvts.getRawParameterValue ("phaseInvert");
    stereoBalance = apvts.getRawParameterValue ("stereoBalance");
    stereoWidth   = apvts.getRawParameterValue ("stereoWidth");
    monoMakerHz   = apvts.getRawParameterValue ("monoMakerHz");

    // the output trim (v2.86.0, Clement's request)
    trimOutDb     = apvts.getRawParameterValue ("trimOutDb");

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
             && phaseInvert != nullptr && stereoBalance != nullptr
             && stereoWidth != nullptr && monoMakerHz != nullptr
             && trimOutDb != nullptr
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

    // v2.80.0 (Clement's remark 6): ONE Hz language for every corner
    // the console displays - plain integer hertz below 1 kHz ("90
    // Hz"), one-decimal kHz the moment it crosses ("1.2 kHz", never
    // "1200 Hz"), and the useless ".0" dropped ("16 kHz", never
    // "16.0 kHz"). No readout grows: the new words are never wider
    // than the old ones. The (value, int) pair speaks the parameter
    // attributes' own stringFromValue signature
    const auto hzText = [] (float value, int)
    {
        if (value >= 999.5f)
        {
            auto k = juce::String (value * 0.001f, 1);

            if (k.endsWith (".0"))
                k = k.dropLastCharacters (2);

            return k + " kHz";
        }

        return juce::String (juce::roundToInt (value)) + " Hz";
    };

    // and its parser half: "kHz" read FIRST (a "kHz" also ends in
    // "hz"), then the bare "hz" suffix - so a typed "1.2 kHz" lands
    // on 1200 and never on 1.2
    const auto hzParse = [] (const juce::String& text)
    {
        auto t = text.trim();
        const bool kHz = t.endsWithIgnoreCase ("khz");

        if (t.endsWithIgnoreCase ("hz"))
            t = t.dropLastCharacters (2).trim();

        const float parsed = t.replaceCharacter (',', '.').getFloatValue();

        return kHz ? 1000.0f * parsed : parsed;
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
            .withStringFromValueFunction (hzText)
            .withValueFromStringFunction (hzParse)));

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
    // two-significant-figure grid (v2.70.0, Clement's remark 5);
    // v2.80.0 (Clement's remark 6): the readout speaks the console's
    // one hzText language now - "240 Hz" at the floor, "24 kHz" at
    // the ceiling - and a typed number without a unit is read as
    // kHz, the unit the knob itself displays
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "highCutHz", 1 }, "High Cut",
        makeLogRange (240.0f, 24000.0f, twoSigFigSnap()), 24000.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (hzText)
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
    // 60 Hz to 3 kHz (v2.80.0: the readout speaks hzText - "2.5 kHz"
    // at the top of its sweep, never "2500 Hz")
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "eq2Hz", 1 }, "EQ 2 Frequency",
        makeLogRange (60.0f, 3000.0f, twoSigFigSnap()), 400.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (hzText)
            .withValueFromStringFunction (hzParse)));

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
                // v2.79.0 (Clement's remark 2): the sweep moved into
                // its band's margin column, so the readout shrank to
                // the number and its unit alone - "1.0 oct"
                return juce::String (value, 1) + " oct";
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
    // both knobs on the two-significant-figure grid (v2.80.0: one
    // decimal now - "2.5 kHz", never "2.50 kHz")
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "eq3Hz", 1 }, "EQ 3 Frequency",
        makeLogRange (500.0f, 12000.0f, twoSigFigSnap()), 1600.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (hzText)
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
    // and (v2.79.0) the same short "1.0 oct" readout as band 2's
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "eq3Oct", 1 }, "EQ 3 Width",
        eqOctaveRange(), 1.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (value, 1) + " oct";
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
    // the same two-significant-figure grid (v2.80.0: the one hzText
    // language - "7.2 kHz", never "7.20 kHz")
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "eq4Hz", 1 }, "EQ 4 Frequency",
        makeLogRange (600.0f, 18000.0f, twoSigFigSnap()), 7200.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (hzText)
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
                // v2.79.0 (Clement's remark 4): the selector line lost
                // its caption, so the readout says its own name at rest
                return value < 0.5f ? juce::String ("KNEE hard")
                                    : juce::String (juce::roundToInt (value)) + " dB";
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                const auto t = text.trim();

                if (t.containsIgnoreCase ("hard") || t.containsIgnoreCase ("off"))
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

    // v2.80.0 (Clement's remark 6): the sidechain readouts draw their
    // own curve glyph beside the value now (the slider's readout
    // label), so the words spell only the value - "off" at rest,
    // hzText once moved - and the same shared lambdas serve all four
    // selectors, the expander's pair and the compressor's. The pair
    // travels BY VALUE in the capture: the attribute holds a
    // std::function, so a capturing lambda is legal, and the little
    // helpers are plain words of this function's own vocabulary
    const auto scHpfText = [hzText] (float value, int)
    {
        if (value < 16.5f)
            return juce::String ("off");

        return hzText (value, 0);
    };

    const auto scHpfParse = [hzParse] (const juce::String& text)
    {
        auto t = text.trim();

        if (t.containsIgnoreCase ("off"))
            return 16.0f;

        return hzParse (t);
    };

    const auto scLpfText = [hzText] (float value, int)
    {
        if (value > 23999.5f)
            return juce::String ("off");

        return hzText (value, 0);
    };

    const auto scLpfParse = [hzParse] (const juce::String& text)
    {
        auto t = text.trim();

        if (t.containsIgnoreCase ("off"))
            return 24000.0f;

        return hzParse (t);
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

    // ------------------------------------------------------------------
    // the IMAGE section (v2.84.0, Clement's remarks 3 to 6): four
    // controls over the stereo image itself, filling four of the five
    // engraved reserved frames of the output chain (OUTPUT TRIM
    // stays the last empty one). Every one parks at a fully neutral
    // default - a fresh strip is sample-for-sample the strip of
    // v2.83 - and the stages hard-bypass at their rest positions
    // ------------------------------------------------------------------

    // the phase invert plate: the empty-set symbol flips the POLARITY
    // of the whole programme - both channels, an exact 180 degrees -
    // the standard polarity check on a console's bus
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "phaseInvert", 1 }, "Phase Invert", false,
        juce::AudioParameterBoolAttributes()
            .withStringFromValueFunction ([] (bool value, int)
            {
                return value ? juce::String ("inverted") : juce::String ("normal");
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                const auto t = text.trim();

                return ! (t.equalsIgnoreCase ("normal")
                            || t.equalsIgnoreCase ("off") || t == "0");
            })));

    // the stereo balance: -100 (full left) to +100 (full right), 0
    // the centre. A TRUE balance, never a pan: the channel you turn
    // TOWARD keeps its exact unity gain and only the OPPOSITE
    // channel is attenuated, on a quarter-cosine taper - half
    // travel sits at the constant-power -3 dB, and the far channel
    // is EXACTLY silent at the stops - so a stereo source is never
    // narrowed the way a pan-pot law would narrow it
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "stereoBalance", 1 }, "Stereo Balance",
        makeSteppedRange (-100.0f, 100.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                if (std::abs (value) < 0.5f)
                    return juce::String ("CENTER");

                return value < 0.0f
                    ? juce::String ("L ") + juce::String (-juce::roundToInt (value))
                    : juce::String ("R ") + juce::String (juce::roundToInt (value));
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                const auto t = text.trim();

                if (t.startsWithIgnoreCase ("c"))
                    return 0.0f;

                const bool left = t.startsWithIgnoreCase ("l");
                const float magnitude = std::abs (t.getFloatValue());

                return juce::jlimit (-100.0f, 100.0f, left ? -magnitude : magnitude);
            })));

    // the stereo width: 0 % (mono) to 200 %, 100 % the honest middle.
    // M/S arithmetic - the side signal scales by width / 100 - so
    // 0 % pins both channels to the mid, 200 % doubles the
    // difference, and 100 % is hard-bypassed in the audio
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "stereoWidth", 1 }, "Stereo Width",
        makeSteppedRange (0.0f, 200.0f, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (juce::roundToInt (value)) + " %";
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                const auto t = text.trim();
                const float parsed = (t.endsWithChar ('%')
                                        ? t.dropLastCharacters (1) : t).trim()
                                            .getFloatValue();

                return juce::jlimit (0.0f, 200.0f, parsed);
            })));

    // the mono maker: everything below the corner folds to the
    // centre line - a 2nd-order Butterworth lowpass per channel, the
    // two halves of the band averaged into one mono voice, re-joined
    // with the untouched top (the sample minus its own lowpass - a
    // perfectly complementary split, no phasey crossover seam). 0 is
    // OFF, the whole stage bypassed
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "monoMakerHz", 1 }, "Mono Maker",
        makeSteppedRange (0.0f, 2000.0f, 10.0f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([hzText] (float value, int)
            {
                return value < 5.0f ? juce::String ("off") : hzText (value, 0);
            })
            .withValueFromStringFunction ([hzParse] (const juce::String& text)
            {
                const auto t = text.trim();

                if (t.isEmpty() || t.startsWithIgnoreCase ("off"))
                    return 0.0f;

                return juce::jlimit (0.0f, 2000.0f, hzParse (t));
            })));

    // v2.86.0 (Clement's request): the TRIM OUT - the last gain the
    // signal rides on its way out, the SHAPE OUT block's own knob.
    // The trim in's own sweep: -24 to +12 dB in 0.1 dB steps, unity
    // at the default
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "trimOutDb", 1 }, "Trim Out",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (dbText)
            .withValueFromStringFunction (dbParse)));

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

    // v2.86.0: the output trim rides the same 20 ms one-pole - a
    // ridden knob is a slow ramp, the trim's own discipline
    trimOutGain.reset (currentSampleRate, 0.02);
    trimOutGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (trimOutDb->load()));

    // the IMAGE section's own states (v2.84.0): the balance and
    // width gains ride the trim's own 20 ms ramp, the mono maker's
    // Butterworth pair starts from silence with its corner cache
    // forced to rebuild
    balanceGainL.reset (currentSampleRate, 0.02);
    balanceGainR.reset (currentSampleRate, 0.02);
    widthAmount.reset (currentSampleRate, 0.02);
    balanceGainL.setCurrentAndTargetValue (1.0f);
    balanceGainR.setCurrentAndTargetValue (1.0f);
    widthAmount.setCurrentAndTargetValue (1.0f);

    for (auto& section : monoMakerLp)
        section.reset();

    lastMonoMakerHz = -1.0f;

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

    // v2.86.0: the output trim tracks its own knob the same way -
    // one target per block, the 20 ms ramp carrying every change
    trimOutGain.setTargetValue (juce::Decibels::decibelsToGain (trimOutDb->load()));

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

    // ------------------------------------------------------------------
    // the IMAGE section (v2.84.0, Clement's remarks 3 to 6): four
    // controls over the stereo image itself, evaluated once per
    // block like every other knob. Each stage is hard-bypassed at
    // its rest position - a parked strip is sample-for-sample the
    // strip of v2.83. The phase plate folds into the input gain's
    // own sign (stage 1), and the balance, width and mono maker run
    // as stage 4, after the tilt, in the panel's own order - the
    // balance rides FIRST so the mono maker can pin the bass back
    // to the centre line whatever the balance says, and the width's
    // side-scaling keeps the balance's intent above the mono band
    // ------------------------------------------------------------------
    const bool phaseFlip = phaseInvert->load() > 0.5f;

    const float balanceRaw = stereoBalance->load();
    const bool  balanceOn  = std::abs (balanceRaw) > 0.5f;
    const float balanceAmount = juce::jlimit (0.0f, 1.0f,
                                                std::abs (balanceRaw) * 0.01f);

    // the quarter-cosine taper: the channel you turn TOWARD keeps
    // its exact unity, the opposite channel eases down to an
    // EXACT silence at the stops (half travel = -3 dB, the
    // constant-power point)
    const float balanceAtt = (balanceAmount >= 1.0f)
        ? 0.0f
        : std::cos (balanceAmount * juce::MathConstants<float>::halfPi);

    balanceGainL.setTargetValue (balanceRaw > 0.5f ? balanceAtt : 1.0f);
    balanceGainR.setTargetValue (balanceRaw < -0.5f ? balanceAtt : 1.0f);

    const float widthRaw = stereoWidth->load();
    const bool  widthOn  = std::abs (widthRaw - 100.0f) > 0.5f;
    widthAmount.setTargetValue (widthOn ? widthRaw * 0.01f : 1.0f);

    // the mono maker's corner rides the same cache discipline as the
    // sidechains; OFF rests the pair and forces the rebuild
    const float monoHz = juce::jlimit (0.0f, 2000.0f, monoMakerHz->load());
    const bool  monoOn = monoHz > 5.0f
                          && monoHz < 0.45f * (float) currentSampleRate;

    if (monoOn && std::abs (monoHz - lastMonoMakerHz) > 1.0e-4f)
    {
        lastMonoMakerHz = monoHz;

        const double w0 = juce::MathConstants<double>::twoPi
                              * (double) monoHz / currentSampleRate;
        const double cs = std::cos (w0);
        const double alpha = std::sin (w0) * juce::MathConstants<double>::sqrt2
                                 * 0.5;   // sin (w0) / (2 * 0.7071)
        const double a0 = 1.0 + alpha;

        const float b0 = (float) ((1.0 - cs) * 0.5 / a0);
        const float b1 = (float) ((1.0 - cs) / a0);
        const float a1 = (float) (-2.0 * cs / a0);
        const float a2 = (float) ((1.0 - alpha) / a0);

        for (auto& section : monoMakerLp)
            section.setCoefficients (b0, b1, b0, a1, a2);
    }
    else if (! monoOn)
    {
        for (auto& section : monoMakerLp)
            section.reset();

        lastMonoMakerHz = -1.0f;
    }

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
        // one smoothed gain per sample, shared by every channel - and
        // v2.84.0: the phase plate rides the input gain's own SIGN,
        // an exact negation when lit, the untouched multiply when
        // parked (Clement's remark 3)
        const float gainIn = phaseFlip ? -trimInGain.getNextValue()
                                       : trimInGain.getNextValue();

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

        // stage 4 (v2.84.0): the IMAGE section - balance, width, mono
        // maker, in the panel's own order. Every stage is bypassed at
        // its rest position (a parked strip is bit-for-bit the v2.83
        // strip), and a mono stream skips the whole stage - an image
        // control needs two channels to say anything
        if (numChannels > 1)
        {
            float left = channels[0][i];
            float right = channels[1][i];

            // the stereo balance: only the channel turned AWAY from
            // is attenuated, on the eased taper, both gains riding
            // the 20 ms one-pole - a ridden knob is a slow ramp
            if (balanceOn)
            {
                left  *= balanceGainL.getNextValue();
                right *= balanceGainR.getNextValue();
            }

            // the stereo width: M/S arithmetic, the side scaled by
            // width / 100 - 0 % pins both channels to the mid, 200 %
            // doubles the difference, 100 % is the honest middle
            if (widthOn)
            {
                const float widthNorm = widthAmount.getNextValue();
                const float mid  = 0.5f * (left + right);
                const float side = widthNorm * 0.5f * (left - right);
                left  = mid + side;
                right = mid - side;
            }

            // the mono maker: the band below the corner folds to the
            // centre line - each channel's lowpassed half is replaced
            // by the two halves' mono average, the complement
            // (the sample minus its own lowpass) riding through
            // untouched, so the split is perfectly complementary
            if (monoOn)
            {
                const float lowL = monoMakerLp[0].processSample (left);
                const float lowR = monoMakerLp[1].processSample (right);
                const float lowMono = 0.5f * (lowL + lowR);
                left  = (left  - lowL) + lowMono;
                right = (right - lowR) + lowMono;
            }

            channels[0][i] = left;
            channels[1][i] = right;
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

    // v2.86.0 (Clement's request): the TRIM OUT - the last gain the
    // signal rides on its way out. The drive stage keeps its own
    // early-out discipline (the blend term vanishes at drive 0, so a
    // parked drive writes nothing), but the trim out must still
    // speak after it - the whole drive block now sits under its own
    // guard, and the trim out closes the chain
    if (drive > 0.0f)
    {

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

    }   // the drive stage's own guard

    // v2.86.0 (Clement's request): the TRIM OUT, the very end of the
    // chain - after the drive, the last word on the way out. It rides
    // the same discipline as the trim in: the multiply always runs,
    // and a parked knob (exactly 0 dB, the default) settles at exactly
    // 1.0f - x * 1.0f is the x that came in, bit for bit, so the strip
    // keeps its parked bit-exactness while a ridden knob ramps home on
    // the 20 ms one-pole like every gain on the panel
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            channels[ch][i] *= trimOutGain.getNextValue();
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

    // v2.87.0 (Clement's remark 4, the AMEK 9099's pastel zones): the
    // ten LOCAL background tiles, added FIRST so they paint behind
    // every knob, plate and word. Tone A - a broken white - wears the
    // TRIM IN, the phase plate, the stereo width, the tilt and the
    // trim out; tone B - a honey beige - wears the two cuts, the
    // stereo balance, the mono maker and the drive with its OS plate.
    // Every tile carries the panel's own grain and never intercepts a
    // click, so the streets stay empty and the knobs keep their drag
    // (highCut is the LPF, lowCut the HPF - the knobs' own names)
    trimInZone.tint = zoneTileA;
    highCutZone.tint = zoneTileB;
    lowCutZone.tint = zoneTileB;
    phaseZone.tint = zoneTileA;
    balanceZone.tint = zoneTileB;
    widthZone.tint = zoneTileA;
    monoMakerZone.tint = zoneTileB;
    tiltZone.tint = zoneTileA;
    driveZone.tint = zoneTileB;
    trimOutZone.tint = zoneTileA;

    for (auto* tile : getZoneTiles())
    {
        tile->grain = &grainTexture;
        addAndMakeVisible (*tile);
    }

    // the ergonomic knob setup shared by all five: horizontal-or-vertical
    // rotary drag, a wide 320 px travel (fine adjustments without
    // touching the keyboard), double-click back to the default, and an
    // editable readout under the cap for exact typed values. v2.62.0
    // (Clement's remark 4): each knob also carries its cap colour on
    // the backgroundColourId. v2.63.0 (Clement's remarks 2-4): a
    // COMMAND-CLICK parks the knob on its default too (CostesSlider),
    // and the HIGH CUT knob joins the HF band on the pastel red cap
    // v2.78.0 (Clement's remark 5): the caption label became optional
    // - the EQ's FREQ/GAIN words are gone, its knobs speak through
    // their readouts alone, so those eight pass nullptr here
    const auto setUpKnob = [this] (CostesSlider& knob, juce::Label* label,
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

        if (label != nullptr)
        {
            label->setText (labelText, juce::dontSendNotification);
            label->setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            label->setJustificationType (juce::Justification::centred);
            label->setColour (juce::Label::textColourId, engraving);
            addAndMakeVisible (*label);
        }
    };

    setUpKnob (trimInKnob, &trimInLabel,  "TRIM IN",  0.0f,    knobDisc);
    // v2.80.0 (Clement's remark 1): the cuts lost their caption words
    // - the merged HPF / LPF button IS the name now, lit when the
    // filter processes, black when it is bypassed, and it takes the
    // old caption's own top-left corner
    setUpKnob (lowCutKnob,   nullptr, "",  16.0f,   knobDisc);
    // v2.63.0 (Clement's remark 4): the LPF knob wears the same
    // pastel red cap as the EQ's HF band
    setUpKnob (highCutKnob,  nullptr, "", 24000.0f, eqHfDisc);
    // v2.86.0 (Clement's request): the tilt rides the SHAPE OUT
    // block now, its own caption word under its knob again
    setUpKnob (tiltKnob,   &tiltLabel,     "TILT",      0.0f,    knobDisc);
    setUpKnob (driveKnob, &driveLabel,   "DRIVE",    0.0f,    knobDisc);

    // the IMAGE section's knobs (v2.84.0, Clement's remarks 4 to 6):
    // the stereo balance parks at CENTER, the stereo width at 100 %,
    // and the mono maker at 0 = off - double-click parks each one
    // back to its honest, bit-exact neutral. v2.86.0 (Clement's
    // request): the IMAGE block's engraved name no longer spells
    // each knob's own title - every knob carries its caption word
    // again, the way the whole column reads now
    setUpKnob (balanceKnob,    &balanceLabel,    "STEREO BALANCE",   0.0f,  knobDisc);
    setUpKnob (widthKnob,      &widthLabel,      "STEREO WIDTH",   100.0f,  knobDisc);
    setUpKnob (monoMakerKnob, &monoMakerLabel,  "MONO MAKER",       0.0f,  knobDisc);

    // v2.86.0 (Clement's request): the TRIM OUT - the SHAPE OUT
    // block's last knob, the gain the signal rides on its way out
    setUpKnob (trimOutKnob, &trimOutLabel, "TRIM OUT", 0.0f, knobDisc);

    // the EQ knobs (v2.61.0): the same ergonomics, one FREQ and one GAIN
    // per band - double-click parks each band on its SSL-style starting
    // point (100 Hz, 400 Hz, 1600 Hz, 7200 Hz) or 0 dB. v2.62.0 (remark
    // 4): the caps speak the SSL band colours - LF keeps the black
    // aluminium, LMF pastel blue, HMF off-white, HF pastel red.
    // v2.78.0 (Clement's remark 5): no caption words any more - the
    // knobs speak through their readouts alone
    setUpKnob (eq1HzKnob,   nullptr, "", 100.0f,  knobDisc);
    setUpKnob (eq1GainKnob, nullptr, "", 0.0f,    knobDisc);
    setUpKnob (eq2HzKnob,   nullptr, "", 400.0f,  eqLoMidDisc);
    setUpKnob (eq2GainKnob, nullptr, "", 0.0f,    eqLoMidDisc);
    setUpKnob (eq3HzKnob,   nullptr, "", 1600.0f, eqHiMidDisc);
    setUpKnob (eq3GainKnob, nullptr, "", 0.0f,    eqHiMidDisc);
    setUpKnob (eq4HzKnob,   nullptr, "", 7200.0f, eqHfDisc);
    setUpKnob (eq4GainKnob, nullptr, "", 0.0f,    eqHfDisc);

    // the section glyphs (v2.60.0, Clement's remark): each icon sits at
    // the left of its caption and paints itself - nothing to add here
    // beyond making them visible
    addAndMakeVisible (trimIcon);
    addAndMakeVisible (lowCutIcon);
    addAndMakeVisible (highCutIcon);
    // v2.86.0: the tilt's own glyph is back - the SHAPE OUT block
    // spells its name at its head, but every knob carries its own
    // caption word and glyph under it again, the column's new way
    addAndMakeVisible (tiltIcon);
    addAndMakeVisible (trimOutIcon);
    addAndMakeVisible (driveIcon);

    // the band glyphs (v2.62.0; re-cut in v2.78.0, Clement's remarks 4
    // and 6): all four bands keep a free glyph under their names now -
    // the two mid bells, and the outer bands' own shapes, which FOLLOW
    // their BELL/SHELF plates and repaint with them (the icon picks its
    // shape at paint time from the plate it shadows)
    addAndMakeVisible (eqBand1Icon);
    addAndMakeVisible (eqBand2Icon);
    addAndMakeVisible (eqBand3Icon);
    addAndMakeVisible (eqBand4Icon);
    eqBand1Icon.follows = &eq1ModeButton;
    eqBand4Icon.follows = &eq4ModeButton;

    // v2.78.0 (Clement's remark 1): the front end is ONE block now and
    // its name is the console's own signature - COSTES, the trace,
    // CONSOLE - painted across the top band where INPUT once read,
    // the same wording that ran down the panel's left edge since
    // v2.70.0. The signature lives in paint()

    // the four band captions (v2.61.0; v2.78.0, Clement's remark 7):
    // LF, LMF, HMF, HF - the SSL E-series naming, each in its band's
    // margin column
    const auto setUpBandCaption = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, engraving);
        addAndMakeVisible (label);
    };

    setUpBandCaption (eqBand1Label, "LF");
    setUpBandCaption (eqBand2Label, "LMF");
    setUpBandCaption (eqBand3Label, "HMF");
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

    // v2.81.0 (Clement's remark 3): the cut slopes shrink to the EQ
    // selectors' own 52 px geometry and speak their "12 dB/oct"
    // SMALLER - readoutFontHeight hands the font to the
    // look-and-feel's factory, so the box is born with it; and the
    // sliders carry `look` themselves, the same root-cause fix the SC
    // selectors' glyph got (a parentless slider answers the DEFAULT
    // look-and-feel at setTextBoxStyle time, and the factory is where
    // the font lands)
    lowCutSlope.readoutFontHeight = 10.0f;
    highCutSlope.readoutFontHeight = 10.0f;
    lowCutSlope.setLookAndFeel (&look);
    highCutSlope.setLookAndFeel (&look);

    setUpSlope (lowCutSlope, 2.0f, 52);
    setUpSlope (highCutSlope, 2.0f, 52);

    // the bandwidth sweeps of EQ bands 2/3 (v2.61.0): the same horizontal
    // selector the cuts use for their slopes, reading the bell's width
    // in octaves - and (v2.79.0, Clement's remark 2) the sweep moved
    // into its band's margin column, all small, its readout shrunk to
    // the bare "1.0 oct"
    setUpSlope (eq2OctSlider, 1.0f, 48);
    setUpSlope (eq3OctSlider, 1.0f, 48);

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
    // v2.80.0 (Clement's remarks 4 and 5): both THRESH knobs wear the
    // console's own red - the HF band's saturated disc, the colour
    // their IN ladders' new threshold bar speaks too
    setUpKnob (expThresholdKnob, &expThresholdLabel, "THRESH", -50.0f, eqHfDisc);
    setUpKnob (expRangeKnob, &expRangeLabel,     "RANGE",    0.0f, expKnobDisc);
    setUpKnob (expReleaseKnob, &expReleaseLabel,   "RELEASE",   0.3f, expKnobDisc);
    setUpKnob (expRatioKnob, &expRatioLabel,      "RATIO",    2.0f, expKnobDisc);
    setUpKnob (expMixKnob, &expMixLabel,        "MIX",     100.0f, expKnobDisc);

    setUpKnob (compThresholdKnob, &compThresholdLabel, "THRESH",  0.0f, eqHfDisc);
    setUpKnob (compRatioKnob, &compRatioLabel,     "RATIO",   2.0f, compKnobDisc);
    setUpKnob (compReleaseKnob, &compReleaseLabel,  "RELEASE", 0.25f, compKnobDisc);
    setUpKnob (compGainKnob, &compGainLabel,      "GAIN",    0.0f, compKnobDisc);
    setUpKnob (compMixKnob, &compMixLabel,      "MIX",     100.0f, compKnobDisc);

    // the sidechain pair of each stage rides the cuts' own horizontal
    // selector - the SC HPF parks at 16 Hz, the SC LPF at 24000 Hz,
    // and both rest positions leave the detector a flat band.
    // v2.80.0 (Clement's remark 6): every SC selector draws its own
    // curve in the readout now - the HPF's own glyph on the two HPF
    // sliders, the LPF's on the two LPF ones - so the value reads
    // [curve] "off" at rest and [curve] "90 Hz" once moved, and the
    // words "SC HPF" are gone. The flags ride BEFORE setUpSlope: the
    // readout label is created once, inside setTextBoxStyle, and the
    // look-and-feel's factory reads the flag right there.
    // v2.81.0 - THE FIX THE v2.80.0 BUILD NEEDED: the factory never
    // ran, because at setTextBoxStyle time the slider is not attached
    // to the editor yet, and JUCE hands the readout box to the
    // DEFAULT look-and-feel, whose plain label has no glyph. The four
    // SC selectors carry `look` THEMSELVES now, so their readout is
    // born from our factory with the flags already set - the curve
    // really stands at the value's left, on screen this time
    compHpfSlider.setLookAndFeel (&look);
    compScLpfSlider.setLookAndFeel (&look);
    expScHpfSlider.setLookAndFeel (&look);
    expScLpfSlider.setLookAndFeel (&look);

    compHpfSlider.readoutGlyph     = SectionIcon::Icon::lowCut;
    compHpfSlider.hasReadoutGlyph  = true;
    compScLpfSlider.readoutGlyph   = SectionIcon::Icon::highCut;
    compScLpfSlider.hasReadoutGlyph = true;
    expScHpfSlider.readoutGlyph    = SectionIcon::Icon::lowCut;
    expScHpfSlider.hasReadoutGlyph  = true;
    expScLpfSlider.readoutGlyph    = SectionIcon::Icon::highCut;
    expScLpfSlider.hasReadoutGlyph = true;

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

    // v2.79.0 (Clement's remark 4): the SC row lost its captions -
    // every selector's readout identifies itself now ("off" with its
    // own curve glyph, "KNEE hard", the LINK's own words), so the
    // labels are gone and the strip sits that much shorter.
    // v2.80.0 (Clement's remark 7): the EXPANDER and COMPRESSOR
    // section titles are gone too - the stages' own EXP / COMP
    // plates name them, and the header band the titles took returns
    // to the knobs below

    // v2.68.0 (Clement's remark): the dynamics sections carry no
    // glyphs any more - the stages read through their captions, their
    // knob colours and their ladders alone

    // the oversampling switch, an SSL-style illuminated plate under the
    // drive knob (v2.57.0, Clement's remark 2): dark plate with a dead
    // LED and a dim caption when off, glowing amber plate with a lit LED
    // and the caption spelled "OS 2x ON" when on - the state reads at a
    // glance, where the old default checkbox tick was too quiet to see.
    // v2.61.0: the caption moved into the plate painter, so the EQ's own
    // on/off plate speaks the same language
    oversampleButton.caption = "OS 2x";
    addAndMakeVisible (oversampleButton);

    // the EQ's global on/off plate (v2.61.0): v2.78.0 (Clement's
    // remark 2) made it the section's own NAME - centred in the
    // header band where the EQ caption and its bell glyph once read,
    // owning the whole section's bypass - "EQ ON" lit amber, "EQ
    // OFF" dark
    eqOnButton.caption = "EQ";
    addAndMakeVisible (eqOnButton);

    // the band-1/4 shape plates (v2.61.0): lit "BELL" or dark "SHELF",
    // one per outer band. v2.78.0 (Clement's remark 4): the plates
    // moved into their bands' margin columns - under the name and its
    // glyph - and gave up the shape they carried inside (v2.70.0);
    // the shape stands under the name now, follows the plate, and
    // the shelfGlyph member still says which shelf the band speaks -
    // the low shelf on band 1, the high shelf on band 4
    eq4ModeButton.shelfGlyph = CostesStripEditor::SectionIcon::Icon::highShelf;
    addAndMakeVisible (eq1ModeButton);
    addAndMakeVisible (eq4ModeButton);

    // the cut on/off plates (v2.80.0, Clement's remark 1): the ON
    // plate and the filter's caption are ONE button now - a
    // ShelfModeToggle-style plate that says "HPF" / "LPF" in both
    // states, lit amber when the filter processes, black with a
    // dead LED when it is bypassed - and it takes the old caption's
    // own top-left corner. It owns the bypass; the slope sweep
    // itself starts at 6 dB/oct
    lowCutOnButton.cutWord = "HPF";
    addAndMakeVisible (lowCutOnButton);
    highCutOnButton.cutWord = "LPF";
    addAndMakeVisible (highCutOnButton);

    // the IMAGE section's phase plate (v2.84.0, Clement's remark 3):
    // v2.86.0 (Clement's request): the plate moved to the head of the
    // IMAGE block and carries its own words now - "PHASE INVERT"
    // spelled beside the LED, the empty-set symbol at its right, the
    // same plate language as every other switch on the panel
    addAndMakeVisible (phaseButton);

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

    // v2.80.0 (Clement's remarks 4 and 5): the THRESHOLD bar - each
    // IN ladder carries its stage's own threshold corner as a red
    // bar, fed straight from the parameter its THRESH knob drives, so
    // the bar and the knob can never disagree
    expInMeter.thresholdDb =
        [this] { return owner.getApvts().getRawParameterValue ("expThreshold")->load(); };
    compInMeter.thresholdDb =
        [this] { return owner.getApvts().getRawParameterValue ("compThreshold")->load(); };

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
    trimOutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "trimOutDb", trimOutKnob);
    oversampleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        owner.getApvts(), "driveOversampled", oversampleButton);

    // the IMAGE section's own attachments (v2.84.0): the phase plate
    // and the three knobs ride their parameters like every other
    // control on the panel
    phaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        owner.getApvts(), "phaseInvert", phaseButton);
    balanceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "stereoBalance", balanceKnob);
    widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "stereoWidth", widthKnob);
    monoMakerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        owner.getApvts(), "monoMakerHz", monoMakerKnob);

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
    // the texture is the same pixels every launch. One dark speckle
    // in eight and one light one, each at a whisper of alpha.
    // v2.80.0 (Clement's remark 8): the felt finish DEEPENED - the
    // alphas climbed from the old 3-to-6 whisper to 7 and up, so the
    // speckles read as the painted-steel grain of a real module the
    // way the brainworx console's own background does, still fine
    // noise and never a poster. The tile is a member image, the paint
    // tiles it - no per-frame cost beyond the blits the clipping
    // region asks for
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
                        .withAlpha ((juce::uint8) (8 + ((n >> 4) & 3u))));
                else if (bucket > 12u)
                    grainData.setPixelColour (x, y, juce::Colour (0xfffdf6e3)
                        .withAlpha ((juce::uint8) (7 + ((n >> 4) & 3u))));
            }
        }
    }

    // v2.78.0 (Clement's remarks): the console TIGHTENS AGAIN - the
    // signature moves from the left edge to the merged front-end
    // block's own header, the EQ caption words and band intertitles
    // are gone, and the panel drops from 860 x 814 to 860 x 764.
    // v2.81.0 (Clement's remark 3): the two front-end lines grow 30 px
    // each for the fatter filter knobs (the 107 x 72 squares, 54 px
    // discs) and their centred slope selectors - the panel takes the
    // 60 px back, 860 x 824, and the EQ keeps its own whole room.
    // v2.85.0 (Clement's request): the front end INTERLOCKS - the
    // tilt leaves for the output column, the trim rides the top
    // middle with the phase plate under it, and the cuts' own
    // button + slope stacks ride under their knobs - the two 154 px
    // lines become ONE 288 px block and the panel drops another
    // 24 px, 860 x 800, the EQ keeping its own 105 px pitch
    // v2.86.0 (Clement's request): the console loses another 14 px -
    // the phase plate left for the output column and the TRIM IN
    // caption moved to the trim's side, the front-end block dropping
    // from 288 to 274, the EQ keeping its own whole 105 px room
    setSize (860, 786);

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
    phaseAttachment.reset();
    balanceAttachment.reset();
    widthAttachment.reset();
    monoMakerAttachment.reset();
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
    // the cream. The alphas live inside the tile, so the texture
    // reads as paint grain; v2.80.0 (Clement's remark 8) deepened
    // them from the old 3-to-6 whisper to a visible felt finish.
    // JUCE 8 retired drawImageTiled, so the tiles are drawn directly
    // - a few dozen tiny blits, and the clipping region keeps the
    // cost proportional to what actually repaints
    for (int tileY = 0; tileY < getHeight(); tileY += grainTexture.getHeight())
        for (int tileX = 0; tileX < getWidth(); tileX += grainTexture.getWidth())
            g.drawImageAt (grainTexture, tileX, tileY);

    g.setColour (plateFill);
    g.drawRect (getLocalBounds(), 2);

    // v2.78.0 (Clement's remark 1): the engraved console signature
    // comes BACK to the top band - horizontal now, naming the merged
    // front-end block the way INPUT once did, centred over column 1.
    // The trace, COSTES and CONSOLE read left to right: the seismic
    // line, the name in dark engraving, a small tick, CONSOLE in dim
    // caps - the same wording that ran down the panel's left edge
    // from v2.70.0 to v2.77.0, and whose margin the columns now own
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

        const auto costesFont = juce::Font (juce::FontOptions (13.0f, juce::Font::bold));
        const auto consoleFont = juce::Font (juce::FontOptions (9.5f, juce::Font::bold));
        const int costesW = costesFont.getStringWidth ("COSTES");
        const int consoleW = consoleFont.getStringWidth ("CONSOLE");

        // the group centred over column 1: the trace, a breath, the
        // name, the tick, a breath, CONSOLE
        const int groupW = 24 + 6 + costesW + 3 + 1 + 3 + consoleW;
        const int groupX = 12 + juce::jmax (0, (230 - groupW) / 2);
        const int bandY = 26;
        const int bandH = 18;

        // the seismic line, the logo's own 50 x 20 trace scaled to
        // 24 px - a heartbeat running under the console's name
        g.setColour (engraving);

        for (int i = 1; i < 14; ++i)
            g.drawLine (juce::Line<float> (groupX + trace[i - 1][0] * 0.48f,
                                           bandY + bandH * 0.5f + (trace[i - 1][1] - 10.0f) * 0.48f,
                                           groupX + trace[i][0] * 0.48f,
                                           bandY + bandH * 0.5f + (trace[i][1] - 10.0f) * 0.48f), 1.8f);

        // the name itself: COSTES in engraving, a small tick, CONSOLE
        // in dim caps - the same stacked wording as ever
        const int nameX = groupX + 24 + 6;
        g.setFont (costesFont);
        g.drawText ("COSTES", juce::Rectangle<int> (nameX, bandY, costesW, bandH),
                    juce::Justification::centredLeft);
        g.setColour (engravingDim.withAlpha (0.7f));
        g.drawVerticalLine (nameX + costesW + 3, bandY + 3.0f, bandY + 15.0f);
        g.setFont (consoleFont);
        g.drawText ("CONSOLE", juce::Rectangle<int> (nameX + costesW + 7, bandY,
                                                     consoleW, bandH),
                    juce::Justification::centredLeft);
    }

    // v2.75.0 (Clement's request): the silkscreen follows the three
    // columns - two long vertical dividers separate the front end
    // from the dynamics and the dynamics from the output chain, a
    // short engraved rule names each of column 1's two bands
    // (v2.78.0: the COSTES CONSOLE block and the EQ), and the
    // reserved slots draw their own empty frames with the section's
    // name at the frame's head
    g.setColour (engravingDim.withAlpha (0.5f));

    g.drawVerticalLine (248, 32.0f, (float) getHeight() - 12.0f);
    g.drawVerticalLine (700, 32.0f, (float) getHeight() - 12.0f);

    g.drawLine (14.0f, 47.0f, 240.0f, 47.0f, 1.0f);
    // v2.80.0: the rule rides under the EQ's header band.
    // v2.83.0 (Clement's remark 3): the taller front-end rows had
    // carried it down to y = 339 - straight through the HPF knob - so
    // it returns to its own place: JUST above the EQ ON plate.
    // v2.86.0: the tighter front end carries the header up with it -
    // the plate's band starts at y = 340 (the signature 26 + the
    // 274 px block + the 12 px breath + the EQ area's own 4 px), and
    // the rule rides 2 px above, at 338
    g.drawLine (14.0f, 338.0f, 240.0f, 338.0f, 1.0f);

    // v2.75.0: the reserved slots - engraved empty frames that name
    // the sections the future builds will fill (a degenerate resize
    // paints nothing rather than an inverted frame). v2.86.0
    // (Clement's request): the output column reads as TWO blocks
    // now - IMAGE (the phase plate and the image knobs) at the top
    // of the column, SHAPE OUT (the tilt, the drive, the trim out)
    // under it - and the silkscreen paints each block's own name at
    // its head
    {
        static const char* const reservedNames[2] =
            { "IMAGE", "SHAPE OUT" };

        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));

        for (int i = 0; i < 2; ++i)
        {
            auto frame = reservedFrames[(size_t) i].reduced (4, 2);

            if (frame.getWidth() < 12 || frame.getHeight() < 12)
                continue;

            g.drawRect (frame, 1);
            g.drawText (reservedNames[i],
                        juce::Rectangle<int> (frame.getX(), frame.getY(),
                                              frame.getWidth(), 16),
                        juce::Justification::centred);
        }

        auto transientSlot = transientFrame.reduced (4, 2);

        if (transientSlot.getWidth() >= 12 && transientSlot.getHeight() >= 12)
        {
            g.drawRect (transientSlot, 1);
            g.drawText ("TRANSIENT",
                        juce::Rectangle<int> (transientSlot.getX(), transientSlot.getY(),
                                              transientSlot.getWidth(), 16),
                        juce::Justification::centred);
        }
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

    // v2.81.0 (Clement's remark 4): the outer bands' glyphs repaint
    // here too - they FOLLOW their BELL/SHELF plates (the shape is
    // picked at paint time), but nothing ever told them the plate
    // had moved, so a toggle left the v2.78.0 shape stale on screen.
    // The 30 Hz pass hands the two of them their fresh shape within
    // a frame of ANY change - a plate click, a session restore,
    // anything
    eqBand1Icon.repaint();
    eqBand4Icon.repaint();
}

void CostesStripEditor::resized()
{
    auto area = getLocalBounds();

    area.removeFromTop (26);    // the engraved signature band (v2.69.0)
    area.removeFromBottom (8); // v2.75.0: the taller panel's own margin

    // v2.75.0 (Clement's spec): the console reads as THREE vertical
    // columns and the sound flows through them - down column 1 (the
    // front end: the COSTES CONSOLE block, the cuts, the EQ), down
    // column 2 (the dynamics: EXPANDER, the reserved TRANSIENT slot,
    // COMPRESSOR), down column 3 (the output chain: the reserved
    // slots and DRIVE). v2.78.0 (Clement's remark 1): the signature
    // left the left edge, so the margin it read down returns to the
    // console - the columns start at x = 12 and the front end grows
    // to 230 px
    area.removeFromLeft (12);   // the grid's own left margin

    auto column1 = area.removeFromLeft (230);
    area.removeFromLeft (12);   // the first divider's seam
    auto column2 = area.removeFromLeft (440);
    area.removeFromLeft (12);   // the second divider's seam
    auto column3 = area;        // 142 wide, 12 of margin at the right

    // ---- column 1: the front end ----
    // v2.78.0 (Clement's remark 1): ONE block now - the COSTES
    // CONSOLE signature names it from the top band (painted in
    // paint(), where INPUT once read). v2.85.0 (Clement's request):
    // the block INTERLOCKS - the tilt left for the output column,
    // so the trim rides the TOP MIDDLE, the two cuts sharing the
    // BOTTOM band, the LPF at the left edge and the HPF at the
    // right, each with its own button + slope stack riding UNDER its
    // knob. v2.86.0 (Clement's request): the phase plate left for
    // the IMAGE block and the TRIM IN caption moved to the knob's
    // LEFT, so the block drops from 288 to 274 - the console loses
    // another 14 px of height, and the EQ keeps its own whole room
    column1.removeFromTop (24);   // the signature's own band

    // v2.86.0 (Clement's request): the front end tightens again -
    // the phase plate left for the IMAGE block at the head of the
    // output column, and the TRIM IN caption rides at the KNOB'S
    // LEFT now, glyph and word one mid-height unit, so the caption's
    // own height below the trim returns to the console. The block
    // drops from 288 to 274: the trim row, a breath, then the cuts'
    // band with each button + slope stack riding under its own knob
    auto frontEnd = column1.removeFromTop (274);
    auto line = frontEnd.reduced (4, 2);   // 222 x 270

    // the input trim, TOP MIDDLE - the EQ's own 83 x 101 square,
    // centred on the cuts' own middle axis (withSizeKeepingCentre
    // would round 139 px of slack to the left and put the knob 1 px
    // off the street's own centre - the knob is placed from the
    // strip's centre itself, so the trim and the free street below
    // it read as one column)
    auto trimStrip = line.removeFromTop (101);
    trimInKnob.setBounds (juce::Rectangle<int> (trimStrip.getCentreX() - 83 / 2,
                                                trimStrip.getY(), 83, 101));

    // the trim's caption AT THE KNOB'S LEFT (v2.86.0, Clement's
    // request): glyph and word ONE centred unit riding the free 70 px
    // left of the knob, at the knob row's own mid-height.
    // v2.87.0 (Clement's remark 1): the word and its glyph ride at a
    // small breath now - the old one-unit formula drew the word
    // centred in the FULL cell while the glyph sat as if the word
    // began 18 px into the unit, so the word's first letters rode
    // INTO the glyph. The word's box is its own advance plus a 4 px
    // margin at each side - never tighter than the string's own
    // sub-pixel advance (the v2.83 lesson) - the glyph sits at the
    // box's left with its own 4 px breath, and the whole unit hangs
    // centred in the street
    const auto trimCaptionBounds = juce::Rectangle<int> (line.getX(),
                                                        trimStrip.getY() + (101 - 18) / 2,
                                                        trimInKnob.getX() - line.getX(),
                                                        18);

    const int trimTextWidth = trimInLabel.getFont()
                                  .getStringWidth (trimInLabel.getText());
    const int trimUnitW = 16 + trimTextWidth + 8;
    const int trimUnitX = trimCaptionBounds.getCentreX() - trimUnitW / 2;
    trimIcon.setBounds (trimUnitX, trimCaptionBounds.getY() + 1, 16, 16);
    trimInLabel.setBounds (trimUnitX + 16, trimCaptionBounds.getY(),
                           trimTextWidth + 8, 18);

    // the cuts' own band, under a breath: the LPF at the LEFT edge,
    // the HPF at the RIGHT, both wearing the 83 x 101 square - and
    // the middle street between them FREE now (v2.86.0: the phase
    // plate lives in the IMAGE block at the head of the output
    // column; the process order itself never moves - the phase stays
    // folded into the input gain's own sign)
    line.removeFromTop (8);
    auto cutKnobStrip = line.removeFromTop (101);
    highCutKnob.setBounds (cutKnobStrip.removeFromLeft (83));    // the LPF, bottom left
    lowCutKnob.setBounds (cutKnobStrip.removeFromRight (83));    // the HPF, bottom right

    // under each cut knob, its own stack: the merged plate centred
    // on the knob's own axis, DIRECTLY under its feet, and the slope
    // selector under the plate - the whole stack 60 px tall, ending
    // flush with the front end's own floor
    line.removeFromTop (2);
    auto plateBand = line.removeFromTop (18);
    line.removeFromTop (2);
    auto slopeBand = line.removeFromTop (38);

    highCutOnButton.setBounds (plateBand.removeFromLeft (83).withSizeKeepingCentre (56, 18));
    highCutSlope.setBounds (slopeBand.removeFromLeft (83).withSizeKeepingCentre (52, 38));

    lowCutOnButton.setBounds (plateBand.removeFromRight (83).withSizeKeepingCentre (56, 18));
    lowCutSlope.setBounds (slopeBand.removeFromRight (83).withSizeKeepingCentre (52, 38));

    // and the cut glyphs, inside their knobs' own value strips (the
    // 76 x 18 readout box every knob carries), at the Hz value's left
    // edge - the text centred in its box, the curve hugging it from
    // the left, never more than 2 px away, and never further left
    // than the knob's own edge (the v2.83 fix, kept)
    const auto cutGlyph = [] (CostesSlider& cutKnob, SectionIcon& cutIcon)
    {
        cutIcon.setVisible (true);
        auto* cutBox = dynamic_cast<juce::Label*> (cutKnob.getChildComponent (0));
        const int cutTextWidth = (cutBox != nullptr)
            ? cutBox->getFont().getStringWidth (cutBox->getText()) : 40;
        const int boxX = cutKnob.getX() + (cutKnob.getWidth() - 76) / 2;
        const int glyphX = juce::jmax (cutKnob.getX() - 8,
                                      boxX + (76 - cutTextWidth) / 2 - 18);
        cutIcon.setBounds (juce::Rectangle<int> (glyphX,
                                                 cutKnob.getBottom() - 17, 16, 16));
    };
    cutGlyph (highCutKnob, highCutIcon);
    cutGlyph (lowCutKnob, lowCutIcon);

    // v2.87.0 (Clement's remark 4): the front end's own zone tiles -
    // tone A behind the trim's whole row (the word's street and the
    // knob), tone B behind each cut's whole stack (knob, plate and
    // slope) - flush with the block's frame at the outer edges, grown
    // 6 px into the streets between them, flush with their zone's
    // own top and floor (highCut is the LPF, lowCut the HPF)
    trimInZone.setBounds (juce::Rectangle<int> (line.getX(), trimStrip.getY(),
                                                trimInKnob.getRight() + 6 - line.getX(),
                                                trimStrip.getHeight()));
    highCutZone.setBounds (juce::Rectangle<int> (line.getX(), cutKnobStrip.getY(),
                                                 83 + 6,
                                                 slopeBand.getBottom() - cutKnobStrip.getY()));
    lowCutZone.setBounds (juce::Rectangle<int> (lowCutKnob.getX() - 6,
                                                cutKnobStrip.getY(),
                                                83 + 6,
                                                slopeBand.getBottom() - cutKnobStrip.getY()));

    // v2.79.0 (Clement's remark 1): a real breath between the end of
    // the filters and the start of the EQ - the merged block's last
    // line and the section's own name never touch
    column1.removeFromTop (12);

    // ---- the EQ section (v2.61.0, Clement's spec; stacked in
    // v2.75.0; SSL-ordered in v2.77.0; v2.78.0, Clement's remarks):
    // the section closes column 1 and its four bands read as four
    // stacked rows - HF, HMF, LMF, LF down the column, the SSL way -
    // and the EQ ON plate IS the section's name now, centred in the
    // header band where the caption and its bell glyph once read.
    // v2.79.0 (Clement's remark 2): the selector lines are GONE - the
    // mids' width sweeps ride the margin column under their glyphs,
    // so each row is one knob line and the knobs own its whole room
    auto eqArea = column1;
    eqArea.removeFromTop (4);
    auto eqHeader = eqArea.removeFromTop (18);

    eqOnButton.setBounds (eqHeader.withSizeKeepingCentre (76, 18));

    // the four band cells: the GAIN knob at the LEFT of the row and
    // the FREQ at its RIGHT (v2.78.0, Clement's remark 9 - the SSL's
    // own reading), the band's margin column at the row's left
    // carrying its name, its glyph and (on the outer bands) the
    // BELL/SHELF plate, and (on the mids, v2.79.0) the width sweep
    // under the glyph - one slim strip carries the band's identity
    const int eqBandH = eqArea.getHeight() / 4;

    for (int band = 3; band >= 0; --band)
    {
        auto column = eqArea.removeFromTop (eqBandH).reduced (4, 2);

        auto* bandLabel = &eqBand1Label;
        auto* freqKnob  = &eq1HzKnob;
        auto* gainKnob  = &eq1GainKnob;
        juce::Slider* octSlider   = nullptr;
        ShelfModeToggle* modeButton = &eq1ModeButton;

        // v2.78.0 (Clement's remarks 4 and 6): every band keeps a free
        // glyph under its name now - the mid bells, and the outer
        // bands' own shapes, which follow their BELL/SHELF plates
        // (the follows member set in the constructor)
        SectionIcon* bandGlyph = &eqBand1Icon;

        if (band == 1)
        {
            bandLabel = &eqBand2Label; freqKnob = &eq2HzKnob;
            gainKnob  = &eq2GainKnob;
            octSlider = &eq2OctSlider; modeButton = nullptr;
            bandGlyph = &eqBand2Icon;
        }
        else if (band == 2)
        {
            bandLabel = &eqBand3Label; freqKnob = &eq3HzKnob;
            gainKnob  = &eq3GainKnob;
            octSlider = &eq3OctSlider; modeButton = nullptr;
            bandGlyph = &eqBand3Icon;
        }
        else if (band == 3)
        {
            bandLabel = &eqBand4Label; freqKnob = &eq4HzKnob;
            gainKnob  = &eq4GainKnob;
            modeButton = &eq4ModeButton;
            bandGlyph = &eqBand4Icon;
        }

        // v2.79.0 (Clement's remark 2): the selector line is GONE -
        // the row is one knob line and the knobs own its whole room
        auto knobRow = column;

        // the margin column (v2.77.0: the name; v2.78.0: the glyph
        // under it, then the outer bands' BELL/SHELF plate under the
        // glyph; v2.79.0: the mids' own width sweep under the glyph
        // too) - the whole band's identity in one slim strip at the
        // row's left, level with its knobs
        auto nameColumn = knobRow.removeFromLeft (56);
        bandLabel->setBounds (nameColumn.removeFromTop (16));
        bandGlyph->setBounds (nameColumn.removeFromTop (18)
                                  .withSizeKeepingCentre (16, 16));

        if (modeButton != nullptr)
            modeButton->setBounds (nameColumn.removeFromTop (20));

        // GAIN at the LEFT, FREQ at the RIGHT - no caption words any
        // more (v2.78.0, Clement's remark 5), the readouts speak alone
        const int half = knobRow.getWidth() / 2;
        gainKnob->setBounds (knobRow.removeFromLeft (half));
        freqKnob->setBounds (knobRow);

        if (octSlider != nullptr)
        {
            // v2.79.0 (Clement's remark 2): the mid bells' width
            // sweep rides the margin column now, all small under the
            // glyph - a 52 px selector whose "1.0 oct" readout rides
            // under its own track
            octSlider->setBounds (nameColumn.removeFromTop (38)
                                      .withSizeKeepingCentre (52, 38));
        }
    }

    // ---- the dynamics (v2.68.0, re-cut in v2.70.0, stacked in
    // v2.75.0): the two stages read as two stacked stages down the
    // console's middle column - the EXPANDER at the top, the
    // COMPRESSOR under it, the reserved TRANSIENT frame between them
    // exactly where the sound will one day flow. Each stage is one
    // horizontal block: the IN, OUT and GR ladders together at its
    // left edge (v2.72.0, Clement's request), and a three-by-three
    // control grid beside them - MIX, THRESH and RELEASE across the
    // top, RANGE (GAIN on the compressor) and RATIO across the middle
    // with the FAST ATK plate beside them, the SC HPF and SC LPF
    // selectors and the LISTEN push across the bottom. The knobs sit
    // smaller than the row-1 and EQ columns - the SSL 500-series
    // density. v2.70.0 (remark 6): the ATTACK and LISTEN captions are
    // gone - the plates say FAST ATK / SLOW ATK and LISTEN on their
    // own, so their cells stand full height
    auto expBlock = column2.removeFromTop (245);
    auto compBlock = column2.removeFromBottom (245);

    // v2.76.0 (Clement's remarks): the stages returned to their
    // v2.74 height, and the TRANSIENT slot keeps its own engraved
    // 220 px - centred between the two stages, the free air split
    // evenly above and below it
    transientFrame = column2.withSizeKeepingCentre (column2.getWidth(), 220);

    // one layout for both stages: the same control set in the same
    // order, the same ladders, so the two halves read as one design.
    // v2.74.0 (Clement's remark 1): FAST ATK and LISTEN moved up into
    // the middle row's third cell, and the lambda's SC row grew its
    // own third caption cell - the expander's KNEE, the compressor's
    // LINK, sitting level with the SC selectors
    auto layoutDynStage = [] (juce::Rectangle<int> block,
                                 PlateToggle& onButton,
                                 DynamicsMeter& inMeter,
                                 DynamicsMeter& outMeter,
                                 DynamicsMeter& grMeter,
                                 CostesSlider* knobs[5],
                                 juce::Label* captions[5],
                                 FastAtkToggle& attackButton,
                                 CostesSlider& hpfSlider,
                                 CostesSlider& lpfSlider,
                                 ListenToggle& listenButton,
                                 CostesSlider& plateSelector)
    {
        block = block.reduced (4, 2);
        block.removeFromTop (6);      // a breath under the band above

        // v2.80.0 (Clement's remark 7): the section titles are GONE -
        // the stages read through their own EXP / COMP plates, their
        // knob colours and their ladders - so no header band at all:
        // the on/off plate rides the meters column's own top line,
        // and the height the titles' header once took belongs to the
        // knobs below
        onButton.setBounds (block.getX(), block.getY(), 88, 18);

        // the three ladders: IN, OUT and GR together at the block's
        // left edge (v2.72.0, Clement's request - the GR meter found
        // its place between the OUT meter and the knobs), the full
        // height of the stage under the plate's own top line.
        // v2.73.0 (Clement's remark 1): the trio tightened - the IN
        // ladder keeps the ONE engraved scale the OUT ladder beside
        // it shares (its own copy is gone), so the pair reads as one
        // meter with one series of numbers.
        // v2.74.0 (Clement's remark 2): the IN ladder gave its numbers
        // column the same width the OUT ladder gives its air - the
        // frame-to-frame distance reads EVEN now, IN to OUT exactly
        // the OUT-to-GR seam
        auto meterColumn = block.removeFromLeft (144);
        meterColumn.removeFromTop (20);
        auto meterIn  = meterColumn.removeFromLeft (44);
        auto meterOut = meterColumn.removeFromLeft (44);
        auto meterGr  = meterColumn.removeFromLeft (56);
        inMeter.setBounds (meterIn.reduced (2, 2));
        outMeter.setBounds (meterOut.reduced (2, 2));
        grMeter.setBounds (meterGr.reduced (2, 2));

        // v2.79.0 (Clement's remark 4): the SC row lost its caption
        // line - the three selectors ride a slim strip at the stage's
        // foot, their readouts identifying themselves (the curve
        // glyph and "off"), and the two knob rows share the height
        // the caption row once took. v2.80.0 (Clement's remark 7):
        // with the section titles gone too, every knob grows to the
        // 84 px square - the height the two headers once took
        auto scStrip = block.removeFromBottom (39);

        // every cell sits its caption at the bottom and centres its
        // control in the room that is left
        const auto captionCell = [] (juce::Rectangle<int> cell, juce::Label& caption)
        {
            caption.setBounds (cell.removeFromBottom (14));
            return cell;
        };

        const int cellH = block.getHeight() / 2;

        // the top row: MIX, THRESH and RATIO
        auto knobRow = block.removeFromTop (cellH);
        const int knobW = knobRow.getWidth() / 3;

        for (int i = 0; i < 3; ++i)
        {
            auto cell = captionCell (knobRow.removeFromLeft (knobW).reduced (2, 0),
                                     *captions[(size_t) i]);
            // v2.80.0: the clamp rode up from 72 to 84 - the section
            // titles' own room, returned to the knobs
            knobs[(size_t) i]->setBounds (cell.withSizeKeepingCentre (
                juce::jmin (cell.getWidth(), 84),
                juce::jmin (cell.getHeight(), 84)));
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
            // v2.80.0: the same 84 px square as the top row
            knobs[(size_t) (3 + i)]->setBounds (cell.withSizeKeepingCentre (
                juce::jmin (cell.getWidth(), 84),
                juce::jmin (cell.getHeight(), 84)));
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

        // the SC strip (v2.79.0, Clement's remark 4): the sidechain
        // corners and the stage's own selector pulled DOWN into a slim
        // uncaptioned strip - each slider's readout says whose it is
        // (its own curve glyph, then "off" or its own hertz), so the
        // caption line the row once needed is gone and the knobs
        // above own the height it took
        const int scW = scStrip.getWidth() / 3;

        hpfSlider.setBounds (scStrip.removeFromLeft (scW).reduced (2, 0)
                                 .withSizeKeepingCentre (76, 36));
        lpfSlider.setBounds (scStrip.removeFromLeft (scW).reduced (2, 0)
                                 .withSizeKeepingCentre (76, 36));
        plateSelector.setBounds (scStrip.reduced (2, 0)
                                     .withSizeKeepingCentre (76, 36));
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

        layoutDynStage (expBlock, expOnButton,
                        expInMeter, expOutMeter, expGrMeter,
                        expKnobs, expCaptions,
                        expFastAttackButton,
                        expScHpfSlider,
                        expScLpfSlider,
                        expListenButton,
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

        layoutDynStage (compBlock, compOnButton,
                        compInMeter, compOutMeter, compGrMeter,
                        compKnobs, compCaptions,
                        compFastAttackButton,
                        compHpfSlider,
                        compScLpfSlider,
                        compListenButton,
                        compLinkSlider);
    }

    // ---- column 3: the output chain ----
    // TWO blocks down the shorter panel (v2.86.0, Clement's request):
    // the five little reserved frames and DRIVE's own slot became TWO
    // engraved blocks - IMAGE at the top of the column (the phase
    // plate with its own words, the stereo balance, the stereo width
    // and the mono maker, every knob with its caption word under
    // it), SHAPE OUT under it (the tilt, the drive with its OS
    // plate, and the new TRIM OUT - the gain the signal rides on its
    // way out). Both blocks 370 px tall with an 8 px breath between
    // them, ending flush under the EQ's own floor
    auto imageBlock = column3.removeFromTop (370);
    reservedFrames[0] = imageBlock;
    column3.removeFromTop (8);   // the breath between the blocks
    auto shapeBlock = column3.removeFromTop (370);
    reservedFrames[1] = shapeBlock;

    // ---- the IMAGE block ----
    auto imageBody = imageBlock;
    imageBody.removeFromTop (16);   // the engraved name's own band

    // the phase plate at the block's head, under its engraved name:
    // "PHASE INVERT" with the LED at its left and the empty-set
    // symbol at its right, 118 px of the column's own width
    imageBody.removeFromTop (2);
    phaseButton.setBounds (imageBody.removeFromTop (26)
                                .withSizeKeepingCentre (118, 26));

    // the three image knobs, each with its caption word under it -
    // the block's engraved name names the SECTION now, so every knob
    // spells its own title again (v2.86.0, the column's new way).
    // v2.87.0 (Clement's remark 3): the caption hugs ITS OWN knob -
    // ONE breath under its feet - and leaves a THREE-breath street
    // before the next knob, so no word can read as the knob's below
    CostesSlider* imageKnobList[3] = { &balanceKnob, &widthKnob, &monoMakerKnob };
    juce::Label* imageWordList[3]  = { &balanceLabel, &widthLabel, &monoMakerLabel };
    ZoneTile* imageTileList[3]     = { &balanceZone, &widthZone, &monoMakerZone };

    for (int i = 0; i < 3; ++i)
    {
        imageBody.removeFromTop (3);   // the street after the unit above
        const auto imageKnobBounds = imageBody.removeFromTop (86)
                                         .withSizeKeepingCentre (83, 86);
        imageKnobList[(size_t) i]->setBounds (imageKnobBounds);
        imageBody.removeFromTop (1);   // the caption hugs its own knob
        const auto imageWordBounds = imageBody.removeFromTop (18)
                                         .reduced (4, 0);
        imageWordList[(size_t) i]->setBounds (imageWordBounds);

        // v2.87.0 (Clement's remark 4): the zone's tile - grown 6 px
        // past the knob's sides, flush with the zone's own top (the
        // knob) and floor (the caption word)
        imageTileList[(size_t) i]->setBounds (imageKnobBounds.expanded (6, 0)
                                                  .withBottom (imageWordBounds.getBottom()));
    }

    // the phase plate's own tile, tone A - 6 px past the plate's
    // sides, flush with its top and floor
    phaseZone.setBounds (phaseButton.getBounds().expanded (6, 0));

    // ---- the SHAPE OUT block ----
    auto shapeBody = shapeBlock;
    shapeBody.removeFromTop (16);   // the engraved name's own band

    // the tilt, its caption word and glyph back under its knob.
    // v2.87.0 (Clement's remark 3): the caption hugs ITS OWN knob -
    // ONE breath under its feet, a THREE-breath street before the
    // next - and (remark 2) the glyph rides at a small breath from
    // its word: the word is drawn centred in its box, so the glyph
    // takes its place 4 px left of the word's own left edge (the old
    // formula sat it 7 px INTO the word)
    shapeBody.removeFromTop (2);
    const auto tiltKnobBounds = shapeBody.removeFromTop (86)
                                    .withSizeKeepingCentre (83, 86);
    tiltKnob.setBounds (tiltKnobBounds);
    shapeBody.removeFromTop (1);   // the caption hugs its own knob
    auto tiltCaption = shapeBody.removeFromTop (18).reduced (4, 0);
    tiltLabel.setBounds (tiltCaption);
    const int tiltTextWidth = tiltLabel.getFont()
                                  .getStringWidth (tiltLabel.getText());
    tiltIcon.setBounds (tiltCaption.getCentreX() - tiltTextWidth / 2 - 16 - 4,
                        tiltCaption.getY() + 1, 16, 16);
    tiltZone.setBounds (tiltKnobBounds.expanded (6, 0)
                           .withBottom (tiltCaption.getBottom()));

    // the drive - the knob in the block's own 83 x 86 square now,
    // its caption under it, the OS plate under the caption, the same
    // per-knob pattern as the whole column. The OS plate belongs to
    // the drive, so the drive's tile carries them BOTH
    shapeBody.removeFromTop (3);
    const auto driveKnobBounds = shapeBody.removeFromTop (86)
                                     .withSizeKeepingCentre (83, 86);
    driveKnob.setBounds (driveKnobBounds);
    shapeBody.removeFromTop (1);   // the caption hugs its own knob
    auto driveCaption = shapeBody.removeFromTop (18).reduced (4, 0);
    driveLabel.setBounds (driveCaption);
    const int driveTextWidth = driveLabel.getFont()
                                  .getStringWidth (driveLabel.getText());
    driveIcon.setBounds (driveCaption.getCentreX() - driveTextWidth / 2 - 16 - 4,
                         driveCaption.getY() + 1, 16, 16);
    shapeBody.removeFromTop (2);
    oversampleButton.setBounds (shapeBody.removeFromTop (26)
                                    .withSizeKeepingCentre (110, 26));
    driveZone.setBounds (driveKnobBounds.expanded (6, 0)
                            .withBottom (oversampleButton.getBounds().getBottom()));

    // the TRIM OUT, the block's last knob - the gain the signal rides
    // on its way out, the fader glyph at its caption's left. A
    // FOUR-breath street after the OS plate keeps the drive's own
    // zone together and sets the trim out apart
    shapeBody.removeFromTop (4);
    const auto trimOutKnobBounds = shapeBody.removeFromTop (86)
                                       .withSizeKeepingCentre (83, 86);
    trimOutKnob.setBounds (trimOutKnobBounds);
    shapeBody.removeFromTop (1);   // the caption hugs its own knob
    auto trimOutCaption = shapeBody.removeFromTop (18).reduced (4, 0);
    trimOutLabel.setBounds (trimOutCaption);
    const int trimOutTextWidth = trimOutLabel.getFont()
                                     .getStringWidth (trimOutLabel.getText());
    trimOutIcon.setBounds (trimOutCaption.getCentreX() - trimOutTextWidth / 2 - 16 - 4,
                           trimOutCaption.getY() + 1, 16, 16);
    trimOutZone.setBounds (trimOutKnobBounds.expanded (6, 0)
                              .withBottom (trimOutCaption.getBottom()));
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

    // v2.80.0 (Clement's remarks 4 and 5): the THRESHOLD bar on an IN
    // ladder - the stage's own threshold corner drawn as a red bar,
    // the same weight and style the GR ladder's peak-hold bar
    // carries, the HF band's own saturated red. It reads the
    // parameter the THRESH knob drives, so the bar and the knob can
    // never disagree; a degenerate resize returned above and never
    // reaches this bar. v2.81.0 (Clement's remark 2): the bar grew a
    // touch fatter, 3.4 to 4.6 px, to read at a glance
    if (mode == Mode::input && thresholdDb != nullptr)
    {
        const float v = juce::jlimit (lowDb, highDb, thresholdDb());
        float y = bottom - (v - lowDb) / span * height;
        y = juce::jlimit (top, bottom - 1.0f, y);

        g.setColour (eqHfDisc);
        g.drawLine (lx, y, lx + lw, y, 4.6f);
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
        // the IN and OUT ladders' cream marker; v2.81.0 (Clement's
        // remark 2): both grew a touch fatter again - the GR bar 3.4
        // to 4.6 px, level with the IN ladder's threshold bar, the
        // IN and OUT markers 2.4 to 3.6
        if (mode == Mode::gainReduction)
        {
            g.setColour (compKnobDisc);
            g.drawLine (juce::Line<float> (lx, markerY, lx + lw, markerY), 4.6f);
        }
        else
        {
            g.setColour (knobPointer);
            g.drawLine (juce::Line<float> (lx, markerY, lx + lw, markerY), 3.6f);
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

    // v2.78.0 (Clement's remark 4): the shape LEFT the plate - it
    // stands under the band's name now, in the row's margin column,
    // and follows the plate from there (the SectionIcon that shadows
    // this button reads its shelfGlyph) - so the plate keeps its LED
    // and its word alone. v2.80.0 (Clement's remark 1): the cuts
    // speak through this same plate - their word rides cutWord
    // ("HPF" / "LPF") and NEVER changes with the state, only the
    // light does: lit amber when the filter processes, black with a
    // dead LED when it is bypassed, the BELL/SHELF plate's own
    // language
    const juce::String word = cutWord.isNotEmpty()
        ? cutWord
        : juce::String (on ? "BELL" : "SHELF");

    g.setColour (on ? engraving : plateText);
    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
    g.drawText (word, bounds,
                juce::Justification::centred);
}

void CostesStripEditor::PhaseToggle::paintButton (juce::Graphics& g,
                                                   bool highlighted,
                                                   bool /*down*/)
{
    // the IMAGE section's phase plate (v2.84.0, Clement's remark 3):
    // the same SSL plate language as every other switch on the panel
    // - lit amber with a live LED while the programme is inverted,
    // black with a dead LED while it plays normal. v2.86.0
    // (Clement's request): the plate rides the head of the IMAGE
    // block and spells its own name now - "PHASE INVERT" between the
    // LED and the empty-set symbol, drawn as the vector
    // circle-and-slash so the panel never leaves its hundred-percent
    // ASCII rule
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

    // v2.86.0 (Clement's request): the plate carries its own words
    // now - "PHASE INVERT" between the LED and the symbol, the same
    // two-tone engraving as the words on the lit plates. The 118 px
    // plate leaves the word 78 px of its own - the 9.5 bold caption
    // face needs ~70 - so the words read whole
    const auto symbolArea = bounds.removeFromRight (20.0f);

    g.setColour (on ? engraving : plateText);
    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
    g.drawText ("PHASE INVERT", bounds, juce::Justification::centred);

    // the empty-set symbol: a 16 px circle with the slash riding its
    // diagonal, from the top-right rim down to the bottom-left - the
    // same two-tone engraving as the words on the lit plates
    const auto symbol = symbolArea.withSizeKeepingCentre (16.0f, 16.0f);

    g.setColour (on ? engraving : plateText);
    g.drawEllipse (symbol, 1.6f);
    g.drawLine (symbol.getX() + symbol.getWidth(),
                symbol.getY(),
                symbol.getX(),
                symbol.getY() + symbol.getHeight(),
                1.6f);
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

        // LOW CUT: the high-pass response, corrected in v2.79.0
        // (Clement's remark 3): the mirror image of the LPF's own
        // curve about the vertical axis - the knee rises at the LOW
        // end, where the filter bites, and the curve settles to its
        // flat unity plateau along the top right, the way the LPF
        // settles to its floor
        case Icon::lowCut:
            p.startNewSubPath (X (2.0f), Y (13.0f));
            p.quadraticTo (X (5.0f), Y (13.0f), X (8.0f), Y (3.0f));
            p.lineTo (X (14.0f), Y (3.0f));
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

        // LOW SHELF (v2.62.0, Clement's remark 5; corrected in v2.78.0,
        // Clement's remark 3): the band-1 shape - the response the
        // strip's own convention reads, the frequency axis running
        // left to right exactly as the low cut's own glyph draws it.
        // The low shelf sits UP at the LOW end and settles to the
        // unity plateau: flat at the TOP, the knee falling to the
        // low plateau, flat at the bottom
        case Icon::lowShelf:
            p.startNewSubPath (X (2.0f), Y (3.5f));
            p.lineTo (X (5.0f), Y (3.5f));
            p.quadraticTo (X (9.0f), Y (3.5f), X (11.0f), Y (12.5f));
            p.lineTo (X (14.0f), Y (12.5f));
            g.strokePath (p, juce::PathStrokeType (stroke));
            break;

        // HIGH SHELF (v2.62.0, Clement's remark 5; corrected in
        // v2.78.0, Clement's remark 3): the band-4 shape, the low
        // shelf's true mirror - the unity plateau at the low end,
        // the knee rising to the shelf's own level at the high end:
        // flat at the bottom, the knee rising to the top plateau,
        // flat at the top
        case Icon::highShelf:
            p.startNewSubPath (X (2.0f), Y (12.5f));
            p.lineTo (X (5.0f), Y (12.5f));
            p.quadraticTo (X (7.0f), Y (12.5f), X (9.0f), Y (3.5f));
            p.lineTo (X (14.0f), Y (3.5f));
            g.strokePath (p, juce::PathStrokeType (stroke));
            break;

    }
}

void CostesStripEditor::SectionIcon::paint (juce::Graphics& g)
{
    // the component face of the glyph (v2.70.0, Clement's remark 9):
    // the whole drawing lives in drawGlyph now - paint just hands it
    // the local bounds and the strip's engraved silkscreen colour.
    // v2.78.0 (Clement's remark 4): the outer bands' icons FOLLOW
    // their BELL/SHELF plates - the shape is picked at paint time,
    // the bell while the plate reads BELL, the band's own shelf
    // (the shelfGlyph member) when it reads SHELF, so the glyph and
    // its plate never disagree
    const auto shape = follows != nullptr
        ? (follows->getToggleState() ? Icon::eq : follows->shelfGlyph)
        : type;

    drawGlyph (g, getLocalBounds().toFloat(), shape, engraving);
}

void CostesStripEditor::ZoneTile::paint (juce::Graphics& g)
{
    // v2.87.0 (Clement's remark 4, the AMEK 9099's pastel zones): the
    // zone's own background tile - a rounded rectangle in one of the
    // two warm beiges, carrying the panel's own grain so the tile
    // reads as a shade of the cream front, edged with a whisper of
    // the knob bezel. It paints behind everything and never takes a
    // click; the degenerate-size paints of the harness's chaos test
    // find nothing to divide here
    auto b = getLocalBounds().toFloat();

    if (b.getWidth() < 2.0f || b.getHeight() < 2.0f)
        return;

    g.setColour (tint);
    g.fillRoundedRectangle (b, 8.0f);

    if (grain != nullptr)
    {
        juce::Path clip;
        clip.addRoundedRectangle (b, 8.0f);
        g.saveState();
        g.reduceClipRegion (clip);

        for (int tileY = 0; tileY < getHeight(); tileY += grain->getHeight())
            for (int tileX = 0; tileX < getWidth(); tileX += grain->getWidth())
                g.drawImageAt (*grain, tileX, tileY);

        g.restoreState();
    }

    g.setColour (juce::Colour (0xffb0a380).withMultipliedAlpha (0.55f));
    g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);
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
