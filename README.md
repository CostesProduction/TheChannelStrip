# TheChannelStrip

**Version 2.15.5** — VST3 / AU / Standalone (macOS, Windows, Linux)

TheChannelStrip is a **console-strip shell**: it *hosts* your own licensed
bx_console plugins (SSL 4000 E, SSL 4000 G, SSL 9000 J, AMEK 9099,
AMEK 200, Focusrite SC, bx_console N) through the public JUCE hosting APIs, and wraps
them in a clean, focused workflow:

- **Seven fixed slots** — one button per console, stacked in the **left
  sidebar** with short labels (SSL E, SSL G, SSL J, A9099, A200,
  Focusrite, Bx N). Clicking a
  button instantiates that plugin; buttons for plugins that are not
  installed are disabled. Selection is exclusive (radio behaviour), one
  console at a time, and the sidebar shows the hosted console's name.
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
  The scale is nine divisions deep with the top at
  3 x dB/div, so the 0 line sits one third of the panel height from the
  top in both settings. Pick **10 or 6 dB per division** by clicking the
  left dB scale labels
  (10 → +30 to -60 dB, 6 → +18 to -36 dB). The horizontal axis is a clean log scale
  from **16 Hz to 24 kHz** (v2.9: the data-free edges centre the useful
  band and keep the labels clear of the panel borders; no value written
  at the 16 Hz left edge) with a graduation at every 20/30/.../90,
  100/200/.../900, 1k/.../9k, 10k, 20k tick and labels on the classic
  frequencies (20, 100, 200, 1k, 2k, 10k, 20 kHz).
- **Measured transfer function (v2.6)** — a third curve, **OUT-IN**, shows
  what the hosted console actually does to your signal (EQ, dynamics and
  saturation combined), computed live from the two existing analyzer
  spectra. It costs nothing on the audio thread, is independent of the
  display tilt (which cancels in the difference), and is only drawn in
  the frequency ranges where the input has energy. v2.7 gives it a
  **dedicated scale** (classic EQ display range, labelled on the
  right edge, faint 0 dB anchor line) so EQ moves are easy to read, plus
  a slow input-energy-weighted smoothing (~0.4 s at full energy) so the
  curve stays stable and readable. v2.8 makes that scale selectable:
  **4 dB per division (+/-18 dB) or 2 dB per division (+/-9 dB)**, from
  the sidebar.
- **Display toggle (v2.8)** — a "Display" checkbox in the sidebar hides or
  shows the analyzer strip entirely; the window re-fits (smaller without
  the strip), so the space is genuinely freed. The choice is saved with
  the session.
- **Zero-cost when closed (v2.8)** — all measurements (LUFS meters and
  both spectrum analyzers) run **only while the plugin window is open**.
  With the editor closed the audio thread does nothing but the console
  itself, so a mixer full of instances carries no analysis overhead.
- **LUFS gain staging (display)** — the sidebar continuously
  shows, in LUFS-derived dB, how much to raise (+) or lower (-) the sound
  so the hosted console's output matches its input loudness (short-term
  BS.1770). The readout is smoothed over ~1.5 s for a stable, readable
  average and holds its last value while the signal is silent. You apply
  it by hand on the hosted plugin's own output knob.
- **Embedded hosted UI** — the hosted plugin's own editor is embedded in a
  viewport that fills the centre column below the analyzer, so the guest
  plugin is actually visible and usable. The layout is a **condensed left
  sidebar + centre column, nothing else** (v2.4): the sidebar carries the
  console choice, the hosted name, the tilt, the LUFS/delta readouts and
  Rescan; the centre column is used only by the analyzer strip
  and the hosted plugin itself. The **window auto-fits the hosted editor**
  once it is embedded (and re-fits when you switch consoles, since their
  GUI sizes differ): the analyzer strip gets the plugin's full width and
  nothing needs to scroll. The window stays resizable (1024×800 minimum
  with the analyzer shown, 1024×610 with it hidden;
  below the auto-fit size the viewport simply scrolls again).
- **Latency forwarding** — the hosted plugin's reported latency is passed
  on to the DAW.
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
   header. Use the tilt buttons (-3 / -4.5 / -6 dB/oct) to switch slope
   weighting, the SCALE buttons (10 / 5 dB per division) to change the
   vertical range and the EQ buttons (4 / 2 dB per division) to change
   the transfer-curve range; "Display" hides or shows the whole analyzer
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
    analyser's left dB scale toggles 10 / 6 dB per division; clicking
    the right green EQ scale toggles 4 / 2 dB per division. The window
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
