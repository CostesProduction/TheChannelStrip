#include "PluginEditor.h"
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

void SpectrumPanel::updateBallistics()
{
    float in[SpectrumAnalyzer::numScopeBins];
    float out[SpectrumAnalyzer::numScopeBins];

    processor.getAnalyzerIn().copyScopeData  (in, SpectrumAnalyzer::numScopeBins);
    processor.getAnalyzerOut().copyScopeData (out, SpectrumAnalyzer::numScopeBins);

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

    // v2.6: measured transfer function, OUT minus IN per bin. The tilt
    // cancels in the difference, so this curve is tilt-independent.
    // v2.7 stability (UI thread only, zero added audio cost): a 5-bin
    // pre-smoothing plus a slow, input-energy-weighted EMA - strong
    // program material updates the curve confidently, weak material
    // barely moves it.
    // v2.10: the settling time is FREQUENCY-DEPENDENT (long in the
    // bass, short in the treble). Even with the finer 8192-point FFT
    // the low end is intrinsically noisier (few periods per window,
    // and the console's compressor genuinely varies there with the
    // program), so the bass gets the long average while the treble
    // stays quicker.
    // v2.12: PERSISTENT CURVE. Bins whose input drops below the gate
    // now simply HOLD their last measured value (previously the path
    // broke at draw time and the curve erased itself piecewise as the
    // program's spectrum changed - Clement's complaint); a bin that has
    // been measured at least once is drawn forever after (until the
    // panel is recreated).
    // v2.13: the settling times came back down - the v2.12 doubling
    // (~4 s bass / ~0.6 s treble) felt too sluggish to Clement. With
    // the persistence fix in place, the curve can afford to be snappy
    // again without flickering: ~1 s in the bass, ~0.25 s in the
    // treble (between the v2.11 and v2.12 values).
    for (int b = 0; b < SpectrumAnalyzer::numScopeBins; ++b)
    {
        const int b0 = juce::jmax (0, b - 2);
        const int b1 = juce::jmax (0, b - 1);
        const int b3 = juce::jmin (SpectrumAnalyzer::numScopeBins - 1, b + 1);
        const int b4 = juce::jmin (SpectrumAnalyzer::numScopeBins - 1, b + 2);

        const float o = out[b0] + 4.0f * out[b1] + 6.0f * out[b]
                      + 4.0f * out[b3] + out[b4];
        const float i = in[b0]  + 4.0f * in[b1]  + 6.0f * in[b]
                      + 4.0f * in[b3]  + in[b4];

        if (in[b] > -80.0f)
        {
            // v2.12: this bin carries (or has carried) input energy -
            // from now on it stays part of the drawn curve, even when
            // the program later moves away from it
            eqMeasured[(size_t) b] = true;

            // the more energy the input has in this bin, the more the
            // measurement can be trusted and the faster it may settle
            const float w = juce::jlimit (0.0f, 1.0f, (in[b] + 80.0f) / 24.0f);

            // 20 Hz -> tau ~1 s, 20 kHz -> tau ~0.25 s (log-linear)
            const double freq = processor.getAnalyzerIn().getBinFrequency (b);
            const float tau = (float) (0.25 + 0.75 * (std::log10 (20000.0 / freq) / 3.0));

            const float alpha = (1.0f / (1.0f + 30.0f * tau)) * (0.25f + 0.75f * w);

            smoothDiff[(size_t) b] += alpha * ((o - i) / 16.0f
                                              - smoothDiff[(size_t) b]);
        }
        // below the gate: hold the last measured value (no update)
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

void SpectrumPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0e1420));

    drawGrid (g);
    drawCurve (g, smoothIn,  juce::Colour (0xffe2a14f));
    drawCurve (g, smoothOut, juce::Colour (0xff54c8e8));

    // v2.10: 10-second sliding maximum of the raw output - a thin trace
    // (no fill) on top of OUT, showing where the output peaked over the
    // last 10 s. It resets when the editor is reopened.
    drawCurve (g, maxOut, juce::Colour (0xff9adcf0).withAlpha (0.70f), 1.2f, false);

    // v2.6: the measured transfer function, drawn on top without fill.
    drawTransferCurve (g);

    // legend (v2.8: centred at the top; the left edge now carries the
    // scale labels and the right edge the EQ range labels)
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    const int legendX = getWidth() / 2 - 130;

    g.setColour (juce::Colour (0xffe2a14f));
    g.drawText ("IN", legendX, 4, 34, 16, juce::Justification::left);

    g.setColour (juce::Colour (0xff54c8e8));
    g.drawText ("OUT", legendX + 42, 4, 40, 16, juce::Justification::left);

    // v2.6: measured OUT-IN response of the hosted plugin
    g.setColour (juce::Colour (0xff7ee08a));
    g.drawText ("OUT-IN", legendX + 90, 4, 70, 16, juce::Justification::left);

    // v2.10: the 10-second max hold trace
    g.setColour (juce::Colour (0xff9adcf0));
    g.drawText ("MAX", legendX + 160, 4, 40, 16, juce::Justification::left);
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
    g.setColour (juce::Colour (0xff222b3f));

    for (double decade : { 10.0, 100.0, 1000.0, 10000.0 })
    {
        for (int m = 1; m <= 9; ++m)
        {
            const double f = decade * (double) m;

            // outside the 16 Hz - 24 kHz axis (10 Hz only)
            if (f < 16.0 || f > 24000.0)
                continue;

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
                                          : juce::Colour (0xff222b3f));
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

    // ---- X labels: 20 / 100 / 200 / 1k / 2k / 10k / 20k ----
    // v2.9: the axis runs from 16 Hz to 24 kHz; both 20 Hz and 20 kHz
    // are inside the panel now, so every label is a plain centred label
    // (the 16 Hz left edge carries no value, like the 24 kHz right
    // edge). The 20 kHz label is back (v2.8.2 had removed it).
    g.setFont (juce::Font (juce::FontOptions (11.0f)));

    const std::pair<double, const char*> xLabels[] =
    {
        { 20.0,     "20" },
        { 100.0,    "100" },
        { 200.0,    "200" },
        { 1000.0,   "1k" },
        { 2000.0,   "2k" },
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

        g.setColour (colour.withAlpha (0.20f));
        g.fillPath (fill);
    }

    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (lineThickness));
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

    // v2.7: a faint anchor line at the EQ 0 dB, so the curve reads against
    // it like a classic EQ display (the spectrum grid's 0 dB is elsewhere).
    g.setColour (juce::Colour (0xff7ee08a).withAlpha (0.22f));
    g.drawHorizontalLine ((int) yForEqDb (0.0f), 0.0f, (float) getWidth());

    g.setColour (juce::Colour (0xff7ee08a));
    g.strokePath (path, juce::PathStrokeType (2.4f));

    // the EQ scale labels, on the right edge, match the curve's range
    // (v2.8: dynamic, "+18"/"0"/"-18" at 4 dB/div, "+9"/"0"/"-9" at 2).
    // v2.9: the 20 kHz frequency label is back in the bottom-right
    // corner, so the column right-aligns just left of its label box.
    g.setFont (juce::Font (juce::FontOptions (10.0f)));

    const int eqLabelRight = (int) xForFrequency (20000.0) - 22;

    for (float db : { range, 0.0f, -range })
    {
        const juce::String label = (db > 0.0f) ? "+" + juce::String (db, 0)
                                   : ((db < 0.0f) ? "-" + juce::String (-db, 0)
                                                  : juce::String ("0"));
        const int y = juce::jlimit (2, getHeight() - 14, (int) yForEqDb (db) - 6);
        g.drawText (label, eqLabelRight - 28, y, 28, 12,
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
    setSize (1180, 900);
    setResizable (true, false);
    setResizeLimits (1024, proc.getAnalyserVisible() ? 840 : 660, 2560, 1600);

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

    spectrumPanel.setTooltip ("Click the LEFT dB scale to toggle 10/6 dB per division; "
                              "click the RIGHT green EQ scale to toggle 4/2 dB per division; "
                              "click the loudness reference label (e.g. \"Mix (-23 LUFS)\") "
                              "to cycle the Record/Mix/Master reference (-37/-23/-14 LUFS).");

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
// shorter, and the minimum height follows (840 / 660 with the nine
// console buttons of v2.16).
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
    const int newH = juce::jlimit (showStrip ? 840 : 660, 1600, targetH);

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
    setResizeLimits (1024, show ? 840 : 660, 2560, 1600); // v2.16: 9 console buttons
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
}

void TheChannelStripAudioProcessorEditor::refreshConsoleButtons()
{
    const auto slots = proc.getConsoleSlots();
    juce::String activeName;

    for (size_t i = 0; i < slots.size(); ++i)
    {
        auto& btn = consoleButtons[i];
        const auto& slot = slots[i];

        btn.setEnabled (slot.found);

        btn.setTooltip (slot.found
                            ? "Host " + slot.desc.name
                            : juce::String (slot.name) + " - not found (install it or Rescan)");

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
