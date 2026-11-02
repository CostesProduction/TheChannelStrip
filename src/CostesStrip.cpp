// ---------------------------------------------------------------------------
// src/CostesStrip.cpp - v2.63.0 (roadmap C2)
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
    // around it (the pointer flips dark on the bright caps)
    const juce::Colour eqLoMidDisc (0xffaac6df);
    const juce::Colour eqHiMidDisc (0xfff0e9d8);
    const juce::Colour eqHfDisc    (0xffd8a49c);
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

    jassert (trimInDb != nullptr && lowCutHz != nullptr && lowCutOrder != nullptr
             && lowCutOn != nullptr && highCutHz != nullptr && highCutOrder != nullptr
             && highCutOn != nullptr && tiltDb != nullptr && driveDb != nullptr
             && driveOversampled != nullptr
             && eqOn != nullptr && eq1Hz != nullptr && eq1Gain != nullptr
             && eq1Bell != nullptr && eq2Hz != nullptr && eq2Gain != nullptr
             && eq2Oct != nullptr && eq3Hz != nullptr && eq3Gain != nullptr
             && eq3Oct != nullptr && eq4Hz != nullptr && eq4Gain != nullptr
             && eq4Bell != nullptr);
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

    // band 1 (LF): the corner sweeps 20 to 600 Hz, logarithmic, snapped
    // to whole Hz like the low cut; the readout speaks Hz and accepts it
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "eq1Hz", 1 }, "EQ 1 Frequency",
        makeLogRange (20.0f, 600.0f,
                      [] (float start, float end, float v)
                      {
                          return juce::jlimit (start, end, std::round (v));
                      }),
        100.0f,
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
        juce::ParameterID { "eq1Gain", 1 }, "EQ 1 Gain",
        juce::NormalisableRange<float> (-15.0f, 15.0f, 0.1f), 0.0f,
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

    // band 2 (LO MID): the same whole-Hz plumbing over 60 Hz to 3 kHz
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "eq2Hz", 1 }, "EQ 2 Frequency",
        makeLogRange (60.0f, 3000.0f,
                      [] (float start, float end, float v)
                      {
                          return juce::jlimit (start, end, std::round (v));
                      }),
        400.0f,
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
        juce::NormalisableRange<float> (-15.0f, 15.0f, 0.1f), 0.0f,
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
                return juce::String (value, 2) + " oct";
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                auto t = text.trim();

                if (t.endsWithIgnoreCase ("oct"))
                    t = t.dropLastCharacters (3).trim();

                return t.replaceCharacter (',', '.').getFloatValue();
            })));

    // band 3 (HI MID): the same pair, kHz-spoken like the high cut
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "eq3Hz", 1 }, "EQ 3 Frequency",
        makeLogRange (500.0f, 12000.0f,
                      [] (float start, float end, float v)
                      {
                          return juce::jlimit (start, end, 10.0f * std::round (v / 10.0f));
                      }),
        1600.0f,
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
        juce::NormalisableRange<float> (-15.0f, 15.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (dbText)
            .withValueFromStringFunction (dbParse)));

    // band 3's own width (v2.62.0): the same seven equal-step widths
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "eq3Oct", 1 }, "EQ 3 Width",
        eqOctaveRange(), 1.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (value, 2) + " oct";
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                auto t = text.trim();

                if (t.endsWithIgnoreCase ("oct"))
                    t = t.dropLastCharacters (3).trim();

                return t.replaceCharacter (',', '.').getFloatValue();
            })));

    // band 4 (HF): bell or high shelf, 600 Hz to 18 kHz
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "eq4Hz", 1 }, "EQ 4 Frequency",
        makeLogRange (600.0f, 18000.0f,
                      [] (float start, float end, float v)
                      {
                          return juce::jlimit (start, end, 10.0f * std::round (v / 10.0f));
                      }),
        7200.0f,
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
        juce::NormalisableRange<float> (-15.0f, 15.0f, 0.1f), 0.0f,
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

            // the four-band EQ (v2.61.0): between the cuts and the tilt,
            // exactly where the SSL channel strips put it
            for (int k = 0; k < activeEqSections; ++k)
                sample = eqBands[k][ch].processSample (sample);

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

    // the four band glyphs (v2.62.0, Clement's remark 5): one small icon
    // per band, under its selector - the LF band's low-shelf response,
    // the two mid bells' shared bump and the HF band's high shelf
    addAndMakeVisible (eqBand1Icon);
    addAndMakeVisible (eqBand2Icon);
    addAndMakeVisible (eqBand3Icon);
    addAndMakeVisible (eqBand4Icon);

    // the EQ section header (v2.61.0): the caption is a label so the
    // icon can hug its left edge with the same geometry the knob
    // captions use - the engraved silkscreen look, drawn as a component
    eqSectionLabel.setText ("EQ", juce::dontSendNotification);
    eqSectionLabel.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    eqSectionLabel.setJustificationType (juce::Justification::centred);
    eqSectionLabel.setColour (juce::Label::textColourId, engravingDim);
    addAndMakeVisible (eqSectionLabel);

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
    const auto setUpSlope = [this] (CostesSlider& slope, float defaultValue)
    {
        slope.setSliderStyle (juce::Slider::LinearHorizontal);
        // 76 x 18 (v2.60.0, Clement's remark 3): the 72 x 16 box clipped
        // the bottom of "24 dB/oct" - the knob readouts' own 18 px height
        // fix, applied to the slopes too
        slope.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 76, 18);
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
    // octaves ("0.40 oct" ... "4.00 oct" since v2.63.0)
    setUpSlope (eq2OctSlider, 1.0f);
    setUpSlope (eq3OctSlider, 1.0f);

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
    addAndMakeVisible (eq1ModeButton);
    addAndMakeVisible (eq4ModeButton);

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

    // v2.61.0: the second row added its header (20) plus the band
    // columns (152) plus the breathing room between them. v2.62.0
    // (Clement's remark 5): each band column also hosts its own 16 px
    // glyph under the selector, so the row grows to 200 px
    setSize (880, 400);
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

    // the EQ section's own rules (v2.61.0): a full divider under the
    // row-1 silkscreen and one under the EQ header - the section reads
    // as its own engraved band of the module front
    g.setColour (engravingDim.withAlpha (0.5f));
    g.drawLine (14.0f, 197.0f, (float) width - 14.0f, 197.0f, 1.0f);
    g.drawLine (14.0f, 224.0f, (float) width - 14.0f, 224.0f, 1.0f);

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

    // row 1 (v2.61.0): the original five-knob band keeps its own 150 px,
    // so every column sits exactly where it sat on the v2.60 panel -
    // everything below is the new EQ section
    auto row1 = area.removeFromTop (150);
    auto eqArea = area;

    // five equal columns: knob above, caption below; the cut columns also
    // carry their slope slider under the caption, and the drive column
    // hosts the OS toggle under its caption
    const int columnWidth = row1.getWidth() / 5;

    for (int i = 0; i < 5; ++i)
    {
        auto column = row1.removeFromLeft (columnWidth).reduced (8, 2);

        auto* knob = &trimInKnob;
        auto* caption = &trimInLabel;
        juce::Slider* slope = nullptr;
        SectionIcon* icon = &trimIcon;

        if (i == 1)      { knob = &lowCutKnob;  caption = &lowCutLabel;  slope = &lowCutSlope;  icon = &lowCutIcon; }
        else if (i == 2) { knob = &highCutKnob; caption = &highCutLabel; slope = &highCutSlope; icon = &highCutIcon; }
        else if (i == 3) { knob = &tiltKnob;    caption = &tiltLabel;    icon = &tiltIcon; }
        else if (i == 4) { knob = &driveKnob;   caption = &driveLabel;   icon = &driveIcon; }

        // the slope columns give their selector a 26 px band - the 18 px
        // readout box plus a slice of track above it (v2.60.0)
        const int knobHeight = (i == 4) ? column.getHeight() - 48
                             : (slope != nullptr) ? column.getHeight() - 46
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
            auto slopeBounds = column.removeFromBottom (26).reduced (6, 0);
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

        // the section glyph (v2.60.0): a 16 px icon hugging the caption's
        // left edge - the caption stays centred in its column, the icon
        // sits exactly where the text begins
        const int textWidth = caption->getFont().getStringWidth (caption->getText());
        icon->setBounds (captionBounds.getCentreX() - textWidth / 2 - 21,
                         captionBounds.getY() + 1, 16, 16);

        knob->setBounds (column.removeFromTop (knobHeight));
    }

    // ---- the EQ section (v2.61.0, Clement's spec) ----
    // a breath under the row-1 silkscreen, then the header: the global
    // on/off plate at the left and the section caption centred, its
    // bell glyph hugging the text's left edge with the same geometry
    // the knob captions use for their icons
    eqArea.removeFromTop (8);
    auto eqHeader = eqArea.removeFromTop (20);

    eqOnButton.setBounds (eqHeader.getX() + 14, eqHeader.getY() + 1, 76, 18);
    eqSectionLabel.setBounds (eqHeader);

    const int eqTextWidth = eqSectionLabel.getFont()
                                .getStringWidth (eqSectionLabel.getText());
    eqIcon.setBounds (getWidth() / 2 - eqTextWidth / 2 - 21,
                      eqHeader.getY() + 2, 16, 16);

    // the four band columns: a caption above, the FREQ and GAIN knobs
    // side by side with their own small captions, the band's own
    // selector underneath - the bandwidth sweep for the mid bells, the
    // BELL/SHELF plate for the outer bands - and (v2.62.0, Clement's
    // remark 5) a 16 px glyph in a final row: the LF band's low-shelf
    // response, the two mid bells' bump and the HF band's high shelf
    const int eqColumnWidth = eqArea.getWidth() / 4;

    for (int i = 0; i < 4; ++i)
    {
        auto column = eqArea.removeFromLeft (eqColumnWidth).reduced (4, 2);

        auto* bandLabel = &eqBand1Label;
        auto* freqKnob  = &eq1HzKnob;
        auto* freqLabel = &eq1HzLabel;
        auto* gainKnob  = &eq1GainKnob;
        auto* gainLabel = &eq1GainLabel;
        juce::Slider* octSlider   = nullptr;
        ShelfModeToggle* modeButton = &eq1ModeButton;
        SectionIcon* bandIcon = &eqBand1Icon;

        if (i == 1)
        {
            bandLabel = &eqBand2Label; freqKnob = &eq2HzKnob;  freqLabel = &eq2HzLabel;
            gainKnob  = &eq2GainKnob;  gainLabel = &eq2GainLabel;
            octSlider = &eq2OctSlider; modeButton = nullptr;
            bandIcon  = &eqBand2Icon;
        }
        else if (i == 2)
        {
            bandLabel = &eqBand3Label; freqKnob = &eq3HzKnob;  freqLabel = &eq3HzLabel;
            gainKnob  = &eq3GainKnob;  gainLabel = &eq3GainLabel;
            octSlider = &eq3OctSlider; modeButton = nullptr;
            bandIcon  = &eqBand3Icon;
        }
        else if (i == 3)
        {
            bandLabel = &eqBand4Label; freqKnob = &eq4HzKnob;  freqLabel = &eq4HzLabel;
            gainKnob  = &eq4GainKnob;  gainLabel = &eq4GainLabel;
            modeButton = &eq4ModeButton;
            bandIcon  = &eqBand4Icon;
        }

        bandLabel->setBounds (column.removeFromTop (16));
        column.removeFromTop (4);   // a breath under the band caption

        // the bottom band hosts the selector (26 px) and the band glyph
        // beneath it (v2.62.0): a centred 16 x 16 icon
        auto bottom = column.removeFromBottom (44);
        auto iconRow = bottom.removeFromBottom (18);
        bandIcon->setBounds (iconRow.withSizeKeepingCentre (16, 16));
        auto control = bottom;

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
            // geometry, one band width wide
            octSlider->setBounds (control.reduced (1, 1));
        }
        else if (modeButton != nullptr)
        {
            // the outer bands' BELL/SHELF plate, centred under its band
            modeButton->setBounds (control.withSizeKeepingCentre (
                juce::jmin (control.getWidth(), 76), 20));
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
    // dark engraving on the lit plate, dim cream on the dark one
    g.setColour (on ? engraving : engravingDim);
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

    // the caption spells the shape: "BELL" / "SHELF", dark engraving on
    // the lit plate, dim cream on the dark one
    g.setColour (on ? engraving : engravingDim);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText (on ? "BELL" : "SHELF", bounds,
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

void CostesStripEditor::SectionIcon::paint (juce::Graphics& g)
{
    // the glyphs (v2.60.0, Clement's remark; the EQ bell joined in
    // v2.61.0, the two shelf plateaus in v2.62.0): clean single-stroke
    // vector shapes in the engraved silkscreen colour, drawn on a 16 x
    // 16 design grid scaled to the component's bounds so a degenerate
    // resize can never divide by anything. Every shape is drawn to be
    // read instantly: a fader for the trim, the two mirrored filter
    // responses for the cuts, a tilted line for the tilt, the soft
    // saturation transfer curve for the drive and the bell bump for
    // the EQ
    const auto b = getLocalBounds().toFloat();

    const float sx = b.getWidth() / 16.0f;
    const float sy = b.getHeight() / 16.0f;
    const float stroke = 1.5f * (sx + sy) * 0.5f;

    const auto X = [&b, sx] (float v) { return b.getX() + v * sx; };
    const auto Y = [&b, sy] (float v) { return b.getY() + v * sy; };

    g.setColour (engraving);

    juce::Path p;

    switch (type)
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
}
