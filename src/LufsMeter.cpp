#include "LufsMeter.h"
#include <cmath>

void LufsMeter::Biquad::setHighShelf (double fs, double centreFreq, double q, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double omega = juce::MathConstants<double>::twoPi * centreFreq / fs;
    const double cs = std::cos (omega);
    const double sn = std::sin (omega);
    const double alpha = sn / (2.0 * q);
    const double sqA2a = 2.0 * std::sqrt (A) * alpha;

    const double b0r = A * ((A + 1.0) + (A - 1.0) * cs + sqA2a);
    const double b1r = -2.0 * A * ((A - 1.0) + (A + 1.0) * cs);
    const double b2r = A * ((A + 1.0) + (A - 1.0) * cs - sqA2a);
    const double a0r = (A + 1.0) - (A - 1.0) * cs + sqA2a;
    const double a1r = 2.0 * ((A - 1.0) - (A + 1.0) * cs);
    const double a2r = (A + 1.0) - (A - 1.0) * cs - sqA2a;

    b0 = (float) (b0r / a0r);
    b1 = (float) (b1r / a0r);
    b2 = (float) (b2r / a0r);
    a1 = (float) (a1r / a0r);
    a2 = (float) (a2r / a0r);
}

void LufsMeter::Biquad::setHighPass (double fs, double centreFreq, double q)
{
    const double omega = juce::MathConstants<double>::twoPi * centreFreq / fs;
    const double cs = std::cos (omega);
    const double sn = std::sin (omega);
    const double alpha = sn / (2.0 * q);
    const double a0r = 1.0 + alpha;

    b0 = (float) ((1.0 + cs) / 2.0 / a0r);
    b1 = (float) (-(1.0 + cs) / a0r);
    b2 = b0;
    a1 = (float) (-2.0 * cs / a0r);
    a2 = (float) ((1.0 - alpha) / a0r);
}

void LufsMeter::prepare (double newSampleRate, int maxNumChannels)
{
    sampleRate = juce::jmax (8000.0, newSampleRate);
    hopSamples = juce::jmax (1, (int) juce::roundToInt (0.1 * sampleRate));

    channels.resize ((size_t) juce::jmax (1, maxNumChannels));
    hopEnergy.assign (channels.size(), std::vector<float> ((size_t) historyLength, 0.0f));

    for (auto& ch : channels)
    {
        // BS.1770 K-weighting: +4 dB high shelf then RLB high-pass
        ch.shelf.setHighShelf (sampleRate, 1681.974450765574, 0.7071752369554196, 3.99984385397);
        ch.highPass.setHighPass (sampleRate, 38.13547087602444, 0.5003270373238773);
        ch.hopSum = 0.0;
    }

    resetMeasurement();
    reset();
}

void LufsMeter::reset()
{
    hopCounter = 0;
    for (auto& ch : channels)
    {
        ch.shelf.resetState();
        ch.highPass.resetState();
        ch.hopSum = 0.0;
    }
}

void LufsMeter::resetMeasurement()
{
    hopEnergy.assign (channels.size(), std::vector<float> ((size_t) historyLength, 0.0f));
    historyPos = 0;
    hopsStored = 0;
    shortTermLufs.store (-120.0f, std::memory_order_relaxed);
    smoothPower = 0.0;
    smoothValidHops = 0;
    smoothSilentHops = 0;
    smoothedLufs.store (-120.0f, std::memory_order_relaxed);
}

void LufsMeter::process (const juce::AudioBuffer<float>& buffer)
{
    const int numChans = juce::jmin ((int) channels.size(), buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    if (numChans <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        for (int c = 0; c < numChans; ++c)
        {
            auto& ch = channels[(size_t) c];
            const float x = buffer.getSample (c, i);
            const float weighted = ch.highPass.process (ch.shelf.process (x));
            ch.hopSum += (double) weighted * (double) weighted;
        }

        if (++hopCounter >= hopSamples)
        {
            storeHop();
            hopCounter = 0;
            for (int c = 0; c < numChans; ++c)
                channels[(size_t) c].hopSum = 0.0;
        }
    }
}

void LufsMeter::storeHop()
{
    const int numChans = (int) channels.size();

    for (int c = 0; c < numChans; ++c)
        hopEnergy[(size_t) c][(size_t) historyPos] =
            (float) (channels[(size_t) c].hopSum / (double) hopSamples);

    // v2.42 (point 4): the gain-staging readout source - a 5 s exponential
    // average of the hop POWER (not dB: a dB-domain average weights a
    // quiet passage as much as a loud one, which biased the old readout
    // ~0.3 dB low on dynamic material; the Python simulation that settled
    // this is preserved with the project work files). Hops under the
    // -70 LUFS floor never enter the average: a silence gap holds the
    // last value, and a full 3 s of silence invalidates the output (the
    // same schedule the draining short-term window always had), so a
    // muted console cannot keep flashing a stale correction. A handful
    // of flops and one log10 per hop - no allocation, no extra memory.
    {
        double hopPower = 0.0;
        for (int c = 0; c < numChans; ++c)
            hopPower += hopEnergy[(size_t) c][(size_t) historyPos];

        if (powerToLufs (hopPower) > -70.0)
        {
            smoothSilentHops = 0;
            smoothPower += smoothAlpha * (hopPower - smoothPower);
            if (++smoothValidHops >= smoothWarmupHops)
                smoothedLufs.store (powerToLufs (smoothPower),
                                    std::memory_order_relaxed);
        }
        else if (++smoothSilentHops >= smoothSilentHopsToInvalid)
        {
            smoothValidHops = 0;   // a real silence must re-warm for 1 s
            smoothedLufs.store (-120.0f, std::memory_order_relaxed);
        }
    }

    historyPos = (historyPos + 1) % historyLength;
    hopsStored = juce::jmin (hopsStored + 1, historyLength);

    if (hopsStored < 4)
        return;

    // short-term loudness over 3 s (30 hops of 100 ms). v2.32: the
    // BS.1770 integrated-loudness gating that used to run here (a
    // std::vector push_back per 400 ms block, then two log10 loops over
    // up to 20k entries to average them) was removed: nothing in the
    // shell ever displayed it, it was
    // the one remaining path that could ALLOCATE on the audio thread,
    // and the erase-from-front once the vector saturated copied 160 KB
    // every 400 ms. The realtime-safety audit (v2.32 test harness) now
    // proves the whole measurement path allocation-free.
    {
        const int stHops = juce::jmin (30, hopsStored);
        double power = 0.0;
        for (int c = 0; c < numChans; ++c)
            for (int k = 0; k < stHops; ++k)
                power += hopEnergy[(size_t) c]
                    [(size_t) ((historyPos - 1 - k + 2 * historyLength) % historyLength)];

        power /= (double) stHops;
        shortTermLufs.store (powerToLufs (power), std::memory_order_relaxed);
    }
}
