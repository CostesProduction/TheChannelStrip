#pragma once

#include <array>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

// FFT-based spectrum analyser fed on the audio thread, read on the UI thread
// through atomic bin values.
class SpectrumAnalyzer
{
public:
    // v2.10: order 13 (8192 points) instead of 11 (2048). The native
    // resolution goes from ~23 Hz to ~6 Hz at 48 kHz, which is what the
    // low end needs: with 2048 points, EVERY display bin between 20 and
    // 47 Hz read the same FFT bin, and a 30 Hz tone spanned barely more
    // than one period per window - hence the wobbly, ill-defined bass
    // on the EQ curve. A frame is now computed every 8192 samples
    // (~170 ms at 48 kHz); the ballistics on the UI thread smooth the
    // lower frame rate, so the display only gets calmer.
    static constexpr int fftOrder = 13;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int numScopeBins = 256;

    void prepare (double newSampleRate);

    // v2.41: clear every trace of past audio (band energies back to the
    // floor, fifo rewound and zeroed). Called when the editor re-opens
    // or the analyser strip is re-shown, so no pre-close / pre-hide
    // frame ever leaks into the fresh display (same contract as
    // LufsMeter::resetMeasurement).
    void reset();

    // Mono mixdown push; computes one frame every fftSize samples.
    void processAudio (const juce::AudioBuffer<float>& buffer);

    void copyScopeData (float* destination, int numBins) const;
    // v2.21.1: RAW band energies, BEFORE the cosmetic spatial pass - the
    // honest measurement data (the EQ curve and its validity gate read
    // this, not the display-smoothed values; see SpectrumPanel).
    void copyRawScopeData (float* destination, int numBins) const;
    double getBinFrequency (int bin) const
    {
        return binFrequencies[(size_t) juce::jlimit (0, numScopeBins - 1, bin)];
    }

private:
    void compute();

    juce::dsp::FFT fft { fftOrder };
    double sampleRate = 48000.0;

    std::array<float, (size_t) fftSize> window {};
    // v2.21: sum of the window taps, computed in prepare(). The dB
    // normalisation divides by windowSum / 2 (for the old Hann window
    // that sum is 0.5 * fftSize, so the scale is unchanged).
    double windowSum = 0.5 * (double) fftSize;
    std::array<float, (size_t) fftSize> fifo {};
    std::array<float, (size_t) (2 * fftSize)> fftData {};
    std::array<std::atomic<float>, (size_t) numScopeBins> scopeData {};
    // v2.21.1: pre-smoothing copy of the band energies (measurement data)
    std::array<std::atomic<float>, (size_t) numScopeBins> rawScopeData {};
    std::array<double, (size_t) numScopeBins> binFrequencies {};

    std::atomic<int> fifoPos { 0 };
};
