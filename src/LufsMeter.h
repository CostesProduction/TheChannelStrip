#pragma once

#include <vector>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

// BS.1770-style loudness meter: K-weighting (high-shelf + RLB high-pass),
// 100 ms hops, 3 s window for the short-term value (the only readout the
// shell uses - the gain-staging delta and the silent-console watch).
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
    // The short-term window below is the only value the shell reads
    // (the gain-staging delta and the v2.24 silent-console watch).

    std::atomic<float> shortTermLufs { -120.0f };
};
