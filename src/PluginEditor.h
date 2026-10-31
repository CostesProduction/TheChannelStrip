#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// ---------------------------------------------------------------------------
// v2.53.1: the scale zoom buttons' own LookAndFeel (Clement's
// readability feedback on v2.53.0: "pas suffisamment lisible - la
// bulle pourrait etre plus grande et un peu plus rectangulaire").
// The default LookAndFeel's TextButton shape at 14 x 14 read as a dim
// little near-circle with a small grey +/- glyph, so v2.53.1 drew a
// clearly RECTANGULAR chip - a 2 px corner-radius body with a visible
// border, brightening on hover and press - and the +/- as THICK
// VECTOR STROKES (two crossing bars for the "+", one bar for the "-"),
// the same minimal-icon language as the preset buttons (v2.26.1).
//
// v2.53.2 (Clement's verdict on 2.53.1: "plus visibles merci, mais
// un petit peu moins abusif"): the chip became a GHOST. At rest the
// button draws NO background and NO border - just the thick +/-
// glyph at 55% alpha, as quiet as the scale numbers around it.
// The chip materialises only under the cursor: on hover (and press)
// a soft halo fills the rounded body and the glyph goes to full
// brightness, so the button is invisible until it is needed and
// unmistakable at the moment of clicking. A disabled chip (at its
// ladder's end, v2.53) rests at 25% alpha. The 22 x 16 click surface
// is unchanged - generous to aim at, invisible to look at.
// ---------------------------------------------------------------------------
class ZoomButtonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                const juce::Colour& /*backgroundColour*/,
                                bool shouldDrawButtonAsHighlighted,
                                bool shouldDrawButtonAsDown) override
    {
        // v2.53.2: the ghost draws nothing at rest - no fill, no
        // border. The soft halo appears only once the cursor is on
        // the button (or it is being pressed), tinted with the text
        // colour so it reads as a light glow, never as a hard box.
        if (! (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown))
            return;

        const float alpha = (b.isEnabled() ? (shouldDrawButtonAsDown ? 0.16f : 0.10f)
                                           : 0.05f);

        g.setColour (b.findColour (juce::TextButton::textColourOffId)
                         .withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (b.getLocalBounds().toFloat().reduced (1.0f), 3.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override
    {
        // v2.53.2: 55% at rest (a quiet label next to the scale
        // numbers), 100% under the cursor, 25% when disabled at the
        // ladder's end.
        const float alpha = b.isEnabled()
            ? ((shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown) ? 1.0f
                                                                        : 0.55f)
            : 0.25f;
        const auto area = b.getLocalBounds().toFloat();
        const float cx = area.getCentreX();
        const float cy = area.getCentreY();
        // the glyph: a bar as long as ~62% of the smaller dimension and
        // ~16% thick - thick enough to read at a glance, minimal enough
        // to match the preset icons
        const float arm = juce::jmin (area.getWidth(), area.getHeight()) * 0.62f;
        const float thick = juce::jmax (area.getHeight() * 0.16f, 2.0f);

        g.setColour (b.findColour (shouldDrawButtonAsDown
                                       ? juce::TextButton::textColourOnId
                                       : juce::TextButton::textColourOffId)
                         .withMultipliedAlpha (alpha));
        g.fillRect (juce::Rectangle<float> (cx - arm * 0.5f,
                                            cy - thick * 0.5f,
                                            arm, thick));

        if (b.getButtonText() == "+")
            g.fillRect (juce::Rectangle<float> (cx - thick * 0.5f,
                                                cy - arm * 0.5f,
                                                thick, arm));
    }
};

// ---------------------------------------------------------------------------
// SpectrumPanel: log-frequency display of the input and output spectra with
// a selectable weighting tilt (-3 / -4.5 / -6 dB per octave) and analyzer
// ballistics (v2.10: ~1 s smoothing on the IN/OUT curves, plus a 10-second
// sliding-maximum hold trace on the output; v2.12: the time constant is
// selectable Fast/Slow - since v2.52 the tilt and speed buttons are children
// of this panel itself, overlaid on its top-left corner, where the display
// they control lives). v2.6 adds the measured transfer
// function of the hosted plugin: OUT minus IN per bin (the tilt cancels in
// the difference, so that curve is tilt-independent). v2.12 makes that EQ
// curve PERSISTENT: a bin that has carried input energy keeps showing its
// last measured value when the program's spectrum moves away, instead of
// erasing itself piecewise.
//
// v2.8 rescales the whole panel around a -23 LUFS loudness reference:
// the vertical axis shows dB *relative to the (smoothed) input loudness,
// so "0" on the left axis means "-23 LUFS absolute" and a mix mastered
// around -23 LUFS reads with its mid-band roughly at the 0 line. Since
// v2.51 the grid is SIX divisions (seven lines), top = 2 x dB/div; the
// user picks 15/12/9 dB per division (zooms [+30;-60] / [+24;-48] /
// [+18;-36]) for the spectra and 8/6/4/2 dB per division (+/-24/18/12/6
// dB, +/- 3 divisions) for the
// EQ curve. v2.53: the two scales are zoomed by the panel's own +/-
// BUTTONS, overlaid on each scale column between the 0 line and the
// first division above / below it (v2.51-v2.52 cycled them by clicking
// the scale labels; the v2.12-v2.50 sidebar button rows are long gone).
// "+" magnifies - fewer dB per division - and "-" steps back out;
// v2.53 defaults: 12 dB/div analyser (zoom [+24;-48]) and 4 dB/div EQ
// (+/-12 dB), Clement's starting points. v2.13: the loudness
// reference itself is clickable too - the reference label at the 0 dB
// line cycles Record -37 / Mix -23 / Master -14 LUFS. A smoothed
// per-panel loudness offset (lufsOffsetDb) is subtracted when drawing
// the spectra; it cancels in OUT-IN, so the EQ curve is unaffected
// by it.
// ---------------------------------------------------------------------------
class SpectrumPanel : public juce::Component,
                      public juce::SettableTooltipClient, // v2.12.1: plain
                        // juce::Component does NOT inherit SettableTooltipClient
                        // (only Button, Label etc. do), so the panel needs it
                        // as an explicit base class for setTooltip() to exist.
                      private juce::Timer
{
public:
    explicit SpectrumPanel (TheChannelStripAudioProcessor& p);

    void paint (juce::Graphics& g) override;
    void resized() override; // v2.52: places the tilt / speed buttons;
                             // v2.53: places the scale +/- buttons too
    // v2.19 (roadmap point 3): hovering the analyser shows a readout of
    // the frequency under the cursor plus the IN / OUT / EQ levels at
    // that frequency. mouseMove just records the cursor and repaints;
    // mouseExit hides the readout again. No cursor lock, no freeze -
    // pure display.
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    // v2.31 (Clement's remark 2): the interactive help mode asks which
    // zone a point falls in so its bubble can document the two dB
    // scales and the LUFS reference individually. Same zones and
    // constants as mouseDown - the bubble always matches what a click
    // there would do. Pure and const: no state is touched.
    enum class HelpZone { graph, analyzerScale, eqScale, lufsReference };
    HelpZone helpZoneFor (juce::Point<int> localPos) const;
    void setTilt (float newTiltDbPerOct);
    void setDbPerDiv (float dbPerDivision);   // v2.51: 15, 12 or 9
    void setEqDbPerDiv (float dbPerDivision); // v2.51: 8, 6, 4 or 2
    void setSpecTau (float tauSeconds) { specTau = juce::jmax (0.05f, tauSeconds); } // v2.11
    // v2.13: loudness reference of the axis (0 dB line = this many LUFS
    // absolute). Changing it re-snaps the calibration immediately.
    void setLufsTarget (float targetLufs);

    // v2.52 (point B): the tilt and response-speed buttons moved from
    // the sidebar ONTO the analyser strip - Clement's layout: the
    // controls that only affect the display live where the display
    // lives, which frees two rows of the left panel for the console
    // workflow. They are children of this panel (constructed and wired
    // in its constructor, laid out in its resized()). These accessors
    // exist so the editor can register their interactive-help texts
    // (the help map is the single home of every explanation, v2.31).
    juce::TextButton* getTiltButton (int index);
    juce::TextButton* getSpeedButton (int index);

    // v2.53 (point B2): the scale zoom buttons. Four small TextButtons
    // overlaid on the analyser's own dB scale columns - the "+" and "-"
    // of the LEFT scale (spectrum) and of the RIGHT green scale (EQ) -
    // placed in the clear bands between the 0 line and the first
    // division above / below it, so the written labels stay untouched
    // (Clement's placement: "+" between 0 and the division above, "-"
    // between 0 and the division below, on both sides). "+" magnifies
    // (fewer dB per division), "-" steps back out; a button at the end
    // of its ladder is disabled. The state stays in the processor
    // (saved with the session, as since v2.51); the panel wires the
    // buttons in its constructor, restores them there and lays them
    // out in resized(). These accessors exist so the editor can
    // register their interactive-help texts and the offline harness
    // can check the ladder's endpoints (v2.31: the help map is the
    // single home of every explanation). Index 0 is "+", 1 is "-".
    juce::TextButton* getAnaZoomButton (int index);
    juce::TextButton* getEqZoomButton (int index);

    // v2.53 (point B2): the zoom ladders as PURE STATIC functions, so
    // the offline test harness can walk them without a visible editor.
    // The ladders themselves are the v2.51 ones, unchanged - only the
    // DEFAULT moved (v2.53: 12 and 4 dB/div). "+" = zoom IN (fewer dB
    // per division), "-" = zoom OUT; both clamp at their ladder's end.
    static float anaZoomIn (float cur);   // 15 -> 12 -> 9 (9 stays)
    static float anaZoomOut (float cur);  // 9 -> 12 -> 15 (15 stays)
    static float eqZoomIn (float cur);  // 8 -> 6 -> 4 -> 2 (2 stays)
    static float eqZoomOut (float cur); // 2 -> 4 -> 6 -> 8 (8 stays)

    // v2.53: read access to the panel's two display scales - so the
    // harness can verify that a panel constructed after the session
    // state was loaded restored the persisted zooms onto itself.
    float getDbPerDiv() const { return dbPerDiv; }
    float getEqDbPerDiv() const { return eqDbPerDiv; }

    // v2.19 (roadmap point 3): hover readout helpers - STATIC and free
    // of any component state so the offline test harness (TcsTests) can
    // exercise the frequency mapping and the value formatting without a
    // live editor. frequencyForX is the exact inverse of the private
    // xForFrequency (the same 16 Hz - 24 kHz log axis).
    static double frequencyForX (double x, double width);
    static juce::String formatFrequency (double freq);
    static juce::String formatReadoutDb (float db);

    // v2.12: scale toggle hooks (called when the user clicks one of the
    // two scale columns; see mouseDown).
    // v2.53: GONE - the click-to-cycle is replaced by the panel's own
    // +/- zoom buttons (see getAnaZoomButton / getEqZoomButton); the
    // remaining scale-column click action is the LUFS reference only.

    // v2.13: called when the user clicks the loudness-reference label
    // at the 0 dB line (cycles Record/Mix/Master).
    std::function<void()> onLufsTargetClicked;

    // v2.21: harness hooks. The offline test suite drives the panel's
    // analysis directly (no visible editor, no running timer) and
    // inspects the measured EQ curve bin by bin.
    void runAnalysisTick();
    // v2.49.0: the EQ curve the panel draws is still ONE curve - now
    // the PURE optimised H1 cross-spectrum member (Clement's A/B
    // build: "only your optimised H1 method, without my Max Hold
    // idea" - so he can compare it against the v2.48 max-hold
    // blends; see smoothEqVerite below).
    float getEqDbAtBin (int bin) const;
    bool isEqBinMeasured (int bin) const;

    // v2.54 (roadmap point A): the v2.46-v2.48 "EQ @ max" hooks are
    // GONE - the machinery was retired with its members (see the
    // note below).

    // v2.41: forget every smoothed trace (curves, EQ diff, max-hold
    // ring). Called when the analyser strip is hidden so a re-show
    // starts from a clean display - the 10 s max-hold would otherwise
    // keep pre-hide peaks alive long after the strip came back.
    void resetDisplay();

private:
    void timerCallback() override;
    void updateBallistics();
    void drawGrid (juce::Graphics& g);
    void drawCurve (juce::Graphics& g, const std::array<float, SpectrumAnalyzer::numScopeBins>& data,
                    juce::Colour colour, float lineThickness = 1.6f, bool withFill = true);
    void drawTransferCurve (juce::Graphics& g);
    float yForDb (float db) const;
    float yForEqDb (float db) const; // v2.8: dedicated EQ scale (+/- 4.5 divisions)
    float xForBin (int bin) const;
    // v2.9: the text drawn at the 0 dB line, e.g. "Mix (-23 LUFS)"
    juce::String lufsTargetLabel() const;
    // v2.9: x position of a frequency on the 16 Hz - 24 kHz log axis
    float xForFrequency (double freq) const;
    // v2.19 (roadmap point 3): the scope bin whose centre frequency is
    // closest to the hovered x position (256 bins, linear scan - the
    // analyser's bins are already log-spaced for display).
    int binForX (float x) const;
    // v2.19: the hover readout itself - a subtle vertical hairline at
    // the cursor and a fixed top-right label (frequency + IN/OUT/EQ
    // levels, colour-matched to the legend). Drawn last so it stays
    // readable above everything.
    void drawHoverReadout (juce::Graphics& g) const;

    // v2.51 (Clement's display rework): the axis is now SIX divisions
    // (seven lines) instead of nine, top = 2 x dB/div and bottom =
    // -4 x dB/div (both relative to the loudness reference), so the
    // 0 dB line stays at one third of the panel height and the exact
    // mid-height of the panel IS a grid line - the middle one - which
    // is where the EQ's symmetric scale puts its own zero (point 2:
    // both zeros sit on a line, on two DIFFERENT lines). The three
    // analyser zooms are 15/12/9 dB per division, i.e. [+30;-60],
    // [+24;-48] and [+18;-36] - all integer dB per division.
    float topDb() const    { return 2.0f * dbPerDiv; }
    float bottomDb() const { return -4.0f * dbPerDiv; }
    // v2.51: the EQ curve spans +/- 3 divisions of its own scale
    // (the full six-division grid), so its zero lands exactly on the
    // middle grid line. 8/6/4/2 dB per division = +/-24/18/12/6 dB.
    float eqRangeDb() const { return 3.0f * eqDbPerDiv; }

    TheChannelStripAudioProcessor& processor;
    float tilt = -4.5f; // dB/oct, SPAN convention: negative flattens a falling spectrum

    // v2.8: selectable scales (see setDbPerDiv / setEqDbPerDiv).
    // v2.53 defaults (Clement's starting points): 12 dB/div analyser
    // (zoom [+24;-48]) and 4 dB/div EQ (+/-12 dB). The ladders are
    // unchanged since v2.51 (15/12/9 and 8/6/4/2); only the default
    // moved, and a session saved by any earlier build keeps its own
    // explicit values (the processor only defaults when nothing was
    // saved).
    float dbPerDiv = 12.0f;
    float eqDbPerDiv = 4.0f;
    // v2.8.1: calibrated offset between the FFT bins' own scale and the
    // display's loudness-referenced axis (0 dB = -23 LUFS): the measured
    // gap between the spectrum's energy-weighted mean and (LUFS + 23).
    // Slow EMA so the grid does not breathe; held while silent.
    float lufsOffsetDb = 0.0f;
    bool lufsOffsetSnapped = false; // snap to the first valid reading

    std::array<float, SpectrumAnalyzer::numScopeBins> smoothIn;
    std::array<float, SpectrumAnalyzer::numScopeBins> smoothOut;
    std::array<float, SpectrumAnalyzer::numScopeBins> smoothDiff; // v2.6: measured OUT-IN

    // v2.12: the EQ curve is persistent. smoothDiff keeps its last
    // value when the input in a bin drops below the gate, and this
    // array remembers which bins have ever been measured - the curve
    // is drawn for exactly those bins, so it no longer erases itself
    // piecewise when the program's spectrum changes. It starts empty
    // (like smoothDiff) and resets with the panel, on editor reopen.
    std::array<bool, SpectrumAnalyzer::numScopeBins> eqMeasured;

    // -----------------------------------------------------------------
    // v2.47.0 -> v2.49.0. ONE EQ curve, always - but WHICH estimator
    // it reads is the ongoing A/B between Clement's max-hold family
    // and my H1 cross-spectrum method:
    //  - v2.47: blend 2/3 H1 (tau 5 s) + 1/3 max anchor;
    //  - v2.48: the max-hold blend - the mean of the 5 s and 2 s
    //    "EQ @ max" windows, with the H1 as the junk-fallback member
    //    ("j'ai l'impression que les formes issues des courbes en
    //    maximum hold sont beaucoup plus propres");
    //  - v2.49 (THIS BUILD, Clement's call): the PURE optimised H1
    //    member alone - "j'aimerais voir ce que ça donne seulement
    //    ta suggestion (vs les blend)" - so the two truths can be
    //    compared on his console, with his ears and eyes.
    //
    //   EQ = smoothDiff, the H1 cross-spectrum estimate
    //        20*log10|Sxy/Sxx|, in every band it ever measured
    //
    // The H1 method, as optimised across the versions (all of it
    // still in the code below or in the TransferAnalyzer): complex
    // cross-spectrum accumulation that cancels the console's
    // uncorrelated floor BY CONSTRUCTION (v2.22), the tau ~5 s
    // energy-weighted EMAs with the deterministic flush + parameter
    // relay so knob and console changes re-converge in ~1 s (v2.47),
    // the Savitzky-Golay 31-tap frequency smoothing (v2.44), the
    // fractional bin sampling below 213 Hz (v2.41), the v2.12
    // persistence and the relative input + coherence gates. The
    // light final one-pole (tau ~0.3 s) keeps the display continuous.
    //
    // v2.54 (roadmap point A): the max-hold members were retired
    // with their whole machinery (see the note near maxHoldFrames);
    // if the blend family must ever come back, git reverts v2.54.
    // Honest trade, for the
    // A/B: a knob DOWN now converges down in ~1 s (no max to hold
    // it), junk is rejected everywhere by construction (no
    // raw-coherence routing), and on a saturating console the H1
    // reads the energy-weighted small-signal transfer rather than
    // the peak-drive response - the shapes Clement found less clean
    // in v2.47.
    // -----------------------------------------------------------------
    std::array<float, SpectrumAnalyzer::numScopeBins> smoothEqVerite {};
    std::array<bool, SpectrumAnalyzer::numScopeBins> eqVeriteMeasured {};

    // v2.22: the EQ value itself (20*log10|H|, the H1 cross-spectrum
    // estimate) and its coherence now come from the TransferAnalyzer
    // on the audio thread, which accumulates the cross-spectrum Sxy
    // COMPLEX per band - the random-phase console floor cancels out of
    // that average, so it no longer votes on the curve (the phantom
    // +3 dB rise above a console lowpass). The panel only applies the
    // validity gates (relative input gate + coherence >= 0.25) and a
    // light display EMA on the already-averaged value; the v2.21
    // per-bin power EMAs are gone, along with the per-frame dB
    // averaging artefacts they existed to tame.

    // v2.10: 10-second sliding maximum of the raw output, as a thin
    // overlay trace on the OUT curve. The ring buffer stores the last
    // 300 raw frames (10 s at 30 Hz); maxOut is recomputed from it at
    // every timer tick (300 x 256 comparisons - trivial on the UI
    // thread). It resets when the editor is closed and reopened (the
    // panel is recreated with it).
    static constexpr int maxHoldFrames = 300;
    std::array<float, SpectrumAnalyzer::numScopeBins> maxOut;
    std::vector<float> maxRing;
    int maxRingIndex = 0;

    // -----------------------------------------------------------------
    // v2.54 (roadmap point A, Clement's call): the v2.46-v2.48
    // "EQ @ max" machinery (the 5 s and 2 s sliding-max windows,
    // their gates, rings and measured flags) is RETIRED. Since the
    // v2.49 pure-H1 A/B call it was computed at every display tick
    // but never drawn - ~210 jmax/bin/tick of pure waste. Git keeps
    // it: reverting to the blend family is a revert of v2.54, not a
    // rewrite. The v2.10 OUTPUT max-hold (maxOut, maxRing, 300
    // frames) stays - it IS drawn.
    // -----------------------------------------------------------------

    // v2.11: selectable IN/OUT smoothing time constant; v2.12 keeps two
    // speeds (Fast ~0.15 s, Slow ~1 s, the default).
    float specTau = 1.0f;

    // v2.13: loudness reference of the axis (0 dB line = this many LUFS
    // absolute); Record -37 / Mix -23 (default) / Master -14. Set from
    // the processor (persisted) and cycled by clicking the reference
    // label (see lufsTargetLabel/mouseDown).
    float lufsTargetDb = -23.0f;

    // v2.19 (roadmap point 3): hover state - the last cursor position
    // inside the panel and whether it is inside at all. UI thread only,
    // written by mouseMove/mouseExit and read in paint.
    juce::Point<int> hoverPos;
    bool mouseInside = false;

    // -----------------------------------------------------------------
    // v2.52 (point B): the display controls that used to live in the
    // sidebar's analyser section - the three tilt buttons (-3 / -4.5 /
    // -6 dB/oct, radio group 2) and the two response-speed buttons
    // (Fast / Slow, radio group 5). They now overlay the panel's own
    // top-left corner: clear of the left scale column (x <= 44, whose
    // clicks cycle the spectrum zoom), of the LUFS reference label at
    // the 0 dB line and of the centred legend - see resized() for the
    // exact geometry and the collision budget. TextButtons styled like
    // the console buttons (the v2.12 lesson: all three tilt labels
    // render at the SAME font size; the old ToggleButtons shrank the
    // wider "-4.5"). The persisted state stays in the processor
    // (tiltDb, specTau - saved with the session exactly as before);
    // the panel restores and re-canonicalises both at construction.
    // -----------------------------------------------------------------
    juce::Label tiltCaption { {}, "tilt" };
    std::array<juce::TextButton, 3> tiltButtons; // -3, -4.5, -6 dB/oct
    std::array<juce::TextButton, 2> speedButtons; // Fast / Slow

    // -----------------------------------------------------------------
    // v2.53 (point B2): the scale zoom buttons - "+" and "-" for the
    // LEFT spectrum scale and for the RIGHT EQ scale. Children of the
    // panel like the v2.52 tilt/speed row, but placed along the two
    // scale columns: each button sits in the clear band between the
    // 0 line and the first division above / below it (band heights are
    // zoom-independent constants of the six-division grid, so the
    // geometry holds for every ladder value - see resized() for the
    // exact collision budget against the label boxes). TextButtons
    // styled like the console buttons, disabled at their ladder's end.
    // -----------------------------------------------------------------
    std::array<juce::TextButton, 2> anaZoomButtons; // 0 = "+", 1 = "-"
    std::array<juce::TextButton, 2> eqZoomButtons;  // 0 = "+", 1 = "-"

    // v2.53.1 -> v2.53.2: the zoom buttons' GHOST LookAndFeel (the
    // class above) - owned here as a member so it outlives the
    // buttons that use it.
    ZoomButtonLookAndFeel zoomButtonLaF;

    // v2.53: refreshes the four zoom buttons' enabled states against
    // the current scales (a button at its ladder's end is disabled).
    void refreshZoomButtons();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumPanel)
};

// ---------------------------------------------------------------------------
// HostedEditorHolder: centers the embedded plugin editor inside the viewport
// (scrollbars appear when the plugin GUI is larger than the available area).
// ---------------------------------------------------------------------------
class HostedEditorHolder : public juce::Component
{
public:
    void paint (juce::Graphics& g) override;
};

// ---------------------------------------------------------------------------
// TheChannelStripAudioProcessorEditor (v2.5 layout: full-width analyser,
// then a sidebar and the plugin column underneath (no top bar))
//
//   +---------------------------------------------+
//   | ===== analyser strip, FULL window width ==== |
//   +---------+-----------------------------------+
//   | TITLE   |                                   |
//   | [SSL E] |      embedded hosted editor,      |
//   | [SSL G] |      framed exactly (auto-fit)     |
//   | [SSL J] |                                   |
//   | [A9099] |                                   |
//   | [A200]  |                                   |
//   | [FOCUS] |                                   |
//   | [BX N]  |                                   |
//   | name    |                                   |
//   | GAIN    |                                   |
//   | IN / OUT|                                   |
//   |Add +1.8 |                                   |
//   | status  |                                   |
//   | [Rescan]|                                   |
//   +---------+-----------------------------------+
//     sidebar (starts under the strip)    centre column
//
// v2.52 (point B): the analyser display controls (TILT -3/-4.5/-6 and
// the Fast/Slow response speed) live ON the analyser strip itself now,
// as a button row overlaid on its top-left corner - the sidebar's
// analyser section keeps only the show/hide toggle.
//
// v2.5: the analyser strip spans the whole window width at the top; the
// condensed sidebar and the hosted plugin column start underneath it. The
// window auto-fits the hosted editor once embedded, so the centre column
// matches the plugin's width exactly and nothing scrolls.
//
// v2.8: the sidebar also carried the analyser options (spectrum scale,
// EQ curve scale, Display toggle). v2.12 removes the two scale button
// rows - the scales are toggled by clicking the analyser's own scale
// labels (see SpectrumPanel::mouseDown) - leaving only the "Display"
// toggle, which hides or shows the analyser strip entirely (the window
// re-fits, smaller without the strip). Measurements only run while this
// editor exists.
// ---------------------------------------------------------------------------
// v2.30: the interactive help mode's overlay (defined in the .cpp) -
// full-editor, on top, transparent; it shows the hover bubbles and
// swallows every click (see enterHelpMode).
class HelpOverlay;
// v2.31 (Clement's remark 3): the mode's bubble rides in its own
// always-on-top DESKTOP window (also defined in the .cpp) - the hosted
// plugin's native NSView sits above every JUCE-drawn layer, so only a
// window of its own can float in front of it.
class HelpBubbleWindow;
// v2.40.0: the contact card's always-on-top desktop window (defined in
// the .cpp, like the bubble's) - it covers the plugin's on-screen
// rectangle, dims it with a scrim, and catches the outside click.
class ContactCardWindow;

class TheChannelStripAudioProcessorEditor : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit TheChannelStripAudioProcessorEditor (TheChannelStripAudioProcessor&);
    ~TheChannelStripAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshConsoleButtons();
    void refreshLufs();
    void tryEmbedHostedEditor();
    void layoutHostedHolder();
    void fitWindowToHostedEditor(); // v2.4: frame the hosted editor exactly
    void applyAnalyserVisibility(); // v2.8: show/hide the analyser strip
    // v2.30: the interactive help mode (see HelpOverlay in the .cpp).
    // "?" enters it: from then on hovering ANY control shows a small
    // contextual bubble (its own words - a few of them), and the
    // hovered control is outlined. ANY click anywhere, or Esc, leaves
    // the mode - a click in the mode never triggers the control
    // underneath. Replaces the v2.20 all-in-one tooltip window: the
    // help comes to the control instead of a wall of text.
    void enterHelpMode();
    void exitHelpMode();
    // v2.30: the bubble text for a hovered component (the deepest
    // component, or one of its parents, that carries a help entry).
    // v2.31: the help texts live ONLY here now (Clement's remark 1):
    // no control carries a standing tooltip any more - not even the
    // ? button, whose half-cut standing hint went away in v2.31.1 -
    // so the standing TooltipWindow stays quiet outside the mode.
    // The editor-local cursor position makes the analyser
    // position-aware (remark 2): its three clickable zones - the
    // left dB scale, the right EQ scale and the LUFS reference at
    // 0 dB - each return their own text.
    juce::String helpTextFor (juce::Component* component,
                              juce::Point<int> positionInEditor);
    // v2.30: the overlay drives the mode end-to-end (Esc/click exits,
    // hover picks the text) - it touches the private members above.
    friend class HelpOverlay;
    // v2.31: the bubble window's own click leaves the mode too, so it
    // needs exitHelpMode as well (it touches nothing else private).
    friend class HelpBubbleWindow;
    // v2.40.0: the contact card's window closes itself through
    // closeContactCard (outside click, Close button, Esc) - it touches
    // nothing else private.
    friend class ContactCardWindow;
    // ---- v2.33 (roadmap idea 4) -> v2.34: the Doctor and the contact card ----
    // The DOCTOR (v2.33) builds the processor's diagnostic report, shows
    // it in a small non-blocking window, writes it to a file next to the
    // plugin database and echoes it to the Terminal. Since v2.34 its
    // trigger is the heartbeat icon button on the Rescan row - the logo
    // moved on. Message thread only; the audio path is never touched.
    void showDoctorReport();
    // v2.34: the logo's new click target - a small card with Clement's
    // contact details (mixing & mastering engineer, mail, website),
    // one click to copy the mail or open the site. Static text, no
    // processor state, message thread only.
    void showContactCard();
    // v2.40.0: closes the contact card's desktop window (outside click,
    // Close button, Esc, or a resize that makes its geometry stale).
    void closeContactCard();
    // Routes the logo's click to the contact card. juce::ImageComponent
    // has no mouseDown override, and keeping the logo's drawing
    // untouched is preferable to a subclass: a MouseListener attached
    // to it in the constructor (and detached in the destructor) does
    // exactly that, without a friend declaration or layout change.
    struct LogoMouseListener : public juce::MouseListener
    {
        TheChannelStripAudioProcessorEditor* owner = nullptr;
        void mouseDown (const juce::MouseEvent&) override;
    } logoMouseListener;
    // ---- v2.26 (roadmap point 4): named presets, sidebar section ----
    // (re)populate the preset combo box from the files on disk
    void refreshPresetList();
    // v2.27: mirror the hosted console's current preset in the combo
    // box (selection + "*" prefix when its knobs have moved)
    void syncPresetComboSelection();
    // the combo selection changed -> load that preset
    void onPresetSelected();
    // v2.29: the floppy -> native save dialog (like a Save As), the
    // name pre-filled with the current preset's, then proc.savePresetTo
    // - the console's current state can be saved anywhere; saving into
    // the root of the preset folder makes it the current preset
    void savePresetToFile();
    // the bin -> with a selection: confirmation, then proc.deletePreset;
    // v2.29: with nothing selected -> browse-to-delete below
    void confirmDeletePreset();
    // v2.29: the browse-to-delete half of the bin - pick any
    // .tcsPreset file (sub-folders included), confirm, then
    // proc.deletePresetFile
    void browseDeletePresetFile();
    // the incoming-arrow tray -> native open dialog, then proc.importPreset
    void importPresetFile();

    // v1.0.4: renamed from "processor" to avoid shadowing the inherited
    // AudioProcessorEditor::processor member (-Wshadow-field).
    TheChannelStripAudioProcessor& proc;

    juce::Label titleLabel;
    juce::Label lufsInLabel, lufsOutLabel, correctionLabel;

    // v2.3: right settings column heading and the hosted-plugin name
    // shown in the left sidebar (under the console buttons).
    juce::Label gainLabel;
    juce::Label hostedNameLabel;
    // v2.34: the merged status block under the hosted console's name -
    // one label, two lines ("track: stereo · crossfade: idle" /
    // "latency: N · sentinel: armed") fed by proc.getSidebarStatusLines().
    // v2.33's separate integrity bandeau and the v2.14.2 channel readout
    // collapsed into it (zero redundancy: "shell: unity" was a constant
    // fact and lives in the Doctor report only).
    juce::Label integrityLabel;

    // ---- v2.26 (roadmap point 4): the presets sidebar section ----
    // One combo box listing every preset in the preset folder - the
    // ROOT and the ALBUM sub-folders alike (v2.30: albums are listed
    // too, the entry text shows the album), then a row of four
    // buttons: three icon buttons (v2.29: floppy = save the current
    // state to a file anywhere, bin = delete the selection or browse,
    // tray with incoming arrow = load a .tcsPreset file - no text, no
    // tooltips: the help mode documents the section) and the "?" help
    // button (v2.30: enters the interactive help mode). The combo
    // selection loads the preset (switching consoles if needed).
    // v2.27: the combo mirrors the hosted console's CURRENT preset
    // ("*" when its knobs have moved); v2.29: the section sits at the
    // TOP of the sidebar, right under the shell's title.
    // v2.26.1: the icon look-and-feels MUST be declared before the
    // buttons they are attached to, so they are destroyed after them.
    // v2.34: a 4th slot draws the Doctor's heartbeat trace.
    std::array<std::unique_ptr<juce::LookAndFeel>, 5> presetButtonLaFs;
    juce::Label presetLabel;
    juce::ComboBox presetCombo;
    juce::TextButton savePresetButton;
    juce::TextButton deletePresetButton;
    juce::TextButton importPresetButton;
    // the combo entries, refreshed by refreshPresetList (itemId is
    // index+1 into this array)
    juce::Array<TheChannelStripAudioProcessor::PresetInfo> presetList;
    // v2.27: what the timer last mirrored into the combo box - the
    // sync only runs when the hosted console, its current preset or
    // its "*" state actually changed.
    juce::String lastSyncedPresetIdent;
    juce::String lastSyncedPresetName;
    bool lastSyncedPresetModified = false;
    // v2.26: the FileChoosers must outlive their async callbacks.
    std::unique_ptr<juce::FileChooser> presetFileChooser;

    // v2.42.1 (Clement's remark): a settled average still flutters between
    // two adjacent display values when the 30 Hz timer reformats the label
    // every tick. The readout now updates at a calm 1 Hz cadence (v2.42.2,
    // his preference) and only when the value actually moved (0.2 dB
    // deadband) - see refreshLufs.
    // (v2.1-v2.41 kept an editor-side 1.5 s dB EMA here; v2.42 moved the
    // smoothing into the meters - see LufsMeter - so the editor only
    // formats the value.)
    float shownCorrectionDb = 0.0f;
    bool correctionShownValid = false;
    int correctionTickCount = 0;

    juce::TextButton rescanButton    { "Rescan" };

    // v2.34: the Doctor icon button (a heartbeat trace), riding the
    // Rescan row at the bottom of the sidebar. The logo's click moved
    // to the contact card.
    juce::TextButton doctorButton;

    // v2.34: the preset-name marquee - the combo's own text field
    // truncates long names, so the editor paints the current name over
    // the combo (opaque fill, clicks pass through to the combo beneath)
    // and scrolls it slowly in a loop when it does not fit. All state
    // lives on the message thread, advanced by the editor's 30 Hz timer.
    juce::String marqueeText;
    float marqueeOffset = 0.0f;
    int marqueePhase = 0;     // 0 = scrolling left, 1 = holding on the tail
    int marqueeHold = 0;      // pause ticks (~3 s) at each end
    juce::Rectangle<int> getMarqueeZone() const;
    void tickPresetMarquee();
    void paintOverChildren (juce::Graphics&) override;

    // v2.16.1: last latency value logged to the Terminal (change logging
    // happens on the message thread, never in processBlock).
    int lastLoggedLatency = -1;

    // v2.24 (point 7): cached colour of the status line (red on ERROR:,
    // amber on WARNING:, grey otherwise) - the label is only touched when
    // the colour actually changes.
    juce::Colour lastStatusColour { 0xff8f9bb0 };

    std::array<juce::TextButton, (size_t) TheChannelStripAudioProcessor::numConsoles> consoleButtons;

    // v2.52 (point B): the tilt and response-speed buttons moved from
    // this sidebar ONTO the analyser strip itself - they are children
    // of SpectrumPanel now (see that class). The v2.12 lessons travel
    // with them: TextButtons styled like the console buttons so all
    // three tilt labels render at the SAME font size, and the
    // persisted state stays in the processor (tiltDb, specTau).

    // v2.8: the "Display" toggle that hides or shows the analyser strip.
    // (v2.12: the SCALE dB/DIV and EQ dB/DIV button rows are gone - the
    // scales are toggled by clicking the analyser's own scale labels.)
    // v2.34.1 (Clement's suggestion): the "Display" text toggle became a
    // wide icon button (a small spectrum glyph) - since v2.52 it is the
    // ONLY control of the sidebar's analyser section (the tilt and
    // speed rows moved onto the strip, so the freed vertical space
    // breathes into the block gaps).
    juce::TextButton analyserToggle;          // show/hide the analyser strip

    SpectrumPanel spectrumPanel;

    // v2.12: the project writes tooltips on several controls but never
    // had a TooltipWindow, so none of them could ever display. One
    // instance, parented to the editor, activates them all (Rescan,
    // tilt, speed, the gain-staging readout and the analyser's scale
    // labels).
    juce::TooltipWindow tooltipWindow;

    juce::Viewport hostedViewport;
    HostedEditorHolder hostedHolder;
    std::unique_ptr<juce::AudioProcessorEditor> embeddedHostedEditor;
    juce::AudioProcessor* lastEmbeddedInstance = nullptr;

    // v2.17: Clement's studio logo (embedded as a binary resource),
    // shown between the gain-staging section and the host status area.
    juce::ImageComponent studioLogo;

    // v2.20 -> v2.30: the "?" button now rides the presets button row
    // (the 4th slot freed by the removed export button) and ENTERS the
    // interactive help mode instead of opening a text window.
    juce::TextButton helpButton { "?" };

    // v2.30: the help-mode overlay while it is on (see enterHelpMode).
    std::unique_ptr<HelpOverlay> helpOverlay;

    // v2.40.0: the contact card's desktop window while it is up (see
    // showContactCard). A RAW pointer, not a unique_ptr: the editor
    // methods defined before the window's class in the .cpp (resized,
    // the destructor pre-v2.40 location) only compare and null it.
    ContactCardWindow* contactWindow = nullptr;

    // v2.31 (remark 3): the help bubble's own always-on-top desktop
    // window - a component drawn inside the editor would hide BEHIND
    // the hosted plugin's native view. It is NOT a child of the
    // editor, so the destructor closes it explicitly, before the
    // editor tears down.
    std::unique_ptr<HelpBubbleWindow> helpBubble;

    // v2.30 -> v2.31: the help-mode bubble texts (since remark 1 the
    // ONLY home of every sidebar and analyser explanation - the
    // controls carry no standing tooltips any more; since v2.31.1
    // not even the ? button keeps one). Filled in the
    // editor's constructor, read only by the help overlay.
    std::map<juce::Component*, juce::String> helpTexts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TheChannelStripAudioProcessorEditor)
};
