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

    // v2.26.1: replaces the preset buttons' text with small vector
    // icons (floppy = save, bin = delete, tray with an incoming arrow =
    // load - thick minimal
    // strokes, in Clément's reference style). One instance per button,
    // each drawing its own icon; the buttons keep the styleConsoleButton
    // colours, so the icon follows the same text colour as before.
    class PresetButtonLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        enum class Icon { floppy, trash, importIn, pulse, spectrum,
                           spectrumLabelled };

        explicit PresetButtonLookAndFeel (Icon iconToDraw) : icon (iconToDraw) {}

        void drawButtonText (juce::Graphics& g, juce::TextButton& b,
                            bool /*shouldDrawButtonAsHighlighted*/,
                            bool shouldDrawButtonAsDown) override
        {
            g.setColour (b.findColour (shouldDrawButtonAsDown
                            ? juce::TextButton::textColourOnId
                            : juce::TextButton::textColourOffId));

            // Each icon is authored in a 16 x 13 box, then centred and
            // scaled to fit the button.
            const auto area = b.getLocalBounds().toFloat().reduced (4.0f);
            const float scale = juce::jmin (area.getWidth()  / 16.0f,
                                           area.getHeight() / 13.0f);
            const float w  = 16.0f * scale;
            const float h  = 13.0f * scale;
            const float x0 = area.getX() + (area.getWidth()  - w) * 0.5f;
            const float y0 = area.getY() + (area.getHeight() - h) * 0.5f;
            auto p = [x0, y0, scale] (float x, float y)
            { return juce::Point<float> (x0 + x * scale, y0 + y * scale); };

            juce::Path path;
            switch (icon)
            {
                case Icon::floppy:
                    // body with a cut top-right corner, the metal
                    // shutter, and the label slot at the bottom
                    path.startNewSubPath (p (2, 1));
                    path.lineTo (p (10, 1));
                    path.lineTo (p (14, 5));
                    path.lineTo (p (14, 12));
                    path.lineTo (p (2, 12));
                    path.closeSubPath();
                    path.startNewSubPath (p (10, 2));
                    path.lineTo (p (10, 5));
                    path.lineTo (p (13, 5));
                    path.startNewSubPath (p (5, 12));
                    path.lineTo (p (5, 8.5f));
                    path.lineTo (p (11, 8.5f));
                    path.lineTo (p (11, 12));
                    break;

                case Icon::trash:
                    // handle, lid, tapered body, two inner ribs
                    path.startNewSubPath (p (6, 1));
                    path.lineTo (p (6, 3));
                    path.lineTo (p (10, 3));
                    path.lineTo (p (10, 1));
                    path.closeSubPath();
                    path.startNewSubPath (p (3, 3.5f));
                    path.lineTo (p (13, 3.5f));
                    path.startNewSubPath (p (4.5f, 3.5f));
                    path.lineTo (p (5.3f, 12));
                    path.lineTo (p (10.7f, 12));
                    path.lineTo (p (11.5f, 3.5f));
                    path.startNewSubPath (p (6.3f, 5.8f));
                    path.lineTo (p (6.3f, 9.8f));
                    path.startNewSubPath (p (9.7f, 5.8f));
                    path.lineTo (p (9.7f, 9.8f));
                    break;

                case Icon::importIn:
                    // same tray, arrow descending into it
                    path.startNewSubPath (p (3, 4));
                    path.lineTo (p (3, 12));
                    path.lineTo (p (13, 12));
                    path.lineTo (p (13, 4));
                    path.startNewSubPath (p (8, 1));
                    path.lineTo (p (8, 10));
                    path.startNewSubPath (p (5.3f, 7.3f));
                    path.lineTo (p (8, 10));
                    path.lineTo (p (10.7f, 7.3f));
                    break;

                case Icon::pulse:
                    // v2.34: the Doctor's heartbeat trace - a flat ECG
                    // line with one sharp beat
                    path.startNewSubPath (p (2, 6.5f));
                    path.lineTo (p (5.5f, 6.5f));
                    path.lineTo (p (7, 2));
                    path.lineTo (p (9, 11));
                    path.lineTo (p (10.5f, 6.5f));
                    path.lineTo (p (14, 6.5f));
                    break;

                case Icon::spectrum:
                {
                    // v2.34.1 (Clement's suggestion): the analyser display
                    // toggle - four filled bars reading as a spectrum
                    // analyser, no text needed
                    const float barX[4]   = { 1.5f, 4.7f, 7.9f, 11.1f };
                    const float barTop[4] = { 9.0f, 5.0f, 2.0f, 6.0f };
                    for (int i = 0; i < 4; ++i)
                    {
                        const auto topLeft = p (barX[i], barTop[i]);
                        path.addRectangle (topLeft.x, topLeft.y,
                                           2.0f * scale,
                                           (13.0f - barTop[i]) * scale);
                    }
                    break;
                }

                case Icon::spectrumLabelled:
                {
                    // v2.37.0 (Clement's design): the display toggle
                    // carries the ANALYSER heading itself - the label and
                    // the four spectrum bars share the button, and the
                    // heading row the v2.36 layout needed is saved.
                    const juce::Font labelFont (juce::FontOptions (11.0f,
                                                                   juce::Font::bold));
                    const float textWidth
                        = juce::GlyphArrangement::getStringWidth (labelFont,
                                                                  "ANALYSER");
                    const float glyphScale = juce::jlimit (0.75f, 1.0f,
                                area.getHeight() / 13.0f);
                    const float glyphWidth  = 16.0f * glyphScale;
                    const float totalWidth  = textWidth + 6.0f + glyphWidth;
                    const float xStart = area.getX()
                                       + (area.getWidth() - totalWidth) * 0.5f;
                    g.setFont (labelFont);
                    g.drawText ("ANALYSER",
                                juce::Rectangle<float> (xStart, area.getY(),
                                                        textWidth,
                                                        area.getHeight()),
                                juce::Justification::centred);
                    const float barX[4]   = { 1.5f, 4.7f, 7.9f, 11.1f };
                    const float barTop[4] = { 9.0f, 5.0f, 2.0f, 6.0f };
                    const float bx = xStart + textWidth + 6.0f;
                    const float by = area.getY()
                                   + (area.getHeight()
                                      - 13.0f * glyphScale) * 0.5f;
                    for (int i = 0; i < 4; ++i)
                        path.addRectangle (bx + barX[i] * glyphScale,
                                           by + barTop[i] * glyphScale,
                                           2.0f * glyphScale,
                                           (13.0f - barTop[i]) * glyphScale);
                    break;
                }
            }

            if (icon == Icon::spectrum || icon == Icon::spectrumLabelled)
                g.fillPath (path);   // bars, not strokes
            else
                g.strokePath (path, juce::PathStrokeType (1.6f,
                                                          juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
        }

    private:
        Icon icon;
    };

    // v2.26.1: the short console label for a preset entry ("SSL E - My
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

    // v2.27: one combo entry's text ("Console - Name"), with the "* "
    // prefix when the entry is the hosted console's current preset and
    // its knobs have moved since it was loaded or saved.
    // v2.30: an album preset (a sub-folder) carries its album after
    // the name, so two same-named presets in different places still
    // read apart.
    juce::String presetItemText (const TheChannelStripAudioProcessor::PresetInfo& info,
                                 bool starred)
    {
        auto text = (starred ? "* " : "")
                  + shortConsoleNameFor (info.consoleName) + " - " + info.name;

        if (info.album.isNotEmpty())
            text << "  [" << info.album << "]";

        return text;
    }

    // v2.4: sidebar + centre column geometry, shared by paint() and
    // resized(). Everything lives in the condensed left sidebar.
    constexpr int sidebarWidth        = 132; // left: all wrapper controls
    // v2.38.0 (Clement's remark): the general frame around everything
    // is as small as it gets - half the previous margin, and the same
    // tight gutter under the analyser strip / beside the sidebar.
    constexpr int layoutMargin        = 5;
    constexpr int columnGap           = 5;
    constexpr int analyserStripHeight = 180; // full-width strip above the plugin

    // v2.35.0: the merged status zone under the console name shows the
    // two-line integrity readout normally; an actionable alert (a missing
    // or blacklisted console after a click, an ERROR:, a WARNING:) takes
    // the zone over, coloured, until it is resolved. Everything else -
    // hosting chatter, preset confirmations, doctor paths - stays in the
    // Terminal and the Doctor report only: zero redundancy on the sidebar.
    bool isSidebarAlert (const juce::String& s)
    {
        return s.startsWith ("ERROR:")
            || s.startsWith ("WARNING:")
            || s.contains ("Press Rescan");
    }
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
    // v2.46: the EQ @ max trace starts with nothing measured (silent).
    maxEqIn.fill (-100.0f);
    maxEqOut.fill (-100.0f);
    maxEqDiff.fill (0.0f);
    maxEqInRing.assign ((size_t) maxEqFrames * SpectrumAnalyzer::numScopeBins, -100.0f);
    maxEqOutRing.assign ((size_t) maxEqFrames * SpectrumAnalyzer::numScopeBins, -100.0f);
    eqMaxMeasured.fill (false);
    // v2.48.0: the short 2 s max window starts empty like the 5 s one.
    maxEqIn2.fill (-100.0f);
    maxEqOut2.fill (-100.0f);
    maxEqDiff2.fill (0.0f);
    maxEqIn2Ring.assign ((size_t) maxEqFrames2 * SpectrumAnalyzer::numScopeBins, -100.0f);
    maxEqOut2Ring.assign ((size_t) maxEqFrames2 * SpectrumAnalyzer::numScopeBins, -100.0f);
    eqMax2Measured.fill (false);
    // v2.47.0: the single EQ-Verite curve starts empty like its members.
    smoothEqVerite.fill (0.0f);
    eqVeriteMeasured.fill (false);

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

// v2.31 (Clement's remark 2): the interactive help mode asks which
// analyser zone a point falls in, so its bubble can document the dB
// scales instead of the panel's generic text alone. The zones and
// constants are the SAME as mouseDown above - the bubble always
// says exactly what a click in that zone would do.
SpectrumPanel::HelpZone SpectrumPanel::helpZoneFor (juce::Point<int> p) const
{
    const float zeroY = yForDb (0.0f);

    if (p.x >= 36 && p.x <= 190 && std::abs ((float) p.y - zeroY) <= 12.0f)
        return HelpZone::lufsReference;

    if (p.x <= 44)
        return HelpZone::analyzerScale;

    if (p.x >= getWidth() - 56)
        return HelpZone::eqScale;

    return HelpZone::graph;
}

void SpectrumPanel::timerCallback()
{
    // v2.41: a hidden strip has nothing to display - skip the tick
    // entirely (the analysers are gated off in processBlock, and the
    // panel keeps the clean state resetDisplay() left it for the next
    // show; no 30 Hz of EMA work for an invisible component).
    if (! processor.getAnalyserVisible())
        return;

    updateBallistics();
    repaint();
}

// v2.41: the display-side reset that pairs with the processor-side
// analyzer resets (see setAnalyserVisible / setEditorOpen): every
// smoothed trace goes back to its constructor state, so a re-shown
// strip starts from silence instead of ghosting the pre-hide curves.
void SpectrumPanel::resetDisplay()
{
    smoothIn.fill  (-100.0f);
    smoothOut.fill (-100.0f);
    smoothDiff.fill (0.0f);
    eqMeasured.fill (false);
    maxOut.fill (-100.0f);
    maxRing.assign ((size_t) maxHoldFrames * SpectrumAnalyzer::numScopeBins, -100.0f);
    maxEqIn.fill (-100.0f);
    maxEqOut.fill (-100.0f);
    maxEqDiff.fill (0.0f);
    maxEqInRing.assign ((size_t) maxEqFrames * SpectrumAnalyzer::numScopeBins, -100.0f);
    maxEqOutRing.assign ((size_t) maxEqFrames * SpectrumAnalyzer::numScopeBins, -100.0f);
    eqMaxMeasured.fill (false);
    maxEqIn2.fill (-100.0f);
    maxEqOut2.fill (-100.0f);
    maxEqDiff2.fill (0.0f);
    maxEqIn2Ring.assign ((size_t) maxEqFrames2 * SpectrumAnalyzer::numScopeBins, -100.0f);
    maxEqOut2Ring.assign ((size_t) maxEqFrames2 * SpectrumAnalyzer::numScopeBins, -100.0f);
    eqMax2Measured.fill (false);
    smoothEqVerite.fill (0.0f);
    eqVeriteMeasured.fill (false);
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

    // v2.47.0: the readout's EQ value is the SAME single curve the
    // panel draws (the EQ-Verite blend), not the bare H1 member.
    if (eqVeriteMeasured[(size_t) bin])
        segments.add (Segment { "EQ " + formatReadoutDb (smoothEqVerite[(size_t) bin]),
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

    // v2.41 (the "bonus joli"): ASYMMETRIC ballistics - the rise is
    // 4x faster than the fall, so a transient shows immediately while
    // the curve keeps decaying at the calm Fast/Slow pace. Classic
    // analyser feel (fast attack, slow release), and it is NOT the
    // v2.10 mistake: that old mode was INSTANT rise + a fixed ~36 dB/s
    // fall, which read as nervous. Here both sides derive from the
    // user tau (alpha = 1 / (1 + 30 * tau), the same one-pole as
    // since v2.10) - only the rise gets a x4 multiplier, capped at 1.
    const float specAlpha  = 1.0f / (1.0f + 30.0f * specTau);
    const float riseAlpha  = juce::jmin (1.0f, 4.0f * specAlpha);

    for (int b = 0; b < SpectrumAnalyzer::numScopeBins; ++b)
    {
        // v2.11/v2.12: tau is selectable in the sidebar (Fast/Slow).
        // Fast: rise 0.72 / fall 0.18 per tick; Slow: rise 0.13 /
        // fall 0.032 - transients pop, the body of the curve stays calm.
        smoothIn  [(size_t) b] += (in[b]  > smoothIn  [(size_t) b] ? riseAlpha : specAlpha)
                               * (in[b]  - smoothIn  [(size_t) b]);
        smoothOut [(size_t) b] += (out[b] > smoothOut [(size_t) b] ? riseAlpha : specAlpha)
                               * (out[b] - smoothOut [(size_t) b]);
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

    // v2.46.0 (Clement's idea): the "EQ @ max" trace - dB MaxOut minus
    // dB MaxIn over 5-second sliding maxima of the SMOOTHED curves
    // (what the eye sees, exactly like the v2.10 max hold above). The
    // max is homogeneous and the IN/OUT one-poles are identical, so
    // for a linear console the difference is the EXACT transfer at
    // the programme's peak drive - the fixed operating point that
    // keeps the trace stable on a saturating console. See the member
    // comment in the header for the full story and the honest costs.
    {
        std::copy (smoothIn.begin(), smoothIn.end(),
                   maxEqInRing.begin() + maxEqRingIndex * SpectrumAnalyzer::numScopeBins);
        std::copy (smoothOut.begin(), smoothOut.end(),
                   maxEqOutRing.begin() + maxEqRingIndex * SpectrumAnalyzer::numScopeBins);
        maxEqRingIndex = (maxEqRingIndex + 1) % maxEqFrames;

        for (int b = 0; b < SpectrumAnalyzer::numScopeBins; ++b)
        {
            float mi = -100.0f;
            float mo = -100.0f;

            for (int f = 0; f < maxEqFrames; ++f)
            {
                mi = juce::jmax (mi, maxEqInRing[(size_t) f * SpectrumAnalyzer::numScopeBins
                                                + (size_t) b]);
                mo = juce::jmax (mo, maxEqOutRing[(size_t) f * SpectrumAnalyzer::numScopeBins
                                                 + (size_t) b]);
            }

            maxEqIn [(size_t) b] = mi;
            maxEqOut[(size_t) b] = mo;
        }

        // the same relative input gate as the transfer curve, applied
        // to the maxima: a bin is drawn once its 5 s max input came
        // within 45 dB of the programme's strongest max band (floor
        // -75 dB) - only bands that really carried energy vote.
        float peakMaxIn = -100.0f;

        for (int b = 0; b < SpectrumAnalyzer::numScopeBins; ++b)
            peakMaxIn = juce::jmax (peakMaxIn, maxEqIn[(size_t) b]);

        const float maxGateDb = juce::jmax (-75.0f, peakMaxIn - 45.0f);

        for (int b = 0; b < SpectrumAnalyzer::numScopeBins; ++b)
            if (maxEqIn[(size_t) b] > maxGateDb)
                eqMaxMeasured[(size_t) b] = true;

        // the raw dB difference, then the same Savitzky-Golay pass as
        // the transfer curve for visual consistency (clamped edges,
        // exactly like the analyzer's own smoothing). Empty bins read
        // 0 dB (both maxima silent) and never pass the gate, so they
        // do not contaminate the drawn region.
        for (int b = 0; b < SpectrumAnalyzer::numScopeBins; ++b)
        {
            double acc = 0.0;

            for (int k = -TransferAnalyzer::smoothHalfWidth;
                 k <= TransferAnalyzer::smoothHalfWidth; ++k)
            {
                const int bb = juce::jlimit (0, SpectrumAnalyzer::numScopeBins - 1, b + k);
                acc += TransferAnalyzer::smoothCoeffs[k + TransferAnalyzer::smoothHalfWidth]
                     * (double) (maxEqOut[(size_t) bb] - maxEqIn[(size_t) bb]);
            }

            maxEqDiff[(size_t) b] = (float) acc;
        }
    }

    // v2.48.0 (Clement's design call): the SHORT max window - the same
    // machinery as the 5 s block above over a 2-second ring (60 frames
    // at 30 Hz). The two windows are the two members of the single EQ
    // curve below: the long one brings the stability, the short one the
    // reactivity. The v2.48 reference simulation (the sim_eq_k
    // scenario: soft-saturation mock, bursty programme, ground truth
    // at two drive levels) raced Clement's three candidate pairs and
    // the (5 s; 2 s) blend won: median wander 2.57 dB p-p (vs 2.54 for
    // the 5 s window alone - the pair inherits its stability), settle
    // ~1.3 s (no flush needed: the max only ever moves up), and the
    // best all-band fidelity of the three blends (0.66 dB). The 1 s
    // pair was eliminated - its ring catches isolated burst spikes in
    // weak bands (14.2 dB p-p excursions once blended) - and the
    // 300 ms pair trades fidelity (0.89 dB) for speed the 2 s pair
    // already delivers.
    {
        std::copy (smoothIn.begin(), smoothIn.end(),
                   maxEqIn2Ring.begin() + maxEqRingIndex2 * SpectrumAnalyzer::numScopeBins);
        std::copy (smoothOut.begin(), smoothOut.end(),
                   maxEqOut2Ring.begin() + maxEqRingIndex2 * SpectrumAnalyzer::numScopeBins);
        maxEqRingIndex2 = (maxEqRingIndex2 + 1) % maxEqFrames2;

        for (int b = 0; b < SpectrumAnalyzer::numScopeBins; ++b)
        {
            float mi = -100.0f;
            float mo = -100.0f;

            for (int f = 0; f < maxEqFrames2; ++f)
            {
                mi = juce::jmax (mi, maxEqIn2Ring[(size_t) f * SpectrumAnalyzer::numScopeBins
                                                  + (size_t) b]);
                mo = juce::jmax (mo, maxEqOut2Ring[(size_t) f * SpectrumAnalyzer::numScopeBins
                                                   + (size_t) b]);
            }

            maxEqIn2 [(size_t) b] = mi;
            maxEqOut2[(size_t) b] = mo;
        }

        // the same relative input gate as the 5 s maxima
        float peakMaxIn = -100.0f;

        for (int b = 0; b < SpectrumAnalyzer::numScopeBins; ++b)
            peakMaxIn = juce::jmax (peakMaxIn, maxEqIn2[(size_t) b]);

        const float maxGateDb = juce::jmax (-75.0f, peakMaxIn - 45.0f);

        for (int b = 0; b < SpectrumAnalyzer::numScopeBins; ++b)
            if (maxEqIn2[(size_t) b] > maxGateDb)
                eqMax2Measured[(size_t) b] = true;

        // raw dB difference, then the same Savitzky-Golay pass
        for (int b = 0; b < SpectrumAnalyzer::numScopeBins; ++b)
        {
            double acc = 0.0;

            for (int k = -TransferAnalyzer::smoothHalfWidth;
                 k <= TransferAnalyzer::smoothHalfWidth; ++k)
            {
                const int bb = juce::jlimit (0, SpectrumAnalyzer::numScopeBins - 1, b + k);
                acc += TransferAnalyzer::smoothCoeffs[k + TransferAnalyzer::smoothHalfWidth]
                     * (double) (maxEqOut2[(size_t) bb] - maxEqIn2[(size_t) bb]);
            }

            maxEqDiff2[(size_t) b] = (float) acc;
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
    // (tau ~5 s since v2.47 - the long energy-weighted window; the
    // knob reactivity is restored by the analyzer's deterministic
    // flush, not by a short window); the frequency-dependent settling and
    // confidence weights of v2.13/v2.21 tuned per-frame dB ratios that
    // no longer exist, so the display EMA here is a single light
    // one-pole for continuity only.
    // v2.43.1 (Clement's "vaguelettes" remark): tau doubled to 0.5 s.
    // The v2.43.0 frequency smoothing killed the band-scale jitter,
    // but on real material slower wave-shaped residuals remain - the
    // analyzer's per-frame estimates wobble a little from frame to
    // frame with the program, and the eye tracks that as small waves.
    // A calmer display pole (still only +0.34 s on a knob turn's
    // settle, measured in the v2.43.1 reference simulation) lets the
    // residual glide instead of shimmer.
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

        const float eqAlpha = 1.0f / (1.0f + 30.0f * 0.5f);

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

    // -----------------------------------------------------------------
    // v2.47.0 -> v2.48.0 (Clement's calls): ONE curve, and its truth is
    // the MAX-HOLD family - "j'ai l'impression que les formes issues
    // des courbes en maximum hold sont beaucoup plus propres". Every
    // member above is computed and gated exactly as before (the two
    // "EQ @ max" windows with their own gates, the H1 transfer with
    // its coherence + relative gates); here they merge into the single
    // line the panel draws:
    //
    //   coherent band:  EQ = (max5s + max2s) / 2
    //   junk-dominated:  EQ = the H1 member (smoothDiff)
    //
    // The raw-coherence fallback is the piece that keeps the v2.22
    // junk rejection alive inside a max-hold curve: a dB difference
    // measures EVERYTHING the console put in the band, so where the
    // RAW coherence (undiluted by the Savitzky-Golay window) says the
    // band's output is not explained by its input, the max members do
    // not vote and the bin reads the cross-spectrum member, which
    // rejects uncorrelated junk by construction - on a console with a
    // floor above its lowpass the maxima would otherwise show the
    // v2.21.1 phantom boost again. g2 is self-normalising, so an
    // honest band reads ~1 from the first frame after a flush and
    // the fallback never lingers on a clean console; the 0.75 floor
    // sits between that honest ~1 and the ~0.5 of a junk-dominated
    // band (the reference simulation's console-tone scenario).
    //
    // The measured flags combine as a UNION and the weights
    // renormalise over whichever members are valid in the bin, so a
    // band only one member ever gated in still reads (and the sticky
    // flags keep the whole curve on screen when the signal stops -
    // the v2.12 persistence, unchanged). A light final one-pole
    // (tau ~0.3 s) keeps the blend continuous when a member wakes up
    // or the fallback hands a bin back to the max members.
    //
    // Why these members: the v2.48 reference simulation raced
    // Clement's three candidate pairs on the soft-saturation mock
    // (ground truth at two drive levels) and the (5 s; 2 s) blend won
    // - see the 2 s block above for the numbers. Documented costs: a
    // knob DOWN glides over the members' hold (up to 5 s, the v2.46
    // contract), and a band whose honest content is dominated by
    // distortion products (low raw coherence, real signal-dependent
    // content) conservatively reads the linear-part H1 transfer
    // there instead of the max.
    // -----------------------------------------------------------------
    {
        // the junk detector: the RAW per-band coherence, undiluted by
        // the frequency smoothing (a 1-band console tone reads 0.50
        // raw but 0.93 once the SG window has diluted its dip)
        float rawCoherence[SpectrumAnalyzer::numScopeBins];
        processor.getTransferAnalyzer().copyRawCoherenceData (
            rawCoherence, SpectrumAnalyzer::numScopeBins);

        constexpr float veriteAlpha = 1.0f / (1.0f + 30.0f * 0.3f);
        constexpr float maxCoherenceFloor = 0.75f;

        for (int b = 0; b < SpectrumAnalyzer::numScopeBins; ++b)
        {
            const size_t bi = (size_t) b;

            const bool coherent = rawCoherence[b] >= maxCoherenceFloor;

            const float wM5 = (eqMaxMeasured [bi] && coherent) ? 1.0f : 0.0f;
            const float wM2 = (eqMax2Measured[bi] && coherent) ? 1.0f : 0.0f;
            const float wMax = wM5 + wM2;

            float target = 0.0f;
            bool haveTarget = false;

            if (wMax > 0.0f)
            {
                // the max-hold truth: the mean of the two windows
                target = (wM5 * maxEqDiff[bi] + wM2 * maxEqDiff2[bi]) / wMax;
                haveTarget = true;
            }
            else if (eqMeasured[bi])
            {
                // junk-dominated (or a band the max never gated in):
                // the cross-spectrum member, immune to junk
                target = smoothDiff[bi];
                haveTarget = true;
            }

            if (! haveTarget)
                continue; // nothing ever measured here: hold

            eqVeriteMeasured[bi] = true;

            smoothEqVerite[bi] += veriteAlpha * (target - smoothEqVerite[bi]);
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
    // v2.48.0: the harness reads the SINGLE curve the panel draws - now
    // the max-hold blend (mean of the 5 s and 2 s "EQ @ max" windows,
    // with the H1 cross-spectrum as the junk fallback member).
    return smoothEqVerite[(size_t) juce::jlimit (0, SpectrumAnalyzer::numScopeBins - 1, bin)];
}

bool SpectrumPanel::isEqBinMeasured (int bin) const
{
    return eqVeriteMeasured[(size_t) juce::jlimit (0, SpectrumAnalyzer::numScopeBins - 1, bin)];
}

// v2.46 -> v2.48: the max machinery is now the pair of MEMBERS of the
// single EQ curve (kept directly testable through these hooks).
float SpectrumPanel::getEqMaxDbAtBin (int bin) const
{
    return maxEqDiff[(size_t) juce::jlimit (0, SpectrumAnalyzer::numScopeBins - 1, bin)];
}

bool SpectrumPanel::isEqMaxBinMeasured (int bin) const
{
    return eqMaxMeasured[(size_t) juce::jlimit (0, SpectrumAnalyzer::numScopeBins - 1, bin)];
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
// (since v2.47.0 that flag is the UNION eqVeriteMeasured - whichever
// member gated the bin in), not just the bins
// where the input currently has energy
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

    // v2.48.0: ONE curve - the max-hold blend (the mean of the 5 s and
    // 2 s "EQ @ max" windows, with the H1 cross-spectrum member as the
    // junk fallback), drawn for exactly the bins any member ever
    // measured (sticky flags, v2.12 persistence).
    for (int bin = 0; bin < SpectrumAnalyzer::numScopeBins; ++bin)
    {
        if (! eqVeriteMeasured[(size_t) bin])
        {
            started = false; // skip bins that were never measured
            continue;
        }

        const float x = xForBin (bin);
        const float y = yForEqDb (juce::jlimit (-range, range, smoothEqVerite[(size_t) bin]));

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
    // v2.39.0 (Clement's remark D): the field behind the hosted plugin
    // is the same neutral black as the hosted plugins' own frame, so
    // the GUIs embed seamlessly - whatever dead margin remains (narrow
    // strip at the width floor, short GUI at the height floor) reads
    // as one dark field with the plugin in it. The navy is the sidebar
    // panel's colour, not the centre's.
    g.fillAll (juce::Colour (0xff0f0f0f));
}

// ---------------------------------------------------------------------------
// v2.30: the interactive help mode (Clement's remark 3, in the spirit of
// the Tokyo Down Record plugins). The "?" button enters it; from then
// on a full-editor transparent overlay sits on top of everything:
// moving the mouse outlines the control under the cursor and shows a
// small bubble with its help text - a few words, the control's entry
// in the editor's helpTexts map (since v2.31 the texts live ONLY there,
// Clement's remark 1 - and since v2.31.1 not even the ? button keeps
// one: the editor carries no standing tooltip at all outside the
// mode. ANY click anywhere, or Esc, leaves the
// mode; the overlay swallows the click, so a button is never triggered
// from inside the mode. Nothing else changes: no veil over the UI, no
// wall of text, and nothing at all while the mode is off. It replaces
// the v2.20 all-in-one help window (the help comes to the control now).
//
// v2.31 (Clement's remark 3): the bubble is no longer painted INSIDE
// the overlay. The hosted plugin's editor is a native NSView layered
// ABOVE everything JUCE draws in the window, so an overlay-drawn
// bubble would vanish behind the console ("it cuts the writing").
// The bubble now lives in its own borderless always-on-top DESKTOP
// window (HelpBubbleWindow below), which floats in front of every
// native view. The overlay keeps the highlight, the keyboard trap and
// the geometry; the bubble window follows the cursor in screen
// coordinates.
// ---------------------------------------------------------------------------

// v2.31: the help bubble's own desktop window. Tiny, borderless,
// transparent, always on top - and above all NOT a child of the
// editor, because children are drawn under the hosted plugin's
// native NSView.
class HelpBubbleWindow final : public juce::Component
{
public:
    explicit HelpBubbleWindow (TheChannelStripAudioProcessorEditor& ed) : editor (ed)
    {
        setOpaque (false);
        setVisible (false); // nothing to show until the cursor picks a control
        addToDesktop (0);   // borderless desktop window, no decorations
        setAlwaysOnTop (true);
    }

    ~HelpBubbleWindow() override
    {
        removeFromDesktop();
    }

    // a click on the bubble leaves the mode, like anywhere else
    void mouseDown (const juce::MouseEvent&) override { editor.exitHelpMode(); }

    // v2.31: the lines to draw, handed over by the overlay on every
    // hover move (the window's bounds are set right before this).
    void setContent (const juce::StringArray& newLines)
    {
        lines = newLines;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        // the same small rounded card the v2.30 overlay drew inline -
        // now in its own window, where the hosted plugin cannot
        // cover it
        g.setColour (juce::Colour (0xf20e1522));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
        g.setColour (juce::Colour (0xff5f6b82));
        g.drawRoundedRectangle (getLocalBounds().toFloat(), 5.0f, 1.0f);

        g.setColour (juce::Colour (0xffdfe6f2));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));

        for (int i = 0; i < lines.size(); ++i)
            g.drawText (lines[i], 8, 5 + i * 16, getWidth() - 16, 16,
                        juce::Justification::centredLeft);
    }

private:
    TheChannelStripAudioProcessorEditor& editor;
    juce::StringArray lines;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HelpBubbleWindow)
};

class HelpOverlay final : public juce::Component
{
public:
    explicit HelpOverlay (TheChannelStripAudioProcessorEditor& ed) : editor (ed)
    {
        // Esc must reach us even though buttons normally take focus
        setWantsKeyboardFocus (true);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            editor.exitHelpMode();
            return true;
        }

        return false;
    }

    // any click leaves the mode - and never reaches the control below
    void mouseDown (const juce::MouseEvent&) override { editor.exitHelpMode(); }

    void mouseMove (const juce::MouseEvent& e) override { updateFor (e.getPosition()); }

    void paint (juce::Graphics& g) override
    {
        // the hovered control, outlined - the lightest highlight there
        // is. The bubble itself is NOT drawn here any more (v2.31): it
        // rides in the editor's always-on-top desktop window, above
        // the hosted plugin's native view (Clement's remark 3).
        if (text.isNotEmpty())
        {
            g.setColour (juce::Colour (0xffdfe6f2).withAlpha (0.9f));
            g.drawRoundedRectangle (highlight.toFloat().expanded (2.0f), 4.0f, 1.2f);
        }
    }

private:
    void updateFor (juce::Point<int> pt)
    {
        // the mode is tearing down (exitHelpMode already released the
        // bubble): nothing left to drive
        if (editor.helpBubble == nullptr)
            return;

        auto* newTarget = targetUnder (&editor, pt, this);
        const auto newText = editor.helpTextFor (newTarget, pt);
        const bool unchanged = (newTarget == target && newText == text);

        // v2.40.0 (Clement's remark 2): sweeping the analyser with the
        // mode on used to move the bubble's DESKTOP window on EVERY
        // mouse-move - a native window move dozens of times per second
        // is what stuttered the whole UI. Within one target with one
        // text the bubble's geometry cannot change (it is a pure
        // function of the two), so do NOTHING when both already match:
        // the bubble now only moves when the target or the text
        // actually change. If a resize hid it (stale screen coords),
        // isVisible() is false and we fall through to recompute and
        // re-show it on this very move.
        if (unchanged && editor.helpBubble->isVisible())
            return;

        target = newTarget;
        text = newText;

        if (target == nullptr || text.isEmpty())
        {
            target = nullptr;
            text = juce::String();
            lines.clear();
            highlight = juce::Rectangle<int>();
            editor.helpBubble->setVisible (false);
        }
        else
        {
            const auto font = juce::Font (juce::FontOptions (12.0f));

            highlight = editor.getLocalArea (target, target->getLocalBounds());
            lines = wrapToWidth (text, font, juce::jmin (340, juce::jmax (60, getWidth() - 16)));

            int textWidth = 0;
            for (auto& l : lines)
                textWidth = juce::jmax (textWidth,
                                         juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, l)));

            const int bubbleW = textWidth + 16;
            const int bubbleH = lines.size() * 16 + 10;

            int bx = juce::jlimit (6, juce::jmax (6, getWidth() - bubbleW - 6),
                                   highlight.getCentreX() - bubbleW / 2);

            int by = highlight.getBottom() + 8;

            if (by + bubbleH > getHeight() - 6)
                by = highlight.getY() - bubbleH - 8;   // no room below: above

            by = juce::jmax (6, by);

            // v2.31: the geometry above is editor-local, but the bubble
            // window lives on the desktop - place it in SCREEN
            // coordinates, in front of the hosted plugin's view.
            const auto screenPos = editor.localPointToGlobal (juce::Point<int> (bx, by));
            editor.helpBubble->setBounds (screenPos.x, screenPos.y, bubbleW, bubbleH);
            editor.helpBubble->setContent (lines);
            editor.helpBubble->setVisible (true);
        }

        if (! unchanged)
            repaint();
    }

    // word-wrap a help text into lines that fit maxWidth
    static juce::StringArray wrapToWidth (const juce::String& text,
                                           const juce::Font& font,
                                           int maxWidth)
    {
        juce::StringArray wrapped;
        juce::String line;

        for (const auto& word : juce::StringArray::fromTokens (text, " ", ""))
        {
            const auto candidate = line.isEmpty() ? word : line + " " + word;

            if (line.isEmpty()
                || juce::GlyphArrangement::getStringWidth (font, candidate) <= (float) maxWidth)
                line = candidate;
            else
            {
                wrapped.add (line);
                line = word;
            }
        }

        if (line.isNotEmpty())
            wrapped.add (line);

        return wrapped;
    }

    // the deepest component at pt, WITHOUT the overlay itself (the
    // overlay sits on top, so the stock getComponentAt would always
    // return it). Topmost children first, like JUCE's own hit test.
    static juce::Component* targetUnder (juce::Component* parent,
                                         juce::Point<int> pt,
                                         const juce::Component* skip)
    {
        if (parent == skip || ! parent->isVisible())
            return nullptr;

        for (int i = parent->getNumChildComponents(); --i >= 0;)
        {
            auto* c = parent->getChildComponent (i);

            if (c == skip || ! c->isVisible())
                continue;

            const auto local = c->getLocalPoint (parent, pt);

            if (c->getLocalBounds().contains (local))
            {
                if (auto* deeper = targetUnder (c, local, skip))
                    return deeper;

                return c;
            }
        }

        return nullptr;
    }

    TheChannelStripAudioProcessorEditor& editor;

    juce::Component* target = nullptr;
    juce::Rectangle<int> highlight;
    // v2.31: the bubble RECT lives in the desktop window's bounds now -
    // only the highlight is drawn here.
    juce::String text;
    juce::StringArray lines;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HelpOverlay)
};

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
    // v2.26.1: presets section now one icon row -> 973 / 793.
    // v2.33: the shell integrity bandeau under the channel readout
    // -> 1003 / 823.
    // v2.34: zone A rework (channel readout merged into the status
    // block, logo halved into the gain zone's right column) -> 890 / 710.
    // v2.34.1: back by request - full-width gain rows with the big
    // correction readout, and the logo in its own band again (now the
    // monochrome variant at 50 px) -> 970 / 790.
    // v2.38.0: the minimal frame (margins/gutters halved) and the
    // justified sidebar free enough room to shrink the default and the
    // minimum - less wide, less tall.
    setSize (1165, 970);
    setResizable (true, false);
    // v2.39.0 (Clement): the minimum WIDTH drops to 860 - the
    // sidebar needs 147, narrow channel strips were drowning in
    // dead margin at the old 1024 floor.
    setResizeLimits (860, proc.getAnalyserVisible() ? 970 : 790, 2560, 1600);

    // v2.4: the title sits at the top of the condensed left sidebar, so
    // it is small and wraps onto two lines inside the column.
    titleLabel.setText ("THE CHANNEL STRIP", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffffff));
    addAndMakeVisible (titleLabel);

    // v2.34.1: the readouts are back to full sidebar width (the logo
    // moved back to its own band below the section, see below), so the
    // original 12 pt row returns.
    for (auto* label : { &lufsInLabel, &lufsOutLabel, &correctionLabel })
    {
        label->setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        label->setJustificationType (juce::Justification::centred);
        label->setColour (juce::Label::textColourId, juce::Colour (0xffdfe6f2));
        addAndMakeVisible (*label);
    }

    // v2.42.1: the correction label starts at "-- dB"; from then on it is
    // the only writer of that text (refreshLufs), under the 1 Hz / 0.2 dB
    // cadence+deadband gate.
    correctionLabel.setText ("-- dB", juce::dontSendNotification);

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
    // v2.31 (Clement's remark 1): the label's help text moved from a
    // standing tooltip into helpTexts - outside the help mode the
    // sidebar stays silent now (since v2.31.1 nothing in the editor
    // carries a standing tooltip at all, not even the ? button).
    helpTexts[&hostedNameLabel] =
        "The hosted console plugin, once one is selected.";
    addAndMakeVisible (hostedNameLabel);

    // v2.34: the merged status block under the hosted console's name -
    // one label fed by proc.getSidebarStatusLines(): the channel format
    // (what the host feeds this instance, and why a mono track is
    // bridged: "refused", a next-best layout like "2/2", or "multi-bus")
    // plus the crossfade on line one, the forwarded latency plus the
    // silence sentinel on line two. v2.35.0/v2.36.0: the zone is three
    // tall now - an actionable alert (missing/blacklisted console,
    // ERROR:, WARNING:) takes it over, coloured, until resolved; the
    // bottom status area is gone ("shell: unity" never changes and
    // lives in the Doctor report only).
    integrityLabel.setFont (juce::Font (juce::FontOptions (10.0f)));
    // v2.36.0 (Clement): top-anchored - the two integrity lines hug the
    // console name right above; the spare line stays below, out of
    // sight, as headroom for a longer alert.
    integrityLabel.setJustificationType (juce::Justification::centredTop);
    integrityLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8f9bb0));
    helpTexts[&integrityLabel] =
        "Status: the channel format the host gives this instance and the "
        "console's audio path, whether a console-swap crossfade is still "
        "blending, the delay forwarded to the host, and the sentinel that "
        "watches for a hosted console which stopped producing audio (a "
        "lapsed licence does exactly that). An actionable alert - a "
        "missing or blacklisted console, an error, a warning - takes "
        "this zone over until it is resolved; technical detail lives in "
        "the Doctor report.";
    addAndMakeVisible (integrityLabel);

    // v2.1: the correction is the main readout: bigger and smoothed.
    // v2.4: slightly smaller so the condensed sidebar fits at min height.
    // v2.13: back to the correction reading (v2.12's measured-deviation
    // display went back to an action at Clement's request), now with an
    // explicit "Add" prefix: "Add +0.7 dB" = raise the output by 0.7 dB
    // to match the input loudness; "Add -0.4 dB" = lower it by 0.4 dB.
    // v2.34.1: back to the full-width single-line readout under the
    // IN/OUT rows - the v2.34 squeeze into the logo column clipped it
    // out of sight, and Clement wants his main readout back as it was.
    correctionLabel.setColour (juce::Label::textColourId, juce::Colour (0xff54c8e8));
    correctionLabel.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    correctionLabel.setJustificationType (juce::Justification::centred);
    // v2.31 (remark 1): the correction's explanation moved into the
    // help-mode texts too - no standing tooltip any more.
    helpTexts[&correctionLabel] =
        "Smoothed short-term BS.1770 correction: what to ADD to "
        "the console's output so it matches its input loudness. "
        "Add +0.7 dB means raise the output by 0.7 dB; Add -0.4 dB "
        "means lower it by 0.4 dB.";

    // v2.17: Clement's studio logo, embedded as a binary resource so the
    // plugin is fully self-contained. v2.34.1 (Clement's design): the
    // sidebar now shows a MONOCHROME variant - only the black design of
    // the original kept and turned white, the beige background made
    // transparent, and a rounded outer frame baked in, so the logo sits
    // in the shell's palette instead of a beige patch. Same ImageComponent
    // placement as always: centred, aspect ratio preserved, never
    // upscaled.
    studioLogo.setImage (juce::ImageCache::getFromMemory (BinaryData::CostesLogoMono_png,
                                                           BinaryData::CostesLogoMono_pngSize),
                         juce::RectanglePlacement (juce::RectanglePlacement::centred
                                                   | juce::RectanglePlacement::onlyReduceInSize));
    addAndMakeVisible (studioLogo);

    // v2.34: clicking the logo opens the CONTACT CARD - who built the
    // shell (mixing & mastering engineer), the mail and the website,
    // one click to copy either. The Doctor (v2.33) moved to its own
    // heartbeat icon button on the Rescan row. A MouseListener instead
    // of a subclass keeps the logo's drawing and layout exactly as
    // they were; the pointing-hand cursor says "clickable". The
    // listener is detached in the destructor.
    logoMouseListener.owner = this;
    studioLogo.addMouseListener (&logoMouseListener, false);
    studioLogo.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    helpTexts[&studioLogo] =
        "Click the logo for Clement's contact card - mixing & mastering "
        "engineer, with the mail and the website one click away.";

    // v2.30: the "?" button no longer rides the logo band - it moved
    // to the presets button row (see the presets section below) and
    // now enters the interactive help mode instead of opening the
    // all-in-one tooltip window.

    addAndMakeVisible (rescanButton);
    rescanButton.onClick = [this] { proc.requestDefaultScan(); };
    // v2.9.1: in a DAW the button only reloads the saved database (loading
    // third-party modules in-process can crash the host); the full folder
    // scan runs in the Standalone, out-of-process.
    // v2.31 (remark 1): the Rescan explanation lives in the help-mode
    // texts now, not as a standing tooltip.
    helpTexts[&rescanButton] =
        "Reload the saved plugin database. In the Standalone "
        "this re-scans the VST3 folders (out-of-process, hostile "
        "plugins can only kill the helper); inside a DAW it only "
        "reloads the database - launch the Standalone to run a "
        "full folder scan.";

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
    // One combo box listing every preset in the preset folder - the
    // ROOT and the album sub-folders alike (v2.30: albums are listed,
    // the entry text shows the album) - then a single row of three
    // icon buttons plus the "?" help button (v2.30). Selecting an
    // entry loads it (switching consoles when needed); every action
    // reports to the status area.
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
    presetCombo.onChange = [this] { onPresetSelected(); };
    addAndMakeVisible (presetCombo);

    // v2.26.1: the preset buttons carry vector icons instead of
    // text. v2.29: three of them (floppy = save, bin = delete, tray
    // with an incoming arrow = load - the old export merged into
    // Save, the old import became Load). Each gets its own
    // look-and-feel that draws the icon where the text would be; the
    // styleConsoleButton colours are unchanged. No tooltips on these
    // buttons or the combo: the "?" panel documents the section.
    presetButtonLaFs[0] = std::make_unique<PresetButtonLookAndFeel> (PresetButtonLookAndFeel::Icon::floppy);
    presetButtonLaFs[1] = std::make_unique<PresetButtonLookAndFeel> (PresetButtonLookAndFeel::Icon::trash);
    presetButtonLaFs[2] = std::make_unique<PresetButtonLookAndFeel> (PresetButtonLookAndFeel::Icon::importIn);
    // v2.34: the 4th icon look-and-feel draws the Doctor's heartbeat.
    presetButtonLaFs[3] = std::make_unique<PresetButtonLookAndFeel> (PresetButtonLookAndFeel::Icon::pulse);
    // v2.34.1: the 5th draws the analyser display toggle's spectrum
    // bars - v2.37.0: labelled variant, "ANALYSER" + bars in one button.
    presetButtonLaFs[4] = std::make_unique<PresetButtonLookAndFeel> (PresetButtonLookAndFeel::Icon::spectrumLabelled);

    savePresetButton.setLookAndFeel    (presetButtonLaFs[0].get());
    deletePresetButton.setLookAndFeel  (presetButtonLaFs[1].get());
    importPresetButton.setLookAndFeel (presetButtonLaFs[2].get());

    for (auto* b : { &savePresetButton, &deletePresetButton,
                    &importPresetButton })
    {
        styleConsoleButton (*b);
        addAndMakeVisible (*b);
    }

    savePresetButton.onClick   = [this] { savePresetToFile(); };
    deletePresetButton.onClick = [this] { confirmDeletePreset(); };
    importPresetButton.onClick = [this] { importPresetFile(); };

    // v2.30: the "?" help button takes the 4th slot of the presets
    // row (freed by the removed export button) and enters the
    // interactive help mode: hovering any control then shows a small
    // contextual bubble; any click, or Esc, leaves the mode.
    // v2.31.1 (Clement's follow-up): the \"?\" button carried the
    // editor's LAST standing tooltip, and the standing TooltipWindow
    // drew it half cut off in the sidebar - so it goes too. Its text
    // moved into helpTexts like everyone else's: the editor now
    // carries NO standing tooltip at all, and the only hint that the
    // \"?\" opens the help is the \"?\" itself.
    styleConsoleButton (helpButton);
    helpTexts[&helpButton] = "Interactive help: hover anything for a short"
                             " explanation. A click or Esc leaves it.";
    helpButton.onClick = [this] { enterHelpMode(); };
    addAndMakeVisible (helpButton);

    // v2.34: the Doctor button - a heartbeat-trace icon on the Rescan
    // row at the bottom of the sidebar. The Doctor itself is unchanged
    // since v2.33 (full diagnostic snapshot, saved as a text file next
    // to the plugin database); only its trigger moved, from the logo's
    // click to this dedicated button.
    doctorButton.setLookAndFeel (presetButtonLaFs[3].get());
    styleConsoleButton (doctorButton);
    doctorButton.onClick = [this] { showDoctorReport(); };
    helpTexts[&doctorButton] =
        "The Doctor: a full diagnostic report of the shell (versions, "
        "host, plugin database, latency, integrity states), shown in a "
        "window and saved as a text file next to the plugin database.";
    addAndMakeVisible (doctorButton);

    // v2.30: short help texts for the help-mode bubbles. They live in
    // the editor's helpTexts map, NOT in setTooltip(): a standing
    // juce::TooltipWindow is active (v2.12) and would show any real
    // tooltip on plain hover - these texts belong to the help mode
    // alone (and the analyser's would cover its hover readout again).
    // v2.31.1: this is now the rule for EVERY control - no setTooltip
    // call remains in the editor at all, so the standing window has
    // nothing left to show outside the mode.
    helpTexts[&presetCombo] =
        "Pick a preset to load it on its console (switching consoles when"
        " needed). A \"*\" means its knobs have moved.";
    helpTexts[&savePresetButton] =
        "Save As: the console's current state to a preset file, anywhere."
        " It becomes the current preset.";
    helpTexts[&deletePresetButton] =
        "Delete the selected preset - or browse for any preset file when"
        " nothing is selected.";
    helpTexts[&importPresetButton] =
        "Load a .tcsPreset from anywhere. Nothing is copied.";

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

            // v2.31 (remark 1): the speed explanation moved into the
            // help-mode texts - no standing tooltip any more.
            helpTexts[&b] = "Analyser response speed: the IN/OUT curves smooth "
                            "with a time constant of ~0.15 s (Fast) or ~1 s (Slow).";
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

        // v2.20: the tilt buttons got a tooltip (they were the only
        // analyser controls without one). v2.31 (remark 1): the text
        // moved into the help-mode texts - no standing tooltip.
        helpTexts[&t] = "Analyser display tilt in dB per octave (reference 1 kHz, "
                        "SPAN convention: a negative tilt flattens a naturally "
                        "falling spectrum). The tilt only affects the drawing, "
                        "never the analysis.";

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

    // v2.20 -> v2.30: the analyser still has no STANDING tooltip - the
    // JUCE-level one was in the way of the hover readout (v2.20). Its
    // help text lives in the editor's helpTexts map instead: it shows
    // as a bubble inside the interactive help mode only, so the
    // readout is untouched outside it.
    helpTexts[&spectrumPanel] =
        "IN and OUT spectra overlaid. The dB scales and the LUFS reference"
        " at 0 dB are clickable; hovering reads the exact level.";

    // restore the saved scales (defaults: 10 dB and 4 dB per division;
    // legacy values are snapped to the two current scales, like the
    // v2.9 button restore did).
    spectrumPanel.setDbPerDiv (proc.getAnaDbDiv() > 7.5f ? 10.0f : 6.0f);
    spectrumPanel.setEqDbPerDiv (proc.getEqDbDiv() > 3.0f ? 4.0f : 2.0f);

    // ---- v2.8: show/hide the analyser strip ----
    // v2.34.1 (Clement's suggestion): the old "Display" text toggle
    // became a wide icon button, styled like the other console buttons.
    // v2.37.0 (Clement's design): the button carries the section heading
    // itself - "ANALYSER" and the spectrum glyph share it, so the
    // separate heading row is gone. Same toggle behaviour: blue when the
    // analyser strip is shown.
    analyserToggle.setLookAndFeel (presetButtonLaFs[4].get());
    styleConsoleButton (analyserToggle);
    analyserToggle.setClickingTogglesState (true);
    // v2.31 (remark 1): help text in the help-mode map, not a tooltip.
    helpTexts[&analyserToggle] =
        "Show or hide the analyser strip (the window re-fits).";
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

void TheChannelStripAudioProcessorEditor::paint (juce::Graphics& g)
{
    // v2.39.0 (Clement's remark D): the dark of the shell is now the
    // NEUTRAL black of the hosted plugins' own frame (brainworx & co,
    // ~0x0f0f0f - the previous 0xff0c0f16 carried a blue tint), so the
    // hosted GUIs visually embed into the shell. The navy stays the
    // sidebar panel's colour.
    g.fillAll (juce::Colour (0xff0f0f0f));

    // v2.5: a subtle panel behind the sidebar (which starts under the
    // full-width analyser strip) so the zones read at a glance.
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
    y += 36;  // v2.38.0 (Clement): less air between title and presets

    // ---- v2.37.0 (Clement's proposal, v2.36 follow-up): the sidebar
    //      reads top to bottom as title, presets + consoles (the
    //      consoles a preset belongs to, together), logo, analyser
    //      section, gain staging, then the name & alarm block above the
    //      pinned Rescan row. ----

    // ---- v2.26 (roadmap point 4): the presets section (a heading, the
    //      global preset list, then one row of icon buttons).
    //      v2.36.0: grouped with the consoles. v2.37.0: directly under
    //      the title, per Clement's proposal. ----
    presetLabel.setBounds (sideX, y, sw, 15);
    y += 17;

    presetCombo.setBounds (sideX, y, sw, 22);
    y += 26;

    // v2.29: one row of three 30 px icon buttons, 3 px apart (floppy =
    // save, bin = delete, tray with the incoming arrow = load).
    // v2.30: the "?" help button takes the 4th slot and enters the
    // interactive help mode.
    savePresetButton.setBounds   (sideX,       y, 30, 22);
    deletePresetButton.setBounds (sideX + 33,  y, 30, 22);
    importPresetButton.setBounds (sideX + 66,  y, 30, 22);
    helpButton.setBounds         (sideX + 99,  y, 30, 22);
    y += 22 + 8;

    for (size_t i = 0; i < consoleButtons.size(); ++i)
    {
        consoleButtons[i].setBounds (sideX, y, sw, 28);
        y += 28 + 5;
    }

    // ---- v2.39.0 (Clement's blocks): the sidebar is FOUR blocks
    //      separated by an IDENTICAL vertical space - (1) title +
    //      presets + consoles, pinned at the top; (2) logo + analyser;
    //      (3) gain staging; (4) the name & alarm block glued to the
    //      Rescan / Doctor row, pinned at the bottom. ----
    const int rescanY = h - m - 26;
    const int alarmY  = rescanY - 42;   // 40 px zone, ends 2 px above Rescan
    const int nameY   = alarmY - 22;

    // block heights: logo + analyser 173 (58 logo, 4 inner gap, 111
    // analyser: toggle + Fast/Slow + tilt), gain staging 87 (heading +
    // IN/OUT + correction); the leftover splits into the three gaps.
    const int leftover = (nameY - y) - (173 + 87);
    const int gap      = juce::jmax (4, leftover / 3);

    // ---- block 2: the frame-free monochrome logo (one click to the
    //      contact card) with the analyser section right under it. ----
    y += gap;
    studioLogo.setBounds (sideX, y, sw, 58);
    y += 58 + 4;

    // ---- v2.8: show/hide the analyser strip. v2.37.0 (Clement's
    //      design): the toggle carries the section heading - the
    //      "ANALYSER" label and the spectrum glyph share the button,
    //      saving the v2.36 heading row. Same toggle behaviour: blue
    //      when the analyser strip is shown. ----
    analyserToggle.setBounds (sideX, y, sw, 26);
    y += 26 + 8;

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

    // ---- v2.34.1 (zone A, back by request): the gain-staging block is
    //      full-width again - IN/OUT rows, then the big single-line
    //      correction readout under them. ----
    y += gap;
    gainLabel.setBounds (sideX, y, sw, 15);
    y += 17;

    lufsInLabel.setBounds  (sideX, y, sw, 19);
    y += 19;
    lufsOutLabel.setBounds (sideX, y, sw, 19);
    y += 21;

    correctionLabel.setBounds (sideX, y, sw, 26);
    y += 30;

    // ---- block 4 (v2.39.0): the console name and its alarm zone
    //      GLUED to the Rescan / Doctor row (the alarm zone ends 2 px
    //      above it) - a "Press Rescan" alert lands next to the button
    //      that answers it. Condensed as before: name 20 px, three-line
    //      alarm zone anchored top. ----
    hostedNameLabel.setBounds (sideX, nameY, sw, 20);

    // v2.35.0: ONE merged status zone - the two-line integrity readout
    // normally, an actionable alert (missing/blacklisted console,
    // ERROR:, WARNING:) taking it over until resolved; console name and
    // latency live here exactly once, hosting chatter lives in the
    // Doctor report. v2.36.0: condensed from four lines to three.
    integrityLabel.setBounds (sideX, alarmY, sw, 40);

    // v2.6: Rescan is the only control pinned at the bottom (the bypass
    // button is gone). v2.34: the Doctor's heartbeat icon rides the same
    // row, to its right.
    rescanButton.setBounds (sideX, h - m - 26, sw - 38, 24);
    doctorButton.setBounds  (sideX + sw - 34, h - m - 26, 34, 24);

    // ---- centre column: the hosted plugin fills the rest, under the
    //      full-width analyser strip ----
    hostedViewport.setBounds (centreX, zoneY, centreW,
                              juce::jmax (60, h - m - zoneY));

    layoutHostedHolder();

    // v2.30: the help overlay covers the whole editor whatever the
    // window does while the mode is on
    if (helpOverlay != nullptr)
    {
        helpOverlay->setBounds (getLocalBounds());

        // v2.31: the bubble window keeps SCREEN coordinates - after a
        // resize they are stale, so drop it until the next mouse move
        // repositions it over its control (mouseMove fires again as
        // soon as the cursor moves, so it comes right back).
        if (helpBubble != nullptr)
            helpBubble->setVisible (false);
    }

    // v2.40.0: the contact card rides on the desktop over the plugin's
    // on-screen rectangle - a resize under it leaves that geometry
    // stale, so close it (one click on the logo reopens it).
    closeContactCard();
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

    // v2.39.0: the width floor drops 1024 -> 860 so a narrow hosted
    // channel strip no longer gets a wide dead margin (the window
    // hugs it down to the sidebar's own needs; below that the
    // viewport scrolls).
    const int newW = juce::jlimit (860, 2560, targetW);
    // v2.8: dynamic minimum so the condensed sidebar always fits.
    // v2.10: +30 px for the A/B row. v2.12: the two scale rows moved
    // onto the analyser. v2.13: two more console buttons -> 770 / 520.
    // v2.16: two more console buttons (Lindell) -> 840 / 660.
    // v2.17: the studio logo block -> 960 / 780.
    // v2.17.1: logo halved -> 900 / 720.
    // v2.26.1: presets section now one icon row -> 973 / 793.
    // v2.33: the shell integrity bandeau -> 1003 / 823.
    // v2.34: zone A rework -> 890 / 710.
    // v2.35.0: merged status zone + bottom logo band -> 1000 / 810.
    // v2.36.0: sidebar module re-order, same vertical budget -> 1000 / 810.
    // v2.38.0: justified sidebar + minimal margins (the middle sections
    // flex, the pinned stacks are tighter) -> 970 / 790.
    const int newH = juce::jlimit (showStrip ? 970 : 790, 1600, targetH);

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

    // v2.41: hiding the strip wipes the display traces (the processor
    // stops the spectra at the gate and resets the analysers on the
    // re-show; the panel must not keep the old curves / max-hold peaks
    // either). Pure display state - safe at any moment of the tick.
    if (! show)
        spectrumPanel.resetDisplay();

    spectrumPanel.setVisible (show);
    setResizeLimits (860, show ? 970 : 790, 2560, 1600); // v2.39.0: min width 860
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
        // v2.35.0: the scan progress rides the merged status zone (the
        // rest of the tick needs the scan lock and would freeze the UI).
        integrityLabel.setText ("Scanning the plugin folders... (progress is printed in the Terminal)",
                                juce::dontSendNotification);

        if (lastStatusColour != juce::Colour (0xff8f9bb0))
        {
            lastStatusColour = juce::Colour (0xff8f9bb0);
            integrityLabel.setColour (juce::Label::textColourId, lastStatusColour);
        }
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

    // v2.27: keep the preset combo box in step with the hosted console
    // - its current preset's name, and the "*" that appears the
    // moment a knob moves. The drain moves the (lock-free) knob-move
    // flag into the console's state first; the combo is only touched
    // when something actually changed.
    proc.drainPresetDirtyFlag();

    if (proc.getCurrentPluginIdent() != lastSyncedPresetIdent
        || proc.getCurrentPresetName() != lastSyncedPresetName
        || proc.isCurrentPresetModified() != lastSyncedPresetModified)
    {
        syncPresetComboSelection();
    }

    // v2.34: advance the preset-name marquee (no-op when the name fits
    // or nothing is selected).
    tickPresetMarquee();

    // v2.35.0: the status area is gone from the bottom of the sidebar;
    // the merged zone above the analyser section IS the status area now
    // - the two-line integrity readout normally, an actionable alert
    // (missing/blacklisted console, ERROR:, WARNING:) taking it over,
    // coloured, until it is resolved. Everything else (hosting chatter,
    // preset confirmations, doctor paths) stays in the Terminal and the
    // Doctor report: zero redundancy.
    const auto status = proc.getHostStatus();
    const bool alert = isSidebarAlert (status);

    // the label itself no-ops when the text has not changed. The
    // crossfade visibly flickers to "active" for the ~50 ms of a console
    // swap, which is exactly the point: the user SEES the shell doing
    // its one transient job.
    integrityLabel.setText (alert ? status
                                  : proc.getSidebarStatusLines(),
                            juce::dontSendNotification);

    // v2.24 (point 7): a warning or an error must not read like the
    // everyday scan chatter - the alert is red for ERROR:, amber for
    // every other actionable line (WARNING: and the missing-console
    // explanations), grey when the zone shows the integrity readout.
    // The colour is cached so the label is only touched when it
    // actually changes.
    const juce::Colour statusColour = ! alert                        ? juce::Colour (0xff8f9bb0)
                                      : status.startsWith ("ERROR:") ? juce::Colour (0xffe25b5b)
                                                                     : juce::Colour (0xffe2a44b);

    if (statusColour != lastStatusColour)
    {
        lastStatusColour = statusColour;
        integrityLabel.setColour (juce::Label::textColourId, statusColour);
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

        // v2.31 (remark 1): the per-slot status text moved into
        // helpTexts - it shows as the help-mode bubble now, not as a
        // standing tooltip on every hover.
        helpTexts[&btn] = tip;

        const bool active = slot.found
                                && proc.identFor (slot.desc) == proc.getCurrentPluginIdent();
        btn.setToggleState (active, juce::dontSendNotification);

        if (active)
        {
            activeName = slot.desc.name;

            // v2.31.2 (Clement's request): the console in use gets an
            // unmistakable SELECTION cue - the project's signature
            // cyan (the OUT curve / correction readout colour) as the
            // fill, with dark navy text for contrast. The other
            // available slots keep the muted navy base, the
            // unavailable ones their dim style - three clearly
            // distinct states at a glance.
            const juce::Colour activeBg (0xff54c8e8);
            btn.setColour (juce::TextButton::buttonColourId,    activeBg);
            btn.setColour (juce::TextButton::buttonOnColourId,  activeBg);
            btn.setColour (juce::TextButton::textColourOffId,   juce::Colour (0xff0e1522));
            btn.setColour (juce::TextButton::textColourOnId,    juce::Colour (0xff0e1522));
        }
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

    // v2.42 (point 4): the smoothing moved INTO the meters - the readout
    // is their 5 s power-domain, silence-gated average, so the editor
    // only formats it now (the old v2.1 editor-side 1.5 s dB EMA and its
    // hold-on-invalid bookkeeping are gone; the meters hold and gate).
    float delta = 0.0f;
    const bool valid = proc.getGainCorrectionDb (delta);

    // v2.42.1 (Clement's remark): even a settled average flutters between
    // two adjacent display values when the 30 Hz timer reformats the
    // label every tick ("Add 3.2" / "Add 3.1" / ...). Lowering the frame
    // rate alone would only slow the flutter down; the fix is a CADENCE
    // plus a DEADBAND. The label reconsiders its text at 1 Hz (v2.42.2,
    // Clement's preference: sound is dynamic, the delta does move - so
    // let those real changes come slowly, at most once a second) and
    // only repaints when the value moved at least 0.2 dB from what it
    // shows - a band the +/-0.05 dB rounding flutter can never cross.
    // On steady material the readout is frozen; a genuine drift (the
    // console's compression riding the song) shows within a second. A
    // validity flip always bypasses the gate, so a console going silent
    // never lingers on a stale number. The meter itself still updates
    // at the 10 Hz hop rate - invisible, free, and it feeds the average.
    constexpr int correctionDisplayTicks = 30;      // the 30 Hz timer -> 1 Hz
    constexpr float correctionDeadbandDb = 0.2f;

    if (valid)
    {
        if (++correctionTickCount >= correctionDisplayTicks)
        {
            correctionTickCount = 0;

            if (! correctionShownValid
                || std::abs (delta - shownCorrectionDb) >= correctionDeadbandDb)
            {
                // v2.13: the readout states the action with an explicit
                // "Add" prefix - "Add +0.7 dB" = the output is 0.7 dB too
                // quiet (raise it by 0.7 dB), "Add -0.4 dB" = lower it.
                // v2.13.1: "Add " goes through an explicit juce::String -
                // a bare string literal concatenated with another
                // const char* (the ternary) does not compile (same
                // lesson as v2.0.0).
                // v2.34: the readout sits under the halved logo in a
                // narrow column, so it breaks after "Add" - two short
                // lines instead of one clipped one.
                shownCorrectionDb = delta;
                correctionShownValid = true;
                correctionLabel.setText (juce::String ("Add ")
                                             + (delta >= 0.0f ? "+" : "")
                                             + juce::String (delta, 1) + " dB",
                                         juce::dontSendNotification);
            }
        }
    }
    else if (correctionShownValid)
    {
        correctionTickCount = 0;
        correctionShownValid = false;
        correctionLabel.setText ("-- dB", juce::dontSendNotification);
    }
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

// v2.40.0: the editor's destructor moved to the END of this file: the
// contact card is a desktop window whose class is defined further down,
// and its synchronous cleanup needs the complete type (the same reason
// this destructor always sat after the HelpOverlay class).

// v2.30: "?" -> the help mode. The overlay fills the editor and rides
// on top of everything, including the hosted plugin; it follows the
// window when resized() moves it.
// v2.31 (remark 3): the bubble now rides in its own always-on-top
// desktop window, so it floats in FRONT of the hosted plugin's
// native view instead of being hidden behind it.
void TheChannelStripAudioProcessorEditor::enterHelpMode()
{
    if (helpOverlay != nullptr)
        return; // already on

    // created first: the overlay drives it on every hover move
    helpBubble = std::make_unique<HelpBubbleWindow> (*this);

    helpOverlay = std::make_unique<HelpOverlay> (*this);
    addAndMakeVisible (helpOverlay.get());
    helpOverlay->setBounds (getLocalBounds());
    helpOverlay->toFront (false);
    helpOverlay->grabKeyboardFocus();
}

// v2.30 -> v2.31: any click anywhere, or Esc. Both mode components are
// deleted OUTSIDE their own mouse/key callbacks (handed to the message
// queue), and if the editor goes first: the overlay is its child, so
// the child cleanup makes the pending deletion a no-op, and the bubble
// is closed synchronously by the editor's destructor - no double
// delete either way.
void TheChannelStripAudioProcessorEditor::exitHelpMode()
{
    if (helpOverlay == nullptr)
        return;

    // the bubble goes first: hidden IMMEDIATELY (it cannot receive a
    // click while hidden, so its editor reference can never be used
    // after this line), then deleted outside this callback. The
    // pending lambda touches no editor state, so it stays safe even
    // if the editor is destroyed before it runs.
    if (helpBubble != nullptr)
    {
        helpBubble->setVisible (false);
        juce::Component::SafePointer<HelpBubbleWindow> safeBubble (helpBubble.release());

        juce::MessageManager::callAsync ([safeBubble]
        {
            if (auto* bubble = safeBubble.getComponent())
                delete bubble;
        });
    }

    juce::Component::SafePointer<HelpOverlay> safe (helpOverlay.release());

    juce::MessageManager::callAsync ([safe]
    {
        if (auto* overlay = safe.getComponent())
            delete overlay;
    });
}

// v2.30 -> v2.31: the bubble text for whatever sits under the cursor:
// the deepest component that carries one, walking up to the editor.
// Since v2.31 (Clement's remark 1) the help texts live ONLY here: the
// controls carry no standing tooltips any more - and since v2.31.1
// not even ? keeps one: the standing TooltipWindow has nothing left
// to show, outside the mode. Every
// sidebar explanation moved into helpTexts. The analyser is
// position-aware (remark 2): its three clickable zones - the left dB
// scale, the right EQ scale and the LUFS reference at 0 dB - each
// return their own text instead of the panel's generic entry.
juce::String TheChannelStripAudioProcessorEditor::helpTextFor (juce::Component* component,
                                                                juce::Point<int> positionInEditor)
{
    for (auto* c = component; c != nullptr; c = c->getParentComponent())
    {
        // v2.31 (remark 2): the analyser zones - SpectrumPanel::
        // helpZoneFor uses the SAME constants as its mouseDown, so the
        // bubble always says exactly what a click in that zone does.
        if (c == &spectrumPanel)
        {
            const auto zone = spectrumPanel.helpZoneFor (
                spectrumPanel.getLocalPoint (this, positionInEditor));

            if (zone == SpectrumPanel::HelpZone::lufsReference)
                return "Loudness reference of the 0 dB line: Record -37, Mix -23 or"
                       " Master -14 LUFS. Click it to cycle the reference.";

            if (zone == SpectrumPanel::HelpZone::analyzerScale)
                return "Left scale: the IN/OUT spectra's display scale. Click it to"
                       " switch between 10 and 6 dB per division.";

            if (zone == SpectrumPanel::HelpZone::eqScale)
                return "Right scale: the EQ (OUT vs IN) curve's own scale. Click it"
                       " to switch between 4 and 2 dB per division.";
        }

        const auto entry = helpTexts.find (c);

        if (entry != helpTexts.end() && entry->second.isNotEmpty())
            return entry->second;

        if (auto* tc = dynamic_cast<juce::TooltipClient*> (c))
        {
            const auto tip = tc->getTooltip();

            if (tip.isNotEmpty())
                return tip;
        }
    }

    return juce::String();
}

// ---------------------------------------------------------------------------
// v2.26 (roadmap point 4): named presets - the sidebar section.
// v2.29: three actions around the list (save-to-file, delete with a
// browse fallback, load-from-file). v2.30: the selector lists the
// preset folder's ROOT and the album sub-folders alike (each album
// entry carries its folder in its text), the entries load by their
// own file, and Load never copies anything anywhere.
// ---------------------------------------------------------------------------
void TheChannelStripAudioProcessorEditor::refreshPresetList()
{
    presetList = proc.getAvailablePresets();
    presetCombo.clear (juce::dontSendNotification);

    for (int i = 0; i < presetList.size(); ++i)
        presetCombo.addItem (presetItemText (presetList[i], false), i + 1);

    // v2.27: with presets on disk but none loaded on the hosted
    // console, an empty list placeholder would be misleading.
    presetCombo.setTextWhenNothingSelected (presetList.isEmpty() ? "(no presets)" : "(none)");

    syncPresetComboSelection();
}

void TheChannelStripAudioProcessorEditor::syncPresetComboSelection()
{
    // v2.27: the combo box mirrors the hosted console's current preset
    // (or no selection at all when the console has none). The current
    // preset's entry grows a "* " prefix as soon as its knobs have
    // moved since it was loaded or saved.
    const auto name = proc.getCurrentPresetName();
    const auto file = proc.getCurrentPresetFile(); // v2.30
    const bool modified = proc.isCurrentPresetModified();

    // v2.30: match the entry by FILE first - the same name can exist
    // at the root and in an album - and fall back to the first name
    // match (an older session tracks only the name).
    int match = -1;

    for (int i = 0; i < presetList.size(); ++i)
        if (presetList[i].file == file)
        {
            match = i;
            break;
        }

    if (match < 0 && name.isNotEmpty())
        for (int i = 0; i < presetList.size(); ++i)
            if (presetList[i].name == name)
            {
                match = i;
                break;
            }

    // refresh every item's text (the "*" comes and goes)
    for (int i = 0; i < presetList.size(); ++i)
        presetCombo.changeItemText (i + 1,
            presetItemText (presetList[i], modified && presetList[i].name == name));

    if (match < 0)
        presetCombo.setSelectedId (0, juce::dontSendNotification);
    else
        presetCombo.setSelectedId (match + 1, juce::dontSendNotification);

    // remember what is mirrored, so the timer only resyncs on change
    lastSyncedPresetIdent = proc.getCurrentPluginIdent();
    lastSyncedPresetName = name;
    lastSyncedPresetModified = modified;

    // v2.34: feed the marquee with exactly what the combo would show
    // (the selected entry's FULL text, "*" included - the combo's own
    // field clips it, the marquee scrolls it) and reset the scroll
    // state whenever the selection changes.
    marqueeText = match < 0 ? juce::String()
                           : presetItemText (presetList[match], modified);
    marqueeOffset = 0.0f;
    marqueePhase = 0;
    marqueeHold = 45;   // ~1.5 s before a too-long name starts scrolling
}

void TheChannelStripAudioProcessorEditor::onPresetSelected()
{
    const int id = presetCombo.getSelectedId();

    if (id < 1 || id - 1 >= presetList.size())
        return;

    // v2.30: the entry's own FILE is loaded - the same name can exist
    // at the root and in an album, and the name-based load only ever
    // reached the root. applyPresetFile posts its own success/failure
    // to the status area.
    if (! proc.applyPresetFile (presetList[id - 1].file))
    {
        // v2.27: a failed load (missing console, corrupt file) must
        // not leave the failed entry selected - snap back to whatever
        // the hosted console actually holds.
        syncPresetComboSelection();
    }
}

// v2.29/v2.30: Save = where. A native save dialog opens on the current
// preset's own file (v2.30 - an album preset re-saves in its album),
// or on the preset folder when the console never came from a preset:
// wherever it lands, the saved preset becomes the console's CURRENT
// one; inside the preset folder (root or album) it shows up in the
// selector, elsewhere it stays off the list. The old name-dialog and
// the old export merged into this one action.
void TheChannelStripAudioProcessorEditor::savePresetToFile()
{
    // v2.30: the dialog opens on the current preset's own FILE - a
    // preset saved in an album re-saves there, one at the root
    // re-saves at the root (the name pre-filled as before).
    const auto start = proc.getCurrentPresetName().isNotEmpty()
        ? proc.getCurrentPresetFile()
        : proc.getPresetsDir();

    // the FileChooser must outlive launchAsync, hence the member
    presetFileChooser = std::make_unique<juce::FileChooser> (
        "Save the current console state as a preset",
        start, "*.tcsPreset");

    presetFileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                        | juce::FileBrowserComponent::canSelectFiles,
                                    [this] (const juce::FileChooser& fc)
                                    {
                                        const auto dest = fc.getResult();

                                        if (dest == juce::File())
                                            return; // cancelled

                                        if (proc.savePresetTo (dest.withFileExtension ("tcsPreset")))
                                            refreshPresetList();
                                    });
}

// v2.29: Delete acts on the selector's selection; with nothing selected
// it becomes the browse-to-delete fallback: any .tcsPreset file
// anywhere can be picked and deleted, after a confirmation. v2.30:
// the selection deletes the ENTRY's own file (a name can exist at the
// root and in an album); whichever path deletes the hosted console's
// current preset's own file, the name is disowned.
void TheChannelStripAudioProcessorEditor::confirmDeletePreset()
{
    const int id = presetCombo.getSelectedId();

    if (id < 1 || id - 1 >= presetList.size())
    {
        browseDeletePresetFile();
        return;
    }

    // v2.30: the selected ENTRY's own file goes - the same name can
    // exist at the root and in an album, so the name alone no longer
    // identifies it.
    const auto& entry = presetList[id - 1];
    const auto file = entry.file;

    juce::String message = "Delete the preset '" + entry.name + "'";

    if (entry.album.isNotEmpty())
        message << " (album: " << entry.album << ")";

    message << "?";

    // async OK/Cancel (JUCE_MODAL_LOOPS_PERMITTED is 0 in a plugin, so
    // the callback overload is the right one); the callback receives 1
    // for OK, 0 for Cancel
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon,
                                        "Delete preset",
                                        message,
                                        "Delete",
                                        "Cancel",
                                        this,
                                        juce::ModalCallbackFunction::create (
                                            [this, file] (int result)
                                            {
                                                if (result == 1 && proc.deletePresetFile (file))
                                                    refreshPresetList();
                                            }));
}

void TheChannelStripAudioProcessorEditor::browseDeletePresetFile()
{
    // v2.29: the browse-to-delete half of Delete (see confirmDeletePreset)
    presetFileChooser = std::make_unique<juce::FileChooser> (
        "Delete a preset file",
        proc.getPresetsDir(),
        "*.tcsPreset");

    presetFileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                        | juce::FileBrowserComponent::canSelectFiles,
                                    [this] (const juce::FileChooser& fc)
                                    {
                                        const auto file = fc.getResult();

                                        if (file == juce::File())
                                            return; // cancelled

                                        juce::AlertWindow::showOkCancelBox (
                                            juce::MessageBoxIconType::WarningIcon,
                                            "Delete preset file",
                                            "Delete '" + file.getFullPathName()
                                                + "'? This cannot be undone.",
                                            "Delete",
                                            "Cancel",
                                            this,
                                            juce::ModalCallbackFunction::create (
                                                [this, file] (int result)
                                                {
                                                    if (result == 1
                                                        && proc.deletePresetFile (file))
                                                        refreshPresetList();
                                                }));
                                    });
}

void TheChannelStripAudioProcessorEditor::importPresetFile()
{
    presetFileChooser = std::make_unique<juce::FileChooser> (
        "Load a preset",
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

// ---------------------------------------------------------------------------
// v2.34: the preset-name marquee. The combo box clips a long preset name
// ("SSL E - My album - Some long name..."), so the editor paints the name
// itself over the combo's text field: an opaque fill in the combo's exact
// background colour hides the clipped text, and the name drifts left in a
// slow loop whenever it is wider than the field. Painting (not a child
// component) keeps every click flowing to the combo beneath - the menu
// opens, the selection works, exactly as before. The 30 Hz timer advances
// the offset; the repaint is limited to the combo's rectangle, and when
// the name fits nothing animates at all.
// ---------------------------------------------------------------------------

// the combo's text zone: inset so the fill never covers the arrow
juce::Rectangle<int> TheChannelStripAudioProcessorEditor::getMarqueeZone() const
{
    const auto r = presetCombo.getBounds();
    return { r.getX() + 2, r.getY() + 3, r.getWidth() - 26, r.getHeight() - 6 };
}

void TheChannelStripAudioProcessorEditor::tickPresetMarquee()
{
    if (marqueeText.isEmpty())
        return;

    const auto zone = getMarqueeZone();
    const auto font = presetCombo.getLookAndFeel().getComboBoxFont (presetCombo);
    const float textW = juce::GlyphArrangement::getStringWidth (font, marqueeText);

    if (textW <= (float) zone.getWidth())
    {
        if (marqueeOffset != 0.0f)
        {
            marqueeOffset = 0.0f;
            repaint (presetCombo.getBounds());
        }
        return;   // fits: painted centred, nothing to animate
    }

    if (marqueeHold > 0)
    {
        --marqueeHold;
        return;    // pause at either end of the loop
    }

    const float maxOffset = textW - (float) zone.getWidth();

    if (marqueePhase == 0)
    {
        marqueeOffset += 0.5f;   // ~15 px/s at 30 Hz

        if (marqueeOffset >= maxOffset)
        {
            marqueeOffset = maxOffset;
            marqueePhase = 1;
            marqueeHold = 90;    // ~3 s pause once the tail is readable
        }

        repaint (presetCombo.getBounds());
    }
    else
    {
        // the tail was shown: snap home and pause before the next pass
        marqueeOffset = 0.0f;
        marqueePhase = 0;
        marqueeHold = 90;
        repaint (presetCombo.getBounds());
    }
}

void TheChannelStripAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    if (marqueeText.isEmpty())
        return;   // nothing selected: the combo's "(none)" placeholder shows

    const auto zone = getMarqueeZone();
    const auto font = presetCombo.getLookAndFeel().getComboBoxFont (presetCombo);
    const float textW = juce::GlyphArrangement::getStringWidth (font, marqueeText);

    // hide the combo's own clipped text with its exact background colour
    g.setColour (juce::Colour (0xff1a2130));
    g.fillRect (zone);

    g.setColour (juce::Colour (0xffdfe6f2));
    g.setFont (font);

    if (textW <= (float) zone.getWidth())
    {
        g.drawText (marqueeText, zone, juce::Justification::centred);
        return;
    }

    // scrolled: left-aligned at the current offset, clipped to the zone
    // so the text never bleeds over the combo's arrow
    g.reduceClipRegion (zone);
    g.drawText (marqueeText,
                juce::Rectangle<int> (zone.getX() - juce::roundToInt (marqueeOffset),
                                      zone.getY(),
                                      (int) textW + 8,
                                      zone.getHeight()),
                juce::Justification::centredLeft);
}

// ---------------------------------------------------------------------------
// v2.33 (roadmap idea 4): the Doctor window's content. A read-only,
// monospaced, scrollable text area with the report plus Copy / Close
// buttons. It holds NO editor reference (the report text is passed in
// and copied to the clipboard from the copy it keeps), so it needs no
// friend declaration and can never dangle. The DialogWindow owns and
// destroys it.
// ---------------------------------------------------------------------------
namespace
{
class DoctorPanel final : public juce::Component
{
public:
    explicit DoctorPanel (const juce::String& reportText)
        : report (reportText)
    {
        text.setReadOnly (true);
        text.setCaretVisible (false);
        text.setMultiLine (true, true);
        text.setScrollbarsShown (true);
        text.setText (report, false);
        // v2.30 lesson: Font(FontOptions) is the JUCE 8 spelling;
        // getDefaultMonospacedFontName keeps the report aligned.
        text.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                                     12.0f, juce::Font::plain)));
        text.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0e1522));
        text.setColour (juce::TextEditor::textColourId,      juce::Colour (0xffdfe6f2));
        addAndMakeVisible (text);

        copyButton.setButtonText ("Copy to clipboard");
        copyButton.onClick = [this]
        {
            juce::SystemClipboard::copyTextToClipboard (report);
            copyButton.setButtonText ("Copied!");
        };
        addAndMakeVisible (copyButton);

        closeButton.setButtonText ("Close");
        closeButton.onClick = [this]
        {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState (0);
        };
        addAndMakeVisible (closeButton);

        setSize (620, 540);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        auto buttons = area.removeFromBottom (28);

        text.setBounds (area);
        copyButton.setBounds (buttons.removeFromLeft (160));
        closeButton.setBounds (buttons.removeFromRight (90));
    }

private:
    juce::String report;
    juce::TextEditor text;
    juce::TextButton copyButton, closeButton;
};
} // namespace

// ---------------------------------------------------------------------------
// v2.34: the contact card's content. Static text - who Clement is, his
// contact details, a French block with its English translation
// underneath (Clement's choice for the zone A branding rework) - plus
// "Copy mail" and "Open website" buttons. It holds NO editor reference,
// so it needs no friend declaration and can never dangle; the desktop
// window that shows it owns and destroys it.
// ---------------------------------------------------------------------------
namespace
{
class ContactPanel final : public juce::Component
{
public:
    // v2.40.0: the card rides in its own desktop window (see
    // ContactCardWindow below) - its Close button reports through this
    // callback instead of hunting for a DialogWindow parent.
    std::function<void()> closeRequested;

    ContactPanel()
    {
        // v2.34.1: juce::String (const char*) reads ASCII, not UTF-8,
        // which is what garbled the accents in the first build - the
        // literals holding non-ASCII bytes must go through the direct
        // String (CharPointer_UTF8 (...)) constructor.
        titleLabel.setText (juce::String (juce::CharPointer_UTF8 ("Clément Costes - Costes Production")),
                            juce::dontSendNotification);
        // v2.35.0 (Clement): everything a bit bigger, and the card no
        // taller than its content - the ~100 px of dead space below
        // the buttons is gone.
        titleLabel.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
        titleLabel.setJustificationType (juce::Justification::centred);
        titleLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffffff));
        addAndMakeVisible (titleLabel);

        // v2.40.0 (Clement): the studio lines are gone, and "Ingénieur
        // Centrale Nantes" now comes before the DJ Network certification.
        frLabel.setText (juce::String (juce::CharPointer_UTF8 ("Ingénieur son : prise de son, mixage & mastering.\n"
                                                "Ingénieur Centrale Nantes - Certifié DJ Network 2022.")),
                         juce::dontSendNotification);
        frLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
        frLabel.setJustificationType (juce::Justification::centred);
        frLabel.setColour (juce::Label::textColourId, juce::Colour (0xffdfe6f2));
        addAndMakeVisible (frLabel);

        // v2.40.0: studio lines gone, Centrale Nantes first - and the
        // translation now renders at the SAME SIZE as the French block
        // (was 11 px); the italic + grey still tell the two apart.
        enLabel.setText ("Sound engineer: recording, mixing & mastering.\n"
                         "Engineer, Centrale Nantes - DJ Network certified 2022.",
                         juce::dontSendNotification);
        enLabel.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::italic)));
        enLabel.setJustificationType (juce::Justification::centred);
        enLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8f9bb0));
        addAndMakeVisible (enLabel);

        mailLabel.setText ("mail: clem.costes@gmail.com", juce::dontSendNotification);
        webLabel.setText  ("web: sites.google.com/view/costesproduction",
                           juce::dontSendNotification);
        for (auto* l : { &mailLabel, &webLabel })
        {
            l->setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            l->setJustificationType (juce::Justification::centred);
            l->setColour (juce::Label::textColourId, juce::Colour (0xff54c8e8));
            addAndMakeVisible (*l);
        }

        copyMailButton.setButtonText ("Copy mail");
        copyMailButton.onClick = [this]
        {
            juce::SystemClipboard::copyTextToClipboard ("clem.costes@gmail.com");
            copyMailButton.setButtonText ("Copied!");
        };
        addAndMakeVisible (copyMailButton);

        // v2.34: open the site in the default browser; if the launch is
        // refused, at least leave the URL on the clipboard.
        openWebButton.setButtonText ("Open website");
        openWebButton.onClick = []
        {
            const juce::URL site ("https://sites.google.com/view/costesproduction");

            if (! site.launchInDefaultBrowser())
                juce::SystemClipboard::copyTextToClipboard (
                    "https://sites.google.com/view/costesproduction");
        };
        addAndMakeVisible (openWebButton);

        // v2.40.0: the card lives in its own desktop window now (no
        // DialogWindow) - the button reports through the callback the
        // window wires up.
        closeButton.setButtonText ("Close");
        closeButton.onClick = [this]
        {
            if (closeRequested)
                closeRequested();
        };
        addAndMakeVisible (closeButton);

        // v2.40.0: the studio lines are gone and so is the DialogWindow
        // chrome - the card is 2 text lines shorter and paints its own
        // rounded background now (see paint below).
        setSize (460, 214);
    }

    // v2.40.0: the panel draws its own card (no more dialog chrome):
    // rounded corners in the theme's background + frame colours.
    void paint (juce::Graphics& g) override
    {
        const auto card = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff141a26));
        g.fillRoundedRectangle (card, 5.0f);
        g.setColour (juce::Colour (0xff5f6b82));
        g.drawRoundedRectangle (card, 5.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);

        titleLabel.setBounds (area.removeFromTop (30));
        frLabel.setBounds    (area.removeFromTop (40));
        enLabel.setBounds    (area.removeFromTop (40));
        mailLabel.setBounds  (area.removeFromTop (22));
        webLabel.setBounds   (area.removeFromTop (22));
        area.removeFromTop (10);

        auto buttons = area.removeFromTop (30);
        copyMailButton.setBounds (buttons.removeFromLeft (140));
        closeButton.setBounds    (buttons.removeFromRight (90));
        openWebButton.setBounds   (buttons);
    }

private:
    juce::Label titleLabel, frLabel, enLabel, mailLabel, webLabel;
    juce::TextButton copyMailButton, openWebButton, closeButton;
};

} // namespace

// ---------------------------------------------------------------------------
// v2.40.0: the card's own always-on-top DESKTOP window, covering exactly
// the editor's on-screen rectangle. Why a desktop window and not a plain
// child overlay: the hosted plugin's native NSView sits above every
// JUCE-drawn layer (the v2.31 lesson), so an in-editor overlay would be
// hidden behind the hosted plugin. The window paints a light scrim over
// the whole plugin - hosted plugin included - and THAT is what makes
// "click outside closes" work everywhere: a click anywhere on the scrim
// closes the card, clicks inside the panel are consumed by it, the Close
// button routes through closeRequested, and Esc is handled too (best
// effort - a desktop window may not get the keys in every host).
//
// NOTE - GLOBAL scope on purpose, exactly like HelpBubbleWindow above:
// the header forward-declares ::ContactCardWindow and befriends it, and
// a class hidden in an anonymous namespace would be a DIFFERENT type
// than the declared one ("reference to 'ContactCardWindow' is
// ambiguous", private-member errors from the friend that no longer
// matches). ContactPanel stays anonymous - it appears in no header.
// ---------------------------------------------------------------------------
class ContactCardWindow final : public juce::Component
{
public:
    ContactCardWindow (TheChannelStripAudioProcessorEditor& ed) : editor (ed)
    {
        setOpaque (false);
        setVisible (false);
        addToDesktop (0);
        setAlwaysOnTop (true);
        setWantsKeyboardFocus (true);

        panel = std::make_unique<ContactPanel>();
        panel->closeRequested = [this] { editor.closeContactCard(); };
        addAndMakeVisible (panel.get());
    }

    ~ContactCardWindow() override { removeFromDesktop(); }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            editor.closeContactCard();
            return true;
        }
        return false;
    }

    // a click anywhere on the scrim (i.e. outside the card) closes it;
    // clicks inside the card never reach here - the panel consumes them.
    void mouseDown (const juce::MouseEvent&) override { editor.closeContactCard(); }

    void paint (juce::Graphics& g) override
    {
        // the light scrim dims the plugin behind the card and makes the
        // "click outside closes" affordance read at a glance.
        g.fillAll (juce::Colour (0x48000000));
    }

    void resized() override
    {
        if (panel != nullptr)
            panel->setBounds (getLocalBounds().withSizeKeepingCentre (panel->getWidth(),
                                                                    panel->getHeight()));
    }

private:
    TheChannelStripAudioProcessorEditor& editor;
    std::unique_ptr<ContactPanel> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContactCardWindow)
};


// v2.34 -> v2.40.0 (Clement's remark 1): the logo click handler - one
// click on the studio logo opens the contact card. The card now rides
// in its own always-on-top desktop window covering exactly the
// plugin's on-screen rectangle - like the help bubble, it must float
// in FRONT of the hosted plugin's native view, which sits above every
// JUCE-drawn child. The window's scrim catches the outside click
// everywhere, and its Close button and Esc close it too (see
// ContactCardWindow above). Pure UI, message thread only.
void TheChannelStripAudioProcessorEditor::showContactCard()
{
    if (contactWindow != nullptr)
        return; // already up

    contactWindow = new ContactCardWindow (*this);
    contactWindow->setBounds (getScreenBounds()); // exactly the plugin window
    contactWindow->setVisible (true);
    contactWindow->toFront (true); // take the keys so Esc closes the card
}

// v2.40.0: closes the contact card - outside click, Close button, Esc,
// or a resize that made its screen geometry stale. The window is hidden
// AT ONCE (hidden it can receive nothing, so its editor reference can
// never dangle) and deleted OUTSIDE its own callback, the exact pattern
// exitHelpMode uses for the help bubble; if the editor dies before the
// pending deletion runs, the destructor removed the window from the
// desktop synchronously and nothing touches the editor anymore.
void TheChannelStripAudioProcessorEditor::closeContactCard()
{
    if (contactWindow == nullptr)
        return;

    contactWindow->setVisible (false);
    juce::Component::SafePointer<ContactCardWindow> safe (contactWindow);
    contactWindow = nullptr;

    juce::MessageManager::callAsync ([safe]
    {
        if (auto* window = safe.getComponent())
            delete window;
    });
}

// v2.33 (roadmap idea 4): the Doctor handler - since v2.34 its trigger is
// the heartbeat button on the Rescan row. The report is built on
// the message thread (it takes the console-state and scan locks, which the
// audio thread never holds), echoed to the Terminal, saved as a text file
// next to the plugin database, and shown in a small NON-BLOCKING window
// (modal loops are off in a plugin - JUCE_MODAL_LOOPS_PERMITTED=0). Every
// failure posts an explicit status message instead of failing silently.
void TheChannelStripAudioProcessorEditor::showDoctorReport()
{
    const juce::String report = proc.buildDoctorReport();

    juce::Logger::writeToLog ("[TheChannelStrip] Doctor report:\n" + report);

    const auto dest = proc.getAppDataDir().getChildFile ("doctor-report.txt");

    if (! dest.getParentDirectory().createDirectory())
        proc.logHostStatus ("ERROR: Doctor report folder could not be created: "
                            + dest.getParentDirectory().getFullPathName());
    else if (! dest.replaceWithText (report))
        proc.logHostStatus ("ERROR: Doctor report could not be written: "
                            + dest.getFullPathName());
    else
        proc.logHostStatus ("Doctor report saved: " + dest.getFullPathName());

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "TheChannelStrip Doctor";
    options.content.setOwned (new DoctorPanel (report));
    options.componentToCentreAround = this;
    options.dialogBackgroundColour = juce::Colour (0xff141a26);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

// v2.33 -> v2.34: the logo's MouseListener - one click on the studio logo
// now opens the contact card (see showContactCard); the Doctor lives on
// its own heartbeat button since v2.34.
void TheChannelStripAudioProcessorEditor::LogoMouseListener::mouseDown (
    const juce::MouseEvent&)
{
    if (owner != nullptr)
        owner->showContactCard();
}

// ---------------------------------------------------------------------------
// v2.40.0: the editor's destructor lives HERE now (it used to sit right
// after the constructor): the contact card's desktop window is defined
// above, and closing it synchronously requires the complete type.
// ---------------------------------------------------------------------------
TheChannelStripAudioProcessorEditor::~TheChannelStripAudioProcessorEditor()
{
    stopTimer();
    // v2.8: closing the editor stops all measurements (LUFS meters and
    // spectrum analysers) until it is opened again.
    proc.setEditorOpen (false);
    // v2.31: the help bubble is a DESKTOP window, not a child - it must
    // be closed HERE, explicitly, before the editor tears down (it is
    // removed from the desktop while the editor is still alive, which
    // is always safe - this is no callback of its own). If the mode is
    // already off, exitHelpMode released it and a pending async
    // deletion owns it instead.
    if (helpBubble != nullptr)
    {
        helpBubble->removeFromDesktop();
        helpBubble.reset();
    }
    // v2.40.0: same for the contact card's desktop window - if it is up
    // at teardown, close it synchronously (hidden first, off the
    // desktop, delete immediately). A pending async deletion from
    // closeContactCard may still hold the SafePointer, which nulls
    // itself as the window dies.
    if (contactWindow != nullptr)
    {
        contactWindow->setVisible (false);
        contactWindow->removeFromDesktop();
        delete contactWindow;
        contactWindow = nullptr;
    }
    // v2.33: the Doctor listener must go before the members fall apart.
    studioLogo.removeMouseListener (&logoMouseListener);
    embeddedHostedEditor.reset(); // detach the hosted editor before anything else
    hostedViewport.setViewedComponent (nullptr, false);
}
