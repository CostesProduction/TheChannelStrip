#pragma once

// ===========================================================================
// v2.103.0 (the hands): the Console 1 bridge.
//
// Clement drives TheChannelStrip with a Softube Console 1 MK2. Since the
// 2026 Console 1 software update the MK2 hardware can also behave as a
// plain MIDI controller (mode set from the On-Screen Display menus, the
// device only emits while the OSD runs). Clement captured every knob on
// 2026-11-09 with MIDI Monitor: all 26 knobs send 7-bit absolute CC on
// channel 1, 0..127. This bridge listens to that stream and moves the
// hosted console.
//
// Three layers, frozen one at a time:
//   1. CC -> role: the table below is frozen from the capture. It is the
//      only hardware-specific data in this file.
//   2. role -> hosted parameter: resolved automatically when a console
//      takes the slot, by matching normalised name synonyms (lowercase
//      alphanumerics only). The longest pattern wins globally, greedy
//      assignment, so "Gate Threshold" maps to the gate and not to the
//      compressor threshold ("gatethreshold" is 13 chars, "threshold"
//      is 9). Roles the console does not expose simply stay idle.
//   3. value: absolute 7-bit, value / 127, snapped to the parameter's
//      steps when it is discrete (2..128 steps).
//
// Everything is applied through setValueNotifyingHost, so the whole
// v2.102 path follows for free: the editor knob, the EQ-Verite flush,
// the GR forwarder, the DAW automation lane and the preset star.
//
// Threading: the MIDI callback only pushes bytes into a lock-free SPSC
// FIFO (overflow drops silently, the knob will send again). The FIFO is
// drained from processBlock on the audio thread - the same thread the
// v2.102 automation proxies already use to push setValueNotifyingHost
// into the hosted console, so no message pump is ever involved and no
// juce::Timer either: a timer drags the shared JUCE timer thread and
// the message queue into a shell that must stay real-time-safe and
// message-loop agnostic. The role->parameter table is swapped under a
// SpinLock because retargets can arrive from the instantiation thread
// while the audio thread drains. The device is auto-detected by name on
// the bridge's own lightweight thread, retried every 2 s, so plugging
// the unit in later just works.
// ===========================================================================

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

// ---- layer 1: the frozen CC table -----------------------------------------

enum class C1Role
{
    inputGain, highCut, lowCut,                  //  0 -  2 : Entree
    gateThreshold, gateRelease, sustain, punch,  //  3 -  6 : Shape
    eqLowFreq, eqLowGain,                        //  7 -  8 : EQ low band
    eqLoMidQ, eqLoMidFreq, eqLoMidGain,          //  9 - 11 : EQ low mid
    eqHiMidQ, eqHiMidFreq, eqHiMidGain,          // 12 - 14 : EQ high mid
    eqHighFreq, eqHighGain,                      // 15 - 16 : EQ high band
    compRatio, compDryWet, compAttack,            // 17 - 19 : Comp
    compRelease, compThreshold,                  // 20 - 21 : Comp
    drive, character, pan, output,               // 22 - 25 : Sortie
    numRoles
};

constexpr int c1NumRoles = static_cast<int> (C1Role::numRoles);

// The capture of 2026-11-09 (Console 1 MK2, MIDI mode, channel 1). Some
// CCs carry exotic CoreMIDI names on the MK2 ("Tremolo Depth" is the low
// band frequency, "General Purpose 2 (fine)" is the ratio) - the numbers
// below are what the hardware actually sends.
inline int c1CcForRole (int role)
{
    static const int ccs[c1NumRoles] = {
        107, 105, 103,       // inputGain, highCut, lowCut
        54,  56,  55,  57,   // gateThreshold, gateRelease, sustain, punch
        92,  91,             // eqLowFreq, eqLowGain
        90,  89,  88,        // eqLoMidQ, eqLoMidFreq, eqLoMidGain
        87,  86,  85,        // eqHiMidQ, eqHiMidFreq, eqHiMidGain
        83,  82,             // eqHighFreq, eqHighGain
        34,  35,  36,  33, 47, // compRatio, compDryWet, compAttack, compRelease, compThreshold
        15,   3,  10,   7    // drive, character, pan, output
    };
    return (role >= 0 && role < c1NumRoles) ? ccs[role] : -1;
}

inline int c1RoleForCc (int cc)
{
    for (int i = 0; i < c1NumRoles; ++i)
        if (c1CcForRole (i) == cc)
            return i;
    return -1;
}

// ---- layer 2: name synonyms -----------------------------------------------
//
// v2.103.1: the in-situ pass of 2026-11-09 (a real MK2 against the
// Costes console) showed six knobs working and twenty resolving
// nothing or the wrong parameter: the Costes console registers
// "Trim In", "Expander Threshold", the numbered EQ ("EQ 2
// Width") and the "Compressor *" knobs, none of which the generic
// patterns knew. The exact normalised Costes names are pinned
// below. Two field-found collisions are settled by pattern length:
// the punch knob takes "Transient Attack" (15 chars) over "Punch
// On" (5), and the character knob is the tilt - the generic
// "character" pattern is gone because on the Costes console it
// grabbed "Compressor Character" (9) and the tilt (4) could never
// win. A hosted console that really exposes a character knob can
// get a pinned pattern the day one exists.

namespace Console1Detail
{
    inline const char* const wInputGain[]     = { "inputgain", "inputtrim", "intrim", "inputlevel", "inlevel",
                                                 "trimin", nullptr };
    inline const char* const wHighCut[]       = { "highcut", "hpf", "highpasscut", nullptr };
    inline const char* const wLowCut[]        = { "lowcut", "inputlowcut", "lowcutfilter", nullptr };
    inline const char* const wGateThreshold[] = { "gatethreshold", "gatethresh", "gatelevel", "gategain", "gate",
                                                 "expanderthreshold", nullptr };
    inline const char* const wGateRelease[]  = { "gaterelease", "shaperelease", "expanderrelease", nullptr };
    inline const char* const wSustain[]      = { "sustain", nullptr };
    inline const char* const wPunch[]         = { "punch", "transientattack", nullptr };
    inline const char* const wEqLowFreq[]    = { "lowfreq", "lowfrequency", "lffreq", "bassfreq", "lowbandfreq",
                                                 "eq1frequency", nullptr };
    inline const char* const wEqLowGain[]    = { "lowgain", "lfgain", "bassgain", "lowbandgain", "eq1gain", nullptr };
    inline const char* const wEqLoMidQ[]     = { "lowmidq", "lmcue", "lowmidcue", "eq2width", nullptr };
    inline const char* const wEqLoMidFreq[]  = { "lowmidfreq", "lowmidfrequency", "lmfreq", "midlowfreq",
                                                 "eq2frequency", nullptr };
    inline const char* const wEqLoMidGain[]  = { "lowmidgain", "lmgain", "midlowgain", "eq2gain", nullptr };
    inline const char* const wEqHiMidQ[]     = { "highmidq", "hmcue", "highmidcue", "eq3width", nullptr };
    inline const char* const wEqHiMidFreq[]  = { "highmidfreq", "highmidfrequency", "hmfreq", "midhighfreq",
                                                 "eq3frequency", nullptr };
    inline const char* const wEqHiMidGain[]  = { "highmidgain", "hmgain", "midhighgain", "eq3gain", nullptr };
    inline const char* const wEqHighFreq[]   = { "highfreq", "highfrequency", "hffreq", "treblefreq", "highbandfreq",
                                                 "eq4frequency", nullptr };
    inline const char* const wEqHighGain[]   = { "highgain", "hfgain", "treblegain", "highbandgain", "eq4gain", nullptr };
    inline const char* const wCompRatio[]    = { "ratio", "compratio", "compressionratio", "compressorratio", nullptr };
    inline const char* const wCompDryWet[]   = { "parallel", "drywet", "mix", "blend", "compressormix", nullptr };
    inline const char* const wCompAttack[]   = { "attack", "attacktime", "compattack", "compressorfastattack", nullptr };
    inline const char* const wCompRelease[]  = { "release", "releasetime", "comprelease", "compressorrelease", nullptr };
    inline const char* const wCompThreshold[]= { "threshold", "thresh", "compthreshold", "compressionthreshold",
                                                 "compressorthreshold", nullptr };
    inline const char* const wDrive[]        = { "drive", "saturation", "saturationdrive", nullptr };
    // v2.103.1: the tilt, not the generic "character" - that pattern
    // always grabbed "Compressor Character" (9 chars) and the tilt
    // (4 chars) could never win the greedy assignment.
    inline const char* const wCharacter[]    = { "tilt", nullptr };
    inline const char* const wPan[]          = { "pan", "balance", "panorama", "panpot", nullptr };
    inline const char* const wOutput[]       = { "output", "outputlevel", "channelvolume", "volume",
                                                 "outlevel", "outtrim", "trimout", "mastervolume", nullptr };

    inline const char* const* const words[c1NumRoles] = {
        wInputGain, wHighCut, wLowCut,
        wGateThreshold, wGateRelease, wSustain, wPunch,
        wEqLowFreq, wEqLowGain,
        wEqLoMidQ, wEqLoMidFreq, wEqLoMidGain,
        wEqHiMidQ, wEqHiMidFreq, wEqHiMidGain,
        wEqHighFreq, wEqHighGain,
        wCompRatio, wCompDryWet, wCompAttack,
        wCompRelease, wCompThreshold,
        wDrive, wCharacter, wPan, wOutput
    };
}

// Lowercase alphanumerics only - case, spaces, punctuation and units are
// stripped so "High Cut", "high-cut" and "HIGHCUT" all read the same.
inline juce::String c1Normalise (const juce::String& name)
{
    // build2: the lowercase copy is a NAMED local. A CharPointer into an
    // unnamed temporary dangles the moment the for-initialiser ends
    // (the temporary dies at its semicolon) - the loop would walk a freed
    // String, silent single-threaded and a segfault under the concurrent
    // swap storm. The local keeps the storage alive for the whole scan.
    const juce::String lower = name.toLowerCase();
    juce::String out;
    for (auto p = lower.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const juce::juce_wchar c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            out += c;
    }
    return out;
}

// The length of the longest synonym of this role that the (normalised)
// parameter name contains. Zero means no match.
inline int c1LongestMatch (int role, const juce::String& normalisedName)
{
    if (role < 0 || role >= c1NumRoles)
        return 0;
    const char* const* w = Console1Detail::words[role];
    int best = 0;
    for (int i = 0; w[i] != nullptr; ++i)
        if (normalisedName.contains (juce::String (w[i])))
            best = juce::jmax (best, (int) std::strlen (w[i]));
    return best;
}

// ---- the bridge ------------------------------------------------------------

class Console1Bridge : private juce::MidiInputCallback
{
public:
    Console1Bridge() = default;

    // The processor calls this once from its constructor: looks the
    // device up right away and starts the 2 s detection thread. The
    // FIFO itself is NOT drained here - processBlock owns the drain,
    // so no juce::Timer and no message thread are ever in the loop.
    void start()
    {
        if (! hardwareEnabled.load (std::memory_order_relaxed))
            return; // the offline harness: no device, no thread
        if (detector.joinable())
            return; // already started

        autoDetectAndOpen();
        detector = std::thread ([this] { detectionLoop(); });
    }

    // Joins the detection thread before the members die (the
    // processor tears the bridge down on the message thread).
    ~Console1Bridge()
    {
        detectorStop.store (true, std::memory_order_relaxed);
        if (detector.joinable())
            detector.join();
    }

    // The offline test harness flips this to false BEFORE constructing any
    // processor, so a real Console 1 unit plugged into the build machine
    // cannot move the hosted consoles mid-run. The plugin binary keeps the
    // default (true). Only the auto-detection is gated - with the gate
    // off the detection thread does not even start - and the tests
    // still exercise the whole pipeline through postController +
    // drainNow.
    static void setHardwareEnabled (bool enabled)
    {
        hardwareEnabled.store (enabled, std::memory_order_relaxed);
    }

    // Called from HostedStrip::onActivePluginChanged (v2.102 contract: the
    // callback fires while the outgoing console is still alive, so reading
    // its parameter table here is safe). Nullptr empties every role.
    void retargetPlugin (juce::AudioProcessor* active)
    {
        // A function-local static keeps the header self-contained: the
        // empty list for the idle case, one instance for the whole
        // process (the same shape as AutomationProxyBank::retarget).
        static const juce::Array<juce::AudioProcessorParameter*> emptyParameters;

        const juce::Array<juce::AudioProcessorParameter*>& params =
            active != nullptr ? active->getParameters() : emptyParameters;

        const int n = (int) params.size();

        // Score every (role, parameter) pair before taking the lock.
        std::vector<std::vector<int>> scores ((size_t) c1NumRoles,
                                              std::vector<int> ((size_t) n, 0));
        if (n > 0)
        {
            std::vector<juce::String> names ((size_t) n);
            for (int i = 0; i < n; ++i)
                names[(size_t) i] = c1Normalise (params.getUnchecked (i)->getName (64));

            for (int r = 0; r < c1NumRoles; ++r)
                for (int i = 0; i < n; ++i)
                    scores[(size_t) r][(size_t) i] = c1LongestMatch (r, names[(size_t) i]);
        }

        // Greedy global assignment: the longest pattern wins first, so
        // "Gate Threshold" goes to the gate before the compressor can
        // claim it with its shorter "threshold".
        std::array<int, c1NumRoles> assigned;
        assigned.fill (-1);
        std::vector<char> used ((size_t) n, 0);

        for (;;)
        {
            int bestScore = 0, bestRole = -1, bestParam = -1;
            for (int r = 0; r < c1NumRoles; ++r)
            {
                if (assigned[(size_t) r] >= 0)
                    continue;
                for (int i = 0; i < n; ++i)
                {
                    if (used[(size_t) i] != 0)
                        continue;
                    const int s = scores[(size_t) r][(size_t) i];
                    if (s > bestScore)
                    {
                        bestScore = s;
                        bestRole  = r;
                        bestParam = i;
                    }
                }
            }
            if (bestScore <= 0)
                break;
            assigned[(size_t) bestRole] = bestParam;
            used[(size_t) bestParam]   = 1;
        }

        // Swap the resolved table under the lock. idx >= 0 can only
        // happen when active was non-null, so getUnchecked is in
        // range by construction.
        const juce::SpinLock::ScopedLockType lock (targetLock);
        for (int r = 0; r < c1NumRoles; ++r)
        {
            const int idx = assigned[(size_t) r];
            targetIndices[(size_t) r] = idx;
            targets[(size_t) r] = (idx >= 0) ? params.getUnchecked (idx) : nullptr;
        }
    }

    // Thread-safe: the MIDI callback (or a test) pushes bytes here.
    void postController (int cc, int value)
    {
        const int h = head.load (std::memory_order_relaxed);
        if (h - tail.load (std::memory_order_acquire) >= fifoCapacity)
            return; // full: drop, the knob will send again on its next tick

        fifo[(size_t) (h & (fifoCapacity - 1))] = { (juce::uint8) juce::jlimit (0, 127, cc),
                                                   (juce::uint8) juce::jlimit (0, 127, value) };
        head.store (h + 1, std::memory_order_release);
    }

    // Drains the FIFO and applies every event. Called from processBlock
    // on the audio thread, and directly by the offline test harness. An
    // empty drain is two atomic loads - free for the real-time path.
    void drainNow()
    {
        int t = tail.load (std::memory_order_relaxed);
        while (t != head.load (std::memory_order_acquire))
        {
            const auto ev = fifo[(size_t) (t & (fifoCapacity - 1))];
            ++t;
            applyEvent (ev.cc, ev.value);
        }
        tail.store (t, std::memory_order_release);
    }

    // ---- introspection (sidebar, doctor, tests) --------------------------

    bool isDeviceOpen() const noexcept  { return midiInput != nullptr; }
    const juce::String& getDeviceName() const noexcept { return deviceName; }

    int getMatchedRoleCount() const
    {
        const juce::SpinLock::ScopedLockType lock (targetLock);
        int count = 0;
        for (int r = 0; r < c1NumRoles; ++r)
            if (targets[(size_t) r] != nullptr)
                ++count;
        return count;
    }

    // The parameter index this role resolved to on the active console,
    // or -1 when the console does not expose the role.
    int getTargetIndexForRole (int role) const
    {
        if (role < 0 || role >= c1NumRoles)
            return -1;
        const juce::SpinLock::ScopedLockType lock (targetLock);
        return targetIndices[(size_t) role];
    }

private:
    // ---- layer 3: value ---------------------------------------------------

    void applyEvent (int cc, int value)
    {
        const int role = c1RoleForCc (cc);
        if (role < 0)
            return; // not one of the 26 captured knobs

        const juce::SpinLock::ScopedLockType lock (targetLock);
        auto* param = targets[(size_t) role];
        if (param == nullptr)
            return; // the active console does not expose this role

        float v = (float) value / 127.0f;

        // Discrete parameters (2..128 steps) snap to the nearest step, so
        // a 5-rung attack ladder lands on rungs, not between them.
        const int steps = param->getNumSteps();
        if (param->isDiscrete() && steps >= 2 && steps <= 128)
            v = (float) juce::roundToInt (v * (float) (steps - 1)) / (float) (steps - 1);

        param->setValueNotifyingHost (v);
    }

    // ---- the hardware ------------------------------------------------------

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
    {
        if (m.isController())
            postController (m.getControllerNumber(), m.getControllerValue());
    }

    bool autoDetectAndOpen()
    {
        if (! hardwareEnabled.load (std::memory_order_relaxed))
            return false;

        for (const auto& d : juce::MidiInput::getAvailableDevices())
        {
            const juce::String low = d.name.toLowerCase();
            if (low.contains ("console 1") || low.contains ("console1"))
            {
                auto input = std::unique_ptr<juce::MidiInput> (
                    juce::MidiInput::openDevice (d.identifier, this));
                if (input != nullptr)
                {
                    midiInput  = std::move (input);
                    deviceName = d.name;
                    midiInput->start();
                    return true;
                }
            }
        }
        return false;
    }

    void detectionLoop()
    {
        while (! detectorStop.load (std::memory_order_relaxed))
        {
            // The unit may be plugged in (or the OSD started) after
            // launch: retry the detection every 2 s while nothing is
            // open. The stop flag is polled twenty times per cycle so
            // the join stays responsive at teardown.
            for (int i = 0; i < 20 && ! detectorStop.load (std::memory_order_relaxed); ++i)
                std::this_thread::sleep_for (std::chrono::milliseconds (100));

            if (detectorStop.load (std::memory_order_relaxed))
                return;

            if (midiInput == nullptr)
                autoDetectAndOpen();
        }
    }

    // ---- state -------------------------------------------------------------

    static constexpr int fifoCapacity = 512; // power of two

    struct Event { juce::uint8 cc; juce::uint8 value; };
    std::array<Event, fifoCapacity> fifo {};
    std::atomic<int> head { 0 };
    std::atomic<int> tail { 0 };

    mutable juce::SpinLock targetLock;
    std::array<juce::AudioProcessorParameter*, c1NumRoles> targets {};
    std::array<int, c1NumRoles> targetIndices {};

    std::unique_ptr<juce::MidiInput> midiInput;
    juce::String deviceName;

    std::thread detector;
    std::atomic<bool> detectorStop { false };

    static inline std::atomic<bool> hardwareEnabled { true };
};
