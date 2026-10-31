# TheChannelStrip

**Version 2.53.1** — VST3 / AU / Standalone (macOS, Windows, Linux)

TheChannelStrip is a **console-strip shell**: it *hosts* your own licensed
bx_console plugins (SSL 4000 E, SSL 4000 G, SSL 9000 J, AMEK 9099,
AMEK 200, Focusrite SC, bx_console N) plus the Lindell channel strips
(Lindell 80 Channel, Lindell 50 Channel) through the public JUCE hosting
APIs, and wraps
them in a clean, focused workflow:

- **Nine fixed slots** — one button per console, stacked in the **left
  sidebar** with short labels (A9099, SSL E, SSL G, SSL J, Bx N,
  Lindell N, Lindell A, Focusrite, A200). Clicking a
  button instantiates that plugin; buttons for plugins that are not
  installed stay clickable and dimmed, and clicking one posts the exact
  situation to the status area. Selection is exclusive (radio
  behaviour), one console at a time, and the sidebar shows the hosted
  console's name.
- **Named presets** — a PRESETS section at the **top of the sidebar,
  right under the title** (v2.37.0),
  lists every preset in the preset folder, root and albums alike:
  the **root is the general list** (« Console - Name »), and each
  **sub-folder is an album**, its presets listed with an
  « [Album] » tag. Selecting a preset **loads it on its console**
  (switching to that console first if another one is hosted). Three
  actions around the list: **Save** (the floppy) is a *Save As* —
  it writes the hosted console's **current state** to a
  `.tcsPreset` file **wherever you choose**, and that file (root,
  album or elsewhere) becomes the console's **current preset**
  right away, pre-filled in the dialog; **Load** (the tray with
  the incoming arrow) loads any `.tcsPreset` from anywhere
  **without copying it** — the preset stays where it lives and
  becomes the current one at its own file; **Delete** (the bin)
  removes the selected preset, or — with nothing selected — opens a
  browser to delete any preset file, sub-folders included (always
  with confirmation). The current preset's own **file** travels
  with the DAW session, so an album preset reloads as that album
  preset. A
  preset holds the **console's own state only** — display settings
  (tilt, scales, LUFS reference) travel with the session, not with
  presets. Presets live as individual `.tcsPreset` files in the
  `Presets/` folder next to the plugin database, so they survive
  reinstalls and can be copied by hand.
- **Interactive help** — the « **?** » button (fourth slot of the
  preset-button row) enters a **help mode**: the interface dims
  slightly, and **hovering any documented control shows a small
  contextual bubble** — a few words each, including the hidden
  clickables (the scale +/− zoom buttons, the LUFS reference at
  0 dB). A
  highlighted outline marks the target under the mouse. **Any
  click, or Esc, leaves the mode** — a click never triggers the
  control underneath. The old all-text help window is gone.
- **Click-free swapping** — switching consoles crossfades over ~50 ms and
  the old instance is destroyed on a background thread.
- **Spectrum analyzer** — input and output spectra overlaid on the same
  display, shown as a strip across the **entire window width** at the top
  (the sidebar only starts underneath it),
  with your choice of **display tilt: -3, -4.5 (default) or
  -6 dB/octave** (reference 1 kHz, SPAN convention: a negative tilt
  flattens a naturally falling spectrum, so -4.5 makes a typical mix read
  roughly flat). The tilt only affects the drawing, never the analysis.
  **v2.8 loudness-referenced axis**: the vertical scale is dB *relative to
  a selectable loudness reference* — the "0" line (annotated, slightly
  brighter) means that many LUFS absolute, and a smoothed
  loudness-anchored calibration (v2.8.1, fixed
  in v2.8.4) keeps a mix played at the reference LUFS reading with its
  energy-dominant bands right at the 0 line; volume changes move the
  curve accordingly. **v2.13: clicking the reference label at the 0 line
  cycles three presets — Record (-37 LUFS), Mix (-23 LUFS, the default)
  and Master (-14 LUFS).**
  **v2.51: the scale is six divisions deep (seven lines) with the top
  at 2 x dB/div, so the 0 line sits one third of the panel height from
  the top and the panel's mid-height line is the EQ's own zero.**
  **v2.53: the small + and − buttons beside each scale zoom it** —
  **15, 12 or 9 dB per division** on the left spectrum scale
  (+30 to -60 dB, +24 to -48 dB, +18 to -36 dB), without disturbing
  the written labels (each button sits between the 0 line and the
  first division above or below it). The horizontal axis is a clean log scale
  from **16 Hz to 24 kHz** (v2.9: the data-free edges centre the useful
  band and keep the labels clear of the panel borders; no value written
  at the 16 Hz left edge) with a graduation at every 20/30/.../90,
  100/200/.../900, 1k/.../9k, 10k, 20k tick and labels on the classic
  frequencies (20, 100, 200, 1k, 2k, 10k, 20 kHz).
  **v2.19 hover readout**: moving the mouse over the analyser draws a
  subtle vertical hairline at the cursor and a fixed top-right readout
  with the frequency under the cursor and the IN / OUT / EQ levels
  there, colour-matched to the legend — exactly the values the curves
  show at that position (same tilt and loudness calibration; bins with
  no energy read "--", and EQ only appears once that bin has been
  measured). Pure display, no cursor lock and no freeze. **v2.20**: the
  IN curve is drawn without its area fill (only OUT keeps its tint),
  each display bin now aggregates the ENERGY of the FFT bins it covers
  (the treble reads where reference analysers put it, and both curves
  get one light spatial smoothing pass), the EQ scale labels sit flush
  at the far right edge with the readout keeping a clear margin, and
  the "?" button next to the studio logo opens every tool tip in one
  window.
- **Measured transfer function (v2.6)** — a third curve, **OUT-IN**, shows
  what the hosted console actually does to your signal (EQ, dynamics and
  saturation combined), computed live from the two existing analyzer
  spectra. It costs nothing on the audio thread, is independent of the
  display tilt (which cancels in the difference), and is only drawn in
  the frequency ranges where the input has energy. v2.7 gives it a
  **dedicated scale** (classic EQ display range, labelled on the
  right edge, faint 0 dB anchor line) so EQ moves are easy to read, plus
  a slow input-energy-weighted smoothing (~0.4 s at full energy) so the
  curve stays stable and readable. v2.51 makes that scale a four-step
  zoom: **+/-24, +/-18, +/-12 or +/-6 dB** (8/6/4/2 dB per division),
  from
  the sidebar. **v2.21 made it a measurement-grade estimate** (power-
  domain averaging of the band energies plus a Smaart-style relative
  validity gate). **v2.22 upgrades it to the full dual-FFT method**:
  a dedicated cross-spectrum analyzer on the audio thread accumulates,
  per band, the input/output auto-spectra AND the complex cross-spectrum
  Sxy; the curve is the H1 estimate 20*log10|Sxy/Sxx|, and a
  **coherence gate** (gamma^2 = |Sxy|^2 / (Sxx*Syy) >= 0.25, Smaart's
  coherence blanking) decides whether a band carries a real transfer.
  Why: with a console lowpass engaged (SSL J LP), the output band is
  the attenuated signal PLUS the console's own noise floor - and once
  the floor exceeds the attenuated signal, an honest power ratio
  Pout/Pin measures a *gain* that is not a transfer (the +3 dB rise
  above 10 kHz on LP-filtered material). The complex Sxy accumulation
  cancels the random-phase floor over frames, so the curve now reads
  the true rolloff deep under the floor and simply stops (holds its
  last value) where the signal sinks into it, instead of rising back.
- **Display toggle (v2.8)** — a "Display" checkbox in the sidebar hides or
  shows the analyzer strip entirely; the window re-fits (smaller without
  the strip), so the space is genuinely freed. The choice is saved with
  the session.
- **Zero-cost when closed (v2.8 / v2.41)** — all measurements (LUFS meters
  and both spectrum analyzers) run **only while the plugin window is open**,
  and since v2.41 the spectrum analyzers additionally stop while the
  analyser strip is hidden. With the editor closed (or the strip hidden)
  the audio thread does nothing but the console itself, so a mixer full
  of instances carries no analysis overhead.
- **LUFS gain staging (display)** — the sidebar continuously
  shows, in LUFS-derived dB, how much to raise (+) or lower (-) the sound
  so the hosted console's output matches its input loudness. The value is
  a **5 s power-domain exponential average** (averaged the way loudness is
  meant to be averaged - quiet passages no longer weigh as much as loud
  ones, which both steadies the readout on dynamic material and removes
  its small low bias), gated to ignore silence: it needs ~1 s of signal to
  appear, holds through short gaps, and falls back to "--" after 3 s of
  silence. You apply it by hand on the hosted plugin's own output knob.
  The label itself repaints at a calm 1 Hz with a 0.2 dB deadband, so a
  settled value never flutters between two adjacent digits - it only
  moves when the correction genuinely drifts.
- **Embedded hosted UI** — the hosted plugin's own editor is embedded in a
  viewport that fills the centre column below the analyzer, so the guest
  plugin is actually visible and usable. The layout is a **condensed left
  sidebar + centre column, nothing else** (v2.4): the sidebar reads as
  one column of four blocks with an identical vertical space between
  them (v2.39.0) — (1) the shell's title, PRESETS and the nine console
  buttons pinned at the top; (2) the frame-free Costes logo (which
  opens a contact card) with the ANALYSER section right under it (its
  toggle carries the heading: "ANALYSER" and the spectrum glyph share
  the button — since v2.52.0 it is the section's only control: the
  tilt and response-speed buttons live on the analyser strip itself); (3) GAIN STAGING
  (LUFS/delta readouts); (4) the hosted name with its alarm zone glued
  to the Rescan / Doctor row at the bottom; the centre column is used only by the
  analyzer strip and the hosted plugin itself. The frame around
  everything is minimal (5 px margins and gutters). The **window
  auto-fits the hosted editor** once it is embedded (and re-fits when
  you switch consoles, since their GUI sizes differ): the analyzer
  strip gets the plugin's full width and nothing needs to scroll. The
  window stays resizable (860×970 minimum with the analyzer shown,
  860×790 with it hidden;
  below the auto-fit size the viewport simply scrolls again).
- **Latency forwarding** — the hosted plugin's reported latency is passed
  on to the DAW, and re-forwarded whenever the plugin changes it at
  runtime (e.g. toggling oversampling inside the Lindell channels), so
  delay compensation stays correct during playback.
- **Gain-reduction forwarding (v2.15.5, Studio One)** — when the hosted
  console reports its gain reduction through PreSonus'
  `IGainReductionInfo` extension, TheChannelStrip forwards it to the host
  so **Studio One displays the console's gate + compressor gain reduction
  on the channel meter**, exactly as with the console inserted directly.
  The chain is a pure 1:1 pull (the host asks the wrapper, the wrapper
  asks the console): no polling thread, no audio-thread cost, and 0.0 dB
  when nothing is hosted or the console does not expose the extension.

## Legal

TheChannelStrip is an **independent tool**, not affiliated with, endorsed
by, or connected to Solid State Logic, Brainworx, Plugin Alliance, or any
other plugin manufacturer. It contains **no code, circuits, presets or
material from any third-party plugin** — it only loads plugins you own,
through their public hosting interfaces. All code in this repository is
100% original. Trademarks belong to their respective owners.

## Building

Requirements: CMake ≥ 3.22, a C++17 compiler (Xcode/AppleClang on macOS,
Visual Studio on Windows, GCC/Clang on Linux). JUCE 8.0.6 is downloaded
automatically via CMake FetchContent on first configure.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

To use a local JUCE checkout instead of downloading:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DJUCE_SOURCE_DIR=/path/to/JUCE
```

Outputs land in:

```
build/TheChannelStrip_artefacts/Release/VST3/TheChannelStrip.vst3
build/TheChannelStrip_artefacts/Release/AU/TheChannelStrip.component   (macOS)
build/TheChannelStrip_artefacts/Release/Standalone/TheChannelStrip.app (macOS)
```

Install the VST3 to `~/Library/Audio/Plug-Ins/VST3/` and the AU to
`~/Library/Audio/Plug-Ins/Components/`, then rescan in your DAW.

## Tests

The build also produces **TcsTests**, a console test harness that
exercises the processor end to end with synthetic material and mock
hosted plugins — no third-party plugin is needed:

```bash
./build/TcsTests_artefacts/Release/TcsTests          # Linux
./build/TcsTests_artefacts/Release/TcsTests.app/Contents/MacOS/TcsTests   # macOS
```

It prints one line per test and one `FAIL` per failed check, exits 0
when everything passes (`ALL TESTS PASSED`). Covered: processor basics
and console inventory, bit-exact dry passthrough (stereo and mono),
measurement gating, LUFS short-term metering, spectrum peak location
(1 kHz and 10 kHz),
the hover readout mapping and formatting, state save/restore, latency forwarding (static and runtime changes),
click-free console swapping, native mono vs stereo-bridge hosting, and
stereo ↔ mono hot switching without re-creating the instance.
The **v2.32 suite** adds a **realtime-safety audit** (the harness
replaces the global `operator new`/`operator delete` pair with a
counting sentinel and proves every steady-state `processBlock` branch —
stereo with the editor open and closed, native mono, the mono bridge
and no console hosted — runs **allocation-free** after warm-up) and
**torture tests** (a 50-swap console storm mid-playback with a full
clear in the middle, sample-rate / block-size / track-format churn on
a never-recreated instance, and hostile session states — garbage
bytes, empty, truncated, huge valid-XML fakes — fed during playback).

Pass `--integration` to additionally host a **real** console found in the
saved plugin database (launch the Standalone once first so a scan has
run):

```bash
./build/TcsTests_artefacts/Release/TcsTests --integration
```

The integration mode loads a third-party module inside the test binary.
In the DAW the shell never does that (scan children are separate
processes), so here a hostile plugin can only kill a throwaway test
process; treat a crash there as a plugin problem, not a shell bug.

## Usage

1. **Scan** — the editor automatically scans the standard VST3 folders on
   first launch (`/Library/Audio/Plug-Ins/VST3` and
   `~/Library/Audio/Plug-Ins/VST3` on macOS). The scan runs **out of
   process**, so a hostile plugin can only kill the helper, never the app.
   The *Rescan* button forces a fresh scan.
2. **Pick a console** — click one of the console buttons. It is
   instantiated on a background thread, then crossfaded in over 50 ms.
   Disabled buttons mean the plugin was not found on this machine.
3. **Watch the spectrum** — the IN and OUT curves are overlaid below the
   header. The **tilt** (-3 / -4.5 / -6 dB/oct) and **Fast / Slow**
   response-speed buttons sit on the analyser strip's own top-left
   corner; the small **+ and − buttons** beside the **left scale** zoom
   the spectrum (15 / 12 / 9 dB per division), the ones beside the
   **right green scale** zoom the EQ curve's range
   (+/-24 / 18 / 12 / 6 dB), and the
   ANALYSER toggle in the sidebar hides or shows the whole analyzer
   strip. All of it is remembered in the session state.
4. **Gain-stage** — play audio; the big readout shows the smoothed
   correction (e.g. "+1.8 dB" means raise the console's output by 1.8 dB,
   "-0.6 dB" means lower it). Adjust the console's output knob by hand until
   the readout approaches 0.0 dB; it holds its last value when playback stops.

### Tips

- Some plugins verify their hosting context and refuse to open inside
  another plugin. The **Standalone** build of TheChannelStrip is a useful
  workaround for loading such picky guests.
- Swap consoles freely during playback: the crossfade makes the switch
  inaudible.
- Launch the Standalone binary directly from a terminal
  (`TheChannelStrip.app/Contents/MacOS/TheChannelStrip`) to see hosting
  messages (`[TheChannelStrip] ...`, scan progress, instantiation errors).

## Data files

Scan results and the scanner pedal file are stored in:

```
~/Library/Application Support/TheChannelStrip/scanned_plugins.xml
~/Library/Application Support/TheChannelStrip/scan_pedal.xml
```

(Windows: `%APPDATA%\TheChannelStrip\` — Linux: `~/.TheChannelStrip/`.)

Delete `scanned_plugins.xml` to redo a full scan.

## Changelog

- **2.53.1**
  - **Readability pass on the analyser's scale controls (Clement's
    feedback on 2.53.0: the +/− chips were too small and not legible
    enough).** The zoom chips grew from 14×14 squares to **22×16
    rectangles** — wider than tall, easier to click — and get their
    own LookAndFeel: a clearly rectangular chip (2 px corner radius,
    visible border, brighter on hover and press) instead of the
    default's near-circular dim button, with the **+ and − drawn as
    thick vector strokes** (the preset-buttons' icon language)
    instead of small font glyphs; a chip disabled at its ladder's
    end dims to 40% so the state reads at a glance.
  - **The dB numbers of both scales are bold** — and the spectrum's
    left column is a notch brighter, the EQ's green gains alpha —
    the numbers read at a glance now. The labels' positions and the
    chips' clear bands between the written numbers are unchanged
    (still no disturbance of the écritures).
- **2.53.0**
  - **The scale zooms are now small + and − buttons, one step per
    press (Clement's point B2).** The v2.51 click-to-cycle on the scale
    columns is gone: the **left spectrum scale** carries its own **+**
    and **−** (15 / 12 / 9 dB per division) and the **right green EQ
    scale** its own pair (8 / 6 / 4 / 2 dB per division, i.e.
    +/-24 / 18 / 12 / 6 dB). **+ magnifies, − steps back out**; a
    button at the end of its ladder is disabled, so a step is always
    one honest click. The buttons sit in the clear bands **between
    the 0 line and the first division above / below it** — the written
    labels are untouched, and the geometry is zoom-independent (the
    six-division grid keeps the 0 line at one third of the height
    whatever dB/div is shown).
  - **New starting points, Clement's choice:** the spectrum opens on
    **[+24;−48] (12 dB per division)** and the EQ on **+/-12 dB
    (4 dB per division)**. The ladders themselves are unchanged since
    v2.51; only the defaults moved, and a session saved by an earlier
    build keeps its own zooms exactly.
  - The buttons are children of the analyser panel (the v2.52
    tilt/speed pattern): created, wired, laid out and **restored from
    the session in the panel itself**, disabled states included. The
    offline harness covers the ladders end to end (14 checks), the
    new defaults and the restore/enabled-state guarantees —
    **26 new checks, 361 total.**
- **2.52.0**
  - **The analyser's tilt and response-speed controls moved onto the
    analyser strip itself (Clement's layout request).** The three tilt
    buttons (**-3 / -4.5 / -6 dB/oct**) and the **Fast / Slow** response
    speed buttons now sit as a small row overlaid on the analyser's
    top-left corner — clear of the clickable left scale column, of the
    LUFS reference label and of the centred legend at every window
    width. The controls that only affect the display live where the
    display lives.
  - **The sidebar's analyser section keeps only the show/hide toggle**,
    and the freed rows (77 px of vertical space) now breathe into the
    sidebar's block gaps — the left panel keeps its room for the
    console workflow. The window's minimum height floor follows the
    freed space (970 → 893 with the strip, 790 → 713 without).
  - No behaviour change otherwise: the buttons are the same radio-group
    TextButtons as before, the persisted state is the same (`tiltDb`,
    `specTau`, saved with the session — any legacy value still snaps to
    a button), the help mode documents them as before, and the analyser
    measurement chain is untouched.

- **2.51.1**
  - **Hotfix: crash of Studio Pro (PreSonus/Fender) 8.1.2 at session
    load.** The host aborted with `POINTER_BEING_FREED_WAS_NOT_ALLOCATED`
    in `operator delete` on a JUCE `LambdaThread`: two concurrent
    `instantiateHosted` runs (session restore racing the end-of-scan
    restore) both pushed to the hosted plugin's `relayedParameters`
    vector, and the double push corrupted the buffer into a double
    free. The race was latent since the relay rework (v2.47) - v2.51.0
    merely changed the timing at session load enough to expose it.
  - **Every `instantiateHosted` call now runs under one instantiation
    lock** (`instantiateGuard`), so only one hosted console is ever
    being built / swapped at a time - across the scan thread, the
    restore path and the console-selection `Thread::launch`.
  - **Superseded-request checkpoints.** A console selection that was
    replaced mid-build (e.g. rapid switching while a heavy plugin
    constructs) now aborts cleanly at three checkpoints instead of
    installing a plugin nobody asked for anymore.
  - **The relay vector itself is now guarded** (`relayGuard` in
    HostedStrip) around attach/detach, so even future call paths
    cannot corrupt the parameter relay.
  - **Duplicate restores are skipped**: a restore with nothing left to
    apply while a console is already hosted no longer tears the strip
    down and rebuilds it.
  - **Scan/restore deadlock fix**: `runScan` releases the scan lock
    *before* triggering the pending restore, so the documented lock
    order `instantiateGuard` -> `scanLock` can never deadlock.
  - No user-facing change: parameters, presets, sessions and the
    analyser display are untouched; sessions saved by 2.51.0 load as
    they are. Test suite extended with superseded-guard,
    duplicate-restore-skip and relay thread-safety stress tests.

- **2.51.0**
  - **Analyser display rework (Clement's four-point request).** The
    horizontal grid went from ten lines (nine divisions) to **seven
    lines (six divisions)**: with the new zoom ladder every scale is
    an integer dB per division again, the panel is calmer, and the
    exact mid-height IS a grid line - which is where the EQ's
    symmetric scale puts its zero. The analyser's 0 dB line keeps its
    spot one third from the top, so **both zeros now sit on grid
    lines, on two different lines**; the EQ-zero line carries a
    discrete green tint (the EQ curve's colour family) so the eye
    pairs it with the green right-hand scale.
  - **Analyser zooms (the + and − buttons on the left scale):**
    [+30;-60]
    at 15 dB/div, [+24;-48] at 12 dB/div, [+18;-36] at 9 dB/div -
    the old two-scale toggle (10/6 dB per division) is gone.
  - **EQ zooms (the + and − buttons on the right green scale):**
    +/-24,
    +/-18, +/-12 then +/-6 dB (8/6/4/2 dB per division over the
    full six-division grid). The old +/-9 dB scale is gone.
  - **Legacy sessions map to the same visual range on load:**
    analyser 10 dB/div -> 15 ([+30;-60] unchanged), 6 -> 9
    ([+18;-36] unchanged); EQ 4 dB/div (was +/-18) -> 6, 2 -> 2.
    The remap is keyed on the session's own version tag, so a 2.51
    session saving 4 (meaning +/-12 dB) is never mistaken for the old
    4 (which meant +/-18 dB). Covered by a new offline test alongside
    the roundtrip.
- **2.50.0**
  - **The low-end comb is fixed - the Savitzky-Golay smoothing now
    runs on the EQ dB curve, not on the accumulated spectra, and the
    averaging EMA is weighted by each band's own energy.** Clement's
    report on 2.49: a ~12 dB peak-to-peak comb in the very low end
    (20-200 Hz) exactly where the console's low cut buries the
    output. The offline benchmark built for the investigation (19
    estimator variants, 5 test programmes x 4 filters x clean / -85 dB
    console noise, analytic ground truth) traced it to the v2.44
    smoothing pass: on bursty bass the accumulators of neighbouring
    bands differ by ORDERS OF MAGNITUDE, so the ratio of smoothed
    values was biased towards the loud neighbours' ratio - 9.2 dB
    RMS / 23.5 dB max error against the truth in the 60-213 Hz zone
    (the measured teeth matched the screenshot at ~11 dB peak-to-
    peak), while the same H1 estimator with the kernel moved AFTER
    the ratio reads 0.53 dB RMS / 1.1 dB max - better than the
    offline Welch ceiling (1.2 / 3.3 dB) and than the v2.48 max-hold
    blends (1.6 / 4.0). The energy-weighted EMA (aEff = alpha * min
    (1, sxx/Sxx) on all four accumulators) holds the estimate
    between the bass notes instead of letting it decay into the
    floor - correct for a static transfer, and the flush still
    restarts everything on a real console change. The displayed
    coherence is now the honest raw-accumulator value too (the 0.25
    gate sees the undiluted dip of a narrow uncorrelated artifact;
    the v2.48 raw-coherence hook carries the same number). New
    regression test: a mock console whose 24 dB/oct low cut IS the
    benchmark's filter A, a bursty bass programme, and the measured
    curve asserted against the filter's analytic response in the
    comb zone - the old code fails it by roughly an order of
    magnitude.
- **2.49.0**
  - **The single EQ curve is now the PURE optimised H1 method -
    Clement's A/B build: "j'aimerais que tu implémentes seulement ta
    méthode H1 optimisée par toi, sans mon idée de Max Hold.
    J'aimerais voir ce que ça donne seulement ta suggestion (vs les
    blend)".** The drawn curve, the hover readout and the harness
    hooks all read the cross-spectrum H1 estimate
    20*log10|Sxy/Sxx| **alone** - the exact member, with the same
    gates and display chain, that v2.48 read in its junk-fallback
    bands, so the comparison against the 2.48 max-hold blends is
    clean: same estimator, no max votes. The method, as optimised
    across the versions: complex cross-spectrum accumulation that
    cancels the console's uncorrelated floor BY CONSTRUCTION (the
    v2.22 junk rejection - no raw-coherence routing patch needed,
    it is the estimator itself); the tau ~5 s energy-weighted EMAs
    with the deterministic flush + parameter relay (v2.47), so a
    knob or console change re-converges within ~1 s in BOTH
    directions; the Savitzky-Golay 31-tap frequency smoothing
    (v2.44); the fractional bin sampling below 213 Hz (v2.41); the
    v2.12 persistence and the relative input + coherence gates.
  - **What changed vs 2.48, honestly.** A knob DOWN now converges
    down within ~1-2 s - the flush restarts the estimators and the
    fast post-flush EMA carries the new response, where the max
    members used to HOLD the old value up to 5 s (the glide). Junk
    is rejected everywhere by construction (2.48 needed the
    raw-coherence 0.75 patch to route junk-dominated bands to this
    very estimator). And on a saturating console the H1 reads the
    energy-weighted small-signal transfer rather than the response
    at the programme's peak drive - the shapes Clement found less
    clean in v2.47 ("je pense que je préfère les courbes issues de
    maximum hold"). That trade is exactly what this build puts on
    the table: the two truths, side by side across versions, for
    his ears and eyes on his console. The v2.47 reference simulation
    (soft-saturation mock, ground truth at two drive levels)
    measured the pure tau-5 s H1 at ~0.9 dB median band wander with
    a ~1.1 s settle on a knob turn - the numbers behind what he
    saw in 2.47.
  - **The max-hold family is NOT deleted.** Both "EQ @ max" windows
    (5 s and 2 s) are still computed at every display tick, with
    their own gates and their direct harness hooks, and the v2.46
    trace test is unchanged - they simply no longer vote on the
    drawn curve, so going back to the blend family (or building a
    third candidate from both) is a one-block change in
    updateBallistics. The RAW coherence hook stays public and
    computed in the TransferAnalyzer for the blend builds; this
    build does not fetch it.
  - **Updated harness test.** The EQ-curve test now exercises the
    pure H1 end to end at the real 30 Hz panel cadence: flat 0 dB
    in passthrough, reacting within ~1 s to a knob turned through
    the REAL hosted parameter (relay -> flush -> fast EMA), and -
    the new discriminator, the assertion that proves "sans Max
    Hold" - re-converging DOWN within ~2 s of a knob-down, where
    the blend would still hold the old +6 dB peak in its 5 s max
    windows; plus the unchanged v2.12 persistence after the signal
    stops.
- **2.48.0**
  - **The EQ curve's truth is now the MAX-HOLD family (Clement's
    verdict after living with v2.47: "j'ai l'impression que les formes
    issues des courbes en maximum hold sont beaucoup plus propres...
    la vérité est une moyenne du maximum hold long terme et d'un
    maximum hold plus court terme").** The single curve is now the
    mean of two "EQ @ max" windows: the v2.46 5-second maximum and a
    new 2-second maximum, both dB MaxOut - dB MaxIn over sliding
    maxima of the smoothed IN/OUT curves - for a linear console the
    exact transfer at the programme's peak drive, the fixed operating
    point that reads clean and stays stable on a saturating console.
    The pair is not a guess: the v2.48 reference simulation (the same
    soft-saturation mock and two-level ground truth as v2.47) raced
    Clement's three candidate pairs - (5 s; 1 s), (5 s; 2 s),
    (5 s; 300 ms) - and the 2 s pair won: median band wander 2.57 dB
    peak-to-peak (vs 2.54 for the 5 s window alone - the pair inherits
    its stability), settle on a knob turn ~1.3 s WITHOUT any flush
    (the max only ever moves up), and the best all-band fidelity of
    the three blends (0.66 dB). The 1 s pair was eliminated - its
    short ring catches isolated burst spikes in weak bands (14.2 dB
    excursions once blended) - and the 300 ms pair trades fidelity
    (0.89 dB) for speed the 2 s pair already delivers.
  - **The v2.22 junk rejection survives inside a max-hold curve.** A
    dB difference measures EVERYTHING the console put in the band -
    where a console lowpass sinks the signal under the console's own
    floor, the maxima would show the v2.21.1 phantom boost again. The
    TransferAnalyzer now also exposes the RAW per-band coherence
    (before the Savitzky-Golay window that dilutes a narrow artifact's
    dip from 0.50 to 0.93), and wherever it says a band's output is
    not explained by its input (below 0.75) the max members do not
    vote: the bin falls back to the cross-spectrum H1 member, which
    rejects uncorrelated junk by construction. g2 is self-normalising,
    so an honest band reads ~1 from the first frame after a flush and
    the fallback never lingers on a clean console.
  - **Everything still speaks the single curve.** The hover readout,
    the harness hooks (getEqDbAtBin / isEqBinMeasured) and the drawn
    line are the same blend; both max windows keep their own gate and
    the 5 s one keeps its direct hooks (getEqMaxDbAtBin), so the v2.46
    trace test is unchanged - the ballistics contract (flat in
    passthrough, immediate up, holding down) is the same. The v2.47
    flush and parameter relay stay (they keep the H1 fallback member
    honest across a knob turn). Updated harness test: the max-hold
    blend flat in passthrough, reacting within ~1 s to a knob turned
    through the REAL hosted parameter, and persisting after the
    signal stops.
  - **Honest costs, kept in the code comments.** On a knob DOWN the
    maxima hold their old value until the carrying passage exits the
    window (up to 5 s / 2 s), which reads as the curve gliding down
    (the v2.46 contract, now at full weight); and a band whose honest
    content is dominated by distortion products (low raw coherence,
    real signal-dependent content) conservatively reads the
    linear-part H1 transfer instead of the max. The v2.47
    energy-weighted H1 blend remains in the code as the fallback
    member and its estimators - retired from the display by
    Clement's call, not deleted.
- **2.47.0**
  - **ONE EQ curve - the "EQ-Vérité" (Clement's design call: "no
    engineer accepts cross-reading several curves; one curve that
    estimates the truth is enough").** The two traces of v2.46 are
    gone from the display; the panel now draws a single line:
    2/3 cross-spectrum H1 transfer (averaging window lengthened to
    tau ~5 s) + 1/3 max anchor (the v2.46 "EQ @ max" machinery,
    demoted from a second curve to the anchor third). The weights
    are not a guess: the v2.47 reference simulation (soft-saturation
    mock, bursty programme, ground truth measured at two drive
    levels) tested every candidate - the production 1/8 window, the
    fast/slow H1s, the max alone, the 4-curve average, the 2-curve
    averages - and 2/3 slow + 1/3 max had the best median wander of
    all the blends that keep a sub-second knob response (2.2 dB
    peak-to-peak vs 4.3 dB for the v2.46 production settings), zero
    staircase steps, and fidelity to the exact energy-weighted
    response of the whole programme within ~0.5 dB. A bin is drawn
    once EITHER member has measured it, the weights renormalise
    over the valid members, and the v2.12 persistence is untouched:
    the curve stays on screen when the signal stops.
  - **The long window without the long wait: the deterministic
    flush.** A tau ~5 s window alone would take ~9 s to read a knob
    turn. The shell now KNOWS when the console's response changed:
    an AudioProcessorParameter listener attached to every parameter
    of the hosted plugin (and the console swap/clear paths) calls
    TransferAnalyzer::requestFlush, and the estimators restart with
    a fast alpha (1/2 for the first 8 frames, ~1.4 s) before the
    long window takes over again. No detection, no thresholds - the
    v2.45 double-EMA dead end (a knob step is indistinguishable from
    the wave noise) is exactly what this avoids: the knob event is
    observable, the signal is not parsed. Thread-safe by design (one
    atomic store), the relay detaches before any plugin retires, and
    the audio-thread cost is unchanged to the flops - the flush
    itself is four array fills consumed once per event.
  - **Why the window had to grow.** The v2.46 1/8 window (~1.4 s)
    only ever sees one operating point of a saturating console, so
    the honest programme-driven waves (the bypass test: flat and
    immobile in bypass on the same dynamic music) moved the curve
    band by band. The 5 s window spans the bursts AND the quiet
    bed and settles on the energy-weighted mixture the ear actually
    hears: median band wander 0.9 dB in the simulation (vs 4.3 dB
    at 1/8) and the best fidelity of every estimator tested.
  - **Readout, help and anchor all speak the single curve.** The
    hover readout's EQ value, the harness hooks (getEqDbAtBin /
    isEqBinMeasured) and the drawn line are the same blend; the max
    machinery keeps its own hooks (getEqMaxDbAtBin) so the anchor
    logic stays directly testable, and the v2.46 trace test is
    unchanged - it still passes (the anchor's ballistics contract
    is the same). New harness test: EQ-Verite flat in passthrough,
    knob turned through the REAL hosted parameter (the full relay ->
    flush path) reacting within ~1 s, and persistence after the
    signal stops.
  - **Honest costs, kept in the code comments.** The max anchor has
    no coherence rejection (a console lowpass sinking the signal
    under the console floor reads the floor - the v2.21.1 artifact,
    at 1/3 weight and outvoted by the H1 member); on a knob down
    the anchor holds its old value until the carrying passage exits
    the 5 s window, which the blend reads as the curve gliding down
    instead of a dashed line squatting high. The 4-curve and plain
    2-curve averages were rejected on the simulation's numbers:
    a blend of correlated members dilutes, it does not filter.
- **2.46.0**
  - **The "EQ @ max" trace (Clement's idea: dB MaxOut - dB MaxIn).**
    A second EQ curve, drawn dashed next to the solid transfer
    curve, computed from 5-second sliding maxima of the smoothed
    IN and OUT curves. For a linear console it is the EXACT
    transfer: the max is homogeneous (max (H*x) = H * max (x)) and
    the IN/OUT display one-poles are identical, so the difference
    is the response at the programme's PEAK DRIVE - a fixed,
    repeatable operating point. That is what finally answers the
    waves: Clement's bypass test proved they are the console's own
    programme-dependent response (asymmetric L/R channels summed
    to mono + level-dependent saturation), an honest measurement
    no averaging can remove. The max trace fixes the operating
    point instead of averaging over it: stable by construction,
    transients count (the attack IS the max), immediate on a knob
    up, holding up to 5 s on a knob down (the window Clement
    chose). Gated like the transfer curve by a relative input
    criterion (a band's 5 s max input within 45 dB of the
    programme's strongest max band, floor -75 dB), smoothed with
    the same Savitzky-Golay pass, and entirely display-side (UI
    thread, 30 Hz, zero audio-thread cost). Documented cost: no
    coherence rejection - where a console lowpass sinks the signal
    under the console's own floor the held max Out eventually
    reads the floor; the coherence-gated cross-spectrum curve
    (solid) remains the reference measurement.
  - **v2.45's slow average reverted (alpha 1/32 -> 1/8).** The
    bypass test settled it: in bypass the EQ curve is a flat,
    immobile line on the same dynamic music, so the waves were
    never estimator variance - the x4 slower average bought
    nothing perceptible and cost the knob reactivity Clement
    wants (~2 s settle restored). The v2.45 experiment and its
    documented dead ends (the undetectable knob-vs-wave
    discriminator; the exhausted frequency-kernel family) are
    kept in the code comments for the record.
- **2.45.0**
  - **The programme-driven waves: longer averaging, not another
    kernel.** After 2.44.0's Savitzky-Golay pass Clement still saw
    slow 1-2-octave waves riding the EQ curve - but only on real
    dynamic music, never on stationary pink noise. That signature
    identified the mechanism: the waves are the ESTIMATOR VARIANCE on
    intermittent energy (a band only measures while its instruments
    play; each burst leaves a slightly different energy-weighted
    estimate behind, so the curve wanders with the programme), not a
    frequency-domain smoothing problem. A 1-2-octave wave is locally
    cubic: every kernel that could remove it would also eat a genuine
    Q1 EQ peak, so the kernel family is exhausted - the only
    remaining discriminator is time (a console response is fixed,
    variance waves move with the music).
  - **The dead end, documented.** A per-band deviation detector (a
    fast EMA watching for the slow display EMA to be overtaken, as a
    "tracking" mode that keeps fast knob response while averaging
    long) was simulated on a 12-instrument bursty reference mock: a
    real +6 dB Q1 knob step measures only ~+2.9 dB at band level
    (energy-weighted aggregation dilutes the peak), while the
    steady-state deviation noise is ~1.2 dB rms - the detector cannot
    tell a knob from a wave. It was dropped.
  - **The fix: the cross-spectrum EMA now averages over ~32 frames
    (~5.4 s) instead of ~8 (~1.4 s).** Same estimator, same kernel,
    one constant. On the bursty mock the typical band's wave drops
    from 2.67 dB peak-to-peak to 0.19 dB (x14 calmer, invisible on
    the display); the random-phase cancellation of console junk in
    the accumulated complex cross-spectrum only improves with more
    frames.
  - **Documented price:** a knob turn now needs ~5-10 s to fully
    settle (was ~2 s) - Clement's accepted trade: the calmest
    possible curve even if a control takes longer to stabilise. On
    the harshest simulated mock the settle is 9.4 s; on real music
    (bands energised far more often) it lands at the low end of that
    range. Warmup is unchanged: the ratio estimates are
    self-normalising, so a band becomes measurable as soon as it
    carries energy.
- **2.44.0**
  - **The "lissage malin" (Clement's framing: display the smart
    smoothing of the measured points, not the raw points).** The 256
    display bands are 256 independent (log f, dB) measurements, and
    the v2.43.x binomial kernels - tuned against band-scale jitter -
    still let slow half-octave waves through: a 13-tap binomial
    cascade passes 76% of a half-octave-period wave, and the eye
    tracks those as the "vaguelettes" Clement kept seeing on real
    material. The smoothing is now a Savitzky-Golay cubic fit over a
    31-band window (~1.2 octave), applied - like before - to the four
    accumulated cross-spectra (Sxx, Syy, SxyRe, SxyIm) before H and
    the coherence are computed: each band's EMAs are replaced by the
    cubic least-squares fit of its neighbourhood, so flat curves,
    slopes and smooth curvatures pass through exactly (the kernel
    sums to 1 and preserves polynomials up to degree 3), while the
    half-octave wave drops to a 27% residual - 2.8x calmer than the
    cascade. Reference simulation on the exact regression scenario:
    band-to-band jaggedness 0.086 -> 0.053 dB max, 0.028 -> 0.022 dB
    rms (raw: 1.35 / 0.58). Honesty preserved: a Q1 EQ peak loses
    0.04 dB, a Q2 peak 0.30 dB, a Q4 peak 0.93 dB (narrow features
    are averaged over ~1.2 octave - that is the price of a wave-free
    curve and it is documented), a steep lowpass flank max error is
    1.17 dB, a narrow single-band uncorrelated artifact now reads as
    0.93 coherence instead of 0.50 (the window dilutes its dip with
    coherent neighbours; the EQ itself still ignores it, and
    broadband junk such as a console noise floor - the gate's real
    job - is unaffected), the HP shape and convergence speed are
    unchanged, and a
    +6 dB knob turn still settles in the same ~3.2 s (the kernel is
    time-invariant; no lag is added). ~32k flops per analysis frame
    (one FIR pass over 4x256 doubles), zero allocations. Coherence
    is still recomputed from the same smoothed spectra, and smoothed
    Sxx stays strictly positive (min ratio 0.25 in the worst notch
    scenario, no blow-up).
- **2.43.1**
  - **Goodbye, "vaguelettes" (Clement's remark on the real Lindell 80
    session).** The v2.43.0 smoothing had killed the fine band-to-band
    jitter, but small waves were still visible on real material - the
    per-frame estimates wobble a little with the program, and the eye
    tracks that as gentle ripples. The smoothing kernel now runs three
    cascaded passes of the same 5-tap binomial (the cascade convolves
    into a 13-tap binomial spanning ~half an octave), and the display's
    one-pole doubles its time constant to 0.5 s so the residual glides
    instead of shimmering. Reference simulation on the exact regression
    scenario: band-to-band jaggedness 0.27 -> 0.08 dB max, 0.12 ->
    0.04 dB rms (3.3x calmer than v2.43.0); the raw curve measures
    1.35 dB there. Honesty preserved: a Q1 EQ peak loses 0.02 dB, a
    tight Q2 peak 0.06 dB, a steep lowpass flank does not move, and
    the coherence is still recomputed from the same smoothed spectra.
    A +6 dB knob turn still settles in the same ~3.2 s (the wider
    kernel adds no lag; the calmer display pole adds ~0.3 s). ~15k
    flops per analysis frame, zero allocations. The one documented
    cost: a pathological Q8/-18 dB notch (no console EQ is that
    narrow) fills ~3 dB more than before.
- **2.43.0**
  - **A smooth EQ curve (Clement's "courbe brute" remark).** Every display
    band is an independent H1 measurement, so on real material the raw
    curve jitters along the frequency axis - the estimators' residual
    variance, not the console's response (a real console response is
    smooth in frequency). The four accumulated cross-spectra
    (Sxx, Syy, SxyRe, SxyIm) are now frequency-smoothed over the
    neighbouring bands with a 5-tap binomial kernel [1 4 6 4 1] / 16
    (~1/6 octave) BEFORE H and the coherence are computed, which keeps
    the physics honest: H = Sxy/Sxx stays the exact H1 estimator of the
    energy-weighted average transfer over the neighbourhood, and the
    coherence gate is recomputed from the same smoothed quantities.
    Reference simulation: the band-to-band jitter drops ~10x (max
    2.0 -> 0.2 dB), a Q8/-18 dB notch fills by only ~0.7 dB, a steep
    lowpass flank does not move, and because the neighbours' EMAs are at
    the same temporal state, NO lag is added - knob turns settle exactly
    as fast as before. ~5k flops per analysis frame, zero allocations.
- **2.42.2**
  - **1 Hz readout (Clement's call).** The calm-correction cadence of
    2.42.1 settles at **1 Hz** instead of 2 Hz: sound is dynamic and the
    delta does move, so let those real changes come slowly - at most one
    visible change per second, still under the 0.2 dB deadband (which
    stays the part that kills the rounding flutter). A genuine drift now
    shows within a second instead of half a second. One-constant change,
    display-only.
- **2.42.1**
  - **A calm correction readout.** The "Add x dB" label flutters between
    two adjacent values ("3.2" / "3.1" / ...) when the 30 Hz editor timer
    reformats it every tick (Clement's remark after the first v2.42.0
    session). Lowering the frame rate alone would only slow the flutter;
    the label now reconsiders its text at **2 Hz** and repaints only when
    the value moved at least **0.2 dB** from what it shows - a band the
    rounding flutter can never cross. On steady material the readout is
    frozen; a genuine drift shows within half a second, and a validity
    flip (value to "--") still bypasses the gate immediately. The meter
    keeps updating at its internal 10 Hz hop rate - the change is
    display-only, zero audio-thread cost.
- **2.42.0**
  - **Gain-staging readout: a real average (point 4).** The "Add x dB"
    value is now the meters' 5 s exponential average of the LUFS
    **power** (BS.1770-style, gated below -70 LUFS per 100 ms hop),
    replacing the old short-term delta plus the editor-side 1.5 s dB
    smoothing. A Python simulation drove the choice: on dynamic material
    the readout is ~2.7x steadier (about ±0.1 dB of wander instead of
    ±0.25 dB) AND more accurate - the old dB-domain averaging biased it
    ~0.3 dB low by weighting quiet passages as much as loud ones. The
    visible rhythm is unchanged in spirit: ~1 s of signal to appear, hold
    through short silence gaps, "--" after 3 s of silence, so a muted
    console cannot keep flashing a stale correction. The smoothing runs
    inside the existing hop loop for a handful of flops per 100 ms per
    meter - no allocation, no extra memory. The IN/OUT LUFS displays and
    the silent-console watch still read the short-term window, untouched.
  - The Doctor report now also prints the current gain-staging line.
- **2.41.0**
  - **Bass stairs fixed (Clement's remark).** Below ~213 Hz at 48 kHz a
    display band is narrower than one FFT bin, so several consecutive
    bands read the SAME bin and the curves descended in visible stairs
    (up to ~10 identical points near 20 Hz). Sub-bin bands now sample
    the spectrum at their exact frequency - the neighbouring bin powers
    are linearly interpolated - which turns the stairs into a smooth,
    honest slope at the FFT's true resolution. Applies to the IN / OUT
    curves and to the measured EQ curve alike (both analysers share the
    band mapping). Bands a bin or wider keep the v2.20 band-energy
    aggregation untouched.
  - **Prettier curve ballistics.** The IN / OUT curves rise 4x faster
    than they fall: a transient shows immediately, the body of the
    curve keeps decaying at the calm Fast / Slow pace. Classic
    fast-attack / slow-release analyser feel, both sides derived from
    the user-selectable response speed.
  - **The analyser now fully stops when it is hidden (Clement's
    remark).** The spectrum analysers and the cross-spectrum transfer
    measurement run only while the editor is open AND the analyser
    strip is shown - a hidden strip costs nothing on the audio thread.
    The LUFS meters stay on the editor-open gate alone, so the gain
    staging readout and the licence-expired silent-console watch keep
    working with the analyser hidden. Re-opening the strip (or the
    editor) resets the analysers and the display, so no pre-hide frame
    or ghost curve ever leaks into the fresh view; the panel's 30 Hz
    tick also skips entirely while the strip is hidden. The Doctor
    report states whether the analyser strip is shown or hidden.
- **2.40.0**
  - **Contact card rework (Clement's remarks).** The studio lines are
    gone (French and English alike); "Ingénieur Centrale Nantes" now
    comes before "Certifié DJ Network 2022" in both languages, and the
    English translation renders at the same 13 px size as the French
    block (still italic + grey). The card now floats in its own
    always-on-top desktop window that covers the plugin's on-screen
    rectangle: a light scrim dims the whole UI - hosted plugin included,
    which is why a plain child overlay could not work (the native NSView
    sits above every JUCE-drawn layer) - and a click anywhere outside the
    card closes it; the Close button and Esc close it too. No more
    separate dialog window with a title bar, and the card is 2 text
    lines shorter (460×214).
  - **Help mode over the analyser: lag fixed (Clement's remark).** The
    help bubble's desktop window used to move on every single
    mouse-move - dozens of native window moves per second while
    sweeping the analyser, which stuttered the whole UI. The bubble now
    only moves when the hovered target or its text actually change.

- **2.39.0**
  - **Minimum width 1024 → 860 (Clement's remark).** Narrow channel
    strips were drowning in dead margin at the old width floor; the
    auto-fit window now hugs them down to the sidebar's own needs,
    and whatever margin remains reads as one dark field (see below).
  - **The sidebar reads as four blocks** separated by an identical
    vertical space: title + presets + consoles (pinned at the top),
    logo + analyser, gain staging, and the name & alarm block glued
    to the Rescan / Doctor row (pinned at the bottom, 2 px apart).
  - **The shell's dark is the hosted plugins' own black.** The
    near-black had a blue tint; the window background and the field
    behind the hosted plugin are now the neutral black of the
    brainworx-style plugin frames, so the hosted GUIs visually embed
    into the shell. The navy stays the sidebar panel's colour.

- **2.38.0**
  - **Back to the pre-v2.37 colours.** The all-navy background read
    lighter than before; the window background is the near-black again,
    with the subtle navy sidebar panel on top of it (as it was from
    v2.5 to v2.36).
  - **Justified sidebar (Clement's proposal).** PRESETS and the nine
    console buttons are pinned right under the title (less air between
    the two), the console name & alarm block is pinned just above the
    Rescan row (the alarm zone ends 2 px above it), and the three
    middle sections — logo, analyser, gain staging — share the
    remaining vertical space evenly, whatever the window height.
  - **Minimal frame.** The margins and gutters around and between the
    zones are halved (10 → 5 px): under the analyser strip, beside the
    sidebar, around the hosted plugin — the mini voids around the
    hosted console tend to zero.
  - **Smaller window.** Default 1165×970 (was 1180×1000), minimum
    1024×970 / 1024×790 (was 1024×1000 / 1024×810); the auto-fit window
    is 15 px narrower and 10 px shorter for the same hosted GUI.

- **2.37.0**
  - **Sidebar re-ordered to Clement's proposal.** Top to bottom: the
    shell's title, PRESETS, the nine console buttons, the frame-free
    Costes logo, the ANALYSER section, GAIN STAGING, the hosted
    console's name and its alarm zone, then the pinned Rescan / Doctor
    row. The presets now sit right under the title with the consoles
    they belong to, and the logo moved from the masthead to below the
    console block, signing the console stack above the monitoring
    sections. Window heights unchanged (1180×1000 default, 1024×1000
    / 1024×810 minimum).
  - **The ANALYSER heading lives inside its toggle.** The separate
    ANALYSER heading row is gone: the display toggle is now a wider
    button carrying "ANALYSER" and the spectrum glyph side by side,
    saving the heading's space. Same behaviour (blue when the
    analyser strip is shown).
  - **One background colour.** The window background was a near-black
    (one shade of dark) framing the navy panels (another shade);
    everything is the one dark navy now — the v2.5 sidebar panel fill
    is gone with it, the zones read by their content.

- **2.36.0**
  - **The sidebar reads as one column of modules (Clement's design,
    decided together).** Top to bottom: the shell's title, the
    frame-free monochrome logo right under it (the masthead — the
    signature anchors the column; a click still opens the contact
    card), the new **ANALYSER** heading labelling the display block
    (display toggle, Fast/Slow, tilt), GAIN STAGING, PRESETS (now
    grouped with the consoles a preset belongs to — reversing v2.29's
    "top of the sidebar" placement), the nine console buttons, then
    the hosted console's name and its alarm zone, closing the working
    stack right above Rescan so a "Press Rescan" alert lands next to
    the button that answers it. Rescan and the Doctor button stay
    pinned at the bottom, unchanged.
  - **Condensed name + alarm block.** The hosted console's name is
    20 px (was 24) and the alarm zone is three lines, top-anchored, so
    the integrity readout hugs the console name instead of floating in
    the middle of a four-line zone; the spare line stays below as
    headroom for a longer alert. The v2.24/v2.35 alert messages were
    trimmed to fit the three-line budget (same call-to-action, same
    test coverage).
  - Window heights unchanged: 1180×1000 default, 1024×1000 / 1024×810
    minimum (the new column fits the same vertical budget).

- **2.35.0**
  - **One merged status zone, zero redundancy (Clement's design).** The
    two "geek" text areas of the sidebar are gone as separate blocks:
    the integrity readout and the former bottom status area are now a
    single four-line zone right under the hosted console's name. The
    persistent "Hosted '...' ready (latency ... samples, N parameters)"
    line disappeared from the display entirely (console name and
    latency live on the sidebar exactly once; parameter counts are
    Doctor material). Actionable alerts — a missing, moved or
    blacklisted console after a click, an `ERROR:`, a `WARNING:` — take
    the zone over, coloured red or amber, until resolved; scan progress
    rides it too. The v2.24 messages were compacted so their
    call-to-action always stays visible in the zone.
  - **Studio logo: frame-free, alone at the bottom (decided together).**
    The v2.34.1 baked frame cut into the waveform and the COSTES
    letters; it is gone (Clement chose the frame-free option on my
    recommendation). The monochrome white design floats on its own band
    pinned just above the Rescan row, slightly taller than before
    (58 px).
  - **Contact card: bigger text, no dead space.** All the lines grew
    (title 17 px bold, French 13 px, translation 11 px, mail/web 13 px
    bold) and the card is now exactly as tall as its content — the
    ~100 px of empty space below the buttons are gone.
  - Window heights: 1180×1000 / 1180×810 minimum (the merged zone and
    the taller logo band need the room).

- **2.34.1**
  - **Text encoding fixed everywhere (mojibake).** The accents of the
    contact card ("Clément", "Ingénieur") came out garbled, and the
    investigation revealed a wider, older flaw: `juce::String (const
    char*)` does **not** read UTF-8 in JUCE 8 — it decodes one codepoint
    per byte, so the middle-dot separators of the status lines had
    actually been rendering as two glyphs since v2.33. Every literal
    holding non-ASCII bytes now goes through `juce::String::fromUTF8`
    (the one clean path: `CharPointer_UTF8` cannot be concatenated with
    `operator+` under AppleClang), and the test harness pins the real
    UTF-8 separator and the absence of mojibake glyphs.
  - **Studio logo redesigned for the dark shell (Clement's design).**
    The sidebar shows a **monochrome variant** of the Costes logo: only
    the black design of the original (the waveform and the COSTES
    lettering) kept, turned white — the same colour as the shell's text —
    with the beige background made transparent and a rounded outer frame
    baked in, so the badge integrates with the dark palette instead of
    sitting on it as a beige patch. The logo sits in its own band below
    the gain-staging section as before v2.34, rendered at 75% of the
    old size (50 px instead of 66). Clicking it still opens the contact
    card.
  - **"Add +X dB" readout restored.** The v2.34 squeeze into the logo
    column clipped the main gain-staging readout out of sight; it is
    back exactly as it was — full sidebar width, one big cyan line
    under the IN/OUT readouts.
  - **"Display" became an icon button (Clement's suggestion).** The
    analyser show/hide control is now a wide button with a small
    spectrum-analyser glyph (four bars), the height of the tilt buttons
    and styled like the other console buttons — blue when the strip is
    shown — sitting at the top of the section, just above Fast/Slow.
  - Window heights follow the restored logo band: the auto-fit drops
    in at 970 (analyzer shown) / 790 (hidden), minimum 1024×970 /
    1024×790.

- **2.34.0**
  - **Sidebar zone A rework: preset marquee.** Long preset names
    (album tags included) no longer read as a truncated line: when the
    text fits, it stays centred and frozen; when it doesn't, it
    scrolls slowly in a loop inside the combo box — a steady
    ~15 px/s drift with a short hold at both ends so the eye can
    catch the start and the end. The scroll repaints only the combo
    box's own rectangle, on the UI timer, nothing else in the
    window moves, and the audio path is untouched.
  - **Doctor gets its own heartbeat button.** The Doctor report no
    longer hides behind the logo: a dedicated pulse-trace button
    (an ECG-style heartbeat icon) sits next to Rescan at the bottom
    of the sidebar, styled like the other console buttons. The
    logo's click is freed for something better —
  - **Contact card on the logo.** Clicking the Costes logo now opens
    a compact contact card: who Clément is (mix & mastering
    engineer, Nantes studio + mobile rig, in English below the
    French), the mail and the website, both one click away
    (**Copy mail** to clipboard, **Open website** in the default
    browser with a clipboard fallback). Message thread only.
  - **Logo halved into the gain-staging zone (zone A).** The Costes
    logo renders at ~half its former size and moves to the right of
    the GAIN STAGING block, with the big **Add +X dB** correction
    readout now sitting **under the logo** in the same right column
    — the block keeps a clean two-column shape and the sidebar gets
    its full-height breathing room back. The whole zone is ~95 px
    shorter, and the window's auto-fit heights drop accordingly
    (890 with the analyzer shown, 710 with it hidden, minimum
    1024×890 / 1024×710).
  - **Merged status lines, zero redundancy.** The v2.33 integrity
    bandeau and the channel-format readout above it collapse into
    ONE compact two-line block under the hosted console's name:
    `track: stereo · crossfade: idle` / `latency: 0 · sentinel:
    armed`. The constant `shell: unity` fact (an architecture
    invariant, always identical) left the sidebar for good — the
    Doctor report is where it's explained. Crossfade, forwarded
    latency and the silence sentinel still update live from the
    same atomics, still with zero audio-thread cost.

- **2.33.0**
  - **Shell integrity bandeau (roadmap idea 3).** A compact two-line
    readout under the channel format in the sidebar answers « is the
    shell transparent? » at a glance: `shell: unity · crossfade:
    idle` / `latency: N · sentinel: armed`. The shell processes at
    unity gain (a fact of the architecture — it never applies gain
    of its own), the **crossfade** line flickers to *active* for the
    ~50 ms of a console swap so you can see the shell doing its one
    transient job, the **latency** is the delay forwarded to the
    host, and the **sentinel** is the licence-expired silence watch
    (*fired* if the hosted console stopped producing audio under a
    loud input; hosting a new console re-arms it). Display-only,
    built from atomics on the UI timer — zero audio-thread cost.
  - **Doctor report (roadmap idea 4).** **Click the logo** for a full
    diagnostic snapshot of the shell: versions (shell, JUCE), host
    and wrapper, sample rate and block size, channel readout, the
    hosted console (name, ident, current preset), the integrity
    readout, the plugin database summary (per console: found / not
    found / blacklisted / file missing, with paths), the preset
    count, the per-console remembered-state sizes and the last
    status message. The report pops up in a small window with a
    **Copy to clipboard** button, is written to
    `doctor-report.txt` next to the plugin database and echoed to
    the Terminal. Message thread only, nothing touches the audio
    path — and any state the sidebar already shows, the Doctor can
    explain.

- **2.32.0**
  - **Realtime-safety audit, enforced by the test harness.** The
    shell's audio thread is now *proven* allocation-free in steady
    state: the harness replaces the global `operator new` / `delete`
    pair with a counting sentinel and asserts that every
    `processBlock` branch (stereo with the editor open and closed,
    native mono, the mono bridge, no console hosted) allocates
    **nothing** after warm-up. A heap lock with an unbounded worst
    case is a dropout hazard by definition; now the build fails if
    one ever sneaks back in. The audit found and removed the one
    remaining offender: the BS.1770 **integrated**-loudness
    bookkeeping in the LUFS meter — a value nothing in the shell ever
    displayed, recomputed with a `std::vector` growth (an audio-thread
    allocation every 400 ms block) plus two `log10` loops over up to
    20k entries, and a 160 KB erase-from-front once saturated. The
    short-term window (the gain-staging delta and the silent-console
    watch) is untouched.
  - **Torture tests: the shell is abused the way real sessions do.**
    A 50-swap console storm mid-playback (the 50 ms crossfade
    interrupted over and over, plus a full clear in the middle) must
    keep producing bounded audio and converge on the last console;
    sample-rate / block-size / track-format churn (down to 1-sample
    buffers, 44.1–96 kHz, mono ↔ stereo) must never re-create the
    hosted instance and must keep the readout and latency forwarding
    truthful; hostile session states (garbage bytes, empty, truncated,
    huge valid-XML fakes naming unscanned plugin paths) fed during
    playback must change nothing, crash nothing, and leave the hosted
    console intact — with a clean session roundtrip restoring a fully
    consistent shell afterwards.

- **2.31.2**
  - **The console in use is now unmistakable in the sidebar.** The
    selected console's slot button takes the project's signature
    cyan (the OUT curve / correction readout colour) with dark navy
    text — a clear « selected » state. The other available consoles
    keep the muted navy base and the unavailable ones their dim
    style: three states readable at a glance, updated instantly on
    every console switch.

- **2.31.1**
  - **No standing help at all, not even on « ? ».** The « ? » button
    kept the editor's last standing tooltip, which the TooltipWindow
    rendered half cut off in the narrow sidebar. It is gone: its text
    moved into the help-mode bubbles like every other control's, so
    outside the help mode nothing pops up anywhere in the editor —
    hover the « ? » inside the mode for the same explanation instead.

- **2.31.0**
  - **Help lives in the help mode alone.** Outside the mode, the
    sidebar and the analyser controls no longer dispense standing
    tooltips on every hover: every explanation (hosted name, track
    format, correction, Rescan, speed, tilt, Display toggle, the
    per-slot console status, the analyser) moved into the help-mode
    bubbles. The « ? » button keeps the only remaining tooltip — it
    is the mode's entry point.
  - **The analyser's dB scales are documented too.** Inside the help
    mode, the three clickable zones of the analyser each get their
    own bubble: the left scale (click to switch 10 / 6 dB per
    division), the right EQ scale (click to switch 4 / 2 dB per
    division) and the LUFS reference at the 0 dB line (click to
    cycle Record −37 / Mix −23 / Master −14) — instead of the
    panel's generic text everywhere.
  - **The help bubble floats in front of the hosted console.** The
    bubble now rides in its own borderless always-on-top desktop
    window, so the hosted plugin's native view can no longer cut
    the writing: whatever the console draws under it, the bubble
    stays readable. It follows the cursor in screen coordinates and
    is closed with the mode (click, Esc, or editor close).

- **2.30.1**
  - **Test-harness fix (no product change).** The v2.30 offline test
    that checks the session roundtrip of the current preset's own
    file built its session from a directly-set mock console, whose
    ident is empty — and the restore deliberately skips empty-ident
    `<consoleState>` elements (a real hosted console always has a
    non-empty one), so two checks failed. The test now inspects the
    real save for the `presetFile` attribute (stored relative to the
    preset folder) and drives the restore with the same fake-session
    pattern as the v2.27 tracking test, with an album path in it.
    Also casts the help-mode bubble's wrap width comparison to float,
    silencing the one warning v2.30 added.

- **2.30.0**
  - **Albums join the list.** The preset selector now lists the
    `.tcsPreset` files of the **whole preset folder**, not just its
    root: sub-folders are your albums, and their presets appear in
    the list tagged « Name  [Album] ». A save into an album shows up
    immediately (this was the v2.29 gap — an album save left the
    selector on the previous preset).
  - **Save As makes the file the current preset, wherever it lives.**
    Saving to any destination — root, album, or elsewhere on disk —
    now always makes the saved file the console's **current preset**:
    the combo box shows it (with its album tag) and the « * » tracks
    it from there.
  - **Load never copies.** Loading a `.tcsPreset` — from an album,
    from outside the folder, from anywhere — **just loads it**; the
    file stays where it lives and becomes the current preset at its
    own file. Nothing is ever duplicated into the root any more, so
    the general list cannot accumulate copies picked up from
    albums.
  - **The current preset's file travels with the session.** The DAW
    session now remembers *which file* the current preset points at
    (relative to the preset folder when inside it), so an album
    preset reopens as that album preset — the combo box re-selects
    it there, Save pre-fills there, and deleting that file disowns
    it.
  - **Interactive help mode.** The « **?** » button moves into the
    preset-button row (fourth slot) and no longer opens the all-text
    window: it enters a **help mode** where hovering any documented
    control — the preset list, the three action buttons, the
    analyser — shows a **small contextual bubble**, a few words
    each, mentioning the hidden clickables (the dB scales and the
    LUFS reference at 0 dB). A soft outline highlights the target
    under the mouse. **Any click, or Esc, leaves the mode**, and a
    click never triggers the control underneath.
  - Preset files are written with version tag `2.30.0`; the offline
    test harness covers the recursive listing, the save-anywhere
    tracking, the no-copy Load and the session roundtrip of the
    current preset's file.

- **2.29.0**
  - **Presets, three actions.** The section is rebuilt around the
    three things presets are actually for — no more separate Export
    button:
    - **Save (the floppy)** opens a native *Save As* dialog and writes
      the **hosted console's current state** wherever you choose —
      into the root of the preset folder, or into a sub-folder
      (an album), or anywhere else on disk. The dialog opens on the
      preset folder with the **current preset's name pre-filled**.
    - **Load (the tray with the incoming arrow)** opens a file browser,
      **loads** the chosen `.tcsPreset` into the hosted console
      (switching consoles first if it was made for another one) and
      **copies it into the root of the preset folder**, so it joins
      the list. Loading a file that is already in the folder just
      loads it.
    - **Delete (the bin)** deletes the preset **selected in the
      list** after a confirmation; with **nothing selected** it
      becomes a *browse-to-delete*: pick any `.tcsPreset` file
      anywhere — sub-folders included — confirm, and it is gone.
      Deleting the current preset's own file disowns it, exactly
      like a list delete.
  - **Preset folder architecture: root = general, sub-folders =
    albums.** The selector lists the `.tcsPreset` files at the **root**
    of the preset folder — that is the general list, always available.
    **Sub-folders are never listed**: they are your albums; presets
    saved there stay reachable only through Load. Saves into the
    root become the console's **current preset** right away (and are
    listed); saves anywhere else never touch the tracking.
  - **The PRESETS section moved to the top of the sidebar**, right
    under the shell's title and above the console buttons — the
    preset list is the most-used control and reads best first.
  - Preset files are written with version tag `2.29.0`; the offline
    test harness covers the new Save/Load/Delete flows (sub-folder
    saves, browse-deletes, disown-on-delete, root-save tracking).

- **2.28.0**
  - **Import loads the preset.** Importing a `.tcsPreset` file used to
    only make it available in the list; it now **loads it into the
    hosted console and makes it the console's current preset**, exactly
    as if it had been picked in the selector:
    - If the preset was made for **another console**, the shell
      **switches to it first**, then applies the preset.
    - Importing a file that **already is** in the preset folder loads
      it too, instead of doing nothing.
    - If the preset's console is **not installed**, the file is still
      imported and stays available in the list — the status area says
      why it could not be loaded.

- **2.27.1**
  - **Fix: the current preset's name and « * » could be dropped from a
    saved session.** They were only written inside the console's
    remembered state block; a console whose state was not on record at
    save time (for instance not yet instantiated) lost its preset
    tracking. The name and the « * » now travel with the session
    independently of the state block, and come back on reload the
    same way. (Caught by the offline test harness: 168/170 → all
    green.)

- **2.27.0**
  - **Current-preset tracking (the « * » workflow).** The preset list
    now always mirrors the **hosted console's current preset**:
    - **Saving** a preset makes it the console's current one — the
      list shows it right away. The save dialog opens with its name
      field **focused** and **pre-filled with the current preset's
      name, selected** (typing replaces it) — no click needed.
    - **Switching consoles updates the name** — the console you land
      on shows its own current preset, or **no name** if its state
      never came from a preset.
    - **Moving a knob on the console** adds a `*` **left of the
      preset's name** — the preset is « modified ». Reloading it or
      saving over it clears the `*`. The shell mutes its own state
      pushes (preset loads, per-console memory, session restores), so
      the `*` only reflects *your* moves.
    - The name **and** the `*` are **saved with the session** and come
      back on reload.
  - **Export clarified:** the export tray acts on the hosted console's
    **current preset only**, and the save dialog **opens with that
    preset's own name pre-filled** — the exported file always matches.
    Exporting with no current preset says so in the status area
    instead of doing nothing.
  - The preset list keeps listing **only the root of the preset
    folder** (sub-folders were already ignored) — which is what makes
    import/export useful.

- **2.26.1**
  - **Preset buttons as icons, on a single line.** The four preset
    buttons drop their text for small vector icons (drawn in code,
    matching the shell's colour scheme): a **floppy** (save), a
    **bin** (delete), a **tray with an outgoing arrow** (export) and
    a **tray with an incoming arrow** (import). They now sit on one
    row next to the preset list. No hover tooltips anywhere in the
    PRESETS section: the « **?** » panel documents the list and all
    four icons. The sidebar's minimum height drops accordingly
    (973 px with the analyser strip, 793 px without).

- **2.26.0**
  - **Named presets (roadmap point 4).** A PRESETS section in the
    sidebar: one combo box listing every preset on disk
    (« Console - Name »), then **Save / Delete / Export / Import**.
    - **A preset is the hosted console's own state** (its knobs),
      nothing else: display settings (tilt, spectrum/EQ scales, LUFS
      reference, response speed) travel with the session — a preset is
      a sound, not a screen configuration.
    - **Global list, console-aware loading.** Every preset of every
      console appears in the list; selecting one switches to its
      console if it is not the one currently hosted, then applies the
      state (which also becomes that console's remembered state).
    - **One file per preset, on disk.** `Presets/` next to the plugin
      database (`~/Library/Application Support/TheChannelStrip/`),
      one `.tcsPreset` per entry — copyable by hand, reusable across
      projects and machines; Import brings an external file in,
      Export copies one out.
    - **Explicit failures everywhere** (same philosophy as v2.24):
      saving without a hosted console, a missing or corrupted preset
      file, a preset whose console is not installed — each posts its
      own message to the status area instead of doing nothing.
    - Minimum window height +100 px for the section (1000 / 820 with /
      without the analyser strip). The test harness gains a preset
      lifecycle suite (19 tests).

- **2.24.1**
  - **Test-harness compile fix** (plugin code unchanged): the new
    MockSilenceConsole stub declared `getProgramName()` without the
    `int` parameter of the pure virtual
    `juce::AudioProcessor::getProgramName (int index)` — the harness
    failed to build while the plugin itself built and linked cleanly.
    The stub now matches the same signature the older mocks already
    use. Everything else is identical to 2.24.0.

- **2.24.0**
  - **Clean missing-plugin / expired-licence handling (roadmap
    point 7).** No console slot is ever a mysterious grey button any
    more: every unavailable state is detected, explained and styled.
    - **Missing consoles stay clickable.** A console that is not
      installed keeps its button active with a dimmed style; clicking
      it posts the exact situation to the status area ("was not found
      in the scanned VST3 folders — check that it is installed, or
      press Rescan") instead of silently doing nothing.
    - **Blacklisted plugins are named.** A plugin that CRASHED the
      out-of-process scanner is blacklisted; its slot now says so
      ("crashed the plugin scanner and was blacklisted — update or
      reinstall it, then press Rescan"), because a plain Rescan will
      not fix that.
    - **Moved/deleted files are detected.** A slot whose scanned file
      no longer exists on disk reports the dead path ("was last seen
      at '…' but that file no longer exists — moved or
      uninstalled?").
    - **Instantiation errors quote the file.** If creating the
      instance fails, the ERROR line now carries the plugin's file
      path too, so a moved .vst3 is immediately identifiable.
    - **Silent-console watch (licence expired).** A lapsed licence
      does not make a console fail to load — it loads and then emits
      digital silence forever. The shell now watches the two
      short-term LUFS meters it already runs: when the input carries
      signal while the hosted console has produced nothing for 5
      seconds, ONE explicit warning is posted ("'SSL 9000 J' produces
      no output while its input is active — licence expired or plugin
      not authorised?"). Once per hosted instance; swapping consoles
      re-arms it. A paused track (silent input) never triggers it.
    - **Status line colour.** ERROR: lines are red, WARNING: lines
      amber, everything else grey — a licence warning no longer reads
      like everyday scan chatter.

- **2.23.0**
  - **Analyser visual pass - épurée (roadmap point 11, the aesthetic
    half).** Same engine, same colours, same interactions - everything
    below is pure drawing quality, in the spirit of "beau ne veut pas
    dire baroque":
    - **Two-tier grid (SPAN convention).** The 1-2-5 frequencies
      (20/50/100/200/500/1k/2k/5k/10k/20k) carry the brighter line AND
      an axis label; the other decade multiples become much fainter
      minor lines (and the horizontal divisions were dimmed to match).
      The old same-weight grid drew ~25 identical lines - busy without
      adding information.
    - **Curves with curved joins + rounded caps** (IN/OUT/EQ/MAX): no
      more miter spikes where a curve turns steeply; the traces read as
      one continuous gesture.
    - **OUT fill fades to transparent** towards the bottom of the
      panel (vertical gradient) instead of a flat tint - the curve
      keeps its glow, the low end stays airy.
    - **EQ curve: 2.4 px -> 1.8 px**, and its 0 dB anchor is now a thin
      DASHED line (the classic reference-line convention of EQ
      displays) instead of a solid tinted band. The measurement reads,
      it no longer shouts.
    - **Legend rebuilt as a colour-chip row**: a small dash in the
      trace colour + a neutral grey label (the words "IN"/"OUT" used
      to be fully coloured and competed with the curves). The transfer
      curve is labelled **"EQ"** - the name the hover readout, the EQ
      scale and the help already use. The row is measured and centred
      as a whole.
    - **MAX envelope at 45% alpha** (was 70%): readable against its
      own curve without turning the treble into lace.
  - No behaviour change: the measurement, gates, ballistics, readout
    and interactions are untouched (the harness passes as-is).

- **2.22.0**
  - **Cross-spectrum transfer measurement + coherence gate (the real
    Smaart method).** New `TransferAnalyzer` on the audio thread: every
    8192-point frame (same Blackman-Harris window as the scope) is
    FFT'd on BOTH the pre-console input and the post-console output,
    and per display band the analyser accumulates the input/output
    auto-spectra Sxx/Syy and the complex cross-spectrum
    Sxy = E[X*conj(Y)] (EMA, effective average over ~8 frames ~ 1.4 s).
    The EQ curve is the **H1 estimate** 20*log10|Sxy/Sxx|; a new
    **coherence gate** gamma^2 = |Sxy|^2/(Sxx*Syy) >= 0.25 blanks the
    curve exactly where the output is dominated by uncorrelated junk.
    The v2.21.1 power ratio measured "attenuated signal + console
    floor" over "signal": with a console lowpass (SSL J LP at 5 kHz)
    the floor exceeds the attenuated treble signal, the ratio turns
    positive, and the EQ honestly reported a **+3 dB rise above
    10 kHz that was not a transfer**. The complex accumulation is the
    fix: the console's noise/dither has a random phase and averages
    OUT of Sxy (it stays visible on the OUT curve, but no longer votes
    on the EQ), while the deeply filtered signal stays correlated with
    the input - so the curve reads the true rolloff and then holds its
    last value once the signal sinks under the floor (coherence
    blanking), exactly like a professional dual-FFT measurement.
    A pure delay (console latency) only rotates Sxy's phase: |H| and
    gamma^2 are exact without any delay compensation.
  - The v2.13/v2.21 per-bin settling times and confidence weights are
    gone (they tuned per-frame dB ratios that no longer exist); the
    measurement is already averaged in the analyser EMAs, so the panel
    keeps a single light display EMA (tau 0.25 s).
  - New harness test (16 tests now): a mock console that **adds an
    uncorrelated 12 kHz tone** over broadband input. The cross-spectrum
    EQ must read flat 0 dB at the tone band while the coherence dips
    below 1 there (the v2.21.1 power ratio would read the tone as a
    +3 dB phantom boost - the reference simulation measured exactly
    that failure mode on the old engine).

- **2.21.1**
  - **Measurement-grade EQ curve + weak-band accuracy (LP-filtered
    mixes, kicks).** The measured EQ (green) is now a true
    **transfer-function estimate**, built the way professional dual-FFT
    measurement (Smaart & co) does it: per-bin **input and output
    energies averaged in the power domain**, the curve being the ratio
    of the two averages - uncorrelated junk (leakage, the hosted
    console's noise / dither floor) averages down instead of voting on
    the curve. A **relative validity gate** (a bin is measured only
    while its input is within 45 dB of the program's peak, absolute
    floor at -75 dB - the counterpart of Smaart's "coherence blanking")
    replaces the old absolute -80 dB gate: bands with no honest signal
    to measure through (the empty treble of a lowpassed mix or a kick)
    now HOLD their last value instead of reporting noise-over-noise -
    this is what drew **phantom rises above 8 kHz** on LP-filtered
    material. The persistence (v2.12), frequency-dependent settling and
    confidence weighting are unchanged. The v2.7 5-bin dB pre-smoothing
    is gone (it biased every filter knee on the display). The EQ and
    its gate read the **raw band energies** - the v2.20 display
    smoothing stays purely cosmetic, so an isolated weak band is no
    longer dragged toward the floor by its empty neighbours.
  - **Blackman-Harris FFT window** (4-term, sidelobes at -92 dB
    instead of Hann's -31 dB, normalisation recomputed from the actual
    window sum so tones read at the exact same level). Weak treble
    bands next to strong program material no longer read several dB
    above their true content - the "20 kHz node too high" on
    lowpassed mixes.
  - New harness test group (15 tests now): a strong 500 Hz + weak
    10 kHz two-tone passthrough must read a flat 0 dB EQ at both
    frequencies, and bass-only material must leave the treble
    unmeasured.
- **2.20.0**
  - **Treble accuracy + UI polish (7-point pass).**
    (1) A **"?" help button** next to the studio logo opens every tool
    tip in one window; the analyser itself no longer shows a tooltip on
    hover (it was hiding the readout). (2) The **IN curve is drawn
    without its area fill** — only OUT keeps its soft tint.
    (3/4/5) The per-display-bin value is now the band's **energy (RMS
    of the covered FFT bins)** instead of their arithmetic mean: above
    ~5 kHz one display bin spans a dozen+ FFT bins, and the mean of
    fluctuating magnitudes reads ~3 dB below the band's true energy —
    the treble now sits where reference analysers put it, and the EQ
    curve's high end is far more stable. (6) The EQ scale labels moved
    **flush to the right edge** (mirroring the dB labels at the far
    left) and the hover readout keeps a 44 px margin — no more
    collision. (7) One light **spatial smoothing pass** across
    neighbouring display bins ([0.25, 0.5, 0.25]) on both spectra, the
    "small smoothness" feel of classic analysers; IN and OUT are
    smoothed identically so the EQ difference stays honest. The
    harness gained a 10 kHz peak-location test (14 tests now).
- **2.19.0**
  - **Hover readout on the analyser (roadmap point 3).** Moving the
    mouse over the spectrum panel shows the frequency under the cursor
    and the IN / OUT / EQ levels at that position, in a fixed top-right
    label colour-matched to the legend, with a subtle vertical hairline
    at the cursor. The IN/OUT numbers go through the exact same tilt
    and loudness-offset transform as the drawn curves, so the readout
    always matches what is on screen; unmeasured bins read "--" and the
    EQ value only appears once a bin has carried energy (v2.12
    persistence). No cursor lock, no freeze - pure display. The
    frequency mapping and the value formatting are covered by a new
    check group in the offline test harness (13 tests now).
- **2.18.3**
  - **Lighter studio logo asset.** The `CostesLogo.png` resource is
    downsized locally from 755 KB to roughly 80-100 KB (the full
    resolution was about ten times more than the displayed ~66 px
    height needs). No code change; since the logo is compiled into the
    binary data, the VST3/AU/Standalone artefacts are about 700 KB
    lighter. A clean rebuild is required (binary data regeneration).
- **2.18.2**
  - **Test harness expectation fix (LUFS check).** The v2.18.1 suite
    failed one check: it expected a stereo 1 kHz sine at -23 dBFS RMS
    per channel to read -23 LUFS, but BS.1770 loudness sums the
    mean-square power of every channel — an identical stereo pair
    measures about 3 dB louder (-20 LUFS). The meter was right and the
    test expectation was wrong; the check now targets -20 LUFS (the
    harness biquad K-weight gives ~+0.43 dB at 1 kHz vs ~+0.69 dB for
    the exact BS.1770 coefficients, so the observed reading is ~-20.25).
    The plugin code itself is unchanged.
- **2.18.1**
  - **Test harness crash fix.** The v2.18.0 `TcsTests` binary segfaulted
    on the very first test: the helper that builds a canonical main-bus
    layout assigned through `BusesLayout::getChannelSet`, which is an
    unchecked array access — a default-constructed `BusesLayout` has
    empty bus arrays, so the write dereferenced a null array. The
    layouts are now built through the public `inputBuses` /
    `outputBuses` members instead. The plugin targets were never
    affected: the shell always starts from a real `getBusesLayout()`,
    whose arrays match the actual buses.
- **2.18.0**
  - **Offline test harness (TcsTests)**. A new console target builds
    alongside the plugins and exercises the shell automatically: 12
    unit tests run with mock hosted plugins (a mock is a real
    `juce::AudioProcessor`, so the whole production path is exercised —
    latency snapshotting, the 50 ms crossfade, mono layout negotiation,
    the stereo bridge) plus one opt-in integration test (`--integration`)
    that hosts a real console from the saved plugin database and logs
    the negotiated mono path. Covered end to end: processor basics and
    console inventory, bit-exact dry passthrough (stereo and mono),
    measurement gating (nothing measured while no editor is open),
    LUFS short-term metering (absolute window and 6 dB tracking),
    spectrum peak location, state save/restore (including the version
    tag), latency forwarding for static AND runtime changes, click-free
    console swapping (bounded inter-sample slew), native mono hosting
    vs stereo-bridge fallback, and stereo ↔ mono hot switching without
    ever re-creating the hosted instance. The plugin targets themselves
    are unchanged — same binary behaviour, one new `tests/` source file
    and one CMake target.
- **2.17.1**
  - **Studio logo halved**. The Costes logo in the sidebar is now half
    its v2.17 size in both directions (the placement keeps the aspect
    ratio). The window's minimum height follows (900 / 720 with the
    analyser visible / hidden).
- **2.17.0**
  - **Console order reworked**. The sidebar buttons now read, top to
    bottom: **A9099, SSL E, SSL G, SSL J, Bx N, Lindell N, Lindell A,
    Focusrite, A200**. Only the display/hosting order changed — saved
    sessions match consoles by name and per-console memory is keyed by
    plugin ident, so existing sessions and remembered states load
    exactly as before.
  - **Studio logo in the sidebar**. The Costes studio logo is embedded
    as a binary resource and displayed between the gain-staging section
    and the host status area. It scales to the sidebar width, aspect
    ratio preserved. The window's minimum height grows accordingly
    (960 / 780 with the analyser visible / hidden).
- **2.16.1**
  - **Runtime latency changes are now forwarded**. The hosted console can
    change its own latency while playing (the Lindell 80/50 Channel
    oversampling does exactly that); previously TheChannelStrip reported
    the latency once at instantiation, so delay compensation drifted when
    oversampling was toggled mid-session. The processor now refreshes the
    hosted plugin's reported latency every block and re-forwards it to the
    DAW (JUCE relays the change as a `kLatencyChanged` notification, the
    same path a directly-inserted plugin uses), and the editor logs
    "latency: now forwarding N samples" to the Terminal on every change.
    Fixed-latency consoles (the bx_consoles, AMEK 200's 32 samples) are
    unaffected.
- **2.16.0**
  - **Two new hosted consoles**: the **Lindell 80 Channel** (button
    "Lindell N") and the **Lindell 50 Channel** (button "Lindell A") join
    the nine-console lineup, appended after the existing buttons. As with
    every slot, a button whose plugin is not installed stays disabled, and
    everything else applies automatically: click-free swap with per-console
    state memory, Studio One gain-reduction forwarding, native-mono
    negotiation, analyser and gain-staging readouts. The window's minimum
    height grows to 840 px (analyser visible) / 660 px (hidden) so the two
    extra buttons always fit in the sidebar.
- **2.15.5**
  - **Studio One gain-reduction forwarding**: TheChannelStrip now answers
    Studio One's `PreSonus::IGainReductionInfo` extension query so the
    hosted console's gain reduction (gate + compressor) is displayed on
    the channel meter, exactly as with the console inserted directly.
    The chain is pure pull and 1:1 - when the host asks the wrapper, the
    wrapper asks the hosted console's own `IGainReductionInfo` (queried
    through its VST3 `IComponent` / `IEditController` when the console is
    instantiated) and returns the same value. No polling thread, no
    audio-thread cost. With no console hosted, or a console without the
    extension, the answer is 0.0 dB. If a console does not expose the
    extension, its read-only parameter inventory is dumped to the
    Terminal for diagnosis (the fallback path for a future version). AU
    and Standalone builds are unchanged - the extension is VST3-only,
    like Studio One's meter. Implementation: new `src/GRForwarder.{h,cpp}`
    connected at the end of `instantiateHosted`, disconnected in
    `clearHostedPlugin` and the processor destructor, plus the
    `getVST3ClientExtensions()` hook. Base: the tested 2.14.3 source,
    unchanged apart from these grafts.
- **2.14.3**
  - Fix: compilation error in `PluginProcessor.cpp` (`juce::AudioChannelSet`
    has no `isMono()` member - the main-bus mono post-check now compares
    the settled channel sets against `juce::AudioChannelSet::mono()`
    instead, same lesson as 2.13.1: JUCE API spelling must be verified
    against the module headers). No behaviour change from the 2.14.2
    design.
- **2.14.2**
  - **Main-bus-only mono negotiation (sidechain-safe)**: v2.14.0 bailed
    out of the native-mono request whenever the hosted plugin declared
    any extra bus (e.g. the sidechain bus of a bx_console VST3), which
    forced every such console onto the stereo bridge even on a mono
    track. The mono request now targets **only the main bus**: the main
    input and output are asked for mono->mono while sidechain and other
    auxiliary buses keep the layout the host granted, so plugins with a
    sidechain can still run natively in mono.
  - **Exact refusal reason in the sidebar readout**: the channel readout
    under the hosted console's name now distinguishes the fallback
    cases — "track: mono (bridge: refused)" (the console rejected the
    mono layout), "track: mono (bridge: N/M)" (the console settled on
    the next-best layout with N input / M output channels on the main
    bus) and "track: mono (bridge: multi-bus)" (no main bus was found,
    which should no longer happen). "track: mono (native)" confirms the
    mono->mono path is active.
  - **Bus inventory in the Terminal logs**: when a console is
    instantiated, TCS logs the exact bus layout the fresh instance
    advertises ("hosted 'X' buses: N in / M out") before the mono
    negotiation, making future layout questions answerable at a glance.
- **2.14.1**
  - **Channel-format diagnostics** for the native-mono feature: the
    sidebar now shows, right under the hosted console's name, what the
    DAW feeds the instance and which audio path the console takes —
    "track: stereo", "track: mono (native)" (console accepted the
    mono->mono layout and runs its own mono path) or "track: mono
    (bridge)" (console refused, the v2.8.2 stereo bridge is used).
    The mono-layout negotiation is also logged to the Terminal with its
    refusal reason. No audio-path change.
- **2.14.0**
  - **Native mono hosting**: on a mono track, the hosted console is now
    asked for a native mono->mono bus layout and runs its own mono
    processing path (roughly half the DSP of the previous always-stereo
    bridge). The layout is re-negotiated every time the track switches
    mono <-> stereo; the console instance is never re-created, so its
    parameters and internal state survive the switch (no param reset).
    If a console refuses the mono layout, TCS falls back to the proven
    v2.8.2 stereo bridge (up-mix L = R, stereo console, down-mix
    (L + R) / 2), and the per-plugin scratch buffers keep the swap
    crossfade safe in both directions.
- **2.13.1**
  - Fix: compilation error in `PluginEditor.cpp` (a bare string literal
    cannot be concatenated with the ternary's `const char*` result -
    the "Add " prefix now goes through an explicit `juce::String`,
    same lesson as v2.0.0).
  - Fix: `-Wimplicit-int-float-conversion` warning in
    `SpectrumPanel::mouseDown` (explicit `(float)` cast on the mouse
    coordinate).
  - Everything else is unchanged from 2.13.0.
- **2.13.0**
  - **Two new consoles**: the list now hosts **bx_console Focusrite SC**
    (button "Focusrite") and the **bx_console N** (button "Bx N",
    back from the v1 shell at Clement's request). Plugins that are not
    installed simply show a disabled button, like the others.
  - **Gain staging readout**: back to the correction reading ("what to
    add"), now with an explicit prefix — the display reads
    **"Add +0.7 dB"** (raise the output by 0.7 dB to match the input
    loudness) or **"Add -0.4 dB"** (lower it by 0.4 dB).
  - **Faster EQ curve**: the transfer curve (OUT-IN) settles ~4x
    faster than in 2.12 (~1 s in the bass, ~0.25 s in the treble).
    The v2.12 persistence is unchanged: a measured bin keeps showing
    its last value; it just reacts quicker when the program changes.
  - **Selectable loudness reference (v2.13's headline)**: clicking the
    **reference label at the 0 dB line** (e.g. "Mix (-23 LUFS)", now
    drawn bigger and brighter) cycles three presets:
    **Record (-37 LUFS) → Mix (-23 LUFS, default) → Master (-14 LUFS)**.
    The whole analyser axis recalibrates instantly; the choice is saved
    with the session.
- **2.12.1**
  - Fix: compilation error in `PluginEditor.cpp` (`no member named
    'setTooltip' in 'SpectrumPanel'` - a plain `juce::Component` does
    not inherit `SettableTooltipClient`, only the widget classes like
    Button/Label do). The panel now declares `SettableTooltipClient`
    as an explicit base class, which is exactly how JUCE documents
    adding a tooltip to a custom component. Everything else is
    unchanged from 2.12.0.
- **2.12.0**
  - **Two speeds only**: the analyser speed selector is back to two
    buttons, Fast (~0.15 s, the pre-2.10 snappy ballistics) and Slow
    (~1 s, the v2.10/v2.11 default; the old Med). The old 3 s Slow is
    gone. The choice is saved with the session (the old saved value
    snaps to the closest of the two).
  - **Gain staging readout inverted** (more intuitive): the delta now
    states the *measured deviation*, not the correction to apply.
    `+1.2 dB` means the console's output is 1.2 dB **too loud**;
    `-1.0 dB` means it is 1 dB too quiet. (Before, `+1.2 dB` meant
    "raise the output by 1.2 dB".)
  - **Persistent EQ curve**: the transfer curve (OUT-IN) no longer
    erases itself piecewise when the program's spectrum changes. A bin
    that has carried input energy keeps showing its last measured
    value until new energy updates it. The curve also smooths twice as
    slowly (~4 s in the bass, ~0.6 s in the treble), as requested.
  - **Scale controls moved onto the analyser itself**: the SCALE dB/DIV
    and EQ dB/DIV button rows are gone from the sidebar. Clicking the
    analyser's left dB scale cycles 15 / 12 / 9 dB per division; clicking
    the right green EQ scale cycles +/-24 / +/-18 / +/-12 / +/-6 dB. The window
    minimum height drops accordingly (700 px with the analyser, 520
    without).
  - **Uniform tilt button font**: the tilt buttons (-3 / -4.5 / -6) and
    the speed buttons are now styled like the console buttons
    (TextButton radio behaviour); the old ToggleButton look
    auto-shrank the "-4.5" label to fit, which made it visibly smaller
    than its neighbours.
- **2.11.1**
  - Fix: compilation error in `PluginProcessor.cpp` (`getChildIteratorWithName`
    does not exist in JUCE 8 - the restore path now iterates the children
    with `getChildIterator()` and matches the `consoleState` tag with
    `hasTagName()`). Everything else is unchanged from 2.11.0.
- **2.11.0**
  - **Per-console settings memory** (replaces the v2.10 A/B slots, which
    were removed): switching consoles used to reset every knob to
    factory defaults. Each console's last state is now remembered, so
    switching SSL J → SSL G → SSL J brings J back exactly as it was
    left (a console hosted for the very first time still starts from
    factory defaults). The memory is saved with the session, one state
    per console.
  - **Analyser response speed**: new Fast / Med / Slow buttons in the
    sidebar - the IN/OUT curves smooth with a time constant of ~0.15 s
    (Fast, close to the pre-v2.10 behaviour), ~1 s (Med, the v2.10
    default) or ~3 s (Slow). The choice is saved with the session.
  - **Max trace fixed**: the MAX curve now shows the maximum of the
    *displayed* (smoothed) OUT curve over the last 10 seconds, hugging
    the OUT curve as an envelope. v2.10 took the maximum of the raw
    per-frame FFT values instead, which sat far above the smoothed
    curve (instantaneous bins fluctuate 10+ dB frame to frame).
  - Fix: `-Wsign-conversion` warning in PluginEditor.cpp.
- **2.10.0**
  - **Calmer analyser**: the IN/OUT curves now smooth with a fixed
    **~1 s time constant** (was: instant rise, ~36 dB/s fall).
  - **Max hold trace**: a thin light-cyan "MAX" curve overlays the OUT
    curve with the **maximum of the last 10 seconds** per bin - the
    peaks leave a 10 s trace, then drop out. It resets when the editor
    is reopened.
  - **EQ curve, low end fixed**: the transfer curve (OUT-IN) was wobbly
    and ill-defined in the bass. Two causes, two fixes:
    1. The FFT now runs at **8192 points** (was 2048): the native
       resolution goes from ~23 Hz to ~6 Hz at 48 kHz. With 2048
       points, every display bin between 20 and 47 Hz read the *same*
       FFT bin and a 30 Hz tone spanned barely one period per window.
    2. The EQ curve now smooths with a **frequency-dependent time
       constant**: ~2 s in the bass, ~0.3 s in the treble (the console's
       compressor genuinely varies OUT-IN at low frequencies with the
       program, so the bass needs the longer average).
  - **A/B preset compare** for the hosted console: two memory slots
    (**A** / **B** buttons) plus **Copy** (duplicates the current
    settings into the other slot). Toggling stores the settings being
    left and recalls the target; the first visit to a slot starts it
    from the current settings so the toggle never jumps. The slots are
    cleared when another console is instantiated, and they are **saved
    with the session**. Sidebar minimum heights: 800 (analyser shown) /
    610 (hidden), was 770/580.
- **2.9.1**
  - **Crash fix: the DAW could die while scanning the plugin folders.**
    The out-of-process scan helper only exists in the Standalone; inside a
    DAW, "Rescan" fell back to loading every third-party VST3 module
    *in the DAW's own process*, on a background thread. Some modules
    cannot survive that: Native Instruments' **VC 76** calls
    HIToolbox's `TSMGetInputSourceProperty` from its module startup,
    which asserts it runs on the host's **main thread** ->
    `dispatch_assert_queue_fail` -> SIGTRAP -> the whole DAW is killed
    (crash report, Studio Pro 8.1.2, October 2026). Anti-tamper
    SIGTRAPs (iZotope-style) are the same class of problem.
  - Inside a DAW, "Rescan" now only **reloads the saved plugin
    database** (`scanned_plugins.xml`, shared with the Standalone); it
    never loads third-party modules anymore. If the database is empty
    the status line tells you to launch the Standalone once: the full
    folder scan runs **in the Standalone, out of process**, where a
    hostile plugin can only kill the helper.
  - New tooltip on the Rescan button explaining this.
- **2.9.0**
  - **Wider spectrum range**: the vertical scale now tops at **+30 dB**
    (was +10): 10 dB/div spans **+30 to -60 dB**, and the fine scale is
    now **6 dB/div** (was 5), spanning **+18 to -36 dB**. The top is
    always 3 divisions above 0 and the bottom 6 below, so the 0 dB line
    (-23 LUFS) keeps the exact same position — one third of the panel
    height from the top — in both scales. Sessions saved with the old
    5 dB/div are snapped to 6 on reload.
  - **Frequency axis 16 Hz - 24 kHz** (was 10 Hz - 20 kHz): the 20 Hz -
    20 kHz data band is centred with small guarded margins, and the
    **20 kHz label is back** (it now sits inside the panel, like 20 Hz).
    The 16 Hz and 24 kHz edges carry no value. The EQ scale labels on
    the right edge shift just left of the 20 kHz label so the
    bottom-right corner stays readable.
- **2.8.4**
  - Fix: the **-23 LUFS spectrum anchor was calibrated on the wrong
    statistic**, so the curve read far too high on real material. The
    v2.8.1 calibration anchored the average energy *per active bin*
    (`10*log10 (sumEnergy / numActiveBins)`), which only matches the
    dominant level on a perfectly flat spectrum: on a real mix (energy
    concentrated in the bass/mids, hundreds of weak bins still above
    the -90 dB gate) that average lands well below the dominant bands,
    pushing the whole display up by `10*log10 (active/dominant)` —
    easily +10..+20 dB (curve riding at the top of the panel while the
    LUFS readout said -20). The anchor is now the **energy-weighted
    mean of the bin levels** (`sum (w·dB)/sum (w)`, `w = 10^(dB/10)`):
    the loudest bands dominate the average, so the energy-dominant
    bands of a -23 LUFS program sit right on the 0 line whatever the
    spectrum shape, a flat (pink noise) spectrum reads flat at
    LUFS + 23, and volume changes still move the curve 1:1. The EQ
    (OUT-IN) curve is unaffected (the offset cancels in the difference).
- **2.8.3**
  - Build fix: `AudioBuffer::copyFrom` (copying from another buffer) has
    no gain overload, unlike `addFrom` — the mono down-mix now sums both
    bridge channels at unity gain and applies the 0.5 scale once
    (`copyFrom` + `addFrom` + `applyGain`), which is the same (L+R)/2.
    2.8.2 never compiled.
- **2.8.2**
  - **Fix: crash on mono tracks (Studio One, SIGSEGV)**. The five hosted
    bx_console plugins are stereo-only; when the host handed the wrapper
    a mono buffer (e.g. switching a stereo track to mono while the plugin
    is running), the hosted console was left reading an invalid second
    channel pointer and crashed inside its own processing call. The
    hosted console is now ALWAYS instantiated and prepared as 2-in/2-out,
    and `processBlock` bridges mono buffers: the channel is duplicated to
    L/R before the hosted call and down-mixed back ((L+R)/2) after. On
    stereo tracks the buffer is passed straight through with zero copies.
  - **Analyzer frequency axis now spans 10 Hz - 20 kHz** (v2.8.0 started
    at 20 Hz): the 10 Hz left edge carries no value, the 20 Hz label is a
    normal centred label inside the panel, and the 20 kHz label was
    removed to keep the bottom-right corner free for the EQ scale labels
    (which moved back to the panel edge).
- **2.8.1**
  - Fix: the **-23 LUFS reference now anchors the spectrum correctly**.
    v2.8.0 subtracted the program loudness from the display twice, so the
    curve never moved with the volume and never reached the 0 line (the
    FFT bins live on their own scale, tens of dB below the program's
    LUFS). The axis calibration now measures the actual gap between the
    spectrum's energy-weighted mean (over the bins carrying energy, tilt
    applied) and the loudness target: a program played at **-23 LUFS
    reads with its energy-dominant bands on the 0 line**, and playing it
    X dB louder moves the whole spectrum up by X dB (the calibration
    itself settles in ~1.6 s and snaps immediately on the first reading;
    it still cancels in OUT-IN, so the EQ curve is unaffected).
- **2.8.0**
  - **Loudness-referenced analyzer axis**: "0" on the vertical scale now
    corresponds to **-23 LUFS** absolute (annotated next to the grid);
    nine divisions with +10 dB at the top, and a smoothed per-session
    loudness offset (slow EMA, silent passages hold it) shifts the
    spectra so a mix around -23 LUFS reads around the 0 line. It is a
    display calibration only - the measurement code is unchanged.
  - **Selectable scales, in the sidebar next to the tilt**:
    spectrum **10 or 5 dB per division** (+10 to -80 / +10 to -35 dB) and
    transfer curve **4 or 2 dB per division** (+/-18 dB / +/-9 dB). All
    saved in the session state and restored on reload (same fix pattern
    as the v2.7.1 tilt).
  - **Clean log-frequency grid**: vertical graduation at every decade
    tick from 20 Hz to 20 kHz (20, 30, ... 90, 100, 200, ... 900, 1k,
    ... 9k, 10k), labels on the classic frequencies only (20, 100, 200,
    1k, 2k, 10k, 20 kHz).
  - **"Display" toggle**: hides or shows the analyzer strip; the window
    and its minimum height re-fit (770 with the strip, 580 without), so
    hiding it genuinely frees the space. Saved with the session.
  - **Measurements only while the editor is open**: the LUFS meters and
    both spectrum analyzers are skipped in processBlock when the plugin
    window is closed (the meters reset on reopen, so no stale values).
    Sessions full of instances carry zero analysis overhead when the
    mixer view has no open plugin windows.
  - Sidebar compacted: the tilt buttons now share one row.
- **2.7.1**
  - Fix: the saved **tilt is now restored visually** when a session
    reopens. The processor always reloaded it from the state, but the
    editor never read it back, so the buttons and the spectrum fell back
    to the -4.5 dB/oct default after a reload.
- **2.7.0**
  - **Dedicated +/-18 dB scale for the OUT-IN curve**: the transfer
    function now reads like a classic EQ display (its own scale across
    the full panel height, +18/0/-18 labels on the right edge, faint
    0 dB anchor line) instead of being crushed by the spectrum's
    -90..+45 dB scale.
  - **Stable EQ curve**: 5-bin pre-smoothing plus a slow,
    input-energy-weighted EMA (~0.4 s at full energy; weak material
    barely moves the curve). All of it on the UI thread - the audio
    thread is untouched.
- **2.6.0**
  - **Measured transfer function curve (OUT-IN)**: a third green curve in
    the analyzer shows what the hosted console does to the signal, live,
    from the existing IN/OUT spectra — zero added cost on the audio thread.
    Tilt-independent (the tilt cancels in the difference), 3-bin
    pre-smoothing plus a light ~0.1 s EMA, and drawn only where the input
    has energy (silence shows nothing instead of garbage).
  - **Wrapper bypass removed** (button, parameter and the whole dry-copy
    + crossfade path in processBlock): the DAW and the hosted plugin
    already provide a bypass, and the removed path was the wrapper's
    largest per-sample overhead. Rescan is now the only control pinned
    at the bottom of the sidebar. The wrapper exposes no parameter
    anymore; older sessions still load (the saved bypass value is
    ignored).
- **2.5.0**
  - **Analyser strip spans the entire window width** at the top: the
    sidebar and the plugin column only start underneath it, so the
    spectrum display uses all the horizontal space.
  - Minimum window height raised to 720 px so the condensed sidebar still
    fits below the strip; the auto-fit re-frames the window to the hosted
    editor as before.
- **2.4.0**
  - **Everything moved into the condensed left sidebar** (no right column
    anymore): console choice, hosted name, tilt, LUFS IN/OUT, delta,
    status, Rescan and Bypass all live in one 132 px column.
  - **Analyzer takes the full plugin width**: the window now auto-fits
    the hosted editor once it is embedded (and re-fits when switching
    consoles, since their GUI sizes differ), so the analyser strip spans
    exactly the plugin's width instead of a fraction of it.
  - **No scrolling**: the window height also auto-fits, so the whole
    console GUI is visible without scrollbars (clamped to 1024×660 /
    2560×1600; below the fitted size the viewport scrolls as before).
- **2.3.0**
  - **Three-column layout, no top bar**: every control moved off the top
    rows to free vertical space. Left sidebar: console choice, hosted
    plugin name, status, Rescan and Bypass (pinned at the bottom).
    Right settings column: tilt, LUFS IN/OUT and the delta correction.
    The vertical space is now used only by the analyzer strip and the
    hosted plugin editor itself.
  - The spectrum analyzer is now a full-width strip at the **top** of the
    centre column; the hosted editor fills the rest.
  - Window: **1360×840** default, 1024×660 minimum — the tightest width
    that still fits a full ~1000 px bx_console editor between the two
    columns (1280 was not enough for sidebar + console + settings).
- **2.2.0**
  - **bx_console N dropped**: it was never detected on the target machine;
    the shell now hosts five consoles.
  - Console buttons now use short labels: **SSL E, SSL G, SSL J, A9099,
    A200**.
  - **Tilt sign fixed**: -3 / -4.5 / -6 dB/oct were applied backwards
    (flattening became steepening). A negative tilt now flattens a
    naturally falling spectrum, SPAN-style — with -4.5, a typical mix
    reads roughly flat and pink noise appears nearly level.
  - Window resized to frame a bx_console editor without dead space:
    **1024×840** default (was 1280×840), 760×660 minimum.
- **2.1.0**
  - Output-knob detection is gone: the bx_console VST3 plugins expose their
    parameters under names that no name-based heuristic could match
    reliably, so the one-click apply feature was dropped entirely.
  - The LUFS correction is now a **display-only** readout and the main
    indicator of the header: a smoothed (~1.5 s time constant) average of
    the short-term BS.1770 delta (input minus output), telling you how much
    to raise (+) or lower (-) the console's output by hand. It holds its
    last value while the signal is silent, and is bigger and centred.
- **2.0.2**
  - Output-knob detection now scores parameter **word tokens** ("out",
    "output", "outgain", "busout", ...) instead of raw substrings only:
    Plugin Alliance exposes camelCase VST3 names that the previous
    `contains("output")` check missed on every console.
  - When no knob scores high enough, the shell now dumps every continuous
    hosted parameter (name / unit label / range) to the Terminal, so the
    detection can be tuned against the real plugin data.
  - A session saved by v1 (which could host any plugin) is no longer
    auto-hosted at launch: only the five bx_console models are restored.
- **2.0.1**
  - Compile fix (AppleClang 21): the gain-staging log line concatenated two
    C string literals with `+`, which is not a valid binary expression —
    the first literal is now an explicit `juce::String`. No behaviour change.
- **2.0.0**
  - Scope reduction: the only hostable plugins are now the six bx_console
    models (SSL 4000 E, SSL 4000 G, SSL 9000 J, AMEK 9099, AMEK 200,
    bx_console N), selected through six exclusive horizontal buttons
    instead of a combo box listing every scanned plugin.
  - Parameter mapping is gone: no canonical strip controls, no mapping
    editor, no per-plugin mapping file. TheChannelStrip only hosts the
    plugin and shows its own editor.
  - Spectrum analyzer: input and output curves are now **overlaid** on a
    single display, and the display tilt is user-selectable among
    **-3 dB/oct, -4.5 dB/oct (default) and -6 dB/oct** (1 kHz reference).
  - LUFS gain staging replaces the old match/auto engine: a live
    indicator shows the short-term delta in dB (how much to add or
    remove so output loudness matches input), and one click on
    **Apply** sends that correction to the hosted plugin's own
    **Output** knob (auto-detected by name/unit probing). Superseded in
    2.1.0 by a display-only smoothed readout (the auto-detection never
    matched the bx_console VST3 parameter names).
  - The embedded hosted editor now lives in a scrollable viewport that
    fills most of the window and is centred, so the guest plugin is
    finally visible and usable. Default window size 1280×840,
    minimum 1024×660.
  - Session state format updated (tilt, hosted plugin identity and state,
    plain bypass); the out-of-process scanner from 1.0.10 is unchanged.
- **1.0.10**
  - The collection scan now runs **out of process**: the Standalone re-launches
    its own executable as a scan helper (`--tcs-scan-child`), so a hostile
    plugin (anti-tamper SIGTRAP / embedded crash-reporter, as observed with
    iZotope RX) can only kill the helper — never the app. The parent relaunches
    helpers automatically until the scan completes; each helper resumes right
    after the plugin that killed its predecessor (blacklisted via the dead
    man's pedal file). The helper's output is forwarded to the parent's
    Terminal, a helper stuck silent for 5 minutes is killed, and in a DAW host
    (where spawning ourselves is not safe) the scan falls back to in-process.
  - Livelock fix: the pedal-file blacklist taken over by the scanner
    constructor is now persisted immediately — two hostile plugins back to
    back could previously ping-pong between two crashes without ever
    persisting either blacklist.
  - `scanned_plugins.xml` is now written write-then-rename, so a helper being
    killed mid-write cannot truncate the list.
- **1.0.9**
  - A scan killed mid-way by a hostile plugin no longer loses progress: the
    known-plugin list is now saved after **every** scanned file (a scanned
    plugin can crash or even kill the whole process — e.g. the SIGTRAP raised
    by an iZotope RX anti-tamper/Crashpad child process during in-process
    validation). On relaunch, the dead man's pedal file (`scan_pedal.xml`)
    automatically blacklists the file that was being scanned when the process
    died, and the scan resumes right after it — progress is monotonic even if
    several plugins kill the host (each relaunch blacklists the next one).
  - New end-of-folder log line (`folder done: … N scanned, B blacklisted so
    far`) and the final summary now reports the blacklist count.
- **1.0.8**
  - Fixed the crash on quit (SIGSEGV at address 0) when saving state with no
    plugin ever loaded (empty vector indexed out of bounds).
  - The UI no longer freezes while a scan runs: the editor timer skips the
    scan lock while `isScanning()` is set, and the status line shows that a
    scan is in progress.
  - The one-time auto-scan now covers VST3 folders only (scanning AudioUnits
    as well doubled the duration for little benefit).
  - Scan progress is saved after each folder, so quitting mid-scan keeps
    whatever was already found, and every 10th plugin is printed to the
    Terminal to show liveness.
- **1.0.7**
  - Root cause of "0 plugin(s) known": the build had no
    `JUCE_PLUGINHOST_VST3=1` / `JUCE_PLUGINHOST_AU=1`, so
    `AudioPluginFormatManager::addDefaultFormats()` registered nothing and
    every scan silently found 0 plugins. Both flags are now set. If no format
    is ever registered, the scan now logs a clear ERROR instead.
  - On macOS a `.vst3` / `.component` is a directory bundle, so
    `existsAsFile()` is false and the file scan branch was never taken; the
    selected bundle is now explicitly recognized and scanned.
  - Log messages use plain ASCII (em-dashes showed as mojibake in some
    terminal configurations).
- **1.0.6**
  - The default scan now also covers the system-wide plugin folders
    (`/Library/Audio/Plug-Ins/VST3` and `/Library/Audio/Plug-Ins/Components`),
    where most installers (Brainworx, etc.) actually install. The scan picker
    now opens in whichever VST3 folder (system or per-user) contains the
    most plugins.
  - Hosting is no longer silent: scan results, instantiation attempts,
    instantiation errors (previously discarded), latency and parameter count
    are logged to the Terminal (`[TheChannelStrip] ...`) and the last event is
    shown in the status line of the UI.
- **1.0.5**
  - Fixed the link error "Undefined symbols: createPluginFilter()": the
    JUCE plugin entry point (declared in juce_audio_plugin_client, expected
    by the AU / VST3 / Standalone wrappers) is now defined in
    PluginProcessor.cpp and returns a new TheChannelStripAudioProcessor.
- **1.0.4**
  - Fixed two compile errors: `getMainOutputChannelSet()` is not a member of
    `AudioProcessor` in JUCE 8 (channel counts are now taken from
    `getTotalNumInputChannels()` / `getTotalNumOutputChannels()`), and
    `KnownPluginList::scanAndAddFile()` expects a specific `AudioPluginFormat&`
    instead of the format manager (the format matching the file extension is
    now selected explicitly).
  - Added a valid `BUNDLE_ID` to silence the CMake warning about spaces in
    the bundle identifier.
  - Cleaned up shadow warnings (`-Wshadow`, `-Wshadow-field`) and an unused
    variable.
- **1.0.3**
  - Scan picker now opens in `~/Library/Audio/Plug-Ins/VST3` (it previously
    pointed at a non-existent `Application Support/Audio/Plug-Ins` folder).
  - The picker now accepts `.vst3` bundles (file selection was disabled, so
    every plugin appeared greyed out on macOS).
  - First launch now auto-scans the default plugin folders.
- **1.0.2** — 8 compilation fixes (JUCE 8.0.6 API corrections).
- **1.0.1 / 1.0.0** — initial builds.

## License

MIT — see `LICENSE`. Copyright (c) 2026 Clement Costes.
