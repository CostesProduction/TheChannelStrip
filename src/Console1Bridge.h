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
// v2.104.0 (the buttons): the MK2's momentary buttons ride the same
// three layers with their own frozen CC table. A press (7F, or 3F when
// the button owns a middle state, the CUT BELL SHELF pair) flips the
// switch the button resolved by name - a 2-state parameter toggles, a
// multi-state one cycles a step - and the Track Select range (CC
// 21..30, Track 1..10) parks console selections that the editor
// heartbeat applies on the message thread, because a console swap can
// never run on the audio thread.
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
//
// v2.103.2: five of those numbers were wrong, and the field found
// them. The capture had been decoded from the CoreMIDI display names
// and the "General Purpose #n (fine)" family was read as 33-36, but
// the fine controllers mirror the coarse ones at +32 and GP #1..4
// coarse are 16-19, so GP #1..4 (fine) are 48-51. The character knob
// had been decoded the same broken way (3 instead of GP #2 coarse,
// 18). The expert-mode hex re-capture of 2026-11-09 settled it byte
// for byte: release B0 30 (48), ratio B0 31 (49), parallel B0 32
// (50), attack B0 33 (51), character B0 12 (18). The table below is
// byte-exact with the hardware now, and the offline suite freezes
// the five corrections.
inline int c1CcForRole (int role)
{
    static const int ccs[c1NumRoles] = {
        107, 105, 103,       // inputGain, highCut, lowCut
        54,  56,  55,  57,   // gateThreshold, gateRelease, sustain, punch
        92,  91,             // eqLowFreq, eqLowGain
        90,  89,  88,        // eqLoMidQ, eqLoMidFreq, eqLoMidGain
        87,  86,  85,        // eqHiMidQ, eqHiMidFreq, eqHiMidGain
        83,  82,             // eqHighFreq, eqHighGain
        49,  50,  51,  48, 47, // compRatio, compDryWet, compAttack, compRelease, compThreshold
        15,  18,  10,   7    // drive, character, pan, output
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

// ---- layer 1, part 2: the frozen button CC table (v2.104.0) -----------
//
// The knobs carry absolute values; the buttons are momentary presses -
// the MK2 sends 7F on press (3F when the button owns a middle state,
// the CUT BELL SHELF pair), 00 on release. Any nonzero value is one
// press; the release is ignored. The capture of 2026-11-09, expert
// mode, channel 1:
//   Phase Invert         B0 6C -> 108
//   Filters to comp      B0 3D ->  61 (no Costes target; generic
//                                  patterns for third-party consoles)
//   Hard Gate            B0 3B ->  59
//   SHAPE                B0 35 ->  53
//   EQUALIZER            B0 50 ->  80
//   Cut Bell Shelf low   B0 5D ->  93
//   Cut Bell Shelf high  B0 41 ->  65
//   COMPRESSOR           B0 2E ->  46
//   Order                B0 0E ->  14 (no Costes target either)
// Track Select is a RANGE, not one CC: B0 15..1E -> CC 21..30 for
// Track 1..10. Track 1 selects the internal Costes strip, Tracks
// 2..10 the nine console slots - but a console swap can never run on
// the audio thread, so those presses park in an atomic and the editor
// heartbeat (message thread) applies them.

enum class C1Button
{
    phaseInvert, filtersToComp, hardGate, shapeOn,
    eqOn, lowMode, highMode, compOn, order,
    numButtons
};

constexpr int c1NumButtons = static_cast<int> (C1Button::numButtons);

inline int c1CcForButton (int button)
{
    static const int ccs[c1NumButtons] = {
        108,  61,  59,  53,   // phaseInvert, filtersToComp, hardGate, shapeOn
        80,  93,  65,  46, 14 // eqOn, lowMode, highMode, compOn, order
    };
    return (button >= 0 && button < c1NumButtons) ? ccs[button] : -1;
}

inline int c1ButtonForCc (int cc)
{
    for (int i = 0; i < c1NumButtons; ++i)
        if (c1CcForButton (i) == cc)
            return i;
    return -1;
}

// Track Select: CC 21..30 for Track 1..10, zero-based; -1 outside.
inline int c1TrackIndexForCc (int cc)
{
    return (cc >= 21 && cc <= 30) ? cc - 21 : -1;
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
// win. v2.105.0: the hosted bx consoles expose the character knob as
// "V-Gain" / "Virtual Gain", so those patterns carry the role there
// (the Costes tilt keeps the pattern it always had).

namespace Console1Detail
{
    inline const char* const wInputGain[]     = { "inputgain", "inputtrim", "intrim", "trimin", "ingain",
                                                 "pregain", nullptr };
    inline const char* const wHighCut[]       = { "highcut", "lowpass", "lpffrequency", "lpffreq", nullptr };
    inline const char* const wLowCut[]        = { "lowcut", "inputlowcut", "lowcutfilter", "hpffrequency",
                                                 "hpffreq", "highpassfreq", "highpassfilter", nullptr };
    inline const char* const wGateThreshold[] = { "gatethreshold", "gatethresh", "gatelevel", "gategain", "gate",
                                                 "expanderthreshold", "gethreshold", "expthresh", nullptr };
    inline const char* const wGateRelease[]  = { "gaterelease", "shaperelease", "expanderrelease", "gerelease",
                                                 "exprelease", nullptr };
    inline const char* const wSustain[]      = { "sustain", "gatehold", nullptr };
    inline const char* const wPunch[]         = { "punch", "transientattack", nullptr };
    inline const char* const wEqLowFreq[]    = { "lowfreq", "lowfrequency", "lffreq", "bassfreq", "lowbandfreq",
                                                 "eq1frequency", nullptr };
    inline const char* const wEqLowGain[]    = { "lowgain", "lfgain", "bassgain", "lowbandgain", "eq1gain", nullptr };
    inline const char* const wEqLoMidQ[]     = { "lowmidq", "lmcue", "lowmidcue", "eq2width", "lmfq", nullptr };
    inline const char* const wEqLoMidFreq[]  = { "lowmidfreq", "lowmidfrequency", "lmfreq", "midlowfreq",
                                                 "eq2frequency", "lmffreq", "midfreq", nullptr };
    inline const char* const wEqLoMidGain[]  = { "lowmidgain", "lmgain", "midlowgain", "eq2gain", "lmfgain",
                                                 "midgain", nullptr };
    inline const char* const wEqHiMidQ[]     = { "highmidq", "hmcue", "highmidcue", "eq3width", "hmfq",
                                                 "midhighq", nullptr };
    inline const char* const wEqHiMidFreq[]  = { "highmidfreq", "highmidfrequency", "hmfreq", "midhighfreq",
                                                 "eq3frequency", "hmffreq", nullptr };
    inline const char* const wEqHiMidGain[]  = { "highmidgain", "hmgain", "midhighgain", "eq3gain", "hmfgain",
                                                 nullptr };
    inline const char* const wEqHighFreq[]   = { "highfreq", "highfrequency", "hffreq", "treblefreq", "highbandfreq",
                                                 "eq4frequency", nullptr };
    inline const char* const wEqHighGain[]   = { "highgain", "hfgain", "treblegain", "highbandgain", "eq4gain", nullptr };
    inline const char* const wCompRatio[]    = { "ratio", "compratio", "compressionratio", "compressorratio", nullptr };
    inline const char* const wCompDryWet[]   = { "parallel", "drywet", "mix", "blend", "compressormix",
                                                 "compmix", nullptr };
    inline const char* const wCompAttack[]   = { "attack", "attacktime", "compattack", "compressorfastattack",
                                                 "comptiming", "compfast", nullptr };
    inline const char* const wCompRelease[]  = { "release", "releasetime", "comprelease", "compressorrelease",
                                                 "vcarelease", "comprecovery", nullptr };
    inline const char* const wCompThreshold[]= { "threshold", "thresh", "compthreshold", "compressionthreshold",
                                                 "compressorthreshold", nullptr };
    inline const char* const wDrive[]        = { "drive", "saturation", "saturationdrive", "thd", nullptr };
    // v2.103.1: the tilt, not the generic "character" - that pattern
    // always grabbed "Compressor Character" (9 chars) and the tilt
    // (4 chars) could never win the greedy assignment.
    inline const char* const wCharacter[]    = { "tilt", "vgain", "virtualgain", nullptr };
    inline const char* const wPan[]          = { "pan", "balance", "panorama", "panpot", nullptr };
    inline const char* const wOutput[]       = { "output", "outputlevel", "channelvolume", "volume",
                                                 "outlevel", "outtrim", "trimout", "mastervolume", "outgain",
                                                 nullptr };

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

// The button synonyms (v2.104.0) - resolved in a SECOND greedy pass,
// over the parameters the knobs did not claim (one parameter, one
// driver, so a button can never fight a knob for a target). On the
// Costes console: Phase Invert, Expander Fast Attack (the Hard Gate
// button), Expander On (SHAPE), EQ On, EQ 1/4 Mode (the CUT BELL
// SHELF pair) and Compressor On. "Filters to compressor" and "Order"
// carry generic patterns for third-party consoles and stay idle on
// the Costes console.
namespace Console1ButtonDetail
{
    // v2.105.0: bare "invert" is GONE - it matched "GE Invert" (6 chars)
    // over "Phase" (5) on every SSL-style console and steered the
    // phase button into the gate expander.
    inline const char* const wBPhaseInvert[]   = { "phaseinvert", "polarity", "phase", nullptr };
    inline const char* const wBFiltersToComp[] = { "filtertocompressor", "filtertocomp", "filtertocmp",
                                                  "fltposition", "filterposition", nullptr };
    inline const char* const wBHardGate[]      = { "expanderfastattack", "hardgate", "gatefastattack",
                                                  "gemode", "expandermode", "gateexpander", "gatefast",
                                                  nullptr };
    // v2.105.0: no bare "gatein" - it hides inside "Gate Invert(ed)";
    // the Lindell "Gate In" switches are pinned instead.
    inline const char* const wBShapeOn[]       = { "expanderon", "shapeon", "gateon", "geonoff",
                                                  "gateexponoff", nullptr };
    inline const char* const wBEqOn[]          = { "eqon", "equalizeron", "eqin", nullptr };
    inline const char* const wBLowMode[]      = { "eq1mode", "lowbell", "lowshelf", "lowmode", "lowpeak",
                                                  nullptr };
    inline const char* const wBHighMode[]     = { "eq4mode", "highbell", "highshelf", "highmode", "highpeak",
                                                  nullptr };
    inline const char* const wBCompOn[]       = { "compressoron", "compon", "compin", "dynonoff", "lconoff",
                                                  nullptr };
    inline const char* const wBOrder[]        = { "chainorder", "routingorder", "signalorder", "eqposition",
                                                  "eqpostdyn", nullptr };

    inline const char* const* const words[c1NumButtons] = {
        wBPhaseInvert, wBFiltersToComp, wBHardGate, wBShapeOn,
        wBEqOn, wBLowMode, wBHighMode, wBCompOn, wBOrder
    };
}

// v2.105.0 (the nine consoles): the pinned overrides, applied AFTER
// the two greedy passes. The resolver works on substrings, and some
// facts of the real consoles cannot be expressed that way: the AMEK
// 200 lists "High Pass Filter In" (the switch) one slot before "High
// Pass Filter" (the frequency), and on four consoles the three letters
// "pan" hide inside "exPANder" and would steer the pan knob into the
// gate-expander switch. A pin can also force a driver idle (-1): the
// Lindell 50 hosts TWO compressors, and the "mix" the greedy finds
// belongs to the FET one, not the VCA the MK2 comp section drives. The
// key is the hosted plugin getName() - the exact string the doctor
// report prints - so a pin only ever applies to the console it froze
// and an unknown console stays pure greedy. The frozen per-console
// tests in the offline suite validate every index below.
namespace Console1PinDetail
{
    struct C1Pin { const char* console; int driver; int index; };

    inline const C1Pin pins[] = {
        { "bx_console AMEK 9099",    (int) C1Role::pan,                     -1 },
        { "bx_console AMEK 9099",    c1NumRoles + (int) C1Button::hardGate, 69 },
        { "bx_console SSL 9000 J",   (int) C1Role::pan,                     -1 },
        { "bx_console SSL 9000 J",   c1NumRoles + (int) C1Button::hardGate, 28 },
        { "bx_console Focusrite SC", (int) C1Role::pan,                     -1 },
        { "bx_console Focusrite SC", c1NumRoles + (int) C1Button::hardGate, 46 },
        { "bx_console AMEK 200",     (int) C1Role::lowCut,                  19 },
        { "bx_console AMEK 200",     (int) C1Role::highCut,                 21 },
        { "bx_console AMEK 200",     (int) C1Role::compRelease,             -1 },
        { "Lindell 50 Channel",      (int) C1Role::compDryWet,              -1 },
        { "Lindell 50 Channel",      (int) C1Role::pan,                     -1 },
        { "Lindell 50 Channel",      c1NumRoles + (int) C1Button::hardGate, 60 },
        { "Lindell 50 Channel",      c1NumRoles + (int) C1Button::shapeOn,  59 },
        { "Lindell 80 Channel",      (int) C1Role::inputGain,                1 },
        { "Lindell 80 Channel",      (int) C1Role::lowCut,                  12 },
        { "Lindell 80 Channel",      (int) C1Role::gateRelease,             30 },
        { "Lindell 80 Channel",      (int) C1Role::pan,                     -1 },
        { "Lindell 80 Channel",      c1NumRoles + (int) C1Button::hardGate, 32 },
        { "Lindell 80 Channel",      c1NumRoles + (int) C1Button::shapeOn,  31 },
        { "Lindell 80 Channel",      c1NumRoles + (int) C1Button::eqOn,     14 }
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

// The button twin of c1LongestMatch.
inline int c1LongestButtonMatch (int button, const juce::String& normalisedName)
{
    if (button < 0 || button >= c1NumButtons)
        return 0;
    const char* const* w = Console1ButtonDetail::words[button];
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
        // v2.104.0: the button twin, filled in the same pass.
        std::vector<std::vector<int>> buttonScores ((size_t) c1NumButtons,
                                                    std::vector<int> ((size_t) n, 0));
        if (n > 0)
        {
            std::vector<juce::String> names ((size_t) n);
            for (int i = 0; i < n; ++i)
                names[(size_t) i] = c1Normalise (params.getUnchecked (i)->getName (64));

            for (int r = 0; r < c1NumRoles; ++r)
                for (int i = 0; i < n; ++i)
                    scores[(size_t) r][(size_t) i] = c1LongestMatch (r, names[(size_t) i]);

            for (int b = 0; b < c1NumButtons; ++b)
                for (int i = 0; i < n; ++i)
                    buttonScores[(size_t) b][(size_t) i] =
                        c1LongestButtonMatch (b, names[(size_t) i]);
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

        // v2.104.0 (the buttons): a second greedy pass over what the
        // knobs left behind - one parameter, one driver.
        std::array<int, c1NumButtons> buttonAssigned;
        buttonAssigned.fill (-1);

        for (;;)
        {
            int bestScore = 0, bestButton = -1, bestParam = -1;
            for (int b = 0; b < c1NumButtons; ++b)
            {
                if (buttonAssigned[(size_t) b] >= 0)
                    continue;
                for (int i = 0; i < n; ++i)
                {
                    if (used[(size_t) i] != 0)
                        continue;
                    const int s = buttonScores[(size_t) b][(size_t) i];
                    if (s > bestScore)
                    {
                        bestScore = s;
                        bestButton = b;
                        bestParam  = i;
                    }
                }
            }
            if (bestScore <= 0)
                break;
            buttonAssigned[(size_t) bestButton] = bestParam;
            used[(size_t) bestParam]   = 1;
        }

        // v2.105.0: the pinned overrides, applied before the swap so
        // the greedy result and the pins publish atomically. A pin
        // whose index sits past the parameter list is skipped - a
        // plugin update may have moved it, so the greedy claim then
        // stands and the frozen tests catch the shift. The "used"
        // flags are not rewritten: no pass runs after this point.
        if (active != nullptr)
        {
            const juce::String consoleName = active->getName();
            for (const auto& pin : Console1PinDetail::pins)
            {
                if (! consoleName.equalsIgnoreCase (juce::String (pin.console)))
                    continue;
                if (pin.index >= n)
                    continue;
                if (pin.driver < c1NumRoles)
                    assigned[(size_t) pin.driver] = pin.index;
                else if (pin.driver < c1NumRoles + c1NumButtons)
                    buttonAssigned[(size_t) (pin.driver - c1NumRoles)] = pin.index;
            }
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
        for (int b = 0; b < c1NumButtons; ++b)
        {
            const int idx = buttonAssigned[(size_t) b];
            buttonTargetIndices[(size_t) b] = idx;
            buttonTargets[(size_t) b] = (idx >= 0) ? params.getUnchecked (idx) : nullptr;
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

    // v2.104.0 (the buttons): the twin introspection.
    int getMatchedButtonCount() const
    {
        const juce::SpinLock::ScopedLockType lock (targetLock);
        int count = 0;
        for (int b = 0; b < c1NumButtons; ++b)
            if (buttonTargets[(size_t) b] != nullptr)
                ++count;
        return count;
    }

    // The parameter index this button resolved to on the active
    // console, or -1 when the console does not expose it.
    int getButtonTargetIndexForRole (int button) const
    {
        if (button < 0 || button >= c1NumButtons)
            return -1;
        const juce::SpinLock::ScopedLockType lock (targetLock);
        return buttonTargetIndices[(size_t) button];
    }

    // v2.104.0 (the buttons): the Track Select presses park here (the
    // audio thread can never swap consoles). The editor heartbeat,
    // on the message thread, takes the latest selection: -1 = idle,
    // 0 = Track 1 (the internal Costes strip), 1..9 = the console
    // slots 1..9. Consume-and-forget: the exchange reads it once.
    int consumePendingConsoleSelection()
    {
        return pendingConsoleSelection.exchange (-1, std::memory_order_acq_rel);
    }

private:
    // ---- layer 3: value ---------------------------------------------------

    void applyEvent (int cc, int value)
    {
        // v2.104.0 (the buttons): Track Select first - a CC range, not
        // a role. A press (7F, or 3F on a middle-state button) parks
        // the selection; the release (00) parks nothing.
        const int track = c1TrackIndexForCc (cc);
        if (track >= 0)
        {
            if (value > 0)
                pendingConsoleSelection.store (track, std::memory_order_release);
            return;
        }

        const int role = c1RoleForCc (cc);
        if (role >= 0)
        {
            const juce::SpinLock::ScopedLockType lock (targetLock);
            auto* param = targets[(size_t) role];
            if (param == nullptr)
                return; // the active console does not expose this role

            float v = (float) value / 127.0f;

            // Discrete parameters (2..128 steps) snap to the nearest step,
            // so a 5-rung attack ladder lands on rungs, not between them.
            const int steps = param->getNumSteps();
            if (param->isDiscrete() && steps >= 2 && steps <= 128)
                v = (float) juce::roundToInt (v * (float) (steps - 1)) / (float) (steps - 1);

            param->setValueNotifyingHost (v);
            return;
        }

        // v2.104.0 (the buttons): a momentary press flips the target -
        // a 2-state switch toggles, a multi-state discrete parameter
        // cycles one step at a time.
        const int button = c1ButtonForCc (cc);
        if (button < 0 || value <= 0)
            return; // not one of the captured buttons, or the release

        const juce::SpinLock::ScopedLockType lock (targetLock);
        auto* param = buttonTargets[(size_t) button];
        if (param == nullptr)
            return; // the active console does not expose this button

        const int steps = param->getNumSteps();
        if (param->isDiscrete() && steps > 2 && steps <= 128)
        {
            const int current = juce::roundToInt (param->getValue() * (float) (steps - 1));
            const int next = (current + 1) % steps;
            param->setValueNotifyingHost ((float) next / (float) (steps - 1));
        }
        else
        {
            param->setValueNotifyingHost (param->getValue() > 0.5f ? 0.0f : 1.0f);
        }
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

    std::array<juce::AudioProcessorParameter*, c1NumButtons> buttonTargets {};
    std::array<int, c1NumButtons> buttonTargetIndices {};
    std::atomic<int> pendingConsoleSelection { -1 };

    std::unique_ptr<juce::MidiInput> midiInput;
    juce::String deviceName;

    std::thread detector;
    std::atomic<bool> detectorStop { false };

    static inline std::atomic<bool> hardwareEnabled { true };
};
