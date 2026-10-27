#pragma once

#include <array>
#include <atomic>
#include <map>
#include <juce_audio_processors/juce_audio_processors.h>
#include "HostedStrip.h"
#include "GRForwarder.h"
#include "LufsMeter.h"
#include "SpectrumAnalyzer.h"
#include "TransferAnalyzer.h"

class TheChannelStripAudioProcessorEditor;

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
    juce::String getCurrentPluginIdent() const { return currentPluginIdent; }
    // Last hosting event (scan result / instantiation error). Sticky.
    juce::String getHostStatus();
    // v2.27: status posts from the editor (e.g. export with no current
    // preset) share the processor's status area.
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
    void clearHostedPlugin();

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
    };

    // The folder holding the .tcsPreset files (created on demand).
    juce::File getPresetsDir() const;
    // Every preset file currently on disk, sorted by name.
    juce::Array<PresetInfo> getAvailablePresets();
    // Save the currently hosted console's state under the given name
    // (an existing preset of the same name is overwritten).
    bool savePreset (const juce::String& presetName);
    // Load a preset: if its console is hosted, apply directly; if not,
    // switch to that console and apply it once the instance is ready.
    bool applyPreset (const juce::String& presetName);
    // Delete a preset file.
    bool deletePreset (const juce::String& presetName);
    // Copy a preset file to an arbitrary location (sharing/backup).
    bool exportPreset (const juce::String& presetName, const juce::File& destination);
    // Copy an external .tcsPreset file into the Presets folder, then
    // load it: it becomes the console's current preset (v2.28.0),
    // exactly as if it had been picked in the selector. The copy can
    // succeed while the load is refused (console not installed) - the
    // file stays available in the list, the status area says why.
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
    // to a correction at Clement's request). False when either side is
    // silent (no reliable measurement yet).
    bool getGainCorrectionDb (float& deltaDb) const;

    // ---- analysis ----
    LufsMeter& getLufsIn()  { return lufsIn; }
    LufsMeter& getLufsOut() { return lufsOut; }
    SpectrumAnalyzer& getAnalyzerIn()  { return analyzerIn; }
    SpectrumAnalyzer& getAnalyzerOut() { return analyzerOut; }
    // v2.22: the cross-spectrum transfer measurement (EQ curve + coherence)
    TransferAnalyzer& getTransferAnalyzer() { return transferAnalyzer; }

    float getTiltDb() const { return tiltDb.load (std::memory_order_relaxed); }
    void setTiltDb (float tilt) { tiltDb.store (tilt, std::memory_order_relaxed); }

    // ---- v2.8: analyser display options (persisted with the state) ----
    // Spectrum vertical scale: dB per division (top = +10 dB relative to
    // the -23 LUFS reference, 9 divisions down).
    float getAnaDbDiv() const { return anaDbDiv.load (std::memory_order_relaxed); }
    void setAnaDbDiv (float div) { anaDbDiv.store (div, std::memory_order_relaxed); }
    // EQ transfer curve scale: dB per division (+/- 4.5 divisions).
    float getEqDbDiv() const { return eqDbDiv.load (std::memory_order_relaxed); }
    void setEqDbDiv (float div) { eqDbDiv.store (div, std::memory_order_relaxed); }
    // Show/hide the analyser strip (the window re-fits when it changes).
    bool getAnalyserVisible() const { return analyserVisible.load (std::memory_order_relaxed); }
    void setAnalyserVisible (bool visible) { analyserVisible.store (visible, std::memory_order_relaxed); }
    // v2.11: analyser response speed - the IN/OUT smoothing time
    // constant. v2.12 keeps two speeds: Fast ~0.15 s / Slow ~1 s,
    // persisted with the state.
    float getSpecTau() const { return specTau.load (std::memory_order_relaxed); }
    void setSpecTau (float tauSeconds) { specTau.store (tauSeconds, std::memory_order_relaxed); }

    // ---- v2.13: selectable loudness reference for the analyser axis ----
    // The "0 dB" line of the analyser scale means "this many LUFS
    // absolute". Three presets: Record -37 LUFS, Mix -23 LUFS (the
    // default), Master -14 LUFS. Clicking the reference label on the
    // analyser cycles through them; the choice is persisted with the
    // state (missing attribute in an older session = Mix).
    float getLufsTarget() const { return lufsTarget.load (std::memory_order_relaxed); }
    void setLufsTarget (float targetLufs) { lufsTarget.store (targetLufs, std::memory_order_relaxed); }

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
    juce::String currentPluginIdent;
    juce::String hostStatus;
    juce::MemoryBlock savedHostedStateForRestore; // uses reset()/append(): MemoryBlock has no clear()
    std::atomic<bool> pendingRestore { false };
    std::atomic<bool> scanning { false };
    std::atomic<float> tiltDb { -4.5f };

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
    // v2.27: current-preset tracking, keyed by plugin ident like
    // consoleStates and persisted the same way (attributes on each
    // <consoleState> element). consolePresetNames holds the preset
    // associated with each console (empty = none), consolePresetModified
    // whether its knobs have moved since that preset was loaded or
    // saved (the "*" marker the editor shows left of the name).
    // Guarded by consoleStateLock.
    std::map<juce::String, juce::String> consolePresetNames;
    std::map<juce::String, bool> consolePresetModified;
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
    std::atomic<float> anaDbDiv { 10.0f };
    std::atomic<float> eqDbDiv { 4.0f };
    std::atomic<bool> analyserVisible { true };
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TheChannelStripAudioProcessor)
};
