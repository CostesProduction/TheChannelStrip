#pragma once

#include <vector>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

// BS.1770-style loudness meter: K-weighting (high-shelf + RLB high-pass),
// 100 ms hops, 3 s window for the short-term value (feeds the IN/OUT
// displays and the silent-console watch) plus a 5 s power-domain
// exponential average (v2.42: feeds the gain-staging readout).
// v2.32: the never-displayed integrated value was removed (see below).
class LufsMeter
{
public:
    LufsMeter() = default;

    void prepare (double newSampleRate, int maxNumChannels);
    void reset();
    void resetMeasurement();
    void process (const juce::AudioBuffer<float>& buffer);

    float getShortTermLufs() const  { return shortTermLufs.load (std::memory_order_relaxed); }

    // v2.42 (point 4): the gain-staging readout source - a 5 s exponential
    // average of the hop POWER (not dB), gated per hop below -70 LUFS. It
    // reads the floor until 1 s of gated signal has been seen, holds
    // through short silence gaps, and invalidates after 3 s of silence
    // (the schedule the draining short-term window always had).
    float getSmoothedLufs() const    { return smoothedLufs.load (std::memory_order_relaxed); }

private:
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        void setHighShelf (double fs, double centreFreq, double q, double gainDb);
        void setHighPass (double fs, double centreFreq, double q);
        inline float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void resetState() { z1 = z2 = 0.0f; }
    };

    struct ChannelState
    {
        Biquad shelf, highPass;
        double hopSum = 0.0;
    };

    static float powerToLufs (double power)
    {
        return power > 1e-12 ? (float) (-0.691 + 10.0 * std::log10 (power)) : -120.0f;
    }

    void storeHop();

    double sampleRate = 48000.0;
    int hopSamples = 480;
    int hopCounter = 0;
    int historyLength = 64;
    int historyPos = 0;
    int hopsStored = 0;

    std::vector<ChannelState> channels;
    std::vector<std::vector<float>> hopEnergy; // [channel][historyIndex]

    // v2.32: the BS.1770 integrated-loudness bookkeeping (blockPowers +
    // its gated average) was REMOVED from the audio thread. Nothing in
    // the shell ever displayed it, and it was the only remaining path
    // that could allocate on the audio thread (a std::vector growth
    // every 400 ms block) plus two log10 loops over up to 20k entries.
    // The shell reads the short-term window below (the IN/OUT displays
    // and the v2.24 silent-console watch) and, since v2.42, the 5 s
    // power average (the gain-staging readout) - both allocation-free.

    std::atomic<float> shortTermLufs { -120.0f };

    // v2.42: state for the smoothed (5 s power average) output. smoothAlpha
    // is the per-hop EMA coefficient for tau = 5 s at the 100 ms hop rate.
    static constexpr double smoothAlpha = 0.1 / (5.0 + 0.1);        // ~0.0196
    static constexpr int smoothWarmupHops = 10;                     // 1 s of signal
    static constexpr int smoothSilentHopsToInvalid = 30;           // 3 s, like the ST window
    double smoothPower = 0.0;
    int smoothValidHops = 0;
    int smoothSilentHops = 0;
    std::atomic<float> smoothedLufs { -120.0f };
};
