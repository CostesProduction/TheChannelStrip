#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <juce_audio_processors/juce_audio_processors.h>
#include "HostedStrip.h"
#include "GRForwarder.h"
#include "AutomationProxyBank.h"
#include "Console1Bridge.h"

// v2.54.0: the version string comes from the build system (CMakeLists
// injects TCS_VERSION on every target). The fallback keeps the
// processor compilable outside the CMake project.
#ifndef TCS_VERSION
#define TCS_VERSION "undefined"
#endif
#include "LufsMeter.h"
#include "SpectrumAnalyzer.h"
#include "TransferAnalyzer.h"

class TheChannelStripAudioProcessorEditor;

// ---------------------------------------------------------------------------
// v2.107.0 (the session link): all the TheChannelStrip instances of one
// DAW session live in the SAME PROCESS, so one process-wide registry can
// tell every linked instance when any of them changes console. An
// instance with its SESSION LINK toggle checked follows every console
// change of the other linked instances - and publishes its own, mouse
// clicks and Console 1 Track Select alike. Unchecked, an instance stays
// fully independent, exactly as every version before. Emptying the slot
// stays a private move, and a change applied BY the link is never
// re-published: the delivery is one-way, so a chain of linked instances
// can never ping-pong.
// ---------------------------------------------------------------------------
class TheChannelStripAudioProcessor;

class SessionConsoleLink
{
public:
    static SessionConsoleLink& getInstance();

    // the processor's constructor and destructor own the registration
    void add (TheChannelStripAudioProcessor* processor);
    void remove (TheChannelStripAudioProcessor* processor);

    // message thread only: every caller (a UI click, the editor
    // heartbeat) runs on the message thread, so a delivered console
    // change can never race a processor being destroyed.
    void publish (TheChannelStripAudioProcessor* sender, int selection);

    // v2.108.0 (the channel walker): the membership query behind the
    // walker's fall-home rule - a viewed instance that left the
    // registry (its track was deleted) is never embedded again.
    // Message thread only.
    bool contains (TheChannelStripAudioProcessor* processor);

    // v2.108.0: the walk order - every live instance, sorted by
    // channel number (a stable sort, so equal numbers keep
    // registration order). Message thread only.
    juce::Array<TheChannelStripAudioProcessor*> instancesInChannelOrder();

private:
    SessionConsoleLink() = default;

    juce::SpinLock registryLock;
    juce::Array<TheChannelStripAudioProcessor*> members;
};

// v2.2: the shell is deliberately minimal. It hosts exactly one of the
// supported bx_console plugins, shows a spectrum analyser and displays a
// stable smoothed LUFS correction (how much to add to the output to
// match the input loudness); the user applies it by hand on the hosted
// plugin's own controls.
class TheChannelStripAudioProcessor : public juce::AudioProcessor,
                                       public juce::AudioProcessorParameter::Listener
{
public:
    // v2.13: seven consoles - the five originals plus the Focusrite SC
    // and the bx_console N (added back at Clement's request; it was
    // dropped in v2.2 because his scan never found it).
    // v2.16: nine consoles - the two Lindell channel strips were added
    // (Lindell 80 Channel, Lindell 50 Channel).
    static constexpr int numConsoles = 9;

    // Full plugin names, in UI order.
    static const char* const consoleNames[numConsoles];
    // Short button label for a console index.
    static juce::String consoleLabel (int index);

    TheChannelStripAudioProcessor();
    ~TheChannelStripAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // v2.15.5: Studio One gain-reduction forwarding (see GRForwarder.h).
    // The JUCE VST3 wrapper queries the returned object whenever the host
    // asks our edit controller for an interface the wrapper does not
    // implement itself (that is how Studio One reaches our
    // IGainReductionInfo). Only the VST3 wrapper ever calls this.
    juce::VST3ClientExtensions* getVST3ClientExtensions() override;

    const juce::String getName() const override { return "TheChannelStrip"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- editor API ----
    juce::AudioProcessorValueTreeState& getApvts() { return apvts; }
    HostedStrip& getHostedStrip() { return hostedStrip; }
    juce::CriticalSection& getScanLock() { return scanLock; }
    // v2.51.1: guarded read - the ident is written on the message/host
    // thread and read from the background instantiation threads.
    juce::String getCurrentPluginIdent() const
    {
        const juce::ScopedLock lock (identGuard);
        return currentPluginIdent;
    }
    // v2.51.1: test hook - drives the (now serialized) instantiation
    // path directly; production flows go through selectConsole and
    // maybeRestoreHosted.
    void instantiateHostedForTest (const juce::PluginDescription& desc, bool applyPendingState)
    {
        instantiateHosted (desc, applyPendingState);
    }
    // Last hosting event (scan result / instantiation error). Sticky.
    juce::String getHostStatus();
    // v2.27: status posts from the editor (e.g. a failed Save-to-file)
    // share the processor's status area.
    void logHostStatus (const juce::String& message);
    // True while a background scan is running (lets the UI skip the lock).
    bool isScanning() const { return scanning.load (std::memory_order_relaxed); }

    // ---- v2.27: current-preset tracking (the "*" workflow) ----
    // The name of the preset currently associated with the hosted
    // console (empty = none), and whether its knobs have moved since
    // that preset was loaded or saved (the "*" marker). The editor
    // timer drains the lock-free knob-move flag first, then reads.
    juce::String getCurrentPresetName() const;
    bool isCurrentPresetModified() const;
    void drainPresetDirtyFlag();
    // ---- juce::AudioProcessorParameter::Listener (v2.27) ----
    // Attached to the HOSTED console's parameters, so a knob move can
    // flag the current preset as modified. Never a call into the
    // plugin - the listener list lives in our JUCE parameter objects.
    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;

    // ---- v2.70.2: session-dirty notification ----
    // The shell exposes NO parameter to the DAW, so the host has no
    // way to know that a knob move, a console switch or a preset
    // action changed the plugin state - Studio One never marked the
    // song modified and "Save" stayed greyed out. JUCE's official
    // channel for exactly this is updateHostDisplay with
    // nonParameterStateChanged: the VST3 wrapper maps it to setDirty
    // (true) (verified in juce_audio_plugin_client_VST3.cpp, 8.0.6),
    // which tells the host the state needs saving. Only genuine user
    // actions call it - every restore / preset push runs under
    // HostedStatePushGuard and never reaches this.
    void markSessionStateChanged();

    // v2.24 (roadmap point 7): a slot is no longer just found / greyed
    // out. Three distinct situations are reported to the editor so a
    // dead button always has an explanation:
    //   - not found: nothing scanned matches the console name
    //   - blacklisted: the plugin CRASHED the out-of-process scanner
    //     and was blacklisted (update or reinstall it)
    //   - fileMissing: a scanned description matches, but the file it
    //     points to no longer exists (moved / uninstalled since the scan)
    struct ConsoleSlot
    {
        int index = -1;                    // 0..numConsoles-1
        juce::String name;                 // full plugin name
        juce::PluginDescription desc;     // valid only when found
        bool found = false;
        bool blacklisted = false;
        bool fileMissing = false;
    };

    // Exactly numConsoles entries, one per supported console (found = false
    // while the plugin is neither installed nor scanned yet).
    std::array<ConsoleSlot, numConsoles> getConsoleSlots();

    // Host one of the supported consoles. Silently ignored if the index is
    // out of range; reports to the status line if the plugin is not
    // installed.
    void selectConsole (int consoleIndex);

    // v2.107.0 (the session link): when true, this instance follows every
    // console change of the other linked instances of the session and
    // publishes its own - mouse clicks and Console 1 Track Select alike.
    // False by default: the pre-2.107 independence.
    bool isSessionLinkEnabled() const;
    void setSessionLinkEnabled (bool enabled);

    // v2.108.0 (the channel walker): this instance's channel number -
    // auto-assigned at construction as the lowest free number of the
    // live registry, editable from the editor's badge, saved with the
    // session state. The editor's arrows walk the session's instances
    // in this order.
    int getChannelNumber() const;
    void setChannelNumber (int number);

    // v2.108.0: a walker of another instance embeds this instance's
    // console only while this instance's own editor is closed - this
    // query is that rule's guard. The v2.8 flag already tracks the
    // open editor for the meters, it is simply exposed here.
    bool isEditorOpen() const;

    // v2.108.1 (the walker keeps walking): the walker must skip
    // instances whose track is gone - the host keeps a removed
    // plugin's processor alive for undo, and the registry alone
    // cannot tell those apart. A live track receives blocks
    // continuously, so the freshness of this timestamp IS the
    // connection to the graph. The age rides the wrapping
    // millisecond clock, so the subtraction below stays modular.
    static constexpr juce::uint32 walkerStaleAfterMs = 3000;

    juce::uint32 getLastProcessMs() const;

    static bool isStaleTimestamp (juce::uint32 lastMs, juce::uint32 nowMs)
    {
        return nowMs - lastMs > walkerStaleAfterMs;
    }

    static bool isInstanceStale (const TheChannelStripAudioProcessor& instance,
                                 juce::uint32 nowMs)
    {
        return isStaleTimestamp (instance.getLastProcessMs(), nowMs);
    }

    // v2.109.0 (the sidebar follows the view): how many walker editors
    // view this instance right now. Each held view keeps the
    // measurement path running (see processBlock) so a walked console
    // still feeds the walking shell's meters, spectrum and EQ measure
    // with its own editor closed. Message thread only, like the walk.
    void acquireWalkerView()  { viewedByWalkers.fetch_add (1, std::memory_order_relaxed); }
    void releaseWalkerView()  { viewedByWalkers.fetch_sub (1, std::memory_order_relaxed); }
    bool isViewedByWalkers() const
    {
        return viewedByWalkers.load (std::memory_order_relaxed) > 0;
    }

    // the SessionConsoleLink's delivery point - message thread only
    void receiveSessionConsoleChange (int selection);

    // v2.104.0 (the buttons): applies a Track Select press parked by
    // the Console 1 bridge. Message thread only (the editor heartbeat):
    // a console swap must never run on the audio thread. Track 1 is
    // the internal Costes strip, Tracks 2..10 the nine console slots.
    void consumeConsole1Selection();

    // v2.106.0 (the lights): flushes the Console 1 bridge's LED
    // feedback to the hardware - the full sync parked by the last
    // retarget, then every queued echo. Message thread only (the
    // editor heartbeat); without a device it is a no-op.
    void pumpConsole1LedSync();
    void clearHostedPlugin();

    // ---- v2.55.0 (roadmap C1): the internal Costes channel strip ----
    // A small AudioProcessor compiled into the shell, hosted on the very
    // same HostedStrip as the consoles - it inherits the click-free
    // crossfade, the per-ident settings memory and the session paths
    // without a single new wire. The shell's title is its front door:
    // one click hosts it, another click clears it (Clement's choice -
    // no tenth console button).
    void toggleCostesStrip();
    bool isCostesStripActive() const;

    // ---- v2.26 (roadmap point 4): named presets ----
    // A preset is the HOSTED CONSOLE'S OWN STATE (its knobs), saved on
    // disk as one file per preset in the Presets folder next to the
    // plugin database. Display settings (tilt, scales, LUFS reference)
    // are deliberately NOT part of a preset: they travel with the
    // session, a preset is a sound. The list shown in the editor is
    // GLOBAL (every preset of every console); loading a preset whose
    // console is not currently hosted switches to that console first,
    // then applies the state. All failures post an explicit message to
    // the status area and return false (never a silent no-op).
    struct PresetInfo
    {
        juce::String name;         // the user-given preset name
        juce::String consoleName;  // the console the preset belongs to
        // v2.30: the exact file on disk (the selector resolves presets
        // by file now - the same name can exist in several places) and
        // the album it sits in (the sub-folder path under the preset
        // folder, empty = at the root).
        juce::String album;
        juce::File file;
    };

    // The folder holding the .tcsPreset files (created on demand).
    juce::File getPresetsDir() const;
    // Every preset file currently on disk, root AND sub-folders
    // (albums) - sorted root-first then album, name within each group.
    juce::Array<PresetInfo> getAvailablePresets();
    // v2.30: the exact file the hosted console's current preset lives
    // in (falling back to the root file its name implies when only a
    // name is on record - an older session). Empty when no preset is
    // tracked. Used by the editor to pre-fill the Save dialog and by
    // the delete paths to disown the name when its own file goes.
    juce::File getCurrentPresetFile();
    // Save the hosted console's current state under the given name, as
    // a file in the ROOT of the preset folder (a "general" preset -
    // listed in the selector every time). An existing preset of the
    // same name is overwritten; the preset becomes the console's
    // current one.
    bool savePreset (const juce::String& presetName);
    // v2.29/v2.30: Save proper - the hosted console's CURRENT state
    // written to any file chosen in a save dialog. Wherever it lands
    // (the root, an album sub-folder, anywhere else) the saved preset
    // becomes the console's CURRENT one ("Save As" semantics) and the
    // tracking remembers the exact file. It appears in the selector
    // when saved inside the preset folder (root or album).
    bool savePresetTo (const juce::File& destination);
    // Load a root preset BY NAME (the root file its sanitised name
    // implies) - kept for the tests and the name-based flows. The
    // selector and Load use the file-based variant below.
    bool applyPreset (const juce::String& presetName);
    // v2.30: load any preset file on disk, wherever it lives (root,
    // album, or outside the preset folder entirely). If its console is
    // hosted, apply directly; if not, switch to that console and apply
    // it once the instance is ready. The preset becomes the console's
    // current one, tracked at its own file.
    bool applyPresetFile (const juce::File& file);
    // Delete a preset from the preset folder's root, by name.
    bool deletePreset (const juce::String& presetName);
    // v2.29: delete any preset file on disk (the browse-to-delete
    // fallback). If the deleted file was the hosted console's current
    // preset, the name is disowned (the console keeps its sound).
    bool deletePresetFile (const juce::File& file);
    // v2.30: Load - open any .tcsPreset file and load it into the
    // hosted console (switching consoles first when needed). It is
    // NEVER copied anywhere: the folder stays exactly as the user
    // arranged it (v2.29's copy-into-the-root dropped - the root was
    // filling up with duplicates). The loaded preset becomes the
    // console's current one, tracked at the file it was loaded from.
    bool importPreset (const juce::File& source);

    // ---- v2.11: per-console settings memory ----
    // Switching consoles destroys and re-creates the hosted plugin,
    // which used to reset every knob to factory defaults. Each
    // console's last state is now remembered: switching SSL J -> SSL G
    // -> SSL J brings J back exactly as it was left (a console hosted
    // for the very first time still starts from factory defaults). The
    // memory is saved with the session as one <consoleState> element
    // per console. (The v2.10 A/B slots were removed: Clement's use
    // case was comparing consoles, and the bx_consoles already ship
    // their own A/B/C/D presets.)
    void rememberCurrentConsoleState(); // public: getStateInformation refreshes the entry

    // ---- gain staging (display only) ----
    // Short-term BS.1770 correction (input - output): what to ADD to the
    // output to sit at the same loudness as the input. v2.13: the readout
    // states the action ("Add +0.7 dB" = raise the output by 0.7 dB),
    // as in v2.1-v2.11 (the v2.12 measured-deviation reading went back
    // to a correction at Clement's request). v2.42 (point 4): the value
    // is the meters' 5 s power-domain exponential average (silence-
    // gated), computed on the audio thread for a handful of flops - not
    // the short-term delta anymore. False when either side is silent
    // (no reliable measurement yet).
    bool getGainCorrectionDb (float& deltaDb) const;

    // ---- analysis ----
    LufsMeter& getLufsIn()  { return lufsIn; }
    LufsMeter& getLufsOut() { return lufsOut; }
    SpectrumAnalyzer& getAnalyzerIn()  { return analyzerIn; }
    SpectrumAnalyzer& getAnalyzerOut() { return analyzerOut; }
    // v2.22: the cross-spectrum transfer measurement (EQ curve + coherence)
    TransferAnalyzer& getTransferAnalyzer() { return transferAnalyzer; }

    // v2.102.0: the tilt is a real exposed parameter now (see
    // AutomationProxyBank.h) - Studio One can automate it. The editor
    // keeps this exact interface: the getter reads the choice
    // parameter's ladder, the UI setter snaps to the nearest rung and
    // rides the host gesture so write-mode automation records the
    // click, and the session save/restore still uses the plain tiltDb
    // attribute (byte-compatible with older sessions).
    static constexpr float tiltLadder[3] = { -3.0f, -4.5f, -6.0f };

    float getTiltDb() const
    {
        return tiltLadder[(size_t) tiltParam->getIndex()];
    }

    // Nearest rung of the ladder: any legacy saved value maps onto a
    // button, exactly like the editor's restore always did.
    static int tiltIndexFor (float tilt)
    {
        int best = 1;
        float bestDistance = 1.0e9f;

        for (int i = 0; i < 3; ++i)
        {
            const float d = std::abs (tiltLadder[i] - tilt);

            if (d < bestDistance)
            {
                best = i;
                bestDistance = d;
            }
        }

        return best;
    }

    // v2.70.2: the UI-only setters also flag the session - these values
    // are persisted with the state, so the host must know they moved.
    // The session restore bypasses this setter (see
    // restoreTiltFromSession), so a reload never raises a spurious
    // dirty.
    void setTiltDb (float tilt)
    {
        const int index = tiltIndexFor (tilt);

        if (index != tiltParam->getIndex())
        {
            // the host gesture wraps the change so write-mode automation
            // (touch / latch) records the button click
            tiltParam->beginChangeGesture();
            tiltParam->setValueNotifyingHost (tiltParam->convertTo0to1 ((float) index));
            tiltParam->endChangeGesture();
        }

        markSessionStateChanged();
    }

    // Session restore: snap onto the ladder and adopt the value WITHOUT
    // flagging the session (the host saved this tilt, it must not hear
    // it back as a change).
    void restoreTiltFromSession (float tilt)
    {
        tiltParam->setValueNotifyingHost (tiltParam->convertTo0to1 ((float) tiltIndexFor (tilt)));
    }

    // v2.102.0: the automation bank (one proxy slot per hosted
    // parameter position). Test hook for the bridge suite.
    AutomationProxyBank& getAutomationBank() { return *automationBank; }
    Console1Bridge& getConsole1Bridge() { return *console1; }
    juce::AudioParameterChoice* getTiltParameter() const { return tiltParam; }

    // ---- v2.8: analyser display options (persisted with the state) ----
    // v2.51: spectrum vertical scale: dB per division over a six-division
    // grid (top = 2 x dB/div relative to the loudness reference). The
    // ladder is 15/12/9; legacy 10/6 values are mapped on load (see
    // setStateInformation).
    float getAnaDbDiv() const { return anaDbDiv.load (std::memory_order_relaxed); }
    void setAnaDbDiv (float div)
    {
        anaDbDiv.store (div, std::memory_order_relaxed);
        markSessionStateChanged();
    }
    // v2.51: EQ transfer curve scale: dB per division (+/- 3 divisions,
    // ladder 8/6/4/2 = +/-24/18/12/6 dB; legacy 4/2 mapped on load).
    float getEqDbDiv() const { return eqDbDiv.load (std::memory_order_relaxed); }
    void setEqDbDiv (float div)
    {
        eqDbDiv.store (div, std::memory_order_relaxed);
        markSessionStateChanged();
    }
    // Show/hide the analyser strip (the window re-fits when it changes).
    // v2.41: defined in the .cpp - re-showing the strip resets the
    // spectrum analysers, so no pre-hide frame leaks into the display
    // (the fifos hold stale audio across the hidden gap).
    bool getAnalyserVisible() const { return analyserVisible.load (std::memory_order_relaxed); }
    void setAnalyserVisible (bool visible);
    // v2.11: analyser response speed - the IN/OUT smoothing time
    // constant. v2.12 keeps two speeds: Fast ~0.15 s / Slow ~1 s,
    // persisted with the state.
    float getSpecTau() const { return specTau.load (std::memory_order_relaxed); }
    void setSpecTau (float tauSeconds)
    {
        specTau.store (tauSeconds, std::memory_order_relaxed);
        markSessionStateChanged();
    }

    // ---- v2.13: selectable loudness reference for the analyser axis ----
    // The "0 dB" line of the analyser scale means "this many LUFS
    // absolute". Three presets: Record -37 LUFS, Mix -23 LUFS (the
    // default), Master -14 LUFS. Clicking the reference label on the
    // analyser cycles through them; the choice is persisted with the
    // state (missing attribute in an older session = Mix).
    float getLufsTarget() const { return lufsTarget.load (std::memory_order_relaxed); }
    void setLufsTarget (float targetLufs)
    {
        lufsTarget.store (targetLufs, std::memory_order_relaxed);
        markSessionStateChanged();
    }

    // ---- v2.8: measurements only run while the editor is open ----
    // The LUFS meters and spectrum analysers cost a tiny bit of CPU per
    // block; a closed editor never looks at them, so processBlock gates
    // every measurement on this flag. Reopening resets the meters so no
    // stale pre-close values leak into the display.
    void setEditorOpen (bool isOpen);

    static juce::String identFor (const juce::PluginDescription& d);
    juce::File getAppDataDir() const;

    // Rescan the default VST3 folders (out-of-process in the Standalone).
    void requestDefaultScan();

    // ---- v2.14.2: channel-format diagnostics for the editor readout ----
    // What the DAW currently feeds this wrapper (1 = mono track, 2 = stereo)
    // and which audio path the console takes, with the exact fallback
    // reason when the native mono layout is not used.
    juce::String getChannelReadout() const;

    // ---- v2.33 (roadmap idea 3): shell integrity readout ----
    // Two compact sidebar lines that answer "is the shell transparent?"
    // at a glance (the question of the +3 dB investigation): the shell
    // runs at unity gain (a fact of the architecture - it never applies
    // gain of its own), whether a console-swap crossfade is in flight,
    // the latency forwarded to the host, and the silence sentinel's
    // state (armed = the watch has not fired for the hosted console;
    // swapping or re-hosting re-arms it). Built from atomics and cached
    // ints only - no locks - and safe to call from the 30 Hz UI timer.
    juce::String getShellIntegrityLine() const;

    // ---- v2.34: the sidebar's merged status lines --------------------------
    // The two compact lines under the hosted console's name: channel
    // format + crossfade on line one, forwarded latency + sentinel on
    // line two. The v2.33 integrity bandeau and the v2.14.2 channel
    // readout collapsed into this single block with ZERO redundancy
    // between the sidebar areas: "shell: unity" is a constant
    // architectural fact and lives in the Doctor report only. Same
    // facts as getChannelReadout() / getShellIntegrityLine(), which the
    // Doctor report keeps using untouched. Display-only, message thread.
    juce::String getSidebarStatusLines() const;

    // ---- v2.57.0 (Clement's remark 3): the estimated CPU load ---------
    // The average share of real time the audio processing itself takes,
    // measured on the audio thread and published about once a second:
    // 25.0 means processBlock spends a quarter of the wall clock the
    // host gives it. Starts at -1 (no reading yet - the sidebar shows
    // "--" until a full second of audio has flowed); the editor reads
    // it on its timer and refreshes the readout at most once a second.
    double getEstimatedCpuLoadPercent() const
    {
        return estimatedCpuLoadPercent.load (std::memory_order_relaxed);
    }

    // ---- v2.33 (roadmap idea 4): the Doctor report ----
    // A plain-text snapshot of everything worth knowing when something
    // misbehaves: shell and JUCE versions, host and wrapper, sample
    // rate and block size, the channel readout, the hosted console and
    // its forwarded latency, the integrity readout above, the plugin
    // database summary (per console: found / not found / blacklisted /
    // file missing), the number of presets on disk, the last status
    // message and the per-console remembered-state block sizes. Message
    // thread only (it takes scanLock and consoleStateLock); the editor
    // builds it when the logo is clicked, shows it in a small window
    // and writes it to a file next to the plugin database.
    juce::String buildDoctorReport();

    // v2.70.1: what the last session restore applied to the internal
    // strip, as published the moment its blob landed (see the restore
    // branch of instantiateCostesStrip). The doctor report prints it
    // next to the strip's live reading - the pair pinpoints whether a
    // value was already wrong when the blob landed. Thread-safe.
    juce::String getLastRestoreSummary() const;

    // v2.16.1: the latency currently forwarded to the host (the editor
    // logs it to the Terminal whenever it changes).
    int getForwardedLatency() const { return lastReportedLatency; }

    // v2.24 (point 7): how many times the silent-console watch has fired
    // over the life of the processor (each hosted instance can fire at
    // most once). The test harness asserts the re-arm behaviour with it.
    int getSilenceWarningCount() const
    {
        return silenceWarningCount.load (std::memory_order_relaxed);
    }

private:
    void runScan();
    void scanDirectoryForFormat (const juce::File& directory);
    // v2.27: fold the lock-free knob-move flag into the hosted
    // console's "*" state. Message thread only (the editor timer, a
    // console switch, a session save) - and it MUST run before
    // currentPluginIdent changes, so a knob move made just before a
    // switch stars the preset of the console it belongs to.
    void flushPresetDirtyIntoCurrentConsole();

    // v2.29: shared by deletePreset and deletePresetFile - if the
    // given file is the hosted console's current preset's own file,
    // disown the name (the console keeps its sound, no preset name).
    void disownPresetIfItsFileIs (const juce::File& file);

    // v2.30: the file the given console's current preset lives in -
    // the tracked one when there is one (v2.30 sessions and every
    // save/load path track it), else the root file its name implies
    // (an older session). consoleStateLock must be held by the caller.
    juce::File currentPresetFileFor (const juce::String& ident);

    // ---- v2.26: preset file helpers ----
    // deterministic name -> filename mapping (the user-given name is
    // stored verbatim inside the file; the filename is sanitised)
    static juce::String presetFileNameFor (const juce::String& presetName);
    // parse one .tcsPreset file; false when missing/corrupt/empty state
    bool readPresetFile (const juce::File& file, PresetInfo& info, juce::MemoryBlock& state);

    // ---- v2.24 (roadmap point 7): silent-console watch ----
    // Detects the classic "licence expired" failure mode: a third-party
    // console loads fine but produces digital silence (Plugin Alliance
    // plugins do exactly this once their licence lapses). Runs from
    // processBlock while the editor is open: when the input carries
    // signal but the hosted console has emitted nothing for 5 seconds,
    // ONE explicit warning is posted to the status area. Once per
    // hosted instance - swapping consoles or re-hosting re-arms it.
    void watchHostedSilence (int numSamples);
    std::atomic<bool> silenceWarned { false };
    std::atomic<int> silenceWarningCount { 0 };
    double silenceWatchSeconds = 0.0;          // audio thread only
    juce::AudioProcessor* silenceWatchedPlugin = nullptr; // audio thread only

    // ---- v1.0.10: out-of-process collection scan ----
    bool isStandaloneApp() const;
    bool launchScanChildren();
    void drainChildOutput (juce::ChildProcess& child);
    [[noreturn]] void runScanChildAndExit();

    void instantiateHosted (const juce::PluginDescription& desc, bool applyPendingState);
    void maybeRestoreHosted();

    // v2.55.0 (roadmap C1): the internal strip's hosting path. It never
    // goes through the KnownPluginList (there is no file to scan), so it
    // gets its own synchronous instantiate that build+swap on the caller's
    // thread under the same instantiateGuard. selectCostesStrip publishes
    // the ident; the session restore (maybeRestoreHosted) recognises the
    // ident and comes here with applyPendingState = true.
    static juce::String costesIdent();
    void instantiateCostesStrip (bool applyPendingState);

    // v2.55.0: the per-ident settings memory + queued-preset tail of
    // instantiateHosted, factored out so the internal strip's path
    // applies exactly the same rules (ident-checked pending preset
    // first, remembered state second, HostedStatePushGuard around the
    // push so coming back cannot raise the preset's "*").
    void applyRememberedOrQueuedState (const juce::String& targetIdent);

    void selectCostesStrip();

    // v2.107.0: the two selection paths publish to the session link once
    // their change has actually landed (an unavailable console returns
    // before this point). Selection 0 = the internal strip,
    // 1..numConsoles = the console slots.
    void publishSessionLinkChange (int selection);

    // v2.51.1: identGuard-guarded write (see the member declaration).
    void setCurrentPluginIdent (const juce::String& ident);

    // ---- v2.14: native mono hosting ----
    // tryApplyMonoLayout asks a hosted console for a native mono->mono bus
    // layout and returns true only if the plugin really ended up mono.
    // restoreStereoLayout puts an instance back on the proven v2.8.2
    // 2-in/2-out configuration.
    bool tryApplyMonoLayout (juce::AudioProcessor& plugin);
    void restoreStereoLayout (juce::AudioProcessor& plugin);

    void loadKnownPluginList();
    void saveKnownPluginList();

    juce::AudioProcessorValueTreeState apvts;

    HostedStrip hostedStrip;
    LufsMeter lufsIn, lufsOut;
    SpectrumAnalyzer analyzerIn, analyzerOut;
    // v2.22: fed the pre-console input (pushInput) and the post-console
    // output (pushOutput) of every block, so its frames stay aligned.
    TransferAnalyzer transferAnalyzer;

    // v2.15.5: forwards the hosted console's gain reduction to Studio One
    // (see GRForwarder.h). Declared AFTER hostedStrip on purpose: members
    // are destroyed in reverse order, so the forwarder (which borrows COM
    // pointers from the hosted console) is destroyed BEFORE the console.
    GRForwarder grForwarder;

    juce::CriticalSection scanLock;
    // v2.51.1: currentPluginIdent is written on the message/host thread
    // (selectConsole, clearHostedPlugin, setStateInformation) and read on
    // the background instantiation threads - every cross-thread access
    // goes through the identGuard (getCurrentPluginIdent /
    // setCurrentPluginIdent).
    juce::String currentPluginIdent;
    mutable juce::CriticalSection identGuard;
    // v2.51.1: serializes instantiateHosted (see PluginProcessor.cpp) -
    // two overlapping instantiations raced the HostedStrip relay vector
    // to a double free (Studio Pro 8.1.2 crash, POINTER_BEING_FREED_WAS_
    // NOT_ALLOCATED). Lock order against scanLock is ALWAYS
    // instantiateGuard first, scanLock second (instantiateHosted logs
    // through logHostStatus), so runScan must release scanLock before
    // it calls maybeRestoreHosted.
    juce::CriticalSection instantiateGuard;
    juce::String hostStatus;
    juce::MemoryBlock savedHostedStateForRestore; // uses reset()/append(): MemoryBlock has no clear()
    std::atomic<bool> pendingRestore { false };

    // v2.70.1: the summary the last session restore published when its
    // blob landed on the hosted strip (see costesStripSummary in
    // the .cpp and getLastRestoreSummary for the public read).
    juce::String lastRestoreSummary;
    mutable juce::CriticalSection restoreSummaryLock;
    std::atomic<bool> scanning { false };

    // v2.102.0: handles to the exposed parameters. The AudioProcessor
    // base owns both (addParameter) - these pointers never delete. The
    // bank keeps non-owning proxy pointers for forwarding.
    juce::AudioParameterChoice* tiltParam = nullptr;
    std::unique_ptr<AutomationProxyBank> automationBank;

    // v2.103.0 (the hands): the Console 1 bridge - MIDI in, the frozen
    // 26-role CC table, name-based resolution onto the hosted console.
    // Declared after the bank so it is destroyed first: no hosted
    // parameter pointer can outlive the console it points into.
    std::unique_ptr<Console1Bridge> console1;

    // ---- v2.11: per-console settings memory (see the editor API above) ----
    // v2.27: mutable - the current-preset getters take the lock too.
    mutable juce::CriticalSection consoleStateLock;
    std::map<juce::String, juce::MemoryBlock> consoleStates; // keyed by plugin ident
    // v2.26: a preset whose console is NOT the currently hosted one is
    // applied right after that console is instantiated (see applyPreset
    // and instantiateHosted). Guarded by consoleStateLock. Cleared in
    // clearHostedPlugin so a stale request can never surprise a later
    // manual selection (the hosting itself checks the ident anyway).
    juce::MemoryBlock pendingPresetState;
    juce::String pendingPresetIdent;
    // v2.27: the name that belongs with the queued preset state above;
    // consumed by instantiateHosted right next to it.
    juce::String pendingPresetName;
    // v2.30: the file the queued preset was loaded from (see
    // consolePresetFiles below).
    juce::String pendingPresetFile;
    // v2.27: current-preset tracking, keyed by plugin ident like
    // consoleStates and persisted the same way (attributes on each
    // <consoleState> element). consolePresetNames holds the preset
    // associated with each console (empty = none), consolePresetModified
    // whether its knobs have moved since that preset was loaded or
    // saved (the "*" marker the editor shows left of the name).
    // v2.30: consolePresetFiles holds the exact file each console's
    // current preset lives in (a preset in an album, or loaded from
    // outside the preset folder, does not sit at the root path its
    // name implies). Empty = unknown (an older session), and the
    // root file the name implies is assumed.
    // Guarded by consoleStateLock.
    std::map<juce::String, juce::String> consolePresetNames;
    std::map<juce::String, bool> consolePresetModified;
    std::map<juce::String, juce::String> consolePresetFiles;
    // v2.27: knob-move detection. Any state the SHELL pushes into the
    // hosted plugin (a preset, the per-console remembered state, a
    // session restore) is not a user modification, so the parameter
    // listener is muted while the push runs (HostedStatePushGuard in
    // PluginProcessor.cpp). The callback can fire on any thread
    // (automation, the plugin's own editor), so it only raises this
    // lock-free flag; drainPresetDirtyFlag (message thread) moves it
    // into consolePresetModified.
    std::atomic<bool> suppressPresetDirty { false };
    std::atomic<bool> presetDirtyPending { false };
    // v2.8: display options + editor-open gate (see the editor API above).
    std::atomic<float> anaDbDiv { 12.0f }; // v2.53 default: 15/12/9 dB/div
    std::atomic<float> eqDbDiv { 4.0f };   // v2.53 default: 8/6/4/2 dB/div
    std::atomic<bool> analyserVisible { true };

    // v2.107.0 (the session link): the opt-in flag (rides the session
    // state) and the re-entrancy guard that keeps a follow applied BY
    // the link from being re-published to it.
    std::atomic<bool> sessionLinkEnabled { false };
    // v2.108.0 (the channel walker): the session-local channel number;
    // 0 only during the constructor's auto-assignment.
    std::atomic<int> channelNumber { 0 };
    // v2.108.1: refreshed at the head of every processBlock - the
    // walker's freshness probe (see isStaleTimestamp above).
    std::atomic<juce::uint32> lastProcessMs { 0 };
    // v2.109.0: the walker-view refcount (see acquireWalkerView).
    std::atomic<int> viewedByWalkers { 0 };
    bool applyingSessionLinkChange = false;   // message thread only
    std::atomic<bool> measuringEnabled { false };
    // v2.11: analyser response speed (IN/OUT smoothing time constant).
    std::atomic<float> specTau { 1.0f };
    // v2.14: true while the active hosted console runs natively mono->mono
    // (mono track and the plugin accepted the layout); false = the process
    // block stereo bridge below is used instead.
    std::atomic<bool> hostedMonoMode { false };
    // v2.14.1: track format the DAW currently gives this wrapper
    // (1 = mono, 2 = stereo), for the editor's channel readout.
    std::atomic<int> wrapperChannels { 2 };
    // v2.14.2: why the native mono layout is NOT in use, for the readout:
    // 0 = native mono, 1 = refused by the plugin, 2 = a next-best layout
    // was applied instead, 3 = not applicable (no main bus), 4 = stereo
    // track / never negotiated.
    std::atomic<int> hostedMonoResult { 4 };
    // v2.14.2: the main-bus channel counts the plugin settled on when the
    // mono request was answered with a next-best layout.
    std::atomic<int> settledMainIn  { 2 };
    std::atomic<int> settledMainOut { 2 };
    // v2.13: loudness reference of the analyser axis (0 dB line),
    // in LUFS (-37 / -23 / -14).
    std::atomic<float> lufsTarget { -23.0f };

    // v2.8.2 crash fix, kept as the FALLBACK since v2.14: on a mono track
    // the host hands processBlock a 1-channel buffer; a stereo-only hosted
    // console crashes on one. Since v2.14 the console is first asked for a
    // native mono layout (see hostedMonoMode) - this bridge is only used
    // when the plugin refuses it: the mono signal is up-mixed into this
    // 2-channel bridge buffer, the console runs in stereo, and the result
    // is down-mixed back into the mono channel.
    juce::AudioBuffer<float> stereoBridge;

    double currentSampleRate = 48000.0;
    int currentBlockSize = 512;
    int numChannels = 2;
    int lastReportedLatency = -1;

    // ---- v2.57.0 (Clement's remark 3): the CPU metering window --------
    // Everything below except the published percent lives on the audio
    // thread alone (single writer, no atomics needed): each block adds
    // its wall-clock work, and once the window has seen a full second
    // of audio the percent of real time is published for the sidebar.
    // A gap of half a second or more between blocks re-arms the window,
    // so a stop/restart never dilutes a fresh measurement with the old
    // one. prepareToPlay resets the whole window and the reading.
    std::atomic<double> estimatedCpuLoadPercent { -1.0 };
    std::chrono::steady_clock::time_point cpuWindowStart {};
    std::chrono::steady_clock::time_point cpuLastBlockEnd {};
    std::chrono::nanoseconds cpuWork { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TheChannelStripAudioProcessor)
};
