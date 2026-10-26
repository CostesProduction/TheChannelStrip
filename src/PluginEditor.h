#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// ---------------------------------------------------------------------------
// SpectrumPanel: log-frequency display of the input and output spectra with
// a selectable weighting tilt (-3 / -4.5 / -6 dB per octave) and analyzer
// ballistics (v2.10: ~1 s smoothing on the IN/OUT curves, plus a 10-second
// sliding-maximum hold trace on the output; v2.12: the time constant is
// selectable Fast/Slow from the sidebar). v2.6 adds the measured transfer
// function of the hosted plugin: OUT minus IN per bin (the tilt cancels in
// the difference, so that curve is tilt-independent). v2.12 makes that EQ
// curve PERSISTENT: a bin that has carried input energy keeps showing its
// last measured value when the program's spectrum moves away, instead of
// erasing itself piecewise.
//
// v2.8 rescales the whole panel around a -23 LUFS loudness reference:
// the vertical axis shows dB *relative to the (smoothed) input loudness,
// so "0" on the left axis means "-23 LUFS absolute" and a mix mastered
// around -23 LUFS reads with its mid-band roughly at the 0 line. Nine
// divisions, top = 3 x dB/div; the user picks 10 or 6 dB per division
// for the spectra and 4 or 2 dB per division (+/-4.5 divisions) for the
// EQ curve. v2.12: those two scales are toggled by CLICKING the
// analyser's own scale labels (left = spectrum dB/div, right green =
// EQ dB/div); the sidebar button rows are gone. v2.13: the loudness
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
    // v2.12: clicking the left dB scale column toggles the spectrum
    // range (10/6 dB per division) and clicking the right green EQ
    // label column toggles the EQ range (4/2 dB per division). The
    // editor wires the two std::function members below; clicks that
    // land in neither column are ignored.
    void mouseDown (const juce::MouseEvent& e) override;
    void setTilt (float newTiltDbPerOct);
    void setDbPerDiv (float dbPerDivision);   // v2.9: 10 or 6
    void setEqDbPerDiv (float dbPerDivision); // v2.8: 4 or 2
    void setSpecTau (float tauSeconds) { specTau = juce::jmax (0.05f, tauSeconds); } // v2.11
    // v2.13: loudness reference of the axis (0 dB line = this many LUFS
    // absolute). Changing it re-snaps the calibration immediately.
    void setLufsTarget (float targetLufs);

    // v2.12: scale toggle hooks (called when the user clicks one of the
    // two scale columns; see mouseDown).
    std::function<void()> onAnalyzerScaleClicked;
    std::function<void()> onEqScaleClicked;

    // v2.13: called when the user clicks the loudness-reference label
    // at the 0 dB line (cycles Record/Mix/Master).
    std::function<void()> onLufsTargetClicked;

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
    // v2.9: x position of a frequency on the 16 Hz - 24 kHz log axis
    float xForFrequency (double freq) const;
    // v2.13: the text drawn at the 0 dB line, e.g. "Mix (-23 LUFS)"
    juce::String lufsTargetLabel() const;

    // v2.9: the axis is 9 divisions deep, top = 3 x dB/div and bottom =
    // -6 x dB/div (both relative to the -23 LUFS reference), so the 0 dB
    // line stays at one third of the panel height in both scales.
    // v2.8 had a fixed +10 dB top; the new tops are +30 (10 dB/div)
    // and +18 (6 dB/div), bottoms -60 and -36.
    float topDb() const    { return 3.0f * dbPerDiv; }
    float bottomDb() const { return -6.0f * dbPerDiv; }
    // v2.8: the EQ curve spans +/- 4.5 divisions of its own scale.
    float eqRangeDb() const { return 4.5f * eqDbPerDiv; }

    TheChannelStripAudioProcessor& processor;
    float tilt = -4.5f; // dB/oct, SPAN convention: negative flattens a falling spectrum

    // v2.8: selectable scales (see setDbPerDiv / setEqDbPerDiv).
    float dbPerDiv = 10.0f;
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

    // v2.11: selectable IN/OUT smoothing time constant; v2.12 keeps two
    // speeds (Fast ~0.15 s, Slow ~1 s, the default).
    float specTau = 1.0f;

    // v2.13: loudness reference of the axis (0 dB line = this many LUFS
    // absolute); Record -37 / Mix -23 (default) / Master -14. Set from
    // the processor (persisted) and cycled by clicking the reference
    // label (see lufsTargetLabel/mouseDown).
    float lufsTargetDb = -23.0f;

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
//   | [F | S] |                                   |
//   | TILT    |                                   |
//   | (-3-4.5)|                                   |
//   | GAIN    |                                   |
//   | IN / OUT|                                   |
//   |Add +1.8 |                                   |
//   | status  |                                   |
//   | [Rescan]|                                   |
//   +---------+-----------------------------------+
//     sidebar (starts under the strip)    centre column
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

    // v1.0.4: renamed from "processor" to avoid shadowing the inherited
    // AudioProcessorEditor::processor member (-Wshadow-field).
    TheChannelStripAudioProcessor& proc;

    juce::Label titleLabel;
    juce::Label lufsInLabel, lufsOutLabel, correctionLabel;

    // v2.3: right settings column heading and the hosted-plugin name
    // shown in the left sidebar (under the console buttons).
    juce::Label gainLabel;
    juce::Label hostedNameLabel;
    // v2.14.1: channel-format diagnostic under the hosted console's name
    // ("track: stereo" / "track: mono (native)" / "track: mono (bridge)").
    juce::Label channelLabel;

    // v2.1: the correction readout is display-only and smoothed (tau ~1.5 s)
    // so it stays readable; it holds the last valid value while silent.
    float smoothedCorrectionDb = 0.0f;
    bool correctionEverValid = false;

    juce::TextButton rescanButton    { "Rescan" };

    // v2.16.1: last latency value logged to the Terminal (change logging
    // happens on the message thread, never in processBlock).
    int lastLoggedLatency = -1;

    std::array<juce::TextButton, (size_t) TheChannelStripAudioProcessor::numConsoles> consoleButtons;
    juce::Label tiltLabel { {}, "tilt" };
    // v2.12: the tilt and speed buttons are TextButtons styled like the
    // console buttons (radio behaviour via setClickingTogglesState). The
    // old ToggleButtons auto-shrank the wider "-4.5" label to fit, which
    // made its font visibly smaller than its neighbours.
    std::array<juce::TextButton, 3> tiltButtons; // -3, -4.5, -6 dB/oct

    // v2.12: analyser response speed - Fast/Slow IN/OUT smoothing
    // (tau ~0.15 / 1 s, stored as specTau in the processor). The old
    // three-speed row (Fast/Med/Slow) is back down to two buttons.
    std::array<juce::TextButton, 2> speedButtons;

    // v2.8: the "Display" toggle that hides or shows the analyser strip.
    // (v2.12: the SCALE dB/DIV and EQ dB/DIV button rows are gone - the
    // scales are toggled by clicking the analyser's own scale labels.)
    juce::ToggleButton analyserToggle { "Display" };         // show/hide the analyser strip

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

    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TheChannelStripAudioProcessorEditor)
};
