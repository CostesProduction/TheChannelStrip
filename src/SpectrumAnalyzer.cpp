#include "SpectrumAnalyzer.h"
#include <cmath>

void SpectrumAnalyzer::prepare (double newSampleRate)
{
    sampleRate = juce::jmax (8000.0, newSampleRate);

    // v2.21: Blackman-Harris 4-term window (periodic form, Harris 1978
    // coefficients). The Hann window used through v2.20 has its first
    // sidelobe at only -31 dB with a skirt that falls off slowly, so
    // strong program material leaks into the bands just above it and
    // weak treble regions (a kick's HF residue, a mix lowpassed at
    // 9 kHz) read several dB ABOVE their true content - Clement's
    // "20 kHz node too high" and part of the weak-band EQ trouble.
    // Blackman-Harris puts the sidelobes at -92 dB with a much faster
    // skirt: the standard window of professional analysers whenever a
    // weak signal must be measured next to a strong one. The cost is a
    // wider main lobe (~4 FFT bins instead of ~2); a display bin
    // already spans a dozen+ FFT bins above 5 kHz, so this is invisible
    // on screen. The normalisation uses the actual sum of the taps
    // (windowSum, computed below), so tones keep reading at the exact
    // same level as with the old Hann normalisation (sumHann / 2 =
    // 0.25 * fftSize).
    windowSum = 0.0;

    for (int i = 0; i < fftSize; ++i)
    {
        const double x = juce::MathConstants<double>::twoPi
                             * (double) i / (double) fftSize;

        window[(size_t) i] = (float) (0.35875 - 0.48829 * std::cos (x)
                                                 + 0.14128 * std::cos (2.0 * x)
                                                 - 0.01168 * std::cos (3.0 * x));
        windowSum += (double) window[(size_t) i];
    }

    // log-spaced display bins from 20 Hz to 20 kHz
    for (int b = 0; b < numScopeBins; ++b)
        binFrequencies[(size_t) b] =
            20.0 * std::pow (1000.0, (double) b / (double) (numScopeBins - 1));

    for (auto& s : scopeData)
        s.store (-100.0f, std::memory_order_relaxed);

    // v2.21.1: the raw measurement copy starts at the floor too
    for (auto& s : rawScopeData)
        s.store (-100.0f, std::memory_order_relaxed);

    fifoPos.store (0, std::memory_order_relaxed);
    for (auto& f : fifo) f = 0.0f;
}

// v2.41: shared by prepare() and the public reset() - everything that
// makes the analyser forget the audio it has seen.
void SpectrumAnalyzer::reset()
{
    for (auto& s : scopeData)
        s.store (-100.0f, std::memory_order_relaxed);

    for (auto& s : rawScopeData)
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

    // v2.20 (Clement's points 3, 4, 5): the per-display-bin value is
    // now the band's ENERGY (RMS of the covered FFT-bin magnitudes)
    // instead of their arithmetic mean. A display bin at 10 kHz spans
    // ~18 FFT bins; the MEAN of 18 independent, fluctuating magnitudes
    // systematically reads ~3 dB BELOW the band's true energy
    // (mean < RMS always), which is exactly why the treble read lower
    // than in Melda-class analysers - and why OUT-IN was noisy above
    // 5 kHz (two noisy means make a noisier difference). The RMS keeps
    // broadband content at its honest level and stabilises the EQ
    // curve's treble; tones read within ~3 dB of their peak instead
    // of ~6 dB under it.
    float raw[(size_t) numScopeBins];

    // v2.41 (Clement's staircase remark): the display bands are spaced
    // 2.75 % apart in frequency, so below ~213 Hz at 48 kHz a band is
    // NARROWER than one FFT bin (5.86 Hz) - the old integer rounding
    // mapped several consecutive bands onto the SAME FFT bin and the
    // curve descended in visible stairs (up to ~10 identical points in
    // a row near 20 Hz). Sub-bin bands now sample the spectrum at their
    // exact frequency: the bin POWERS are linearly interpolated at the
    // band's low edge (the x position the panel draws this band at -
    // see SpectrumPanel::xForBin), which turns the stairs into an
    // honest smooth slope at the FFT's true resolution. Bands a bin or
    // wider keep the v2.20 band-energy RMS untouched - that aggregation
    // is a measurement feature, not a stair. The price of interpolating
    // across the ~4-bin Blackman-Harris main lobe: a tone sitting
    // exactly between two bins reads up to ~1-2 dB lower than its
    // peak bin alone - the standard trade every interpolating analyser
    // makes for a readable low end.
    const double binHz = sampleRate / (double) fftSize;

    for (int b = 0; b < numScopeBins; ++b)
    {
        const double fLow  = binFrequencies[(size_t) b];
        const double fHigh = (b == numScopeBins - 1) ? 0.5 * sampleRate
                                                      : binFrequencies[(size_t) (b + 1)];

        double rms = 0.0;

        if (fHigh - fLow < binHz)
        {
            // narrow path: fractional sampling between the two
            // neighbouring bin powers at the band's low edge
            const double p  = juce::jlimit (1.0, (double) lastBin, fLow / binHz);
            const int    k0 = (int) std::floor (p);
            const double t  = juce::jlimit (0.0, 1.0, p - (double) k0);
            const int    k1 = juce::jmin (lastBin, k0 + 1);

            const double m0 = (double) fftData[(size_t) k0];
            const double m1 = (double) fftData[(size_t) k1];
            const double power = (1.0 - t) * (m0 * m0) + t * (m1 * m1);

            rms = std::sqrt (power);
        }
        else
        {
            // wide path (v2.20): band ENERGY - RMS of the covered FFT
            // bin magnitudes
            const int kLow = juce::jlimit (1, lastBin,
                (int) std::round (fLow * fftSize / sampleRate));
            const int kHigh = juce::jlimit (kLow, lastBin,
                (int) std::round (fHigh * fftSize / sampleRate));

            double energy = 0.0;
            int count = 0;

            for (int k = kLow; k <= kHigh; ++k)
            {
                const double m = fftData[(size_t) k];
                energy += m * m;
                ++count;
            }

            rms = count > 0 ? std::sqrt (energy / (double) count) : 0.0;
        }

        // v2.21: windowSum-based normalisation (see prepare) - identical
        // readings to the old Hann scale, now valid for the Blackman-
        // Harris window too. For a flat spectrum both paths normalise
        // to the same per-bin magnitude, so the narrow/wide handover
        // at the ~213 Hz threshold is level-continuous.
        const float db = (float) (20.0 * std::log10 (rms / (windowSum * 0.5)
                                                     + 1.0e-10));

        raw[(size_t) b] = juce::jlimit (-100.0f, 6.0f, db);
    }

    // v2.21.1: keep the RAW band energies available separately - the EQ
    // curve's transfer-function estimation and its validity gate need
    // the honest per-band energies. The spatial pass below is a DISPLAY
    // nicety; feeding the measurement from its output dragged any
    // isolated weak band down by up to 24 dB (its neighbours sit at the
    // clamped -100 dB floor, and [0.25, 0.5, 0.25] then injects that
    // fake floor into the band), which pushed genuinely measurable weak
    // content below the gate.
    for (int b = 0; b < numScopeBins; ++b)
        rawScopeData[(size_t) b].store (raw[(size_t) b], std::memory_order_relaxed);

    // v2.20 (point 7): one light spatial pass across neighbouring
    // display bins ([0.25, 0.5, 0.25]) - the same feel as a small
    // "smoothness" setting on classic analysers. The editor's temporal
    // EMA already smooths over time; this calms the last bit of
    // bin-to-bin jitter on the curves (and, since IN and OUT are
    // smoothed identically, it also quiets the EQ difference).
    for (int b = 0; b < numScopeBins; ++b)
    {
        const float lo = raw[(size_t) juce::jmax (0, b - 1)];
        const float hi = raw[(size_t) juce::jmin (numScopeBins - 1, b + 1)];

        scopeData[(size_t) b].store (0.25f * lo + 0.5f * raw[(size_t) b] + 0.25f * hi,
                                     std::memory_order_relaxed);
    }
}

void SpectrumAnalyzer::copyScopeData (float* destination, int numBins) const
{
    for (int b = 0; b < numBins && b < numScopeBins; ++b)
        destination[b] = scopeData[(size_t) b].load (std::memory_order_relaxed);
}

void SpectrumAnalyzer::copyRawScopeData (float* destination, int numBins) const
{
    for (int b = 0; b < numBins && b < numScopeBins; ++b)
        destination[b] = rawScopeData[(size_t) b].load (std::memory_order_relaxed);
}
