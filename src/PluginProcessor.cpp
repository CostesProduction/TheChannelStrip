#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdlib>

// v1.0.10: command-line flag that turns the Standalone into the out-of-process
// scan helper spawned by the parent instance (see launchScanChildren()).
static const char* scanChildFlag = "--tcs-scan-child";

static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // v2.6: the wrapper no longer exposes any parameter. The bypass
    // (the only one left) was removed: the DAW and the hosted plugin
    // already provide a bypass, and the wrapper's own crossfade path
    // cost CPU for nothing. The tilt stays a private state attribute.
    return layout;
}

// v2.17: order reworked at Clement's request - the AMEK 9099 leads,
// the SSL trio follows, then the Plugin Alliance channel strips
// (bx_console N, the two Lindells, Focusrite), and the AMEK 200
// closes the list. Only the display/hosting order changed: saved
// sessions match consoles by name, and per-console memory is keyed
// by plugin ident, so nothing else moves.
const char* const TheChannelStripAudioProcessor::consoleNames[TheChannelStripAudioProcessor::numConsoles] =
{
    "bx_console AMEK 9099",
    "bx_console SSL 4000 E",
    "bx_console SSL 4000 G",
    "bx_console SSL 9000 J",
    "bx_console N",
    "Lindell 80 Channel",
    "Lindell 50 Channel",
    "bx_console Focusrite SC",
    "bx_console AMEK 200"
};

juce::String TheChannelStripAudioProcessor::consoleLabel (int index)
{
    switch (index)
    {
        case 0: return "A9099";     // v2.17: bx_console AMEK 9099 leads the list
        case 1: return "SSL E";    // bx_console SSL 4000 E
        case 2: return "SSL G";    // bx_console SSL 4000 G
        case 3: return "SSL J";    // bx_console SSL 9000 J
        case 4: return "Bx N";     // v2.13: bx_console N (back from v1)
        case 5: return "Lindell N"; // v2.16: Lindell 80 Channel
        case 6: return "Lindell A"; // v2.16: Lindell 50 Channel
        case 7: return "Focusrite"; // v2.13: bx_console Focusrite SC
        case 8: return "A200";     // bx_console AMEK 200
        default: return {};
    }
}

TheChannelStripAudioProcessor::TheChannelStripAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    loadKnownPluginList();

    // v1.0.10: helper-child mode. The parent instance re-launches this very
    // executable with --tcs-scan-child to run the collection scan OUT of
    // process, so that a hostile plugin (anti-tamper SIGTRAP, as observed
    // with iZotope RX) can only kill the helper, never the app itself.
    if (juce::JUCEApplicationBase::getCommandLineParameterArray().contains (scanChildFlag))
        runScanChildAndExit();
}

TheChannelStripAudioProcessor::~TheChannelStripAudioProcessor()
{
    saveKnownPluginList();

    // v2.15.5: release the COM references borrowed from the hosted
    // console before the members are torn down (grForwarder is also
    // declared after hostedStrip, so it would be destroyed first
    // anyway - but be explicit about it).
    grForwarder.disconnect();
}

juce::String TheChannelStripAudioProcessor::identFor (const juce::PluginDescription& d)
{
    return d.pluginFormatName + "|" + d.fileOrIdentifier + "|" + d.name;
}

juce::File TheChannelStripAudioProcessor::getAppDataDir() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("TheChannelStrip");
}

bool TheChannelStripAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    // v2.14: mono tracks now run the hosted console natively in mono (see
    // tryApplyMonoLayout) instead of always bridging them to stereo.
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

// ---- v2.14: native mono hosting --------------------------------------------
// On a mono track the hosted console is asked for a native mono->mono bus
// layout. Plugin Alliance's bx_console models run their mono processing
// path on dedicated mono channels in Cubase / ProTools / Logic (their
// support pages confirm the mono GUI shows up on mono tracks there), so
// accepting the layout cuts the console's per-channel DSP cost roughly in
// half compared to the v2.8.2 stereo bridge.
//
// The JUCE VST3/AU hosting only queries the real plugin about a requested
// layout while the instance is INACTIVE (released); an active instance
// accepts any layout locally and the plugin's refusal surfaces too late.
// So this must run between releaseResources() and prepareToPlay() - which
// is exactly when it is called (the host releases us before re-preparing
// after a track format change, and instantiateHosted negotiates a fresh,
// never-prepared instance). A refusal is not an error: processBlock simply
// keeps using the proven stereo bridge.
bool TheChannelStripAudioProcessor::tryApplyMonoLayout (juce::AudioProcessor& plugin)
{
    auto layouts = plugin.getBusesLayout();

    // v2.14.2: only the MAIN bus is switched to mono. Sidechain or other
    // auxiliary buses keep their current (usually disabled/empty) state -
    // exactly what a DAW negotiates for a mono main arrangement. The
    // v2.14.0/2.14.1 bail-out on any extra bus forced every such plugin
    // onto the stereo bridge even though only the main bus mattered.
    if (layouts.inputBuses.isEmpty() || layouts.outputBuses.isEmpty())
    {
        hostedMonoResult.store (3, std::memory_order_relaxed);
        juce::Logger::writeToLog ("[TheChannelStrip] mono layout not applicable: hosted plugin has "
                                  + juce::String ((int) layouts.inputBuses.size()) + " input / "
                                  + juce::String ((int) layouts.outputBuses.size())
                                  + " output buses (stereo bridge is used)");
        return false;
    }

    layouts.getChannelSet (true, 0)  = juce::AudioChannelSet::mono();
    layouts.getChannelSet (false, 0) = juce::AudioChannelSet::mono();

    plugin.releaseResources();

    if (! plugin.setBusesLayout (layouts))
    {
        hostedMonoResult.store (1, std::memory_order_relaxed);
        juce::Logger::writeToLog ("[TheChannelStrip] mono layout REFUSED by the hosted plugin"
                                  " (stereo bridge is used)");
        return false;
    }

    // JUCE can fall back to a next-best layout; only trust a layout the
    // plugin actually ended up in. v2.14.2: judge the outcome on the MAIN
    // bus only - a sidechain bus would add channels to the total counts.
    const auto settled = plugin.getBusesLayout();
    const auto mainIn  = settled.getMainInputChannelSet();
    const auto mainOut = settled.getMainOutputChannelSet();

    // v2.14.3: juce::AudioChannelSet has no isMono() member (compile error
    // on AppleClang 21 in v2.14.2) - the canonical check is a comparison
    // with AudioChannelSet::mono(), same lesson as v2.13.1: verify the
    // exact spelling of JUCE APIs against the module headers before
    // shipping a zip.
    if (mainIn != juce::AudioChannelSet::mono() || mainOut != juce::AudioChannelSet::mono())
    {
        settledMainIn.store  (mainIn.size(),  std::memory_order_relaxed);
        settledMainOut.store (mainOut.size(), std::memory_order_relaxed);
        hostedMonoResult.store (2, std::memory_order_relaxed);
        juce::Logger::writeToLog ("[TheChannelStrip] mono layout requested but the hosted plugin"
                                  " settled on " + juce::String (mainIn.size()) + " in / "
                                  + juce::String (mainOut.size())
                                  + " out on the main bus (stereo bridge is used)");
        return false;
    }

    hostedMonoResult.store (0, std::memory_order_relaxed);
    juce::Logger::writeToLog ("[TheChannelStrip] hosted console switched to NATIVE MONO");
    return true;
}

// ---- v2.14.2: sidebar readout -----------------------------------------------
// Short status line for the editor: what the DAW feeds this instance and,
// on a mono track, which path the console takes and why.
juce::String TheChannelStripAudioProcessor::getChannelReadout() const
{
    const int trackCh = wrapperChannels.load (std::memory_order_relaxed);

    if (trackCh >= 2)
        return "track: stereo";

    switch (hostedMonoResult.load (std::memory_order_relaxed))
    {
        case 0:  return "track: mono (native)";
        case 1:  return "track: mono (bridge: refused)";
        case 2:  return "track: mono (bridge: "
                      + juce::String (settledMainIn.load (std::memory_order_relaxed))
                      + "/" + juce::String (settledMainOut.load (std::memory_order_relaxed))
                      + ")";
        default: return "track: mono (bridge: multi-bus)";
    }
}

void TheChannelStripAudioProcessor::restoreStereoLayout (juce::AudioProcessor& plugin)
{
    plugin.releaseResources();

    // v2.8.2 proven path: 2-in/2-out whatever the wrapper's track format
    // is (mono tracks are bridged in processBlock; see stereoBridge).
    plugin.setPlayConfigDetails (2, 2, currentSampleRate, currentBlockSize);
}

void TheChannelStripAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = juce::jmax (8000.0, sampleRate);
    currentBlockSize = juce::jmax (1, samplesPerBlock);
    numChannels = juce::jlimit (1, 2, juce::jmax (getTotalNumInputChannels(),
                                                 getTotalNumOutputChannels()));

    // v2.14.1: track-format diagnostics (editor readout + Terminal log).
    wrapperChannels.store (numChannels, std::memory_order_relaxed);
    juce::Logger::writeToLog ("[TheChannelStrip] track format: "
                              + juce::String (numChannels)
                              + (numChannels == 1 ? " ch (mono)" : " ch (stereo)"));

    lufsIn.prepare  (currentSampleRate, numChannels);
    lufsOut.prepare (currentSampleRate, numChannels);
    analyzerIn.prepare  (currentSampleRate);
    analyzerOut.prepare (currentSampleRate);

    // v2.14: re-negotiate the hosted console's own bus layout whenever the
    // track format changes (the host has released us before calling this,
    // so the hosted instance is inactive and the JUCE hosting queries the
    // real plugin). The instance is NEVER re-created, so its parameters and
    // internal state survive the mono <-> stereo switch - unlike the
    // param-resetting format swaps some hosts do with separate mono and
    // stereo plugin versions. On a mono track the console is first asked
    // for a native mono layout (about half the DSP of the stereo bridge);
    // a refusal falls back to the v2.8.2 2-in/2-out bridge.
    bool mono = false;

    if (auto* active = hostedStrip.getActivePlugin())
    {
        if (numChannels == 1)
            mono = tryApplyMonoLayout (*active);
        else
            restoreStereoLayout (*active);
    }

    hostedMonoMode.store (mono, std::memory_order_relaxed);

    // The hosted strip's scratch buffers match the console's own layout.
    hostedStrip.prepareToPlay (currentSampleRate, currentBlockSize, mono ? 1 : 2);
}

void TheChannelStripAudioProcessor::releaseResources()
{
    hostedStrip.releaseResources();
}

void TheChannelStripAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                  juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    // v2.8: the LUFS meters and spectrum analysers feed the editor only.
    // When no editor is open they are skipped entirely, so a session full
    // of instances (or a mixer view without open plugin windows) pays
    // nothing for the display path. The audio chain itself is unchanged.
    const bool measure = measuringEnabled.load (std::memory_order_relaxed);

    if (measure)
    {
        lufsIn.process (buffer);
        analyzerIn.processAudio (buffer);
    }

    // v2.8.2 crash fix (SIGSEGV inside the hosted console when Studio One
    // switches a track stereo -> mono), now the FALLBACK since v2.14: a
    // stereo-configured hosted console crashes if handed a 1-channel
    // buffer (its second input channel pointer is invalid, KERN_INVALID
    // ADDRESS at 0 as in the Studio One crash report). Since v2.14 a mono
    // track first asks the console for a NATIVE mono->mono layout
    // (hostedMonoMode) so it runs its own mono processing path; only if
    // the console refuses the layout do we bridge: mono is up-mixed into
    // a stereo scratch (L = R = mono), the console runs in stereo exactly
    // as on a stereo track, and the result is down-mixed (L + R) / 2 back
    // into the mono channel. Stereo tracks run exactly as before, with
    // zero copies added.
    if (buffer.getNumChannels() < 2
        && ! hostedMonoMode.load (std::memory_order_relaxed))
    {
        stereoBridge.setSize (2, numSamples, false, false, true);
        stereoBridge.copyFrom (0, 0, buffer, 0, 0, numSamples);
        stereoBridge.copyFrom (1, 0, buffer, 0, 0, numSamples);

        hostedStrip.processBlock (stereoBridge);

        // down-mix the stereo result back into the mono channel:
        // (L + R) / 2. v2.8.3: AudioBuffer::copyFrom (from another
        // buffer) has no gain overload (unlike addFrom), so sum both
        // channels at unity gain first, then scale once.
        buffer.copyFrom  (0, 0, stereoBridge, 0, 0, numSamples);
        buffer.addFrom   (0, 0, stereoBridge, 1, 0, numSamples);
        buffer.applyGain (0, 0, numSamples, 0.5f);
    }
    else
    {
        hostedStrip.processBlock (buffer);
    }

    if (measure)
    {
        lufsOut.process (buffer);
        analyzerOut.processAudio (buffer);
    }

    // ---- forward the hosted plugin's reported latency to the host ----
    // v2.16.1: refresh the cached value first - the console can change
    // its own latency at runtime (oversampling in the Lindell channels).
    hostedStrip.refreshReportedLatency();
    const int hostedLatency = hostedStrip.getReportedLatency();
    if (hostedLatency != lastReportedLatency)
    {
        lastReportedLatency = hostedLatency;
        setLatencySamples (hostedLatency);
    }
}

// v2.8: the editor calls this from its constructor/destructor (see
// PluginEditor). Opening resets the meters so the display never shows
// values measured before a previous close.
void TheChannelStripAudioProcessor::setEditorOpen (bool isOpen)
{
    measuringEnabled.store (isOpen, std::memory_order_relaxed);

    if (isOpen)
    {
        lufsIn.resetMeasurement();
        lufsOut.resetMeasurement();
    }
}

// ---------------------------------------------------------------------------
// console selection
// ---------------------------------------------------------------------------

std::array<TheChannelStripAudioProcessor::ConsoleSlot, TheChannelStripAudioProcessor::numConsoles>
TheChannelStripAudioProcessor::getConsoleSlots()
{
    std::array<ConsoleSlot, numConsoles> slots;

    juce::Array<juce::PluginDescription> types;
    {
        const juce::ScopedLock lock (scanLock);
        types = hostedStrip.getKnownPluginList().getTypes();
    }

    for (int i = 0; i < numConsoles; ++i)
    {
        slots[(size_t) i].index = i;
        slots[(size_t) i].name  = consoleNames[i];

        int bestScore = 0;

        for (auto& t : types)
        {
            if (! t.name.containsIgnoreCase (consoleNames[i]))
                continue;

            // prefer an exact name match and the VST3 flavour
            int score = t.name.equalsIgnoreCase (consoleNames[i]) ? 2 : 1;

            if (t.pluginFormatName.equalsIgnoreCase ("VST3"))
                score += 2;

            if (score > bestScore)
            {
                bestScore = score;
                slots[(size_t) i].desc  = t;
                slots[(size_t) i].found = true;
            }
        }
    }

    return slots;
}

void TheChannelStripAudioProcessor::selectConsole (int consoleIndex)
{
    if (! juce::isPositiveAndBelow (consoleIndex, numConsoles))
        return;

    auto slots = getConsoleSlots();
    const auto& slot = slots[(size_t) consoleIndex];

    if (! slot.found)
    {
        logHostStatus (juce::String (consoleNames[consoleIndex])
                       + " was not found in the scanned VST3 folders. "
                         "Check that it is installed, or press Rescan.");
        return;
    }

    // v2.11: keep the console being left exactly as it is, so coming
    // back to it later restores these settings.
    rememberCurrentConsoleState();

    currentPluginIdent = identFor (slot.desc);

    logHostStatus ("Instantiating '" + slot.desc.name + "'...");
    juce::Thread::launch ([this, desc = slot.desc] () { instantiateHosted (desc, false); });
}

void TheChannelStripAudioProcessor::clearHostedPlugin()
{
    // v2.15.5: stop forwarding gain reduction BEFORE the console goes
    // away, so the host can never pull through a dying instance.
    grForwarder.disconnect();
    currentPluginIdent = {};
    hostedStrip.clearPlugin();
}

// ---------------------------------------------------------------------------
// v2.11: per-console settings memory. Switching consoles destroys and
// re-creates the hosted plugin, which reset every knob to factory
// defaults (Clement's complaint) - and switching BACK also lost the
// settings. Each console's last state is now remembered: switching
// SSL J -> SSL G -> SSL J brings J back exactly as it was left. A
// console hosted for the very first time still starts from factory
// defaults. The memory lives in RAM and is saved with the session (one
// <consoleState> element per console). Note: the A/B preset slots of
// v2.10 were REMOVED - Clement's use case was comparing CONSOLES, not
// two states of the same console (the bx_consoles already ship their
// own A/B/C/D presets).
// ---------------------------------------------------------------------------

void TheChannelStripAudioProcessor::rememberCurrentConsoleState()
{
    auto* active = hostedStrip.getActivePlugin();

    if (active == nullptr || currentPluginIdent.isEmpty())
        return;

    juce::MemoryBlock state;
    active->getStateInformation (state);

    if (state.getSize() == 0)
        return;

    const juce::ScopedLock lock (consoleStateLock);
    consoleStates[currentPluginIdent] = std::move (state);
}

// ---------------------------------------------------------------------------
// gain staging (display only: the correction is shown, never applied)
// ---------------------------------------------------------------------------

bool TheChannelStripAudioProcessor::getGainCorrectionDb (float& deltaDb) const
{
    const float inLufs  = lufsIn.getShortTermLufs();
    const float outLufs = lufsOut.getShortTermLufs();

    if (inLufs < -70.0f || outLufs < -70.0f)
        return false;

    // v2.13: back to the correction reading (input - output), now
    // prefixed with "Add" in the display: positive = raise the output
    // by that many dB to match the input loudness, negative = lower it.
    deltaDb = inLufs - outLufs;
    return true;
}

// ---------------------------------------------------------------------------
// hosting
// ---------------------------------------------------------------------------

void TheChannelStripAudioProcessor::requestDefaultScan()
{
    // v1.0.8: set the flag on the calling thread BEFORE launching the worker,
    // so the editor timer can skip the scan lock as soon as we return.
    scanning.store (true, std::memory_order_relaxed);

    juce::Thread::launch ([this] () { runScan(); });
}

void TheChannelStripAudioProcessor::runScan()
{
    const juce::ScopedLock lock (scanLock);

    // v1.0.8: clear the scanning flag on every exit path.
    struct FlagReset
    {
        std::atomic<bool>& f;
        ~FlagReset() { f.store (false, std::memory_order_relaxed); }
    } resetFlag { scanning };

    logHostStatus ("Scanning the default VST3 folders...");

    // v1.0.7 fix: without JUCE_PLUGINHOST_VST3 / JUCE_PLUGINHOST_AU,
    // addDefaultFormats() registers NOTHING and every scan silently finds
    // 0 plugins. Fail loudly instead.
    if (hostedStrip.getFormatManager().getNumFormats() == 0)
    {
        logHostStatus ("ERROR: no plugin format is registered. The build is missing "
                       "JUCE_PLUGINHOST_VST3=1 / JUCE_PLUGINHOST_AU=1 "
                       "(see target_compile_definitions in CMakeLists.txt).");
        return;
    }

    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    // v1.0.10: the collection scan runs OUT OF PROCESS. In the Standalone we
    // spawn this same executable with --tcs-scan-child; a hostile plugin can
    // only kill the helper, and the parent relaunches helpers until the scan
    // completes. In a DAW host we do NOT scan at all (v2.9.1, see below); the
    // in-process fallback is only kept for the Standalone when spawning the
    // helper fails (a crash there only kills the Standalone). The scan covers
    // VST3 only (validating a large AudioUnit collection too doubles the scan
    // time for little benefit).
    bool handledByChildren = false;

    if (isStandaloneApp())
        handledByChildren = launchScanChildren();

    if (handledByChildren)
    {
        loadKnownPluginList();
    }
    else if (! isStandaloneApp())
    {
        // v2.9.1 crash fix: NEVER load third-party plugin modules in-process
        // while running inside a DAW. The out-of-process helper only exists
        // in the Standalone, and the old in-process fallback loaded every
        // VST3 module on this background thread - some modules cannot
        // survive that: Native Instruments' VC 76 calls HIToolbox's
        // TSMGetInputSourceProperty from its module startup, which asserts
        // it runs on the host's MAIN thread -> dispatch_assert_queue_fail ->
        // SIGTRAP -> the whole DAW dies (crash report, Studio Pro 8.1.2,
        // October 2026). Anti-tamper SIGTRAPs (iZotope-style) are the same
        // class of problem. So inside a DAW, "Rescan" only RELOADS the saved
        // database (scanned_plugins.xml, shared with the Standalone); the
        // full folder scan runs in the Standalone, where a hostile plugin
        // can only kill the helper.
        loadKnownPluginList();

        const auto& types = hostedStrip.getKnownPluginList().getTypes();

        if (types.isEmpty())
            logHostStatus ("No saved plugin database: launch the Standalone once. "
                           "It scans the VST3 folders OUT OF PROCESS and shares the "
                           "result with the plugin instances.");
        else
            logHostStatus ("Plugin database reloaded - "
                           + juce::String (types.size()) + " plugin(s) known, "
                           + juce::String (hostedStrip.getKnownPluginList().getBlacklistedFiles().size())
                           + " blacklisted. The full folder scan only runs in the "
                             "Standalone (out-of-process); loading third-party modules "
                             "here could crash the host.");
    }
    else
    {
        // Standalone, and spawning the helper failed: in-process fallback.
        // A hostile plugin can only kill the Standalone itself here.
        scanDirectoryForFormat (juce::File ("/Library/Audio/Plug-Ins/VST3"));
        saveKnownPluginList();
        scanDirectoryForFormat (home.getChildFile ("Library/Audio/Plug-Ins/VST3"));
        saveKnownPluginList();
    }

    saveKnownPluginList();
    logHostStatus ("Scan finished - "
                   + juce::String (hostedStrip.getKnownPluginList().getTypes().size())
                   + " plugin(s) known, "
                   + juce::String (hostedStrip.getKnownPluginList().getBlacklistedFiles().size())
                   + " blacklisted.");
    maybeRestoreHosted();
}

void TheChannelStripAudioProcessor::logHostStatus (const juce::String& message)
{
    // Visible in the Terminal when the Standalone is launched from the shell.
    juce::Logger::writeToLog ("[TheChannelStrip] " + message);

    // juce::CriticalSection is re-entrant, so this is safe even when the
    // caller already holds the lock (e.g. runScan).
    const juce::ScopedLock lock (scanLock);
    hostStatus = message;
}

juce::String TheChannelStripAudioProcessor::getHostStatus()
{
    const juce::ScopedLock lock (scanLock);
    return hostStatus;
}

bool TheChannelStripAudioProcessor::isStandaloneApp() const
{
    // In the Standalone, JUCE runs a JUCEApplication whose name is the plugin
    // name (JucePlugin_Name, defined by juce_add_plugin). In a DAW there is
    // either no JUCE application at all, or it is the DAW's own.
    auto* app = juce::JUCEApplicationBase::getInstance();
    return app != nullptr && app->getApplicationName() == "TheChannelStrip";
}

bool TheChannelStripAudioProcessor::launchScanChildren()
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const juce::File doneFlag = getAppDataDir().getChildFile ("scan_done.flag");
    doneFlag.deleteFile();

    const int maxChildRuns = 50;

    for (int run = 1; run <= maxChildRuns; ++run)
    {
        juce::ChildProcess child;

        juce::StringArray args;
        args.add (exe.getFullPathName());
        args.add (scanChildFlag);

        if (! child.start (args))
        {
            logHostStatus ("ERROR: cannot launch the out-of-process scan helper ('"
                           + exe.getFullPathName() + "'). Falling back to an in-process scan.");
            return false;
        }

        logHostStatus ("scan helper " + juce::String (run) + "/" + juce::String (maxChildRuns)
                       + ": scanning out-of-process (a hostile plugin can only kill the helper)...");

        drainChildOutput (child);

        if (doneFlag.existsAsFile())
        {
            logHostStatus ("Out-of-process scan complete.");
            return true;
        }

        // The helper died before finishing: a hostile plugin killed it, the
        // dead man's pedal file blacklisted that plugin, and the next helper
        // run resumes right after it. Progress is monotonic. Pick up what the
        // helpers have found so far so the counters below are accurate.
        loadKnownPluginList();

        logHostStatus ("Scan helper was killed by a hostile plugin - relaunching. "
                       "plugin(s) found so far: "
                       + juce::String (hostedStrip.getKnownPluginList().getTypes().size())
                       + ", blacklisted: "
                       + juce::String (hostedStrip.getKnownPluginList().getBlacklistedFiles().size())
                       + ".");
        juce::Thread::sleep (300);
    }

    logHostStatus ("Scan helper stopped after " + juce::String (maxChildRuns)
                   + " attempts. Relaunch TheChannelStrip to continue the scan.");
    return true;
}

void TheChannelStripAudioProcessor::drainChildOutput (juce::ChildProcess& child)
{
    char buffer[8192];
    juce::String pending;
    auto lastActivityMs = juce::Time::getMillisecondCounter();
    bool wasKilled = false;

    auto flushLines = [&pending] (bool flushRemainder)
    {
        for (;;)
        {
            const auto idx = pending.indexOfChar ('\n');
            if (idx < 0)
                break;

            auto line = pending.substring (0, idx);
            pending = pending.substring (idx + 1);
            line = line.replaceCharacter ('\r', ' ').trimEnd();

            if (line.isNotEmpty())
                juce::Logger::writeToLog (line);
        }

        if (flushRemainder)
        {
            auto line = pending.replaceCharacter ('\r', ' ').trim();
            pending = {};

            if (line.isNotEmpty())
                juce::Logger::writeToLog (line);
        }
    };

    while (child.isRunning())
    {
        const int n = child.readProcessOutput (buffer, (int) sizeof (buffer));

        if (n > 0)
        {
            lastActivityMs = juce::Time::getMillisecondCounter();
            pending += juce::String::fromUTF8 (buffer, n);
            flushLines (false);
        }
        else if (juce::Time::getMillisecondCounter() - lastActivityMs > 300000)
        {
            // 5 minutes of total silence: the helper is stuck (e.g. a plugin
            // popped a modal dialog). Kill it; progress saved so far is kept.
            juce::Logger::writeToLog ("[TheChannelStrip] scan helper silent for 5 min - killing it.");
            wasKilled = true;
            child.kill();
            break;
        }
        else
        {
            juce::Thread::sleep (200);
        }
    }

    // drain whatever is left in the pipe (skipped after kill(): reading from
    // a killed process is documented as undefined behaviour)
    if (! wasKilled)
    {
        for (;;)
        {
            const int n = child.readProcessOutput (buffer, (int) sizeof (buffer));

            if (n <= 0)
                break;

            pending += juce::String::fromUTF8 (buffer, n);
            flushLines (false);
        }
    }

    flushLines (true);
}

void TheChannelStripAudioProcessor::runScanChildAndExit()
{
    juce::Logger::writeToLog ("[TheChannelStrip] scan helper: scanning the default VST3 folders...");

    scanDirectoryForFormat (juce::File ("/Library/Audio/Plug-Ins/VST3"));
    saveKnownPluginList();

    scanDirectoryForFormat (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                .getChildFile ("Library/Audio/Plug-Ins/VST3"));
    saveKnownPluginList();

    getAppDataDir().getChildFile ("scan_done.flag").create();

    juce::Logger::writeToLog ("[TheChannelStrip] scan helper finished - "
                              + juce::String (hostedStrip.getKnownPluginList().getTypes().size())
                              + " plugin(s) known, "
                              + juce::String (hostedStrip.getKnownPluginList().getBlacklistedFiles().size())
                              + " blacklisted.");

    std::exit (0);
}

void TheChannelStripAudioProcessor::scanDirectoryForFormat (const juce::File& directory)
{
    if (! directory.isDirectory())
        return;

    if (directory.getFileName() == "Plug-Ins")
    {
        scanDirectoryForFormat (directory.getChildFile ("VST3"));
        scanDirectoryForFormat (directory.getChildFile ("Components"));
        return;
    }

    auto& fm = hostedStrip.getFormatManager();

    const auto dirName = directory.getFileName().toLowerCase();
    const bool wantAU = dirName.contains ("component");
    juce::AudioPluginFormat* format = nullptr;

    for (int i = 0; i < fm.getNumFormats(); ++i)
    {
        auto* f = fm.getFormat (i);
        if (f == nullptr)
            continue;

        const auto name = f->getName();
        if (wantAU ? name.containsIgnoreCase ("AudioUnit")
                   : name.containsIgnoreCase ("VST3"))
        {
            format = f;
            break;
        }
    }

    if (format == nullptr)
        return;

    getAppDataDir().createDirectory();

    juce::FileSearchPath searchPath;
    searchPath.add (directory);

    juce::PluginDirectoryScanner scanner (hostedStrip.getKnownPluginList(),
                                          *format,
                                          searchPath,
                                          true,
                                          getAppDataDir().getChildFile ("scan_pedal.xml"),
                                          false);

    // v1.0.10: the scanner constructor has just consumed the dead man's
    // pedal file and blacklisted whatever plugin killed the previous scan
    // run. Persist that IMMEDIATELY: with two hostile plugins back to back,
    // a run that dies before its first successful per-file save would
    // otherwise blacklist A then die on B, blacklist B then die on A,
    // forever, without ever persisting either blacklist (ping-pong livelock).
    saveKnownPluginList();

    juce::String nameBeingScanned;
    int n = 0;
    while (scanner.scanNextFile (true, nameBeingScanned))
    {
        ++n;

        // v1.0.9: persist after EVERY scanned file. A scanned plugin can crash
        // or even kill the whole process (SIGTRAP from anti-tamper code, as
        // observed with some iZotope RX plugins); without this, a crash
        // mid-folder lost the entire folder's progress. On relaunch, the
        // dead man's pedal file (scan_pedal.xml) blacklists the file that was
        // being scanned when the process died, and the scan resumes right
        // after it.
        saveKnownPluginList();

        if ((n % 10) == 1)
            juce::Logger::writeToLog ("[TheChannelStrip] scanning... " + juce::String (n)
                                      + " done, at: " + nameBeingScanned);
    }

    juce::Logger::writeToLog ("[TheChannelStrip] folder done: " + directory.getFullPathName()
                              + " (" + juce::String (n) + " scanned, "
                              + juce::String (hostedStrip.getKnownPluginList().getBlacklistedFiles().size())
                              + " blacklisted so far)");
}

void TheChannelStripAudioProcessor::instantiateHosted (const juce::PluginDescription& desc,
                                                       bool applyPendingState)
{
    juce::String error;

    std::unique_ptr<juce::AudioProcessor> plugin (
        hostedStrip.getFormatManager().createPluginInstance (desc, currentSampleRate,
                                                             currentBlockSize, error));

    if (plugin == nullptr)
    {
        // v1.0.6 fix: this failure used to be completely silent.
        logHostStatus ("ERROR: cannot instantiate '" + desc.name + "': "
                       + (error.isEmpty() ? juce::String ("unknown error") : error));
        return;
    }

    // v2.14.2: bus inventory of the fresh instance, so the layout
    // negotiation can be followed in the Terminal (a sidechain bus is
    // expected on many Plugin Alliance VST3s and only ever affects the
    // main-bus mono request, never the bridge).
    {
        const auto freshLayouts = plugin->getBusesLayout();
        juce::Logger::writeToLog ("[TheChannelStrip] hosted '" + desc.name + "' buses: "
                                  + juce::String ((int) freshLayouts.inputBuses.size())
                                  + " in / "
                                  + juce::String ((int) freshLayouts.outputBuses.size())
                                  + " out");
    }

    // v2.14: match the fresh instance to the track's current format. On a
    // mono track the console is asked for a native mono layout first (the
    // instance is fresh and never prepared, so the JUCE hosting queries
    // the real plugin for the request); a refusal falls back to the
    // v2.8.2 proven 2-in/2-out configuration and the processBlock bridge.
    bool mono = false;

    if (getTotalNumInputChannels() == 1)
        mono = tryApplyMonoLayout (*plugin);

    if (! mono)
        restoreStereoLayout (*plugin);

    hostedMonoMode.store (mono, std::memory_order_relaxed);

    plugin->prepareToPlay (currentSampleRate, currentBlockSize);

    const int numHostedParams = (int) plugin->getParameters().size();

    hostedStrip.setPlugin (std::move (plugin));

    juce::String message = "Hosted '" + desc.name + "' ready (latency "
                         + juce::String (hostedStrip.getActivePlugin()->getLatencySamples())
                         + " samples, " + juce::String (numHostedParams) + " parameters"
                         + (mono ? ", native mono layout" : "") + ").";
    logHostStatus (message);

    // v2.15.5: the console is fully instantiated - connect the Studio One
    // gain-reduction forwarder to it (pull-only chain: the host asks the
    // wrapper, the wrapper asks us, we ask the console). The forwarder
    // logs either "forwarding to the host" or the exact reason the
    // console does not expose PreSonus::IGainReductionInfo.
    if (auto* instance = dynamic_cast<juce::AudioPluginInstance*> (hostedStrip.getActivePlugin()))
        grForwarder.connect (*instance);
    else
        juce::Logger::writeToLog ("[TheChannelStrip] gain reduction: hosted plugin is not an "
                                  "AudioPluginInstance - nothing to forward");

    if (applyPendingState && ! savedHostedStateForRestore.isEmpty())
    {
        if (auto* active = hostedStrip.getActivePlugin())
        {
            active->setStateInformation (savedHostedStateForRestore.getData(),
                                         (int) savedHostedStateForRestore.getSize());

            // v2.11: the state restored from the session becomes this
            // console's remembered state (captured BEFORE the reset below)
            const juce::ScopedLock lock (consoleStateLock);
            consoleStates[currentPluginIdent] = savedHostedStateForRestore;
        }

        savedHostedStateForRestore.reset();
        pendingRestore.store (false, std::memory_order_relaxed);
    }

    // v2.11: per-console settings memory. A console chosen by hand
    // recalls the state remembered from the last time it was hosted
    // (factory defaults the first time).
    if (! applyPendingState)
    {
        if (auto* active = hostedStrip.getActivePlugin())
        {
            juce::MemoryBlock remembered;

            {
                const juce::ScopedLock lock (consoleStateLock);
                const auto it = consoleStates.find (currentPluginIdent);

                if (it != consoleStates.end())
                    remembered = it->second;
            }

            if (remembered.getSize() > 0)
                active->setStateInformation (remembered.getData(),
                                             (int) remembered.getSize());
        }
    }
}

void TheChannelStripAudioProcessor::maybeRestoreHosted()
{
    if (! pendingRestore.load (std::memory_order_relaxed) || currentPluginIdent.isEmpty())
        return;

    juce::PluginDescription desc;
    {
        const juce::ScopedLock lock (scanLock);
        auto types = hostedStrip.getKnownPluginList().getTypes();

        for (auto& t : types)
        {
            if (identFor (t) == currentPluginIdent)
            {
                desc = t;
                break;
            }
        }
    }

    if (desc.fileOrIdentifier.isEmpty())
        return; // plugin not scanned yet: retry after the next scan

    instantiateHosted (desc, true);
}

// ---------------------------------------------------------------------------
// persistent data
// ---------------------------------------------------------------------------

void TheChannelStripAudioProcessor::loadKnownPluginList()
{
    const juce::ScopedLock lock (scanLock);

    if (auto xml = juce::parseXML (getAppDataDir().getChildFile ("scanned_plugins.xml")))
        hostedStrip.getKnownPluginList().recreateFromXml (*xml);
}

void TheChannelStripAudioProcessor::saveKnownPluginList()
{
    const juce::ScopedLock lock (scanLock);

    getAppDataDir().createDirectory();

    if (auto xml = hostedStrip.getKnownPluginList().createXml())
    {
        // v1.0.10: write-then-rename so that a scan helper being killed
        // mid-write by a hostile plugin cannot truncate the list file.
        const juce::File target = getAppDataDir().getChildFile ("scanned_plugins.xml");
        const juce::File temp = getAppDataDir().getChildFile ("scanned_plugins.xml.tmp");

        xml->writeTo (temp);

        if (target.existsAsFile())
            target.deleteFile();

        temp.moveFileTo (target);
    }
}

// ---------------------------------------------------------------------------
// plugin state
// ---------------------------------------------------------------------------

void TheChannelStripAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto xml = std::make_unique<juce::XmlElement> ("TCS");
    xml->setAttribute ("version", "2.20.0");
    xml->setAttribute ("hostedIdent", currentPluginIdent);
    xml->setAttribute ("tiltDb", tiltDb.load (std::memory_order_relaxed));

    // v2.8: display options travel with the session too, so a reopened
    // project keeps the chosen scales and analyser visibility (same
    // lesson as the v2.7.1 tilt fix: the editor must be able to read
    // these back, not just the processor).
    xml->setAttribute ("anaDbDiv", anaDbDiv.load (std::memory_order_relaxed));
    xml->setAttribute ("eqDbDiv", eqDbDiv.load (std::memory_order_relaxed));
    xml->setAttribute ("analyserVisible",
                       analyserVisible.load (std::memory_order_relaxed) ? 1 : 0);
    // v2.11/v2.12: analyser response speed (Fast/Slow smoothing).
    xml->setAttribute ("specTau", specTau.load (std::memory_order_relaxed));
    // v2.13: loudness reference of the analyser axis (Record/Mix/Master).
    xml->setAttribute ("lufsTarget", lufsTarget.load (std::memory_order_relaxed));

    if (auto* active = hostedStrip.getActivePlugin())
    {
        juce::MemoryBlock hostedState;
        active->getStateInformation (hostedState);
        xml->setAttribute ("hostedState", hostedState.toBase64Encoding());
    }

    // v2.11: per-console remembered states, one child element per
    // console (the active console's entry is refreshed first so the
    // saved session always holds its latest settings).
    rememberCurrentConsoleState();

    {
        const juce::ScopedLock lock (consoleStateLock);

        for (const auto& [ident, state] : consoleStates)
        {
            auto* e = xml->createNewChildElement ("consoleState");
            e->setAttribute ("ident", ident);
            e->setAttribute ("state", state.toBase64Encoding());
        }
    }

    if (auto apvtsXml = apvts.copyState().createXml())
        xml->addChildElement (apvtsXml.release());

    copyXmlToBinary (*xml, destData);
}

void TheChannelStripAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("TCS"))
        return;

    for (auto* child : xml->getChildIterator())
    {
        if (child->hasTagName (apvts.state.getType()))
        {
            if (auto state = juce::ValueTree::fromXml (*child); state.isValid())
                apvts.replaceState (state);
        }
    }

    currentPluginIdent = xml->getStringAttribute ("hostedIdent");

    // the supported bx_console models are the only hostable plugins. A
    // legacy v1 session (which could host anything, e.g. an EQ or saturator
    // saved by the v1 shell) is politely ignored instead of auto-hosted.
    if (currentPluginIdent.isNotEmpty())
    {
        const auto savedName = currentPluginIdent.fromLastOccurrenceOf ("|", false, false);
        bool isConsole = false;

        for (int i = 0; i < numConsoles; ++i)
            if (savedName.containsIgnoreCase (consoleNames[i]))
            {
                isConsole = true;
                break;
            }

        if (! isConsole)
        {
            logHostStatus ("Ignoring saved hosted plugin '" + savedName
                           + "': v2 hosts the supported bx_console models only.");
            currentPluginIdent = juce::String();
        }
    }

    const auto tilt = xml->getStringAttribute ("tiltDb");
    if (tilt.isNotEmpty())
        tiltDb.store ((float) tilt.getDoubleValue(), std::memory_order_relaxed);

    // v2.8: restore the display options; missing attributes (older
    // sessions) keep the defaults (10 dB/div, 4 dB/div, analyser shown).
    const auto ana = xml->getStringAttribute ("anaDbDiv");
    if (ana.isNotEmpty())
        anaDbDiv.store ((float) ana.getDoubleValue(), std::memory_order_relaxed);

    const auto eq = xml->getStringAttribute ("eqDbDiv");
    if (eq.isNotEmpty())
        eqDbDiv.store ((float) eq.getDoubleValue(), std::memory_order_relaxed);

    analyserVisible.store (xml->getBoolAttribute ("analyserVisible", true),
                           std::memory_order_relaxed);

    // v2.11: analyser response speed; missing attribute (older session)
    // keeps the Slow default (~1 s). v2.12 keeps only two speeds
    // (Fast ~0.15 s / Slow ~1 s); any legacy saved value snaps to the
    // closest when the editor opens.
    const auto tau = xml->getStringAttribute ("specTau");
    if (tau.isNotEmpty())
        specTau.store ((float) tau.getDoubleValue(), std::memory_order_relaxed);

    // v2.13: analyser loudness reference; missing attribute (older
    // session) keeps the Mix default (-23 LUFS). Any legacy value
    // outside the three presets snaps to the closest when the editor
    // opens.
    const auto target = xml->getStringAttribute ("lufsTarget");
    if (target.isNotEmpty())
        lufsTarget.store ((float) target.getDoubleValue(), std::memory_order_relaxed);

    savedHostedStateForRestore.reset();
    pendingRestore.store (false, std::memory_order_relaxed);

    const auto stateB64 = xml->getStringAttribute ("hostedState");
    if (stateB64.isNotEmpty())
    {
        juce::MemoryBlock block;
        if (block.fromBase64Encoding (stateB64))
            savedHostedStateForRestore.append (block.getData(), block.getSize());

        pendingRestore.store (true, std::memory_order_relaxed);
    }

    // v2.11: per-console remembered states. Missing elements (older
    // sessions) simply mean an empty memory; the active console still
    // restores through hostedState above.
    {
        const juce::ScopedLock lock (consoleStateLock);

        consoleStates.clear();

        for (auto* e : xml->getChildIterator())
        {
            if (! e->hasTagName ("consoleState"))
                continue;

            const auto ident = e->getStringAttribute ("ident");
            const auto b64 = e->getStringAttribute ("state");

            if (ident.isEmpty() || b64.isEmpty())
                continue;

            juce::MemoryBlock block;

            if (block.fromBase64Encoding (b64) && block.getSize() > 0)
                consoleStates[ident] = std::move (block);
        }
    }

    maybeRestoreHosted();
}

juce::AudioProcessorEditor* TheChannelStripAudioProcessor::createEditor()
{
    return new TheChannelStripAudioProcessorEditor (*this);
}

// v2.15.5: Studio One gain-reduction forwarding (see GRForwarder.h). Only
// the JUCE VST3 wrapper ever asks for these client extensions; the AU and
// Standalone wrappers never call this, so they are unaffected.
juce::VST3ClientExtensions* TheChannelStripAudioProcessor::getVST3ClientExtensions()
{
    return grForwarder.getVST3ClientExtensions();
}

// ---------------------------------------------------------------------------
// v1.0.5 fix: JUCE plugin entry point.
//
// Every wrapper target (AU, VST3, Standalone) expects this free function,
// declared in juce_audio_plugin_client. Without a definition, all three
// targets fail at link time with "Undefined symbols: createPluginFilter()".
// ---------------------------------------------------------------------------
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TheChannelStripAudioProcessor();
}
