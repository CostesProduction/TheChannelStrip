#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cstdlib>

// v1.0.10: command-line flag that turns the Standalone into the out-of-process
// scan helper spawned by the parent instance (see launchScanChildren()).
static const char* scanChildFlag = "--tcs-scan-child";

// ---------------------------------------------------------------------------
// v2.27: mutes the preset-dirty parameter listener while the shell itself
// pushes a state into the hosted plugin (a preset application, the
// per-console remembered state, a session restore) - those changes are
// ours, not the user's, and must not raise the preset's "*". Without this,
// switching back to a console whose remembered state came from a preset
// would flag it as modified the moment the fresh instance is initialised.
// ---------------------------------------------------------------------------
struct HostedStatePushGuard
{
    explicit HostedStatePushGuard (std::atomic<bool>& flag) : f (flag)
    {
        f.store (true, std::memory_order_relaxed);
    }

    ~HostedStatePushGuard() { f.store (false, std::memory_order_relaxed); }

    HostedStatePushGuard (const HostedStatePushGuard&) = delete;
    HostedStatePushGuard& operator= (const HostedStatePushGuard&) = delete;

    std::atomic<bool>& f;
};

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
    transferAnalyzer.prepare (currentSampleRate, currentBlockSize);

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
        // v2.22: stash the pre-console input for the cross-spectrum
        // transfer measurement (the console overwrites the buffer in
        // place; the stash is drained by the matching pushOutput below).
        transferAnalyzer.pushInput (buffer);
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
        // v2.22: the console's output of the same block - this drain
        // advances the transfer frames (and computes the cross-spectra).
        transferAnalyzer.pushOutput (buffer);

        // v2.24 (point 7): the silent-console watch rides the same
        // measurement gate (the warning is a UI concern; a closed editor
        // has nowhere to show it).
        watchHostedSilence (numSamples);
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

// v2.24 (roadmap point 7): the licence-expired detector. A lapsed Plugin
// Alliance licence does not make its console fail to load - the plugin
// instantiates happily and then emits DIGITAL SILENCE forever, which
// used to look like a broken track with no hint about the cause. The
// watch compares the two short-term LUFS meters that already run anyway:
// input active (> -55 LUFS) while the hosted console has been silent
// (< -80 LUFS) for 5 seconds in a row -> one explicit warning, then
// silence until the hosted instance changes (swapping consoles or
// re-hosting re-arms the watch). The LUFS short-term window is 3 s, so
// the values the watch reads are themselves smoothed - the 5 s hold
// means the condition held for ~2 s of real history, which no legit
// gate or pause in a mix imitates while the input is this loud.
// logHostStatus takes a lock and writes one Terminal line; this happens
// at most once per hosted instance, which is acceptable on the audio
// thread (and is the same one-shot diagnostic pattern as the latency
// reporting right below it).
void TheChannelStripAudioProcessor::watchHostedSilence (int numSamples)
{
    auto* active = hostedStrip.getActivePlugin();

    // a new hosted instance (or none) re-arms the watch
    if (active != silenceWatchedPlugin)
    {
        silenceWatchedPlugin = active;
        silenceWatchSeconds = 0.0;
        silenceWarned.store (false, std::memory_order_relaxed);
    }

    if (active == nullptr)
        return;

    const float inLufs  = lufsIn.getShortTermLufs();
    const float outLufs = lufsOut.getShortTermLufs();

    if (inLufs > -55.0f && outLufs < -80.0f)
    {
        silenceWatchSeconds += (double) numSamples / currentSampleRate;

        if (silenceWatchSeconds >= 5.0
            && ! silenceWarned.load (std::memory_order_relaxed))
        {
            silenceWarned.store (true, std::memory_order_relaxed);
            silenceWarningCount.fetch_add (1, std::memory_order_relaxed);

            logHostStatus ("WARNING: '" + active->getName() + "' produces no output"
                           " while its input is active - licence expired or plugin"
                           " not authorised? Open its interface to check.");
        }
    }
    else
    {
        silenceWatchSeconds = 0.0;
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
    juce::StringArray blacklistedFiles;

    {
        const juce::ScopedLock lock (scanLock);
        types = hostedStrip.getKnownPluginList().getTypes();
        // v2.24 (point 7): a plugin that CRASHED the out-of-process
        // scanner lands here; the button must be able to say so instead
        // of looking mysteriously unavailable.
        blacklistedFiles = hostedStrip.getKnownPluginList().getBlacklistedFiles();
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

        // v2.24 (point 7): the scanner blacklisted a file that belongs to
        // this console slot (it killed a scan helper). Reported BEFORE
        // the plain "not found" case - the user needs to update or
        // reinstall the plugin, a Rescan alone will not fix it.
        for (auto& path : blacklistedFiles)
        {
            if (path.containsIgnoreCase (consoleNames[i]))
            {
                slots[(size_t) i].blacklisted = true;
                break;
            }
        }

        // v2.24 (point 7): the scanned description matches, but the file
        // it points to is gone (uninstalled or moved since the last
        // scan). exists() - not existsAsFile(): a mac VST3 is a bundle,
        // i.e. a directory. Only checked for VST3: other formats use
        // non-path identifiers.
        if (slots[(size_t) i].found
            && slots[(size_t) i].desc.pluginFormatName.equalsIgnoreCase ("VST3")
            && ! juce::File (slots[(size_t) i].desc.fileOrIdentifier).exists())
        {
            slots[(size_t) i].fileMissing = true;
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

    // v2.24 (point 7): clicking an unavailable console must always yield
    // an explicit, actionable message in the status area - never a
    // silent no-op. Three distinct situations, three distinct messages.
    if (slot.blacklisted)
    {
        logHostStatus ("'" + juce::String (consoleNames[consoleIndex]) + "'"
                       + " crashed the plugin scanner and was blacklisted."
                         " Update or reinstall it, then press Rescan.");
        return;
    }

    if (slot.fileMissing)
    {
        logHostStatus ("'" + juce::String (consoleNames[consoleIndex]) + "'"
                       + " was last seen at '" + slot.desc.fileOrIdentifier + "'"
                       + " but that file no longer exists - moved or uninstalled?"
                         " Press Rescan.");
        return;
    }

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

    // v2.27: fold any pending knob-move flag into the console being
    // LEFT (its own preset's "*") before the ident changes - a knob
    // turned just before the switch must not star the next console.
    flushPresetDirtyIntoCurrentConsole();

    currentPluginIdent = identFor (slot.desc);

    logHostStatus ("Instantiating '" + slot.desc.name + "'...");
    juce::Thread::launch ([this, desc = slot.desc] () { instantiateHosted (desc, false); });
}

void TheChannelStripAudioProcessor::clearHostedPlugin()
{
    // v2.15.5: stop forwarding gain reduction BEFORE the console goes
    // away, so the host can never pull through a dying instance.
    grForwarder.disconnect();
    currentPluginIdent = juce::String();

    // v2.26: a pending preset application belongs to the console being
    // cleared away - it must not leak onto whatever is hosted next.
    {
        const juce::ScopedLock lock (consoleStateLock);
        pendingPresetState.reset();
        pendingPresetIdent = juce::String();
        pendingPresetName = juce::String(); // v2.27
        pendingPresetFile = juce::String(); // v2.30
    }

    hostedStrip.clearPlugin();
}

// ---------------------------------------------------------------------------
// v2.26 (roadmap point 4): named presets
// ---------------------------------------------------------------------------

juce::File TheChannelStripAudioProcessor::getPresetsDir() const
{
    return getAppDataDir().getChildFile ("Presets");
}

juce::String TheChannelStripAudioProcessor::presetFileNameFor (const juce::String& presetName)
{
    // sanitise the user-given name into a safe filename (the ORIGINAL
    // name is kept verbatim inside the file; only the filename is
    // derived). Anything outside letters, digits, space, - and _ is
    // replaced with an underscore; the result is trimmed and capped.
    juce::String safe;

    for (auto c : presetName.trim())
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == ' ' || c == '-' || c == '_')
            safe << c;
        else
            safe << '_';
    }

    if (safe.isEmpty())
        safe = "preset";

    return safe.dropLastCharacters (juce::jmax (0, safe.length() - 80)) + ".tcsPreset";
}

bool TheChannelStripAudioProcessor::readPresetFile (const juce::File& file,
                                                    PresetInfo& info,
                                                    juce::MemoryBlock& state)
{
    if (! file.existsAsFile())
        return false;

    auto xml = juce::parseXML (file);

    if (xml == nullptr || ! xml->hasTagName ("tcsPreset"))
        return false;

    info.name        = xml->getStringAttribute ("name");
    info.consoleName = xml->getStringAttribute ("consoleName");

    const auto b64 = xml->getStringAttribute ("state");

    if (info.name.isEmpty() || info.consoleName.isEmpty() || b64.isEmpty())
        return false;

    if (! state.fromBase64Encoding (b64) || state.getSize() == 0)
        return false;

    return true;
}

juce::Array<TheChannelStripAudioProcessor::PresetInfo>
TheChannelStripAudioProcessor::getAvailablePresets()
{
    juce::Array<PresetInfo> presets;

    auto dir = getPresetsDir();

    if (dir.isDirectory())
    {
        // v2.30: the listing is RECURSIVE - the root holds the general
        // presets, the sub-folders are albums, and BOTH show up in the
        // selector (the album is part of the entry text). The same
        // name can exist in several places, so each entry carries its
        // exact file; nothing is copied anywhere.
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, true, "*.tcsPreset"))
        {
            PresetInfo info;
            juce::MemoryBlock state;

            // a corrupt file is skipped by the list (applyPresetFile
            // and friends still report it explicitly when picked)
            if (readPresetFile (f, info, state))
            {
                info.file = f;
                info.album = f.getParentDirectory().getRelativePathFrom (dir);

                if (info.album == ".")
                    info.album = juce::String();

                presets.add (info);
            }
        }
    }

    // root presets first, then the albums, alphabetical by preset name
    // inside each group, so the combo box is stable and reads in bands
    std::sort (presets.begin(), presets.end(),
               [] (const PresetInfo& a, const PresetInfo& b)
               {
                   if (a.album.isEmpty() != b.album.isEmpty())
                       return a.album.isEmpty();

                   if (a.album.compareIgnoreCase (b.album) != 0)
                       return a.album.compareIgnoreCase (b.album) < 0;

                   return a.name.compareIgnoreCase (b.name) < 0;
               });

    return presets;
}

bool TheChannelStripAudioProcessor::savePreset (const juce::String& presetName)
{
    const auto trimmed = presetName.trim();

    if (trimmed.isEmpty())
    {
        logHostStatus ("Preset not saved: the name is empty.");
        return false;
    }

    auto* active = hostedStrip.getActivePlugin();

    if (active == nullptr)
    {
        logHostStatus ("Preset not saved: no console is hosted - select one first.");
        return false;
    }

    juce::MemoryBlock state;
    active->getStateInformation (state);

    if (state.getSize() == 0)
    {
        logHostStatus ("Preset not saved: '" + active->getName()
                       + "' returned an empty state.");
        return false;
    }

    // the console the preset belongs to: the name the host reports
    // (from the ident for a real console, the plugin's own name for a
    // directly-set instance)
    const auto consoleName = currentPluginIdent.isNotEmpty()
        ? currentPluginIdent.fromLastOccurrenceOf ("|", false, false)
        : active->getName();

    auto dir = getPresetsDir();

    if (! dir.isDirectory())
    {
        auto appDir = getAppDataDir();

        if (! appDir.isDirectory())
            appDir.createDirectory();

        dir.createDirectory();

        if (! dir.isDirectory())
        {
            logHostStatus ("ERROR: preset folder could not be created: "
                           + dir.getFullPathName());
            return false;
        }
    }

    auto xml = std::make_unique<juce::XmlElement> ("tcsPreset");
    xml->setAttribute ("version", "2.32.0");
    xml->setAttribute ("name", trimmed);
    xml->setAttribute ("consoleName", consoleName);
    xml->setAttribute ("state", state.toBase64Encoding());

    const auto file = dir.getChildFile (presetFileNameFor (trimmed));

    if (! xml->writeTo (file))
    {
        logHostStatus ("ERROR: preset '" + trimmed + "' could not be written to "
                       + file.getFullPathName());
        return false;
    }

    // v2.27: the saved preset becomes the console's current one, and
    // the knobs now match it by definition - no "*". v2.30: the root
    // file it was written to is remembered with it.
    {
        const juce::ScopedLock lock (consoleStateLock);
        consolePresetNames[currentPluginIdent] = trimmed;
        consolePresetModified[currentPluginIdent] = false;
        consolePresetFiles[currentPluginIdent] = file.getFullPathName();
    }

    presetDirtyPending.store (false, std::memory_order_relaxed);

    logHostStatus ("Preset '" + trimmed + "' saved (" + consoleName + ").");

    return true;
}

// v2.30: the name-based load kept for the root presets (tests, older
// flows) - it resolves the sanitised name into the root file and
// hands over to the file-based load below.
bool TheChannelStripAudioProcessor::applyPreset (const juce::String& presetName)
{
    return applyPresetFile (getPresetsDir().getChildFile (presetFileNameFor (presetName)));
}

// v2.30: load any preset file on disk - the root, an album, or a file
// from outside the preset folder (Load). The old name-based resolution
// could not tell two same-named presets apart nor reach into the
// albums; the selector and Load pass the exact file.
bool TheChannelStripAudioProcessor::applyPresetFile (const juce::File& file)
{
    PresetInfo info;
    juce::MemoryBlock state;

    if (! readPresetFile (file, info, state))
    {
        logHostStatus ("Preset '" + file.getFileNameWithoutExtension().trim()
                       + "' could not be loaded - missing or corrupted file.");
        return false;
    }

    // resolve the console this preset belongs to
    int consoleIndex = -1;

    for (int i = 0; i < numConsoles; ++i)
        if (info.consoleName.containsIgnoreCase (consoleNames[i]))
        {
            consoleIndex = i;
            break;
        }

    // the console is already hosted: apply the state directly and make
    // it the remembered state too (a later switch away and back, or a
    // session save, keeps it).
    if (consoleIndex >= 0)
    {
        auto slots = getConsoleSlots();
        const auto& slot = slots[(size_t) consoleIndex];

        if (! slot.found)
        {
            logHostStatus ("Preset '" + info.name + "' was made for '" + info.consoleName
                           + "', which is not installed - install it (or press Rescan)"
                             " and load the preset again.");
            return false;
        }

        if (identFor (slot.desc) == currentPluginIdent)
        {
            if (auto* active = hostedStrip.getActivePlugin())
            {
                // v2.27: this push is ours, not the user's - the guard
                // keeps the parameter listener from raising "*".
                {
                    const HostedStatePushGuard guard (suppressPresetDirty);
                    active->setStateInformation (state.getData(), (int) state.getSize());
                    presetDirtyPending.store (false, std::memory_order_relaxed);
                }

                const juce::ScopedLock lock (consoleStateLock);
                consoleStates[currentPluginIdent] = state;

                // v2.27: the loaded preset becomes the console's
                // current one, unmodified by definition. v2.30: the
                // tracking remembers the exact file it came from.
                consolePresetNames[currentPluginIdent] = info.name;
                consolePresetModified[currentPluginIdent] = false;
                consolePresetFiles[currentPluginIdent] = file.getFullPathName();
            }

            logHostStatus ("Preset '" + info.name + "' loaded on " + info.consoleName + ".");
            return true;
        }

        // a different console is hosted: remember the one being left,
        // queue the preset, then switch - instantiateHosted applies
        // the queued state once the right console is ready.
        rememberCurrentConsoleState();

        {
            const juce::ScopedLock lock (consoleStateLock);
            pendingPresetState = state;
            pendingPresetIdent = identFor (slot.desc);
            pendingPresetName = info.name; // v2.27
            pendingPresetFile = file.getFullPathName(); // v2.30
        }

        logHostStatus ("Switching to '" + info.consoleName + "' to load preset '"
                       + info.name + "'...");
        selectConsole (consoleIndex);
        return true;
    }

    // the preset's console is not one of the nine but matches whatever
    // is hosted right now (e.g. an externally-named plugin): apply it
    // directly. Anything else is refused explicitly.
    if (auto* active = hostedStrip.getActivePlugin())
    {
        if (active->getName().containsIgnoreCase (info.consoleName)
            || info.consoleName.containsIgnoreCase (active->getName()))
        {
            // v2.27: same guard - the push must not raise "*".
            {
                const HostedStatePushGuard guard (suppressPresetDirty);
                active->setStateInformation (state.getData(), (int) state.getSize());
                presetDirtyPending.store (false, std::memory_order_relaxed);
            }

            if (currentPluginIdent.isNotEmpty())
            {
                const juce::ScopedLock lock (consoleStateLock);
                consoleStates[currentPluginIdent] = state;
            }

            // v2.27: current-preset tracking, like above. The map key
            // is currentPluginIdent whichever way it was resolved -
            // every tracking site (save / delete / apply / drain /
            // getters) uses the same key, so this stays consistent
            // even for a directly-set instance with no console ident.
            {
                const juce::ScopedLock lock (consoleStateLock);
                consolePresetNames[currentPluginIdent] = info.name;
                consolePresetModified[currentPluginIdent] = false;
                consolePresetFiles[currentPluginIdent] = file.getFullPathName();
            }

            logHostStatus ("Preset '" + info.name + "' loaded on " + info.consoleName + ".");
            return true;
        }
    }

    logHostStatus ("Preset '" + info.name + "' was made for '" + info.consoleName
                   + "', which is not one of the consoles - it cannot be loaded.");
    return false;
}

bool TheChannelStripAudioProcessor::deletePreset (const juce::String& presetName)
{
    const auto file = getPresetsDir().getChildFile (presetFileNameFor (presetName));

    if (! file.existsAsFile())
    {
        logHostStatus ("Preset '" + presetName.trim() + "' not found.");
        return false;
    }

    if (! file.deleteFile())
    {
        logHostStatus ("ERROR: preset '" + presetName.trim() + "' could not be deleted.");
        return false;
    }

    // v2.27: deleting the hosted console's current preset disowns it -
    // the console keeps its sound but no longer shows a preset name.
    disownPresetIfItsFileIs (file);

    logHostStatus ("Preset '" + presetName.trim() + "' deleted.");
    return true;
}

// v2.29: the browse-to-delete fallback. The selector only lists the
// preset folder's root, so files saved into sub-folders (albums) have
// no entry to press Delete on - this takes the file directly instead.
bool TheChannelStripAudioProcessor::deletePresetFile (const juce::File& file)
{
    if (! file.existsAsFile())
    {
        logHostStatus ("ERROR: preset file not found: " + file.getFullPathName());
        return false;
    }

    if (! file.deleteFile())
    {
        logHostStatus ("ERROR: preset file '" + file.getFileName()
                       + "' could not be deleted.");
        return false;
    }

    // a file deleted from the root may well be the hosted console's
    // current preset - disown it exactly like deletePreset does.
    disownPresetIfItsFileIs (file);

    logHostStatus ("Preset file '" + file.getFileName() + "' deleted.");
    return true;
}

// v2.30: the file the given console's current preset lives in. The
// tracking knows it since v2.30 (every save/load path stores it);
// an older session restored only the name, so fall back to the root
// file the name implies. Lock held by the caller.
juce::File TheChannelStripAudioProcessor::currentPresetFileFor (const juce::String& ident)
{
    const auto it = consolePresetNames.find (ident);

    if (it == consolePresetNames.end() || it->second.isEmpty())
        return juce::File();

    const auto f = consolePresetFiles.find (ident);

    if (f != consolePresetFiles.end() && f->second.isNotEmpty())
        return juce::File (f->second);

    return getPresetsDir().getChildFile (presetFileNameFor (it->second));
}

// v2.30: the exact file the hosted console's current preset lives in
// (empty when no preset is tracked). The editor uses it to pre-fill
// the Save dialog with the preset's own location.
juce::File TheChannelStripAudioProcessor::getCurrentPresetFile()
{
    const juce::ScopedLock lock (consoleStateLock);
    return currentPresetFileFor (currentPluginIdent);
}

void TheChannelStripAudioProcessor::disownPresetIfItsFileIs (const juce::File& file)
{
    const juce::ScopedLock lock (consoleStateLock);

    if (currentPresetFileFor (currentPluginIdent) == file)
    {
        const auto it = consolePresetNames.find (currentPluginIdent);

        if (it != consolePresetNames.end())
        {
            it->second = juce::String();
            consolePresetModified[currentPluginIdent] = false;
            consolePresetFiles[currentPluginIdent] = juce::String();
        }
    }
}

// v2.29/v2.30: Save proper. The hosted console's CURRENT state is
// written to the exact file the save dialog returned - anywhere on
// disk. Wherever it lands, the saved preset becomes the console's
// CURRENT preset ("Save As" semantics, confirmed by Clement) and the
// tracking remembers the exact file: saving into the root or an
// album sub-folder shows up in the selector (v2.30: albums are
// listed), saving elsewhere keeps it off the list. The preset's name
// is the file's name, so what the selector later shows is exactly
// what was typed in the dialog.
bool TheChannelStripAudioProcessor::savePresetTo (const juce::File& destination)
{
    auto* active = hostedStrip.getActivePlugin();

    if (active == nullptr)
    {
        logHostStatus ("Preset not saved: no console is hosted - select one first.");
        return false;
    }

    juce::MemoryBlock state;
    active->getStateInformation (state);

    if (state.getSize() == 0)
    {
        logHostStatus ("Preset not saved: '" + active->getName()
                       + "' returned an empty state.");
        return false;
    }

    const auto name = destination.getFileNameWithoutExtension();

    if (name.isEmpty())
    {
        logHostStatus ("Preset not saved: the file name is empty.");
        return false;
    }

    if (! destination.getParentDirectory().isDirectory())
    {
        logHostStatus ("ERROR: the folder does not exist: "
                       + destination.getParentDirectory().getFullPathName());
        return false;
    }

    // the console the preset belongs to: the name the host reports
    // (from the ident for a real console, the plugin's own name for a
    // directly-set instance)
    const auto consoleName = currentPluginIdent.isNotEmpty()
        ? currentPluginIdent.fromLastOccurrenceOf ("|", false, false)
        : active->getName();

    auto xml = std::make_unique<juce::XmlElement> ("tcsPreset");
    xml->setAttribute ("version", "2.32.0");
    xml->setAttribute ("name", name);
    xml->setAttribute ("consoleName", consoleName);
    xml->setAttribute ("state", state.toBase64Encoding());

    if (! xml->writeTo (destination))
    {
        logHostStatus ("ERROR: preset '" + name + "' could not be written to "
                       + destination.getFullPathName());
        return false;
    }

    // v2.30: a Save As, wherever it lands - the saved preset becomes
    // the console's current one right away (like a DAW's Save As: the
    // document you just saved is the one you are on) and the tracking
    // remembers the exact file, root or album or elsewhere. The knobs
    // now match it by definition - no "*". Inside the preset folder
    // (root or album) it shows up in the selector; outside it does not.
    const bool inPresetFolder = destination.getParentDirectory() == getPresetsDir()
        || destination.isAChildOf (getPresetsDir());

    {
        const juce::ScopedLock lock (consoleStateLock);
        consolePresetNames[currentPluginIdent] = name;
        consolePresetModified[currentPluginIdent] = false;
        consolePresetFiles[currentPluginIdent] = destination.getFullPathName();
        presetDirtyPending.store (false, std::memory_order_relaxed);
    }

    // the name-based load (tests, older flows) resolves a preset by
    // sanitising its name back into a ROOT file name - a root file
    // whose name does not round-trip cannot be loaded again, so say so
    // explicitly instead of failing silently
    if (destination.getParentDirectory() == getPresetsDir()
        && presetFileNameFor (name) != destination.getFileName())
        logHostStatus ("Note: '" + name + "' uses characters the preset list"
                       " cannot reload from (safe: letters, digits, space, - _).");

    logHostStatus ("Preset '" + name + "' saved ("
                   + (inPresetFolder ? juce::String ("listed in the selector")
                                     : destination.getParentDirectory().getFullPathName())
                   + ").");

    return true;
}

// v2.30: Load - open any .tcsPreset file and load it, full stop. The
// old flows copied the file into the preset folder's root (v2.28) or
// just made it available (v2.26); Clément rejected the copy - loading
// album presets was filling the root with duplicates ("une poubelle").
// Nothing is copied anywhere: the preset loads into the hosted
// console (switching consoles first when needed) and becomes its
// current one, tracked at the file it was loaded from. A file inside
// the preset folder (root or album) keeps its place in the selector.
bool TheChannelStripAudioProcessor::importPreset (const juce::File& source)
{
    PresetInfo info;
    juce::MemoryBlock state;

    if (! readPresetFile (source, info, state))
    {
        logHostStatus ("Load failed: '" + source.getFileName()
                       + "' is not a valid preset file.");
        return false;
    }

    // applyPresetFile posts its own success/failure to the status
    // area and tracks the preset at this exact file
    return applyPresetFile (source);
}

// ---------------------------------------------------------------------------
// v2.27: current-preset tracking (the "*" workflow). The editor drives
// all of this from its timer: drain the (lock-free) knob-move flag,
// read the hosted console's preset name and its "*" state, and mirror
// both in the preset combo box.
// ---------------------------------------------------------------------------

juce::String TheChannelStripAudioProcessor::getCurrentPresetName() const
{
    const juce::ScopedLock lock (consoleStateLock);

    const auto it = consolePresetNames.find (currentPluginIdent);

    return it != consolePresetNames.end() ? it->second : juce::String();
}

bool TheChannelStripAudioProcessor::isCurrentPresetModified() const
{
    const juce::ScopedLock lock (consoleStateLock);

    const auto n = consolePresetNames.find (currentPluginIdent);
    const auto m = consolePresetModified.find (currentPluginIdent);

    return n != consolePresetNames.end() && n->second.isNotEmpty()
        && m != consolePresetModified.end() && m->second;
}

void TheChannelStripAudioProcessor::flushPresetDirtyIntoCurrentConsole()
{
    if (! presetDirtyPending.exchange (false, std::memory_order_relaxed))
        return;

    const juce::ScopedLock lock (consoleStateLock);

    const auto it = consolePresetNames.find (currentPluginIdent);

    if (it != consolePresetNames.end() && it->second.isNotEmpty())
        consolePresetModified[currentPluginIdent] = true;
}

void TheChannelStripAudioProcessor::drainPresetDirtyFlag()
{
    // the editor timer's entry point (message thread)
    flushPresetDirtyIntoCurrentConsole();
}

void TheChannelStripAudioProcessor::parameterValueChanged (int, float)
{
    // v2.27: any hosted-console knob move flags the current preset as
    // modified. The callback can fire on any thread (the plugin's own
    // editor, automation), so only a lock-free flag is raised here;
    // drainPresetDirtyFlag (message thread) turns it into "*".
    if (! suppressPresetDirty.load (std::memory_order_relaxed))
        presetDirtyPending.store (true, std::memory_order_relaxed);
}

void TheChannelStripAudioProcessor::parameterGestureChanged (int, bool) {}

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
        // v2.24 (point 7): the message now carries the exact file path
        // too, so a moved/deleted plugin is immediately identifiable.
        logHostStatus ("ERROR: cannot instantiate '" + desc.name + "': "
                       + (error.isEmpty() ? juce::String ("unknown error") : error)
                       + (desc.fileOrIdentifier.isNotEmpty()
                              ? " (file: " + desc.fileOrIdentifier + ")"
                              : juce::String()));
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

    // v2.27: watch the hosted console's parameters so a knob move can
    // flag the console's current preset as modified ("*"). This only
    // registers our listener in the JUCE parameter objects (never a
    // call into the plugin), and the processor outlives every hosted
    // instance, so no removal is needed.
    for (auto* p : hostedStrip.getActivePlugin()->getParameters())
        p->addListener (this);

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
            // v2.27: session restore is a shell push too - the "*"
            // state comes back with the session (it is persisted on
            // each <consoleState>), so it must not be re-derived here.
            const HostedStatePushGuard guard (suppressPresetDirty);
            active->setStateInformation (savedHostedStateForRestore.getData(),
                                         (int) savedHostedStateForRestore.getSize());
            // a plugin dispatching its parameter notifications just
            // after the restore would star a clean preset for nothing.
            presetDirtyPending.store (false, std::memory_order_relaxed);

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
    // v2.26: a preset queued by applyPreset takes precedence over the
    // remembered state - the user asked for THIS state, on THIS
    // console, right now. Ident-checked so a stale request can never
    // land on the wrong console; consumed exactly once, and it becomes
    // the console's remembered state too.
    if (! applyPendingState)
    {
        if (auto* active = hostedStrip.getActivePlugin())
        {
            juce::MemoryBlock presetToApply;
            juce::String presetNameToApply; // v2.27
            juce::String presetFileToApply; // v2.30
            bool presetMatches = false;

            {
                const juce::ScopedLock lock (consoleStateLock);

                if (pendingPresetState.getSize() > 0
                    && pendingPresetIdent == currentPluginIdent)
                {
                    presetToApply = pendingPresetState;
                    presetNameToApply = pendingPresetName; // v2.27
                    presetFileToApply = pendingPresetFile; // v2.30
                    presetMatches = true;

                    pendingPresetState.reset();
                    pendingPresetIdent = juce::String();
                    pendingPresetName = juce::String();
                    pendingPresetFile = juce::String();
                }
            }

            if (presetMatches)
            {
                {
                    const HostedStatePushGuard guard (suppressPresetDirty);
                    active->setStateInformation (presetToApply.getData(),
                                                (int) presetToApply.getSize());
                    presetDirtyPending.store (false, std::memory_order_relaxed);
                }

                const juce::ScopedLock lock (consoleStateLock);
                consoleStates[currentPluginIdent] = presetToApply;

                // v2.27: the queued preset is now this console's
                // current one, unmodified by definition. v2.30: its
                // exact file is remembered with it.
                consolePresetNames[currentPluginIdent] = presetNameToApply;
                consolePresetModified[currentPluginIdent] = false;
                consolePresetFiles[currentPluginIdent] = presetFileToApply;
            }
            else
            {
                juce::MemoryBlock remembered;

                {
                    const juce::ScopedLock lock (consoleStateLock);
                    const auto it = consoleStates.find (currentPluginIdent);

                    if (it != consoleStates.end())
                        remembered = it->second;
                }

                if (remembered.getSize() > 0)
                {
                    // v2.27: the remembered state is a shell push too
                    // (switching back to a console whose state came
                    // from a preset must not raise "*").
                    const HostedStatePushGuard guard (suppressPresetDirty);
                    active->setStateInformation (remembered.getData(),
                                                 (int) remembered.getSize());
                    presetDirtyPending.store (false, std::memory_order_relaxed);
                }
            }
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
    xml->setAttribute ("version", "2.32.0");
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

    // v2.27: flush a pending knob-move flag first, so the "*" state
    // survives the save too (getStateInformation can run before the
    // editor timer drained it).
    flushPresetDirtyIntoCurrentConsole();

    {
        const juce::ScopedLock lock (consoleStateLock);

        // v2.27.1: one element per remembered console state, plus any
        // console whose preset tracking must survive even when no
        // state block is on record (a console that was not
        // instantiated when the session was saved) - the union of
        // both key sets, so a preset name never rides on the state
        // block being present.
        juce::StringArray idents;

        for (const auto& [ident, state] : consoleStates)
            idents.addIfNotAlreadyThere (ident);

        for (const auto& [ident, name] : consolePresetNames)
            if (name.isNotEmpty())
                idents.addIfNotAlreadyThere (ident);

        for (const auto& ident : idents)
        {
            auto* e = xml->createNewChildElement ("consoleState");
            e->setAttribute ("ident", ident);

            const auto st = consoleStates.find (ident);

            if (st != consoleStates.end())
                e->setAttribute ("state", st->second.toBase64Encoding());

            // v2.27: the console's current preset travels with the
            // session (name + "*" state), so a reopened project shows
            // it again.
            const auto pn = consolePresetNames.find (ident);

            if (pn != consolePresetNames.end() && pn->second.isNotEmpty())
            {
                e->setAttribute ("presetName", pn->second);
                const auto pm = consolePresetModified.find (ident);
                e->setAttribute ("presetModified",
                                 (pm != consolePresetModified.end() && pm->second) ? 1 : 0);

                // v2.30: when the current preset does not sit at the
                // root file its name implies (an album, or a file
                // loaded from outside), remember where it lives so a
                // reopened project pre-fills Save with the right place
                // and disowns correctly on delete. Inside the preset
                // folder the path is stored relative to it, so the
                // session survives a moved home; elsewhere absolute.
                const auto pf = consolePresetFiles.find (ident);

                if (pf != consolePresetFiles.end() && pf->second.isNotEmpty())
                {
                    const auto dir = getPresetsDir();
                    const auto tracked = juce::File (pf->second);
                    const auto implied = dir.getChildFile (presetFileNameFor (pn->second));

                    if (tracked != implied && tracked.existsAsFile())
                    {
                        if (tracked.isAChildOf (dir))
                            e->setAttribute ("presetFile",
                                             tracked.getRelativePathFrom (dir));
                        else
                            e->setAttribute ("presetFile", tracked.getFullPathName());
                    }
                }
            }
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

        // v2.27.1: the preset tracking is part of the session too,
        // so a restore replaces it wholesale instead of merging with
        // whatever was tracked before (a host can call
        // setStateInformation on an instance that already tracked
        // presets). The pending preset is dropped for the same
        // reason: the restored session is authoritative.
        consolePresetNames.clear();
        consolePresetModified.clear();
        consolePresetFiles.clear(); // v2.30
        pendingPresetState.reset();
        pendingPresetIdent = juce::String();
        pendingPresetName = juce::String();
        pendingPresetFile = juce::String(); // v2.30

        for (auto* e : xml->getChildIterator())
        {
            if (! e->hasTagName ("consoleState"))
                continue;

            const auto ident = e->getStringAttribute ("ident");

            if (ident.isEmpty())
                continue;

            const auto b64 = e->getStringAttribute ("state");

            if (b64.isNotEmpty())
            {
                juce::MemoryBlock block;

                if (block.fromBase64Encoding (b64) && block.getSize() > 0)
                    consoleStates[ident] = std::move (block);
            }

            // v2.27: the console's current preset comes back with the
            // session. Older sessions have no attributes, so nothing
            // is restored - the same graceful rule as the states
            // above. v2.27.1: this is independent of the state
            // block being present (a preset can be tracked for a
            // console whose state was not saved).
            const auto pn = e->getStringAttribute ("presetName");

            if (pn.isNotEmpty())
            {
                consolePresetNames[ident] = pn;
                consolePresetModified[ident] = e->getBoolAttribute ("presetModified", false);

                // v2.30: the current preset's own file, when the
                // session knows it (an album preset, or one loaded
                // from outside the folder). Absent on older sessions:
                // the root file the name implies is assumed - exactly
                // the pre-v2.30 behaviour.
                const auto pf = e->getStringAttribute ("presetFile");

                if (pf.isNotEmpty())
                {
                    const auto f = juce::File::isAbsolutePath (pf)
                        ? juce::File (pf)
                        : getPresetsDir().getChildFile (pf);

                    consolePresetFiles[ident] = f.getFullPathName();
                }
            }
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
