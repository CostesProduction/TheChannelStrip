#include "SpectrumAnalyzer.h"
#include <cmath>

void SpectrumAnalyzer::prepare (double newSampleRate)
{
    sampleRate = juce::jmax (8000.0, newSampleRate);

    // manual Hann window (no allocation at run time)
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                     * (float) i / (float) fftSize));

    // log-spaced display bins from 20 Hz to 20 kHz
    for (int b = 0; b < numScopeBins; ++b)
        binFrequencies[(size_t) b] =
            20.0 * std::pow (1000.0, (double) b / (double) (numScopeBins - 1));

    for (auto& s : scopeData)
        s.store (-100.0f, std::memory_order_relaxed);

    fifoPos.store (0, std::memory_order_relaxed);
    for (auto& f : fifo) f = 0.0f;
}

void SpectrumAnalyzer::processAudio (const juce::AudioBuffer<float>& buffer)
{
    const int numChans = buffer.getNumChannels();
    if (numChans <= 0)
        return;

    const int numSamples = buffer.getNumSamples();
    const float norm = 1.0f / (float) numChans;

    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int c = 0; c < numChans; ++c)
            mono += buffer.getSample (c, i);
        mono *= norm;

        const int pos = fifoPos.load (std::memory_order_relaxed);
        fifo[(size_t) pos] = mono;

        if (pos + 1 >= fftSize)
        {
            fifoPos.store (0, std::memory_order_relaxed);
            compute();
        }
        else
        {
            fifoPos.store (pos + 1, std::memory_order_relaxed);
        }
    }
}

void SpectrumAnalyzer::compute()
{
    // The FFT object requires the input array to be twice the FFT size.
    for (int i = 0; i < fftSize; ++i)
    {
        fftData[(size_t) i] = fifo[(size_t) i] * window[(size_t) i];
        fftData[(size_t) (fftSize + i)] = 0.0f;
    }

    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const int lastBin = fftSize / 2 - 1;

    for (int b = 0; b < numScopeBins; ++b)
    {
        const double fLow  = binFrequencies[(size_t) b];
        const double fHigh = (b == numScopeBins - 1) ? 0.5 * sampleRate
                                                      : binFrequencies[(size_t) (b + 1)];

        const int kLow = juce::jlimit (1, lastBin,
            (int) std::round (fLow * fftSize / sampleRate));
        const int kHigh = juce::jlimit (kLow, lastBin,
            (int) std::round (fHigh * fftSize / sampleRate));

        float sum = 0.0f;
        int count = 0;
        for (int k = kLow; k <= kHigh; ++k)
        {
            sum += fftData[(size_t) k];
            ++count;
        }

        const float avg = count > 0 ? sum / (float) count : 0.0f;
        const float db = 20.0f * std::log10 (avg / (0.25f * (float) fftSize) + 1e-10f);

        scopeData[(size_t) b].store (juce::jlimit (-100.0f, 6.0f, db),
                                     std::memory_order_relaxed);
    }
}

void SpectrumAnalyzer::copyScopeData (float* destination, int numBins) const
{
    for (int b = 0; b < numBins && b < numScopeBins; ++b)
        destination[b] = scopeData[(size_t) b].load (std::memory_order_relaxed);
}
