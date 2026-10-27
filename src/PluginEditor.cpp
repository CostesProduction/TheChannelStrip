#include "PluginEditor.h"
#include "BinaryData.h"
#include <cmath>

namespace
{
    juce::String fmtLufs (float v)
    {
        if (v < -60.0f)
            return "----";

        return juce::String (v, 1) + " LUFS";
    }

    void styleConsoleButton (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId,    juce::Colour (0xff1a2130));
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2d6a8a));
        b.setColour (juce::TextButton::textColourOffId,   juce::Colour (0xffdfe6f2));
        b.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xffffffff));
    }

    // v2.26: the short console label for a preset entry ("SSL E - My
    // preset"). A consoleName that is not one of the nine (an exotic
    // plugin the preset was made for) is truncated instead of resolved.
    juce::String shortConsoleNameFor (const juce::String& consoleName)
    {
        for (int i = 0; i < TheChannelStripAudioProcessor::numConsoles; ++i)
            if (consoleName.containsIgnoreCase (
                    juce::String (TheChannelStripAudioProcessor::consoleNames[i])))
                return TheChannelStripAudioProcessor::consoleLabel (i);

        return consoleName.substring (0, juce::jmin (consoleName.length(), 14));
    }

    // v2.4: sidebar + centre column geometry, shared by paint() and
    // resized(). Everything lives in the condensed left sidebar.
    constexpr int sidebarWidth        = 132; // left: all wrapper controls
    constexpr int layoutMargin        = 10;
    constexpr int columnGap           = 10;
    constexpr int analyserStripHeight = 180; // full-width strip above the plugin
}

// ---------------------------------------------------------------------------
// SpectrumPanel
// ---------------------------------------------------------------------------

SpectrumPanel::SpectrumPanel (TheChannelStripAudioProcessor& p)
    : processor (p)
{
    smoothIn.fill  (-100.0f);
    smoothOut.fill (-100.0f);
    smoothDiff.fill (0.0f);
    // v2.12: the EQ curve starts with nothing measured; it is drawn for
    // exactly the bins that have carried input energy at least once.
    eqMeasured.fill (false);
    // v2.10: the 10-second max hold starts empty (silent).
    maxOut.fill (-100.0f);
    maxRing.assign ((size_t) maxHoldFrames * SpectrumAnalyzer::numScopeBins, -100.0f);

    startTimerHz (30);
}

void SpectrumPanel::setTilt (float newTiltDbPerOct)
{
    tilt = newTiltDbPerOct;
}

void SpectrumPanel::setDbPerDiv (float dbPerDivision)
{
    dbPerDiv = juce::jmax (1.0f, dbPerDivision);
}

void SpectrumPanel::setEqDbPerDiv (float dbPerDivision)
{
    eqDbPerDiv = juce::jmax (1.0f, dbPerDivision);
}

// v2.13: the loudness reference of the axis (0 dB line = lufsTargetDb
// LUFS absolute). The editor wires onLufsTargetClicked to cycle the
// three presets; when the reference changes, the calibration re-snaps
// immediately instead of drifting for a second or two.
void SpectrumPanel::setLufsTarget (float targetLufs)
{
    lufsTargetDb = targetLufs;
    lufsOffsetSnapped = false;
}

// v2.13: the label drawn at the 0 dB line - the preset name plus the
// absolute LUFS value it stands for.
juce::String SpectrumPanel::lufsTargetLabel() const
{
    const char* name = "Mix";

    if (std::abs (lufsTargetDb - (-37.0f)) < 0.5f)
        name = "Record";
    else if (std::abs (lufsTargetDb - (-14.0f)) < 0.5f)
        name = "Master";

    return juce::String (name) + " (" + juce::String (lufsTargetDb, 0) + " LUFS)";
}

// v2.12: the scale toggles live on the analyser itself. The left column
// (the dB labels of the spectrum scale) toggles 10/6 dB per division;
// the right column (the green EQ range labels) toggles 4/2 dB per
// division. v2.13: the loudness-reference label at the 0 dB line
// ("Mix (-23 LUFS)", drawn from x=40 at the zero line) cycles the
// Record/Mix/Master presets. The editor wires the actual toggling
// through the std::function hooks (it owns the persisted state); clicks
// anywhere else on the panel are ignored.
void SpectrumPanel::mouseDown (const juce::MouseEvent& e)
{
    // v2.13: the reference-label zone comes first - it overlaps the
    // left scale column horizontally, so a click near the 0 dB line
    // must be read as a reference click, not a scale click
    // v2.13.1: the explicit (float) cast silences the int -> float
    // implicit-conversion warning on the MouseEvent coordinate.
    const float zeroY = yForDb (0.0f);

    if (e.x >= 36 && e.x <= 190 && std::abs ((float) e.y - zeroY) <= 12.0f)
    {
        if (onLufsTargetClicked)
            onLufsTargetClicked();
        return;
    }

    // the left labels are drawn at x 4..38, the right EQ labels just
    // left of the 20 kHz frequency label; both zones get a little
    // slack so they are comfortable to hit
    if (e.x <= 44)
    {
        if (onAnalyzerScaleClicked)
            onAnalyzerScaleClicked();
    }
    else if (e.x >= getWidth() - 56)
    {
        if (onEqScaleClicked)
            onEqScaleClicked();
    }
}

void SpectrumPanel::timerCallback()
{
    updateBallistics();
    repaint();
}

// v2.19 (roadmap point 3): the hover readout. mouseMove just records the
// cursor position and repaints - the readout itself is drawn in paint()
// from the SAME smoothed arrays and the SAME display transform as the
// curves, so the numbers always agree with what is on screen. mouseExit
// hides it again (the readout never lingers after the mouse leaves).
void SpectrumPanel::mouseMove (const juce::MouseEvent& e)
{
    hoverPos = e.getPosition();
    mouseInside = true;
    repaint();
}

void SpectrumPanel::mouseExit (const juce::MouseEvent&)
{
    mouseInside = false;
    repaint();
}

// v2.19: the exact inverse of xForFrequency - same 16 Hz - 24 kHz log
// axis, so frequencyForX (xForFrequency (f)) == f. Static and pure so
// the offline test harness can verify the mapping without an editor.
double SpectrumPanel::frequencyForX (double x, double width)
{
    constexpr double minHz = 16.0;
    constexpr double span  = 3.1760912590556813; // log10 (24000 / 16)

    if (width <= 0.0)
        return minHz;

    const double x01 = juce::jlimit (0.0, 1.0, x / width);
    return minHz * std::pow (10.0, span * x01);
}

// v2.19: frequency as a compact readout string ("45 Hz", "1.23 kHz").
// Below 1 kHz the value is rounded to the nearest hertz; above, two
// decimals of kHz - the convention every analyser uses.
juce::String SpectrumPanel::formatFrequency (double freq)
{
    freq = juce::jlimit (16.0, 24000.0, freq);

    if (freq < 1000.0)
        return juce::String (juce::roundToInt (freq)) + " Hz";

    return juce::String (freq / 1000.0, 2) + " kHz";
}

// v2.19: one decimal for the levels; bins with no energy (the -100 dB
// floor of the smoothing arrays) read "--" instead of a meaningless
// "-100.0".
juce::String SpectrumPanel::formatReadoutDb (float db)
{
    if (db <= -95.0f)
        return "--";

    return juce::String (db, 1);
}

// v2.19: the scope bin whose centre frequency is nearest the hovered
// frequency. 256 bins, linear scan on the UI thread - trivial, and the
// scope bins are already log-spaced for the display.
int SpectrumPanel::binForX (float x) const
{
    const double freq = frequencyForX (x, (double) getWidth());
    int best = 0;
    double bestDist = 1.0e30;

    for (int bin = 0; bin < SpectrumAnalyzer::numScopeBins; ++bin)
    {
        const double dist = std::abs (processor.getAnalyzerIn().getBinFrequency (bin) - freq);

        if (dist < bestDist)
        {
            bestDist = dist;
            best = bin;
        }
    }

    return best;
}

// v2.19: draws the hairline at the cursor plus the fixed top-right
// readout: frequency under the cursor, then the IN / OUT levels and -
// when that bin has been measured at least once (v2.12 persistence) -
// the EQ (OUT-IN) value. The IN/OUT numbers go through the exact same
// tilt + loudness-offset transform as the curves, so they match the
// pixel under the hairline; the EQ value is the raw measured difference
// (tilt and offset cancel in OUT-IN). Colour-matched to the legend.
void SpectrumPanel::drawHoverReadout (juce::Graphics& g) const
{
    const int x = juce::jlimit (0, getWidth() - 1, hoverPos.x);
    const int bin = binForX ((float) x);

    // subtle vertical hairline at the cursor, inside the panel only
    g.setColour (juce::Colour (0x408f9bb0));
    g.drawVerticalLine (x, 0.0f, (float) getHeight());

    const float binFreq = (float) processor.getAnalyzerIn().getBinFrequency (bin);
    const float tiltDb  = tilt * (std::log2 (binFreq / 1000.0f));
    const float dbIn    = smoothIn  [(size_t) bin] - tiltDb - lufsOffsetDb;
    const float dbOut   = smoothOut [(size_t) bin] - tiltDb - lufsOffsetDb;

    const juce::Font font (juce::FontOptions (12.0f, juce::Font::bold));

    struct Segment { juce::String text; juce::Colour colour; };
    juce::Array<Segment> segments;

    segments.add (Segment { formatFrequency (frequencyForX (x, (double) getWidth())),
                            juce::Colour (0xffc8d2e6) });
    segments.add (Segment { "IN "  + formatReadoutDb (dbIn),  juce::Colour (0xffe2a14f) });
    segments.add (Segment { "OUT " + formatReadoutDb (dbOut), juce::Colour (0xff54c8e8) });

    if (eqMeasured[(size_t) bin])
        segments.add (Segment { "EQ " + formatReadoutDb (smoothDiff[(size_t) bin]),
                                juce::Colour (0xff7ee08a) });

    // right-aligned at the top, clear of the centred legend and of the
    // left scale column; each token is drawn in its own colour.
    // v2.20 (point 6): the right margin grew from 10 to 44 px so the
    // readout keeps clear of the EQ scale labels at the far right.
    int totalWidth = 0;

    for (const auto& s : segments)
        totalWidth += 6 + juce::GlyphArrangement::getStringWidth (font, s.text);

    int cursorX = getWidth() - 44 - totalWidth;
    g.setFont (font);

    for (const auto& s : segments)
    {
        const int w = juce::GlyphArrangement::getStringWidth (font, s.text);
        g.setColour (s.colour);
        g.drawText (s.text, cursorX, 4, w + 8, 16, juce::Justification::left);
        cursorX += 6 + w;
    }
}

void SpectrumPanel::updateBallistics()
{
    float in[SpectrumAnalyzer::numScopeBins];
    float out[SpectrumAnalyzer::numScopeBins];

    processor.getAnalyzerIn().copyScopeData  (in, SpectrumAnalyzer::numScopeBins);
    processor.getAnalyzerOut().copyScopeData (out, SpectrumAnalyzer::numScopeBins);

    // v2.21.1: the EQ curve and its validity gate read the RAW band
    // energies, not the display-smoothed ones (the spatial pass is a
    // display nicety; on measurement data it dragged isolated weak
    // bands down by up to 24 dB, below the gate).
    float rawIn[SpectrumAnalyzer::numScopeBins];
    float rawOut[SpectrumAnalyzer::numScopeBins];

    processor.getAnalyzerIn().copyRawScopeData  (rawIn, SpectrumAnalyzer::numScopeBins);
    processor.getAnalyzerOut().copyRawScopeData (rawOut, SpectrumAnalyzer::numScopeBins);

    for (int b = 0; b < SpectrumAnalyzer::numScopeBins; ++b)
    {
        // v2.10: symmetric one-pole EMA (was: instant rise, ~36 dB/s
        // fall, which read as too nervous). One-pole EMA at 30 Hz:
        // alpha = 1 / (1 + 30 * tau).
        // v2.11/v2.12: tau is selectable in the sidebar (Fast/Slow).
        const float specAlpha = 1.0f / (1.0f + 30.0f * specTau);

        smoothIn  [(size_t) b] += specAlpha * (in[b]  - smoothIn  [(size_t) b]);
        smoothOut [(size_t) b] += specAlpha * (out[b] - smoothOut [(size_t) b]);
    }

    // v2.10/v2.11: sliding maximum of the DISPLAYED (smoothed) OUT curve
    // over the last 10 s (300 frames at 30 Hz) - an envelope that hugs
    // the OUT curve and keeps its peaks for 10 s. v2.10 took the max of
    // the RAW per-frame FFT values instead, which sat far above the
    // smoothed curve (instantaneous bins fluctuate 10+ dB frame to
    // frame); Clement expected the max of the curve he sees, so the
    // ring now stores the smoothed values.
    {
        std::copy (smoothOut.begin(), smoothOut.end(),
                   maxRing.begin() + maxRingIndex * SpectrumAnalyzer::numScopeBins);
        maxRingIndex = (maxRingIndex + 1) % maxHoldFrames;

        for (int b = 0; b < SpectrumAnalyzer::numScopeBins; ++b)
        {
            float m = -100.0f;

            for (int f = 0; f < maxHoldFrames; ++f)
                m = juce::jmax (m, maxRing[(size_t) f * SpectrumAnalyzer::numScopeBins
                                          + (size_t) b]);

            maxOut[(size_t) b] = m;
        }
    }

    // v2.22: the EQ curve is the CROSS-SPECTRUM transfer estimate - the
    // full dual-FFT measurement that Smaart-class systems perform. The
    // TransferAnalyzer on the audio thread accumulates, per display
    // band, the input / output auto-spectra AND the complex cross-
    // spectrum (Sxy = E[X * conj(Y)]); the EQ value is 20*log10|Sxy/Sxx|
    // (the H1 estimator). The complex accumulation is the key: the
    // console's noise / dither floor has a RANDOM phase, so it cancels
    // out of the averaged Sxy (it lands in Syy - the OUT curve - but no
    // longer votes on the EQ). With the SSL J's lowpass engaged the
    // v2.21.1 power ratio honestly measured "attenuated signal + console
    // floor" over "signal": once the floor exceeded the attenuated
    // signal, the ratio turned POSITIVE - the +3 dB rise above 10 kHz
    // on Clement's screenshot, a real power gain that is not a
    // transfer. The cross-spectrum reads the true rolloff instead, deep
    // under the floor, because the attenuated signal stays CORRELATED
    // with the input while the floor does not.
    //
    // Two validity gates now decide whether a bin is measured:
    //  - the v2.21.1 RELATIVE input gate (within 45 dB of the program's
    //    peak bin, absolute floor -75 dB): the band must carry honest
    //    input energy to measure a transfer through;
    //  - the COHERENCE gate (g2 = |Sxy|^2 / (Sxx*Syy) >= 0.25, computed
    //    by the analyzer): g2 collapses towards zero exactly where the
    //    output band is dominated by uncorrelated junk - Smaart's
    //    coherence blanking. There the bin HOLDS its last value instead
    //    of reporting noise-over-noise, so the curve shows the true
    //    rolloff down to where the signal sinks under the console
    //    floor, then stops - it no longer rises back.
    // Kept from earlier versions: the v2.12 persistence (a bin
    // measured once stays drawn, holding its last value).
    //
    // The measurement is already time-averaged in the analyzer's EMAs
    // (~1.4 s effective); the frequency-dependent settling and
    // confidence weights of v2.13/v2.21 tuned per-frame dB ratios that
    // no longer exist, so the display EMA here is a single light
    // one-pole (tau 0.25 s at 30 Hz) for continuity only.
    {
        float peakIn = -100.0f;

        for (int b = 0; b < SpectrumAnalyzer::numScopeBins; ++b)
            peakIn = juce::jmax (peakIn, rawIn[b]);

        const float gateDb = juce::jmax (-75.0f, peakIn - 45.0f);

        // the transfer magnitude (dB) and coherence per display bin
        float eqDb        [SpectrumAnalyzer::numScopeBins];
        float eqCoherence [SpectrumAnalyzer::numScopeBins];

        processor.getTransferAnalyzer().copyTransferData (eqDb, eqCoherence,
                                                          SpectrumAnalyzer::numScopeBins);

        const float eqAlpha = 1.0f / (1.0f + 30.0f * 0.25f);

        for (int b = 0; b < SpectrumAnalyzer::numScopeBins; ++b)
        {
            if (rawIn[b] <= gateDb || eqCoherence[b] < 0.25f)
                continue; // not measurable right now: hold the last value

            // this bin carries (or has carried) honest, coherent
            // content - from now on it stays part of the drawn curve
            eqMeasured[(size_t) b] = true;

            smoothDiff[(size_t) b] += eqAlpha * (eqDb[b] - smoothDiff[(size_t) b]);
        }
    }

    // v2.8.1: loudness-anchored axis calibration. The 0 dB line must read
    // as "-23 LUFS": a program played at -23 LUFS shows its
    // energy-dominant bands right ON the 0 line, and playing it X dB
    // louder moves the whole display up by X dB.
    //
    // v2.8 got this wrong: its offset simply tracked (LUFS + 23), i.e.
    // the display subtracted the program level from itself - so the
    // curve never moved with the volume, and since the FFT bins live on
    // their own scale (tens of dB below the program's LUFS, the energy
    // being spread across bins) a -23 LUFS mix never came near 0.
    //
    // v2.8.4 fixed the anchor itself. v2.8.1 anchored the AVERAGE ENERGY
    // PER ACTIVE BIN (10 * log10 (sumE / numActiveBins)), which is only
    // correct for a flat spectrum: on real material (energy concentrated
    // in the bass, hundreds of weak bins still above the -90 dB gate)
    // that average lands far BELOW the dominant level, pushing the whole
    // display UP by 10*log10 (active / dominant) - easily +10..+20 dB,
    // which is why the curve rode at the top of the panel.
    //
    // The anchor is now the ENERGY-WEIGHTED MEAN OF THE BIN LEVELS:
    //
    //   w(b)      = 10^(db(b) / 10)          (db = tilt-weighted bin level)
    //   meanDb    = sum (w * db) / sum (w)   over the bins carrying energy
    //   lufsOffsetDb (target) = meanDb - (shortTermLUFS - lufsTargetDb)
    //   display(b) = raw(b) - tilt(b) - lufsOffsetDb
    //              = (raw(b) - tilt(b) - meanDb) + (LUFS - lufsTargetDb)
    //   (v2.8-v2.12 had a hardcoded +23 there; v2.13 makes it the
    //    selectable Record/Mix/Master reference)
    //
    // The weights make the loudest bands dominate the mean, so the
    // ENERGY-DOMINANT bands read exactly (LUFS - lufsTargetDb): 0 dB at
    // the selected reference LUFS, a flat (pink noise) spectrum reads
    // flat there, and volume changes move the curve instantly (only the
    // loudness-relative anchor is smoothed, EMA tau ~1.6 s at 30 Hz,
    // held while the input is silent). The offset still cancels in
    // OUT-IN, so the EQ curve is unaffected.
    // v2.13: the reference is no longer hardcoded -23; it is the
    // selectable lufsTargetDb (Record -37 / Mix -23 / Master -14),
    // cycled by clicking the label at the 0 dB line.
    const float lufs = processor.getLufsIn().getShortTermLufs();

    if (lufs > -60.0f)
    {
        double sumWeightedDb = 0.0;
        double sumWeights    = 0.0;

        for (int b = 0; b < SpectrumAnalyzer::numScopeBins; ++b)
        {
            // bins without energy must stay out of the mean (a lone
            // tone would otherwise be dragged down by 250 empty bins)
            if (in[b] <= -90.0f)
                continue;

            const float freq = (float) processor.getAnalyzerIn().getBinFrequency (b);
            const float db = in[b] - tilt * (std::log2 (freq / 1000.0f));

            const double w = std::pow (10.0, (double) db / 10.0);
            sumWeightedDb += w * (double) db;
            sumWeights    += w;
        }

        if (sumWeights > 0.0)
        {
            const float meanDb = (float) (sumWeightedDb / sumWeights);
            // v2.13: 0 dB = the selected reference (lufsTargetDb LUFS
            // absolute; was a hardcoded +23 before)
            const float target = meanDb - (lufs - lufsTargetDb);

            // snap on the first valid reading so the axis is already
            // calibrated when the editor opens; smooth afterwards
            if (! lufsOffsetSnapped)
            {
                lufsOffsetDb = target;
                lufsOffsetSnapped = true;
            }
            else
            {
                lufsOffsetDb += 0.02f * (target - lufsOffsetDb);
            }
        }
    }
}

// v2.21: harness hooks - the offline test suite constructs a panel with
// no visible editor and drives the analysis manually (the JUCE timer
// never fires in the console harness, so there is no double ticking).
void SpectrumPanel::runAnalysisTick()
{
    updateBallistics();
}

float SpectrumPanel::getEqDbAtBin (int bin) const
{
    return smoothDiff[(size_t) juce::jlimit (0, SpectrumAnalyzer::numScopeBins - 1, bin)];
}

bool SpectrumPanel::isEqBinMeasured (int bin) const
{
    return eqMeasured[(size_t) juce::jlimit (0, SpectrumAnalyzer::numScopeBins - 1, bin)];
}

void SpectrumPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0e1420));

    drawGrid (g);
    // v2.20 (point 2): the IN curve is drawn WITHOUT its area fill now -
    // only OUT keeps its soft fill, so the two curves read as two lines
    // over one fill instead of two overlapping tints.
    drawCurve (g, smoothIn,  juce::Colour (0xffe2a14f), 1.6f, false);
    drawCurve (g, smoothOut, juce::Colour (0xff54c8e8));

    // v2.10: 10-second sliding maximum of the raw output - a thin trace
    // (no fill) on top of OUT, showing where the output peaked over the
    // last 10 s. It resets when the editor is reopened.
    // v2.23 (épurée): 70% -> 45% alpha - the envelope stays readable
    // against its own curve without turning the treble into lace.
    drawCurve (g, maxOut, juce::Colour (0xff9adcf0).withAlpha (0.45f), 1.2f, false);

    // v2.6: the measured transfer function, drawn on top without fill.
    drawTransferCurve (g);

    // legend (v2.8: centred at the top; the left edge now carries the
    // scale labels and the right edge the EQ range labels)
    // v2.23 (épurée): a proper colour-chip row - a small dash in the
    // trace colour + a NEUTRAL grey label (the words used to be fully
    // coloured and fought with the curves for attention). The transfer
    // curve is labelled "EQ", the name the rest of the UI already uses
    // (hover readout, EQ scale, help); the row is measured and centred
    // as a whole instead of a magic offset.
    {
        struct LegendItem { juce::Colour colour; const char* text; };

        const LegendItem items[] =
        {
            { juce::Colour (0xffe2a14f), "IN"  },
            { juce::Colour (0xff54c8e8), "OUT" },
            { juce::Colour (0xff7ee08a), "EQ"  },
            { juce::Colour (0xff9adcf0), "MAX" }
        };

        const juce::Font font (juce::FontOptions (11.0f));
        g.setFont (font);

        constexpr float chipWidth = 12.0f, chipGap = 4.0f, itemGap = 18.0f;

        // measure the whole row once, then centre it
        float total = -itemGap;

        for (const auto& item : items)
            total += chipWidth + chipGap
                     + juce::GlyphArrangement::getStringWidth (font, item.text)
                     + itemGap;

        float x = juce::jmax (8.0f, ((float) getWidth() - total) * 0.5f);

        for (const auto& item : items)
        {
            g.setColour (item.colour);
            g.fillRoundedRectangle (juce::Rectangle<float> (x, 9.5f,
                                                            chipWidth, 3.0f),
                                    1.5f);
            x += chipWidth + chipGap;

            const int textWidth = (int) juce::GlyphArrangement::getStringWidth (font, item.text);

            g.setColour (juce::Colour (0xff8f9bb0));
            g.drawText (item.text, (int) x, 3, textWidth + 2, 16,
                        juce::Justification::left);

            x += (float) textWidth + itemGap;
        }
    }

    // v2.19 (roadmap point 3): the hover readout comes last, above the
    // legend, so it stays readable over everything
    if (mouseInside)
        drawHoverReadout (g);
}

void SpectrumPanel::drawGrid (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float w = b.getWidth();
    const float h = b.getHeight();

    // v2.9: vertical grid line at EVERY decade tick inside the 16 Hz -
    // 24 kHz axis (20/30/.../90 Hz, 100/200/.../900 Hz, 1k/2k/.../9k,
    // 10k, 20k). 10 Hz is below the left edge and is skipped; 20 kHz is
    // now inside the panel and gets its line back. Labels only on the
    // classic frequencies (below).
    // v2.23 (point 11, esthétique épurée): the vertical grid is now
    // TWO-TIER, the classic analyser hierarchy (SPAN & co). The 1-2-5
    // frequencies (20/50/100/.../10k/20k) carry the brighter line AND
    // the axis label; every other decade multiple is a much fainter
    // minor line. The old same-weight grid put ~25 identical lines on
    // screen - busy without adding information; the two-tier version
    // guides the eye and makes the labelled ticks pop.
    for (double decade : { 10.0, 100.0, 1000.0, 10000.0 })
    {
        for (int m = 1; m <= 9; ++m)
        {
            const double f = decade * (double) m;

            // outside the 16 Hz - 24 kHz axis (10 Hz only)
            if (f < 16.0 || f > 24000.0)
                continue;

            const bool major = (m == 1 || m == 2 || m == 5);

            g.setColour (juce::Colour (major ? 0xff2a3550 : 0xff1a2233));
            g.drawVerticalLine ((int) xForFrequency (f), 0.0f, h);
        }
    }

    // v2.9: horizontals - nine divisions, top = 3 x dB per division
    // (+30 dB at 10 dB/div, +18 dB at 6 dB/div, relative to the
    // loudness reference), so the 0 dB line sits one third of the
    // panel height from the top in both scales. The 0 dB line (which
    // IS the reference LUFS absolute) is slightly brighter and
    // annotated on the left (v2.13: the annotation is clickable).
    const float zeroY = yForDb (0.0f);

    for (int k = 0; k <= 9; ++k)
    {
        const float db = topDb() - (float) k * dbPerDiv;

        g.setColour (std::abs (db) < 0.01f ? juce::Colour (0xff3a4560)
                                          : juce::Colour (0xff1a2233));
        g.drawHorizontalLine ((int) yForDb (db), 0.0f, w);
    }

    // ---- Y labels: dB relative to the loudness reference ----
    g.setColour (juce::Colour (0xff5c6a85));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));

    for (int k = 0; k <= 9; ++k)
    {
        const float db = topDb() - (float) k * dbPerDiv;
        const int y = juce::jmax (2, (int) yForDb (db) - 6);
        const juce::String label = (db > 0.01f ? "+" : (db < -0.01f ? "-" : ""))
                                 + juce::String (std::abs (db), 0);

        g.drawText (label, 4, y, 34, 13, juce::Justification::left);
    }

    // the 0 line carries the loudness reference (drawn to the right of
    // the "0" label so it never collides with the bottom frequency
    // labels). v2.13: the reference is clickable (cycles Record -37 /
    // Mix -23 / Master -14 LUFS) and drawn bigger and brighter than
    // the plain dB labels, so it reads as the control it now is.
    g.setColour (juce::Colour (0xff8f9bb0));
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText (lufsTargetLabel(), 40, (int) zeroY - 8, 150, 15,
                juce::Justification::left);

    // ---- X labels: the 1-2-5 majors (SPAN convention) ----
    // v2.9: the axis runs from 16 Hz to 24 kHz; both 20 Hz and 20 kHz
    // are inside the panel now, so every label is a plain centred label
    // (the 16 Hz left edge carries no value, like the 24 kHz right
    // edge). v2.23 (épurée): the labels sit exactly on the brighter
    // 1-2-5 grid lines - 20/50/100/200/500/1k/2k/5k/10k/20k - a
    // balanced, complete ladder (the old 20/100/200/1k/2k/10k/20k set
    // skipped half of its own ticks).
    g.setFont (juce::Font (juce::FontOptions (11.0f)));

    const std::pair<double, const char*> xLabels[] =
    {
        { 20.0,     "20" },
        { 50.0,     "50" },
        { 100.0,    "100" },
        { 200.0,    "200" },
        { 500.0,    "500" },
        { 1000.0,   "1k" },
        { 2000.0,   "2k" },
        { 5000.0,   "5k" },
        { 10000.0,  "10k" },
        { 20000.0,  "20k" }
    };

    for (const auto& l : xLabels)
    {
        const float x = xForFrequency (l.first);
        g.drawText (l.second, (int) x - 18, (int) h - 15, 36, 13,
                    juce::Justification::centred);
    }
}

float SpectrumPanel::yForDb (float db) const
{
    const float h = (float) getHeight();
    const float y01 = juce::jlimit (0.0f, 1.0f,
                                    (db - bottomDb()) / (topDb() - bottomDb()));
    return h * (1.0f - y01);
}

// v2.8: dedicated scale for the EQ curve: +/- 4.5 divisions of eqDbPerDiv
// spans the full panel height, like a classic EQ display (+/-18 dB at
// 4 dB/div, +/-9 dB at 2 dB/div).
float SpectrumPanel::yForEqDb (float db) const
{
    const float h = (float) getHeight();
    const float y01 = juce::jlimit (0.0f, 1.0f,
                                    (db + eqRangeDb()) / (2.0f * eqRangeDb()));
    return h * (1.0f - y01);
}

float SpectrumPanel::xForBin (int bin) const
{
    return xForFrequency (processor.getAnalyzerIn().getBinFrequency (bin));
}

// v2.9: the horizontal axis now runs from 16 Hz to 24 kHz (log). The
// scope data starts at 20 Hz and stops at Nyquist, so the 16-20 Hz and
// 20 kHz-24 kHz margins stay (mostly) data-free: they centre the useful
// band and keep the axis labels clear of the panel edges, like the
// guarded edges on a pro analyser. 20 Hz and 20 kHz are both INSIDE the
// panel now and get normal centred labels.
float SpectrumPanel::xForFrequency (double freq) const
{
    constexpr double minHz = 16.0;
    constexpr double span  = 3.1760912590556813; // log10 (24000 / 16)

    const double x01 = juce::jlimit (0.0, 1.0, std::log10 (freq / minHz) / span);
    return (float) getWidth() * (float) x01;
}

void SpectrumPanel::drawCurve (juce::Graphics& g,
                               const std::array<float, SpectrumAnalyzer::numScopeBins>& data,
                               juce::Colour colour, float lineThickness, bool withFill)
{
    const auto b = getLocalBounds().toFloat();
    juce::Path path;
    bool started = false;

    for (int bin = 0; bin < SpectrumAnalyzer::numScopeBins; ++bin)
    {
        const float x = xForBin (bin);
        const float freq = (float) processor.getAnalyzerIn().getBinFrequency (bin);

        // weighting tilt (dB/octave, referenced at 1 kHz). SPAN convention:
        // a NEGATIVE tilt flattens a naturally falling spectrum, so -4.5
        // makes a typical mix curve read roughly flat (HF visually boosted,
        // LF visually cut). v2.2 fixed the inverted sign.
        // v2.8: the loudness offset shifts the spectra so the grid's 0 dB
        // line corresponds to -23 LUFS absolute (see updateBallistics).
        const float db = data[(size_t) bin]
                       - tilt * (std::log2 (freq / 1000.0f))
                       - lufsOffsetDb;
        const float y = yForDb (db);

        if (! started)
        {
            path.startNewSubPath (x, y);
            started = true;
        }
        else
        {
            path.lineTo (x, y);
        }
    }

    if (withFill)
    {
        juce::Path fill (path);
        fill.lineTo (b.getWidth(), b.getBottom());
        fill.lineTo (0.0f, b.getBottom());
        fill.closeSubPath();

        // v2.23 (épurée): the fill fades out towards the bottom of the
        // panel instead of a flat tint - the curve keeps its soft glow
        // while the low end of the panel stays airy; the eye lands on
        // the line, not on the block of colour.
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.22f),
                                                 0.0f, 0.0f,
                                                 colour.withAlpha (0.02f),
                                                 0.0f, b.getBottom(),
                                                 false));
        g.fillPath (fill);
    }

    g.setColour (colour);
    // v2.23 (épurée): curved joins + rounded caps - the quality stroke
    // for analyser traces; no more miter spikes where the curve turns
    // steeply (filter knees, panel edges).
    g.strokePath (path, juce::PathStrokeType (lineThickness,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

// v2.6: measured transfer function of the hosted plugin (OUT minus IN).
// The tilt cancels in the difference, so no tilt is applied here - and
// neither does the v2.8 loudness offset. v2.7: the curve uses its own
// scale instead of sharing the spectrum's. v2.8: that scale is
// selectable, +/- 4.5 divisions of 4 or 2 dB (i.e. +/-18 dB or +/-9 dB),
// labelled on the right edge (v2.12: clicking those labels toggles the
// scale).
// v2.12: the path covers every bin that has EVER been measured
// (eqMeasured), not just the bins where the input currently has energy
// - previously the path broke wherever smoothIn fell below -80 dB, so
// whole pieces of the curve vanished and came back with the program's
// spectrum. Measured bins keep their last value; the path only breaks
// at never-measured bins (e.g. the 16-20 Hz and 20k-24k data-free
// margins).
void SpectrumPanel::drawTransferCurve (juce::Graphics& g)
{
    juce::Path path;
    bool started = false;

    const float range = eqRangeDb();

    for (int bin = 0; bin < SpectrumAnalyzer::numScopeBins; ++bin)
    {
        if (! eqMeasured[(size_t) bin])
        {
            started = false; // skip bins that were never measured
            continue;
        }

        const float x = xForBin (bin);
        const float y = yForEqDb (juce::jlimit (-range, range, smoothDiff[(size_t) bin]));

        if (! started)
        {
            path.startNewSubPath (x, y);
            started = true;
        }
        else
        {
            path.lineTo (x, y);
        }
    }

    // v2.7: a reference line at the EQ 0 dB, so the curve reads against
    // it like a classic EQ display (the spectrum grid's 0 dB is elsewhere).
    // v2.23 (épurée): the anchor is now a thin DASHED line - the classic
    // reference-line convention of EQ displays - instead of a solid
    // tinted band, and the curve itself is thinner (1.8 px, with the
    // same curved joins + rounded caps as the spectra). The old 2.4 px
    // stroke was the loudest thing on the panel; épurée means the
    // measurement reads, not shouts.
    {
        // createDashedStroke is a separate PathStrokeType method in
        // JUCE (the constructor takes no dash pattern): bake the dash
        // into a path, then stroke that path.
        juce::Path anchor;
        const float y = yForEqDb (0.0f);
        anchor.startNewSubPath (0.0f, y);
        anchor.lineTo ((float) getWidth(), y);

        const float dashPattern[] = { 4.0f, 4.0f };
        const juce::PathStrokeType dashStroke (1.0f);

        juce::Path dashed;
        dashStroke.createDashedStroke (dashed, anchor, dashPattern, 2);

        g.setColour (juce::Colour (0xff7ee08a).withAlpha (0.35f));
        g.strokePath (dashed, dashStroke);
    }

    g.setColour (juce::Colour (0xff7ee08a));
    g.strokePath (path, juce::PathStrokeType (1.8f,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    // the EQ scale labels, match the curve's range (v2.8: dynamic,
    // "+18"/"0"/"-18" at 4 dB/div, "+9"/"0"/"-9" at 2).
    // v2.20 (point 6): the column moved FLUSH against the right edge -
    // mirroring the spectrum's dB labels at the far left - so it no
    // longer shares the top-right corner with the hover readout (which
    // keeps its own clear margin, see drawHoverReadout).
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    // v2.23 (épurée): an explicit SOFTER green - the labels used to
    // inherit the full-strength curve colour and competed with it
    g.setColour (juce::Colour (0xff7ee08a).withAlpha (0.75f));

    for (float db : { range, 0.0f, -range })
    {
        const juce::String label = (db > 0.0f) ? "+" + juce::String (db, 0)
                                   : ((db < 0.0f) ? "-" + juce::String (-db, 0)
                                                  : juce::String ("0"));
        const int y = juce::jlimit (2, getHeight() - 14, (int) yForEqDb (db) - 6);
        g.drawText (label, getWidth() - 34, y, 30, 12,
                    juce::Justification::centredRight);
    }
}

// ---------------------------------------------------------------------------
// HostedEditorHolder
// ---------------------------------------------------------------------------

void HostedEditorHolder::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff121724));
}

// ---------------------------------------------------------------------------
// TheChannelStripAudioProcessorEditor
// ---------------------------------------------------------------------------

TheChannelStripAudioProcessorEditor::TheChannelStripAudioProcessorEditor (TheChannelStripAudioProcessor& p)
    : AudioProcessorEditor (p),
      proc (p),
      spectrumPanel (p),
      tooltipWindow (this)
{
    // v2.4: one condensed sidebar + centre column. The window auto-fits
    // the hosted editor once it is embedded (fitWindowToHostedEditor),
    // so this is just the pre-embed default.
    // v2.8: dynamic minimum with/without the analyser strip.
    // v2.10: +30 px for the A/B preset row -> 800 / 610.
    // v2.12: the two scale rows moved onto the analyser (click its
    // scale labels) -> 700 / 520.
    // v2.13: two more console buttons (Focusrite SC, bx_console N)
    // -> 770 / 590.
    // v2.16: two more console buttons (Lindell 80/50 Channel) -> 840 / 660.
    // v2.17: the studio logo block -> 960 / 780.
    // v2.17.1: logo halved -> 900 / 720.
    // v2.26: the presets section (combo + two button rows) -> 1000 / 820.
    setSize (1180, 1000);
    setResizable (true, false);
    setResizeLimits (1024, proc.getAnalyserVisible() ? 1000 : 820, 2560, 1600);

    // v2.4: the title sits at the top of the condensed left sidebar, so
    // it is small and wraps onto two lines inside the column.
    titleLabel.setText ("THE CHANNEL STRIP", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffffff));
    addAndMakeVisible (titleLabel);

    for (auto* label : { &lufsInLabel, &lufsOutLabel, &correctionLabel })
    {
        label->setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        label->setJustificationType (juce::Justification::centred);
        label->setColour (juce::Label::textColourId, juce::Colour (0xffdfe6f2));
        addAndMakeVisible (*label);
    }

    // v2.4: section headings + the hosted-plugin name, all in the sidebar.
    gainLabel.setText ("GAIN STAGING", juce::dontSendNotification);
    gainLabel.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    gainLabel.setJustificationType (juce::Justification::centred);
    gainLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8f9bb0));
    addAndMakeVisible (gainLabel);

    hostedNameLabel.setText ("--", juce::dontSendNotification);
    hostedNameLabel.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    hostedNameLabel.setJustificationType (juce::Justification::centred);
    hostedNameLabel.setColour (juce::Label::textColourId, juce::Colour (0xffdfe6f2));
    hostedNameLabel.setTooltip ("The hosted console plugin, once one is selected.");
    addAndMakeVisible (hostedNameLabel);

    // v2.14.2: live channel-format readout - what the DAW feeds this
    // instance and which audio path the console takes. On a mono track the
    // suffix says exactly why the console is bridged: "refused" (the plugin
    // rejected the mono->mono layout), "2/2" (it answered with a next-best
    // layout) or "multi-bus" (no main bus to negotiate).
    channelLabel.setText ("track: stereo", juce::dontSendNotification);
    channelLabel.setFont (juce::Font (juce::FontOptions (10.0f)));
    channelLabel.setJustificationType (juce::Justification::centred);
    channelLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8f9bb0));
    channelLabel.setTooltip ("Channel format the host gives this instance and the console's audio path.");
    addAndMakeVisible (channelLabel);

    // v2.1: the correction is the main readout: bigger and smoothed.
    // v2.4: slightly smaller so the condensed sidebar fits at min height.
    // v2.13: back to the correction reading (v2.12's measured-deviation
    // display went back to an action at Clement's request), now with an
    // explicit "Add" prefix: "Add +0.7 dB" = raise the output by 0.7 dB
    // to match the input loudness; "Add -0.4 dB" = lower it by 0.4 dB.
    correctionLabel.setColour (juce::Label::textColourId, juce::Colour (0xff54c8e8));
    correctionLabel.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    correctionLabel.setJustificationType (juce::Justification::centred);
    correctionLabel.setTooltip ("Smoothed short-term BS.1770 correction: what to ADD to "
                                "the console's output so it matches its input loudness. "
                                "Add +0.7 dB means raise the output by 0.7 dB; Add -0.4 dB "
                                "means lower it by 0.4 dB.");

    // v2.17: Clement's studio logo, embedded as a binary resource so the
    // plugin is fully self-contained. ImageComponent scales it to the
    // sidebar width, preserving the aspect ratio (never upscaling).
    studioLogo.setImage (juce::ImageCache::getFromMemory (BinaryData::CostesLogo_png,
                                                           BinaryData::CostesLogo_pngSize),
                         juce::RectanglePlacement (juce::RectanglePlacement::centred
                                                   | juce::RectanglePlacement::onlyReduceInSize));
    addAndMakeVisible (studioLogo);

    // v2.20 (point 1): the "?" help button sits next to the studio logo
    // and opens every tool tip in one window - the controls themselves
    // keep their small tooltips, but the analyser no longer shows one
    // while hovering (it was hiding the readout).
    styleConsoleButton (helpButton);
    helpButton.setTooltip ("All the tool tips in one place.");
    helpButton.onClick = [this] { showHelpPopup(); };
    addAndMakeVisible (helpButton);

    addAndMakeVisible (rescanButton);
    rescanButton.onClick = [this] { proc.requestDefaultScan(); };
    // v2.9.1: in a DAW the button only reloads the saved database (loading
    // third-party modules in-process can crash the host); the full folder
    // scan runs in the Standalone, out-of-process.
    rescanButton.setTooltip ("Reload the saved plugin database. In the Standalone "
                             "this re-scans the VST3 folders (out-of-process, hostile "
                             "plugins can only kill the helper); inside a DAW it only "
                             "reloads the database - launch the Standalone to run a "
                             "full folder scan.");

    // ---- the console buttons (radio group: exactly one) ----
    for (size_t i = 0; i < consoleButtons.size(); ++i)
    {
        auto& b = consoleButtons[i];

        b.setButtonText (TheChannelStripAudioProcessor::consoleLabel ((int) i));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (1);
        styleConsoleButton (b);
        addAndMakeVisible (b);

        b.onClick = [this, i] { proc.selectConsole ((int) i); };
    }

    // ---- v2.26 (roadmap point 4): the presets section ----
    // One combo box listing every preset on disk, then Save/Delete and
    // Export/Import. Selecting an entry loads it (switching consoles
    // when needed); every action reports to the status area.
    presetLabel.setText ("PRESETS", juce::dontSendNotification);
    presetLabel.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    presetLabel.setJustificationType (juce::Justification::centred);
    presetLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8f9bb0));
    addAndMakeVisible (presetLabel);

    presetCombo.setJustificationType (juce::Justification::centred);
    presetCombo.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1a2130));
    presetCombo.setColour (juce::ComboBox::textColourId,       juce::Colour (0xffdfe6f2));
    presetCombo.setColour (juce::ComboBox::outlineColourId,   juce::Colour (0xff5f6b82));
    presetCombo.setColour (juce::ComboBox::arrowColourId,     juce::Colour (0xff8f9bb0));
    presetCombo.setTextWhenNothingSelected ("(no presets)");
    presetCombo.setTooltip ("Every preset on disk, as \"Console - Name\". Select one to load it:"
                            " if its console is not the one hosted, the shell switches to that"
                            " console first. The preset holds the console's knobs only - display"
                            " settings (tilt, scales, LUFS) travel with the session.");
    presetCombo.onChange = [this] { onPresetSelected(); };
    addAndMakeVisible (presetCombo);

    for (auto* b : { &savePresetButton, &deletePresetButton,
                    &exportPresetButton, &importPresetButton })
    {
        styleConsoleButton (*b);
        addAndMakeVisible (*b);
    }

    savePresetButton.onClick   = [this] { showSavePresetDialog(); };
    deletePresetButton.onClick = [this] { confirmDeletePreset(); };
    exportPresetButton.onClick = [this] { exportSelectedPreset(); };
    importPresetButton.onClick = [this] { importPresetFile(); };

    savePresetButton.setTooltip ("Save the hosted console's current settings as a named"
                                 " preset file (.tcsPreset). An existing preset of the same"
                                 " name is overwritten.");
    deletePresetButton.setTooltip ("Delete the preset selected in the list above (asks first).");
    exportPresetButton.setTooltip ("Copy the preset selected in the list to a file anywhere"
                                   " on disk - for sharing between machines or projects.");
    importPresetButton.setTooltip ("Import a .tcsPreset file from anywhere into the preset"
                                   " folder, so it appears in the list.");

    refreshPresetList();

    // ---- v2.12: analyser response speed (IN/OUT smoothing) ----
    // Back to two speeds: Fast ~0.15 s (close to the pre-v2.10 snappy
    // ballistics) and Slow ~1 s (the v2.10/v2.11 default, formerly
    // "Med"). The old 3 s Slow is gone. The choice is saved with the
    // session (specTau in the processor); any legacy saved value snaps
    // to the closest of the two. TextButtons styled like the console
    // buttons (the old ToggleButtons auto-shrank longer labels).
    {
        const float specTaus[2] = { 0.15f, 1.0f };
        const char* const speedNames[2] = { "Fast", "Slow" };

        for (size_t j = 0; j < speedButtons.size(); ++j)
        {
            auto& b = speedButtons[j];

            b.setButtonText (speedNames[j]);
            b.setClickingTogglesState (true);
            b.setRadioGroupId (5);
            styleConsoleButton (b);
            addAndMakeVisible (b);

            b.onClick = [this, j, specTaus]
            {
                if (speedButtons[j].getToggleState())
                {
                    proc.setSpecTau (specTaus[j]);
                    spectrumPanel.setSpecTau (specTaus[j]);
                }
            };

            b.setTooltip ("Analyser response speed: the IN/OUT curves smooth "
                          "with a time constant of ~0.15 s (Fast) or ~1 s (Slow).");
        }

        // restore the saved speed, snapped to the closest of the two
        // (default Slow ~1 s); canonicalise the processor value too, so
        // the next save stores exactly one of the two current speeds
        const int match = (proc.getSpecTau() < 0.575f) ? 0 : 1;

        proc.setSpecTau (specTaus[match]);
        spectrumPanel.setSpecTau (specTaus[match]);

        speedButtons[0].setToggleState (match == 0, juce::dontSendNotification);
        speedButtons[1].setToggleState (match == 1, juce::dontSendNotification);
    }

    // ---- tilt weighting (-3 / -4.5 / -6 dB per octave) ----
    tiltLabel.setText ("TILT dB/OCT", juce::dontSendNotification);
    tiltLabel.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    tiltLabel.setJustificationType (juce::Justification::centred);
    tiltLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8f9bb0));
    addAndMakeVisible (tiltLabel);

    const float tilts[3] = { -3.0f, -4.5f, -6.0f };
    const char* const tiltNames[3] = { "-3", "-4.5", "-6" };

    for (size_t j = 0; j < tiltButtons.size(); ++j)
    {
        auto& t = tiltButtons[j];

        // v2.12: TextButtons styled like the console buttons, so all
        // three labels render at the SAME font size - the old
        // ToggleButtons used drawFittedText, which shrank "-4.5" (the
        // widest label) to squeeze it beside the tick box, making it
        // visibly smaller than its neighbours.
        t.setButtonText (tiltNames[j]);
        t.setClickingTogglesState (true);
        t.setRadioGroupId (2);
        styleConsoleButton (t);
        addAndMakeVisible (t);

        // v2.20: the tilt buttons get a tooltip too (they were the only
        // analyser controls without one, and the help window lists it).
        t.setTooltip ("Analyser display tilt in dB per octave (reference 1 kHz, "
                      "SPAN convention: a negative tilt flattens a naturally "
                      "falling spectrum). The tilt only affects the drawing, "
                      "never the analysis.");

        t.onClick = [this, j, tilts]
        {
            if (tiltButtons[j].getToggleState())
            {
                proc.setTiltDb (tilts[j]);
                spectrumPanel.setTilt (tilts[j]);
            }
        };
    }

    // v2.7.1: restore the saved tilt (the processor reloads it from the
    // session state before the editor opens). -4.5 dB/octave is the
    // default when nothing (or an unknown value) was saved.
    {
        int match = 1;
        for (int j = 0; j < 3; ++j)
            if (std::abs (tilts[j] - proc.getTiltDb()) < 0.01f)
                match = j;

        tiltButtons[(size_t) match].setToggleState (true, juce::dontSendNotification);
        spectrumPanel.setTilt (tilts[match]);
    }

    // ---- v2.12: the scale button rows are gone - the scales are
    //      toggled by clicking the analyser's own scale labels ----
    // Clicking the analyser's LEFT dB scale toggles 10 / 6 dB per
    // division (spectrum); clicking the RIGHT green EQ scale toggles
    // 4 / 2 dB per division. The state stays in the processor (saved
    // with the session, exactly like the v2.8 buttons); the panel just
    // reports which column was clicked (see SpectrumPanel::mouseDown).
    spectrumPanel.onAnalyzerScaleClicked = [this]
    {
        const float newDiv = (proc.getAnaDbDiv() > 7.5f) ? 6.0f : 10.0f;
        proc.setAnaDbDiv (newDiv);
        spectrumPanel.setDbPerDiv (newDiv);
    };

    spectrumPanel.onEqScaleClicked = [this]
    {
        const float newDiv = (proc.getEqDbDiv() > 3.0f) ? 2.0f : 4.0f;
        proc.setEqDbDiv (newDiv);
        spectrumPanel.setEqDbPerDiv (newDiv);
    };

    // v2.13: clicking the loudness-reference label at the 0 dB line
    // cycles Record -37 -> Mix -23 -> Master -14 -> Record. The choice
    // is persisted with the session (lufsTarget); the panel re-snaps
    // its calibration instantly when it changes.
    spectrumPanel.onLufsTargetClicked = [this]
    {
        const float targets[3] = { -37.0f, -23.0f, -14.0f };

        int match = 1; // default: Mix
        for (int j = 0; j < 3; ++j)
            if (std::abs (targets[j] - proc.getLufsTarget()) < 0.5f)
                match = j;

        const int next = (match + 1) % 3;

        proc.setLufsTarget (targets[next]);
        spectrumPanel.setLufsTarget (targets[next]);
    };

    // restore the saved loudness reference (default Mix -23 LUFS)
    spectrumPanel.setLufsTarget (proc.getLufsTarget());

    // v2.20 (point 1): the analyser no longer carries a tooltip - it
    // was in the way of the hover readout. Its help text (and every
    // other tooltip) now lives in the "?" button next to the studio
    // logo (see showHelpPopup).

    // restore the saved scales (defaults: 10 dB and 4 dB per division;
    // legacy values are snapped to the two current scales, like the
    // v2.9 button restore did).
    spectrumPanel.setDbPerDiv (proc.getAnaDbDiv() > 7.5f ? 10.0f : 6.0f);
    spectrumPanel.setEqDbPerDiv (proc.getEqDbDiv() > 3.0f ? 4.0f : 2.0f);

    // ---- v2.8: show/hide the analyser strip ----
    analyserToggle.setTooltip ("Show or hide the analyser strip (the window re-fits).");
    addAndMakeVisible (analyserToggle);

    analyserToggle.onClick = [this]
    {
        proc.setAnalyserVisible (analyserToggle.getToggleState());
        applyAnalyserVisibility();
    };

    analyserToggle.setToggleState (proc.getAnalyserVisible(), juce::dontSendNotification);

    addAndMakeVisible (spectrumPanel);

    addAndMakeVisible (hostedViewport);
    hostedViewport.setViewedComponent (&hostedHolder, false);

    // v2.4: the status area sits at the bottom of the sidebar.
    statusLabel.setFont (juce::Font (juce::FontOptions (10.0f)));
    statusLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8f9bb0));
    addAndMakeVisible (statusLabel);

    // v2.6: no bypass button/attachment anymore - the DAW and the hosted
    // plugin already provide a bypass, and the wrapper's crossfade path
    // was removed from processBlock with it.

    // auto-scan the default folders the first time the list is empty
    {
        const juce::ScopedLock lock (proc.getScanLock());

        if (proc.getHostedStrip().getKnownPluginList().getTypes().isEmpty())
            proc.requestDefaultScan();
    }

    // v2.8: from now on processBlock measures LUFS and spectra (the
    // meters were reset by setEditorOpen, so nothing stale is shown).
    proc.setEditorOpen (true);

    startTimerHz (30);
}

TheChannelStripAudioProcessorEditor::~TheChannelStripAudioProcessorEditor()
{
    stopTimer();
    // v2.8: closing the editor stops all measurements (LUFS meters and
    // spectrum analysers) until it is opened again.
    proc.setEditorOpen (false);
    embeddedHostedEditor.reset(); // detach the hosted editor before anything else
    hostedViewport.setViewedComponent (nullptr, false);
}

void TheChannelStripAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0c0f16));

    // v2.5: a subtle panel behind the sidebar (which starts under the
    // full-width analyser strip) so the zones read at a glance.
    // v2.8: with the analyser hidden the sidebar starts right below the
    // top margin (the strip and its gap are gone).
    const int zoneY = layoutMargin
                    + (proc.getAnalyserVisible() ? analyserStripHeight + columnGap : 0);
    g.setColour (juce::Colour (0xff121724));
    g.fillRect (layoutMargin, zoneY,
                sidebarWidth, getHeight() - layoutMargin - zoneY);
}

void TheChannelStripAudioProcessorEditor::resized()
{
    const int w = getWidth();
    const int h = getHeight();
    const int m  = layoutMargin;
    const int sw = sidebarWidth;

    // ---- v2.5: analyser strip across the FULL window width on top;
    //      v2.8: hidden entirely when the Display toggle is off ----
    const bool showStrip = proc.getAnalyserVisible();

    spectrumPanel.setVisible (showStrip);

    if (showStrip)
        spectrumPanel.setBounds (m, m, w - 2 * m, analyserStripHeight);

    const int zoneY = m + (showStrip ? analyserStripHeight + columnGap : 0);

    const int sideX   = m;
    const int centreX = sideX + sw + columnGap;
    const int centreW = juce::jmax (60, w - m - centreX);

    // ---- sidebar: everything on one column, rescan pinned at
    //      the bottom ----
    int y = zoneY;

    titleLabel.setBounds (sideX, y, sw, 32);
    y += 38;

    for (size_t i = 0; i < consoleButtons.size(); ++i)
    {
        consoleButtons[i].setBounds (sideX, y, sw, 28);
        y += 28 + 5;
    }

    y += 2;
    hostedNameLabel.setBounds (sideX, y, sw, 24);
    y += 26;
    // v2.14.1: channel-format readout sits right under the console name.
    channelLabel.setBounds (sideX, y, sw, 14);
    y += 18;

    // ---- v2.26 (roadmap point 4): the presets section (a heading, the
    //      global preset list, then Save/Delete and Export/Import) ----
    presetLabel.setBounds (sideX, y, sw, 15);
    y += 17;

    presetCombo.setBounds (sideX, y, sw, 22);
    y += 26;

    savePresetButton.setBounds   (sideX,      y, 64, 22);
    deletePresetButton.setBounds (sideX + 68, y, 64, 22);
    y += 27;

    exportPresetButton.setBounds (sideX,      y, 64, 22);
    importPresetButton.setBounds (sideX + 68, y, 64, 22);
    y += 22 + 8;

    // ---- v2.12: response-speed row (Fast/Slow), one line like the tilt ----
    for (size_t j = 0; j < speedButtons.size(); ++j)
        speedButtons[j].setBounds (sideX + (int) j * 68, y, 64, 22);
    y += 22 + 8;

    tiltLabel.setBounds (sideX, y, sw, 15);
    y += 17;

    // v2.8: the three tilt buttons share one row (the sidebar gained the
    // scale/EQ/display controls and must stay compact).
    for (size_t j = 0; j < tiltButtons.size(); ++j)
        tiltButtons[j].setBounds (sideX + (int) j * 45, y, 41, 22);
    y += 22 + 8;

    // (v2.12: the SCALE dB/DIV and EQ dB/DIV rows are gone - those
    // scales are toggled by clicking the analyser's own scale labels.)

    // ---- v2.8: analyser display toggle ----
    analyserToggle.setBounds (sideX, y, sw, 22);
    y += 22 + 10;

    gainLabel.setBounds (sideX, y, sw, 15);
    y += 17;

    lufsInLabel.setBounds  (sideX, y, sw, 19);
    y += 19;
    lufsOutLabel.setBounds (sideX, y, sw, 19);
    y += 21;

    correctionLabel.setBounds (sideX, y, sw, 26);
    y += 30;

    // v2.17: the studio logo sits between the gain-staging section and
    // the host status area. The block height follows the image's aspect
    // ratio at the sidebar width, capped so the sidebar stays compact.
    // v2.17.1: the logo is half its v2.17 size in both directions (the
    // placement keeps the aspect ratio, so the width follows the height).
    {
        const auto logoImage = studioLogo.getImage();
        const int logoH = logoImage.isValid()
            ? juce::jlimit (20, 75, juce::roundToInt ((double) sw * logoImage.getHeight()
                                                      / logoImage.getWidth()) / 2)
            : 20;
        studioLogo.setBounds (sideX, y + 4, sw, logoH);

        // v2.20 (point 1): the help button rides the logo band, at its
        // right edge - vertically centred on the band, clear of the
        // centred logo image itself.
        helpButton.setBounds (sideX + sw - 24,
                              y + 4 + juce::jmax (0, (logoH - 22) / 2),
                              22, 22);

        y += logoH + 10;
    }

    statusLabel.setBounds (sideX, y, sw, juce::jmax (16, h - m - y - 32));

    // v2.6: Rescan is the only control pinned at the bottom (the bypass
    // button is gone).
    rescanButton.setBounds (sideX, h - m - 26, sw, 24);

    // ---- centre column: the hosted plugin fills the rest, under the
    //      full-width analyser strip ----
    hostedViewport.setBounds (centreX, zoneY, centreW,
                              juce::jmax (60, h - m - zoneY));

    layoutHostedHolder();
}

void TheChannelStripAudioProcessorEditor::layoutHostedHolder()
{
    const int vw = hostedViewport.getWidth();
    const int vh = hostedViewport.getHeight();

    const int ew = (embeddedHostedEditor != nullptr) ? embeddedHostedEditor->getWidth()  : 0;
    const int eh = (embeddedHostedEditor != nullptr) ? embeddedHostedEditor->getHeight() : 0;

    hostedHolder.setSize (juce::jmax (vw, ew), juce::jmax (vh, eh));

    if (embeddedHostedEditor != nullptr)
        embeddedHostedEditor->setTopLeftPosition ((hostedHolder.getWidth() - ew) / 2,
                                                  (hostedHolder.getHeight() - eh) / 2);
}

// v2.4: frame the hosted editor exactly. Called once, right after a new
// hosted editor is embedded (per console: their GUI sizes differ). The
// centre column then matches the plugin's width exactly and nothing
// scrolls. v2.8: without the analyser strip the window is one strip
// shorter, and the minimum height follows (900 / 720 with the nine
// console buttons of v2.16 plus the v2.17.1 studio logo).
void TheChannelStripAudioProcessorEditor::fitWindowToHostedEditor()
{
    if (embeddedHostedEditor == nullptr)
        return;

    const int ew = embeddedHostedEditor->getWidth();
    const int eh = embeddedHostedEditor->getHeight();

    if (ew <= 0 || eh <= 0)
        return;

    const bool showStrip = proc.getAnalyserVisible();
    const int stripH = showStrip ? analyserStripHeight + columnGap : 0;

    const int targetW = 2 * layoutMargin + sidebarWidth + columnGap + ew;
    const int targetH = 2 * layoutMargin + stripH + eh;

    const int newW = juce::jlimit (1024, 2560, targetW);
    // v2.8: dynamic minimum so the condensed sidebar always fits.
    // v2.10: +30 px for the A/B row. v2.12: the two scale rows moved
    // onto the analyser. v2.13: two more console buttons -> 770 / 520.
    // v2.16: two more console buttons (Lindell) -> 840 / 660.
    // v2.17: the studio logo block -> 960 / 780.
    // v2.17.1: logo halved -> 900 / 720.
    // v2.26: the presets section (combo + two button rows) -> 1000 / 820.
    const int newH = juce::jlimit (showStrip ? 1000 : 820, 1600, targetH);

    if (newW != getWidth() || newH != getHeight())
        setSize (newW, newH);
}

// v2.8: the Display toggle hides or shows the analyser strip. The window
// limits, the layout and (once a console is embedded) the window size all
// follow, so hiding the strip genuinely frees the vertical space instead
// of leaving a hole.
void TheChannelStripAudioProcessorEditor::applyAnalyserVisibility()
{
    const bool show = proc.getAnalyserVisible();

    spectrumPanel.setVisible (show);
    setResizeLimits (1024, show ? 1000 : 820, 2560, 1600); // v2.26: presets section
    resized();

    if (embeddedHostedEditor != nullptr)
        fitWindowToHostedEditor();
}

void TheChannelStripAudioProcessorEditor::timerCallback()
{
    // v1.0.8: while a background scan runs, the scan lock is held for the
    // whole scan; blocking the message thread on it froze the UI completely.
    // Skip the lock-dependent work until the scan finishes.
    if (proc.isScanning())
    {
        statusLabel.setText ("Scanning the plugin folders... (progress is printed in the Terminal)",
                             juce::dontSendNotification);
        return;
    }

    // v2.16.1: surface runtime latency changes (oversampling toggled
    // inside a hosted console) in the Terminal log - one line per change.
    const int latencyNow = proc.getForwardedLatency();
    if (latencyNow != lastLoggedLatency)
    {
        juce::Logger::writeToLog ("[TheChannelStrip] latency: now forwarding "
                                  + juce::String (latencyNow)
                                  + " samples to the host");
        lastLoggedLatency = latencyNow;
    }

    refreshConsoleButtons();
    tryEmbedHostedEditor();
    refreshLufs();

    // v2.14.2: live channel-format readout (see the ctor comment for what
    // each state and suffix tells us).
    channelLabel.setText (proc.getChannelReadout(), juce::dontSendNotification);

    // v2.3: the hosted plugin's name lives in the sidebar; the status
    // area only carries scan progress and error messages.
    const auto status = proc.getHostStatus();

    statusLabel.setText (status.isNotEmpty() ? status : juce::String(),
                         juce::dontSendNotification);

    // v2.24 (point 7): a warning or an error must not read like the
    // everyday scan chatter - the line is red for ERROR:, amber for
    // WARNING:, grey otherwise. The colour is cached so the label is
    // only touched when it actually changes.
    const juce::Colour statusColour = status.startsWith ("ERROR:")   ? juce::Colour (0xffe25b5b)
                                      : status.startsWith ("WARNING:") ? juce::Colour (0xffe2a44b)
                                                                       : juce::Colour (0xff8f9bb0);

    if (statusColour != lastStatusColour)
    {
        lastStatusColour = statusColour;
        statusLabel.setColour (juce::Label::textColourId, statusColour);
    }
}

void TheChannelStripAudioProcessorEditor::refreshConsoleButtons()
{
    const auto slots = proc.getConsoleSlots();
    juce::String activeName;

    for (size_t i = 0; i < slots.size(); ++i)
    {
        auto& btn = consoleButtons[i];
        const auto& slot = slots[i];

        // v2.24 (roadmap point 7): an unavailable console is NO LONGER a
        // disabled grey button. It stays clickable - clicking it makes
        // selectConsole post the exact situation to the status area - and
        // only the styling marks it: dimmed text, tooltip saying why.
        styleConsoleButton (btn);

        // any slot that cannot be hosted right now is dimmed (a missing
        // console, a blacklisted one, or a file that vanished since the
        // scan) - but every one of them stays clickable.
        if (! slot.found || slot.fileMissing)
        {
            btn.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff141a26));
            btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff141a26));
            btn.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xff5f6b82));
            btn.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff5f6b82));
        }

        juce::String tip;

        if (slot.blacklisted)
            tip = juce::String (slot.name)
                  + " - crashed the scanner and was blacklisted (click for details)";
        else if (slot.fileMissing)
            tip = juce::String (slot.name)
                  + " - file no longer exists (click for details)";
        else if (slot.found)
            tip = "Host " + slot.desc.name;
        else
            tip = juce::String (slot.name) + " - not found (install it or Rescan)";

        btn.setTooltip (tip);

        const bool active = slot.found
                                && proc.identFor (slot.desc) == proc.getCurrentPluginIdent();
        btn.setToggleState (active, juce::dontSendNotification);

        if (active)
            activeName = slot.desc.name;
    }

    // v2.3: the sidebar shows the hosted console's name under the buttons.
    hostedNameLabel.setText (activeName.isEmpty() ? juce::String ("--") : activeName,
                             juce::dontSendNotification);
}

void TheChannelStripAudioProcessorEditor::refreshLufs()
{
    const float inLufs  = proc.getLufsIn().getShortTermLufs();
    const float outLufs = proc.getLufsOut().getShortTermLufs();

    lufsInLabel.setText  ("IN "  + fmtLufs (inLufs),   juce::dontSendNotification);
    lufsOutLabel.setText ("OUT " + fmtLufs (outLufs),  juce::dontSendNotification);

    float delta = 0.0f;
    const bool valid = proc.getGainCorrectionDb (delta);

    if (valid)
    {
        // v2.1: smooth the correction with a ~1.5 s time constant (the timer
        // runs at 30 Hz) so the readout stays readable, and hold the last
        // smoothed value while the signal is below the meter floor.
        if (! correctionEverValid)
        {
            smoothedCorrectionDb = delta;
            correctionEverValid = true;
        }
        else
        {
            constexpr float timeConstant = 1.5f;                                // seconds
            constexpr float alpha = 1.0f / (1.0f + 30.0f * timeConstant);      // ~0.022
            smoothedCorrectionDb += alpha * (delta - smoothedCorrectionDb);
        }
    }

    // v2.13: the readout states the action with an explicit "Add"
    // prefix - "Add +0.7 dB" = the output is 0.7 dB too quiet (raise
    // it by 0.7 dB), "Add -0.4 dB" = lower it by 0.4 dB.
    // v2.13.1: "Add " goes through an explicit juce::String - a bare
    // string literal concatenated with another const char* (the
    // ternary) does not compile (same lesson as v2.0.0).
    correctionLabel.setText (correctionEverValid
                                 ? juce::String ("Add ") + (smoothedCorrectionDb >= 0.0f ? "+" : "")
                                       + juce::String (smoothedCorrectionDb, 1) + " dB"
                                 : "-- dB",
                             juce::dontSendNotification);
}

void TheChannelStripAudioProcessorEditor::tryEmbedHostedEditor()
{
    auto* hosted = proc.getHostedStrip().getActivePlugin();

    if (hosted == lastEmbeddedInstance)
        return;

    // the hosted instance changed: drop the old editor immediately
    embeddedHostedEditor.reset();
    lastEmbeddedInstance = hosted;

    if (hosted == nullptr)
    {
        layoutHostedHolder();
        return;
    }

    embeddedHostedEditor.reset (hosted->createEditorIfNeeded());

    if (embeddedHostedEditor != nullptr)
    {
        juce::Logger::writeToLog ("[TheChannelStrip] Embedded editor created for hosted plugin.");
        hostedHolder.addAndMakeVisible (*embeddedHostedEditor);
    }
    else
    {
        // v1.0.6 fix: this used to fail silently; now the Terminal shows why
        // the plugin seems to "do nothing" when it actually loaded fine.
        juce::Logger::writeToLog ("[TheChannelStrip] Hosted plugin returned no editor "
                                  "(createEditor() failed) - the plugin IS loaded and processing.");
    }

    layoutHostedHolder();
    fitWindowToHostedEditor();
}

// v2.20 (point 1): the "?" button opens every tool tip in one window.
// The analyser deliberately carries NO tooltip any more (it was hiding
// the hover readout), so its help text is written out here; every other
// section is pulled live from the control it describes, so the window
// always shows exactly what the small tooltips show.
void TheChannelStripAudioProcessorEditor::showHelpPopup()
{
    juce::String info;

    auto addSection = [&info] (const juce::String& title, const juce::String& text)
    {
        if (text.trim().isNotEmpty())
            info << title << "\n" << text << "\n\n";
    };

    addSection ("ANALYSER",
                "Hover anywhere to read the frequency under the cursor and the "
                "IN/OUT/EQ levels there. Click the LEFT dB scale to toggle 10/6 dB "
                "per division; click the RIGHT green EQ scale to toggle 4/2 dB per "
                "division; click the loudness reference label (e.g. \"Mix (-23 LUFS)\") "
                "to cycle the Record/Mix/Master reference (-37/-23/-14 LUFS).");
    addSection ("TILT",               tiltButtons[0].getTooltip());
    addSection ("RESPONSE SPEED",      speedButtons[0].getTooltip());
    addSection ("ANALYSER DISPLAY",   analyserToggle.getTooltip());
    addSection ("GAIN STAGING",       correctionLabel.getTooltip());
    addSection ("CHANNEL FORMAT",     channelLabel.getTooltip());
    addSection ("PRESETS",            presetCombo.getTooltip());
    addSection ("HOSTED CONSOLE",     hostedNameLabel.getTooltip());
    addSection ("PLUGIN DATABASE",    rescanButton.getTooltip());

    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                            "TheChannelStrip - help",
                                            info);
}

// ---------------------------------------------------------------------------
// v2.26 (roadmap point 4): named presets - the sidebar section
// ---------------------------------------------------------------------------
void TheChannelStripAudioProcessorEditor::refreshPresetList()
{
    // keep the current selection across the rebuild when possible
    juce::String selectedName;

    {
        const int id = presetCombo.getSelectedId();

        if (id > 0 && id - 1 < presetList.size())
            selectedName = presetList[id - 1].name;
    }

    presetList = proc.getAvailablePresets();
    presetCombo.clear (juce::dontSendNotification);

    for (int i = 0; i < presetList.size(); ++i)
        presetCombo.addItem (shortConsoleNameFor (presetList[i].consoleName)
                             + " - " + presetList[i].name,
                             i + 1);

    if (selectedName.isNotEmpty())
        for (int i = 0; i < presetList.size(); ++i)
            if (presetList[i].name == selectedName)
            {
                presetCombo.setSelectedId (i + 1, juce::dontSendNotification);
                break;
            }
}

void TheChannelStripAudioProcessorEditor::onPresetSelected()
{
    const int id = presetCombo.getSelectedId();

    if (id < 1 || id - 1 >= presetList.size())
        return;

    // applyPreset posts its own success/failure to the status area
    proc.applyPreset (presetList[id - 1].name);
}

void TheChannelStripAudioProcessorEditor::showSavePresetDialog()
{
    // a small modal asking for the preset name. Async (a plugin must
    // never block the message thread); the window deletes itself after
    // the callback has run.
    auto* aw = new juce::AlertWindow ("Save preset",
                                      "Name for the hosted console's current settings:",
                                      juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", {}, {});
    aw->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true,
                         juce::ModalCallbackFunction::create ([this, aw] (int result)
                         {
                             if (result == 1)
                             {
                                 const auto name = aw->getTextEditorContents ("name").trim();

                                 if (name.isNotEmpty() && proc.savePreset (name))
                                     refreshPresetList();
                             }
                         }),
                         true);
}

void TheChannelStripAudioProcessorEditor::confirmDeletePreset()
{
    const int id = presetCombo.getSelectedId();

    if (id < 1 || id - 1 >= presetList.size())
        return; // nothing selected: harmless no-op

    const auto name = presetList[id - 1].name;

    // async OK/Cancel (JUCE_MODAL_LOOPS_PERMITTED is 0 in a plugin, so
    // the callback overload is the right one); the callback receives 1
    // for OK, 0 for Cancel
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon,
                                        "Delete preset",
                                        "Delete the preset '" + name + "'?",
                                        "Delete",
                                        "Cancel",
                                        this,
                                        juce::ModalCallbackFunction::create (
                                            [this, name] (int result)
                                            {
                                                if (result == 1 && proc.deletePreset (name))
                                                    refreshPresetList();
                                            }));
}

void TheChannelStripAudioProcessorEditor::exportSelectedPreset()
{
    const int id = presetCombo.getSelectedId();

    if (id < 1 || id - 1 >= presetList.size())
        return;

    const auto name = presetList[id - 1].name;

    // the FileChooser must outlive launchAsync, hence the member
    presetFileChooser = std::make_unique<juce::FileChooser> (
        "Export preset '" + name + "'",
        proc.getPresetsDir(),
        "*.tcsPreset");

    presetFileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                        | juce::FileBrowserComponent::canSelectFiles,
                                    [this, name] (const juce::FileChooser& fc)
                                    {
                                        const auto dest = fc.getResult();

                                        if (dest == juce::File())
                                            return; // cancelled

                                        proc.exportPreset (name,
                                                           dest.withFileExtension ("tcsPreset"));
                                    });
}

void TheChannelStripAudioProcessorEditor::importPresetFile()
{
    presetFileChooser = std::make_unique<juce::FileChooser> (
        "Import a preset",
        juce::File(),
        "*.tcsPreset");

    presetFileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                        | juce::FileBrowserComponent::canSelectFiles,
                                    [this] (const juce::FileChooser& fc)
                                    {
                                        const auto source = fc.getResult();

                                        if (source == juce::File())
                                            return; // cancelled

                                        if (proc.importPreset (source))
                                            refreshPresetList();
                                    });
}
