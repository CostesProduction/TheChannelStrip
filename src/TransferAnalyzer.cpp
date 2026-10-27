#include "TransferAnalyzer.h"
#include <cmath>

// v2.22: the EMA factor per completed frame. One frame every 8192
// samples (~170 ms at 48 kHz); 1/8 gives an effective average over the
// last ~8 frames (~1.4 s). Shorter averages leave too much of the
// random-phase console junk in Sxy (it only cancels across frames);
// longer ones would make the curve sluggish.
static constexpr double crossEmaAlpha = 1.0 / 8.0;

void TransferAnalyzer::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = juce::jmax (8000.0, newSampleRate);

    // Blackman-Harris 4-term periodic window - the same coefficients
    // as SpectrumAnalyzer, so the transfer bands cover exactly the
    // frequencies the IN / OUT curves display. (The window actually
    // cancels in |H| and g2 - both are ratios of windowed spectra -
    // but using the identical window keeps the band edges consistent.)
    for (int i = 0; i < fftSize; ++i)
    {
        const double x = juce::MathConstants<double>::twoPi
                             * (double) i / (double) fftSize;

        window[(size_t) i] = (float) (0.35875 - 0.48829 * std::cos (x)
                                                 + 0.14128 * std::cos (2.0 * x)
                                                 - 0.01168 * std::cos (3.0 * x));
    }

    // log-spaced display bins from 20 Hz to 20 kHz (as SpectrumAnalyzer)
    for (int b = 0; b < numScopeBins; ++b)
        binFrequencies[(size_t) b] =
            20.0 * std::pow (1000.0, (double) b / (double) (numScopeBins - 1));

    // fresh estimators: 0 input energy -> coherence 0, transfer 0 dB
    emaSxx.fill (0.0);
    emaSyy.fill (0.0);
    emaSxyRe.fill (0.0);
    emaSxyIm.fill (0.0);

    for (auto& v : transferDb) v.store (0.0f, std::memory_order_relaxed);
    for (auto& c : coherence)  c.store (0.0f, std::memory_order_relaxed);

    fifoPos.store (0, std::memory_order_relaxed);
    fifoIn.fill (0.0f);
    fifoOut.fill (0.0f);
    pendingIn.clear();
    pendingOut.clear();

    // The stashes hold one block of mono mixdowns each. Reserved once
    // here (with headroom for the odd large-buffer host) so the audio
    // thread never allocates.
    pendingIn.reserve  ((size_t) juce::jmax (maxBlockSize * 2, 2 * fftSize));
    pendingOut.reserve ((size_t) juce::jmax (maxBlockSize * 2, 2 * fftSize));
}

// The input half: a mono mixdown of the block as it ARRIVES, before
// the console runs. It is stashed rather than written into the frame
// fifo, so that a block larger than the FFT window cannot overwrite
// input samples a pending frame still needs (the two halves of a frame
// must stay sample-aligned whatever the host's buffer size).
void TransferAnalyzer::pushInput (const juce::AudioBuffer<float>& buffer)
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

        pendingIn.push_back (mono * norm);
    }
}

// The output half: the same block after the console. The stashed input
// and this output drain into the two fifos together, one slot at a
// time, so every frame's halves cover the same program samples; when
// the slot wraps, both fifos hold one complete aligned frame and the
// cross-spectrum is computed.
void TransferAnalyzer::pushOutput (const juce::AudioBuffer<float>& buffer)
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

        pendingOut.push_back (mono * norm);
    }

    const size_t count = juce::jmin (pendingIn.size(), pendingOut.size());
    int pos = fifoPos.load (std::memory_order_relaxed);

    for (size_t i = 0; i < count; ++i)
    {
        fifoIn [(size_t) pos] = pendingIn[i];
        fifoOut[(size_t) pos] = pendingOut[i];

        if (pos == fftSize - 1)
        {
            compute();
            pos = 0;
        }
        else
        {
            ++pos;
        }
    }

    fifoPos.store (pos, std::memory_order_relaxed);
    pendingIn.clear();
    pendingOut.clear();
}

void TransferAnalyzer::compute()
{
    // performRealOnlyForwardTransform: real samples in the first half
    // of a 2*fftSize array, interleaved complex bins [re, im] on return
    // (the JUCE FFT contract; see juce_FFT.h). Windowed identically on
    // both sides, so the window cancels in every ratio below.
    for (int i = 0; i < fftSize; ++i)
    {
        fftIn [(size_t) i] = fifoIn [(size_t) i] * window[(size_t) i];
        fftOut[(size_t) i] = fifoOut[(size_t) i] * window[(size_t) i];
        fftIn [(size_t) (fftSize + i)] = 0.0f;
        fftOut[(size_t) (fftSize + i)] = 0.0f;
    }

    fft.performRealOnlyForwardTransform (fftIn.data(),  true);
    fft.performRealOnlyForwardTransform (fftOut.data(), true);

    const int lastBin = fftSize / 2 - 1;
    const float* xin = fftIn.data();
    const float* yout = fftOut.data();

    for (int b = 0; b < numScopeBins; ++b)
    {
        const double fLow  = binFrequencies[(size_t) b];
        const double fHigh = (b == numScopeBins - 1) ? 0.5 * sampleRate
                                                      : binFrequencies[(size_t) (b + 1)];

        const int kLow = juce::jlimit (1, lastBin,
            (int) std::round (fLow * fftSize / sampleRate));
        const int kHigh = juce::jlimit (kLow, lastBin,
            (int) std::round (fHigh * fftSize / sampleRate));

        // per-frame band aggregates (power sums stay real; the cross
        // term X*conj(Y) is complex - that phase is the information)
        double sxx = 0.0, syy = 0.0, sre = 0.0, sim = 0.0;

        for (int k = kLow; k <= kHigh; ++k)
        {
            const double xr = xin [(size_t) (2 * k)];
            const double xi = xin [(size_t) (2 * k + 1)];
            const double yr = yout[(size_t) (2 * k)];
            const double yi = yout[(size_t) (2 * k + 1)];

            sxx += xr * xr + xi * xi;
            syy += yr * yr + yi * yi;
            // X * conj(Y): (xr + i.xi)(yr - i.yi)
            sre += xr * yr + xi * yi;
            sim += xi * yr - xr * yi;
        }

        const size_t bi = (size_t) b;

        emaSxx  [bi] += crossEmaAlpha * (sxx - emaSxx  [bi]);
        emaSyy  [bi] += crossEmaAlpha * (syy - emaSyy  [bi]);
        emaSxyRe[bi] += crossEmaAlpha * (sre - emaSxyRe[bi]);
        emaSxyIm[bi] += crossEmaAlpha * (sim - emaSxyIm[bi]);

        // H1 transfer estimate: H = Sxy / Sxx. The dB form uses
        // 20*log10|H| (== 10*log10|H|^2), the same scale the old power
        // ratio produced for a pure gain, so the display keeps its
        // meaning.
        const double magH = std::sqrt (emaSxyRe[bi] * emaSxyRe[bi]
                                    + emaSxyIm[bi] * emaSxyIm[bi]);
        const double db = emaSxx[bi] > 1.0e-30
            ? 20.0 * std::log10 (juce::jmax (magH / emaSxx[bi], 1.0e-30))
            : 0.0;

        transferDb[bi].store ((float) juce::jlimit (-60.0, 60.0, db),
                              std::memory_order_relaxed);

        // coherence g2 = |Sxy|^2 / (Sxx * Syy): 1 when the output band
        // is entirely explained by the input, towards 0 when it is
        // dominated by uncorrelated junk (the console's floor above
        // its lowpass). This is Smaart's coherence-blanking quantity.
        const double den = emaSxx[bi] * emaSyy[bi];
        const double g2 = den > 1.0e-60
            ? juce::jlimit (0.0, 1.0, (magH * magH) / den)
            : 0.0;

        coherence[bi].store ((float) g2, std::memory_order_relaxed);
    }
}

void TransferAnalyzer::copyTransferData (float* db, float* coherenceOut,
                                         int numBins) const
{
    for (int b = 0; b < numBins && b < numScopeBins; ++b)
    {
        if (db != nullptr)
            db[b] = transferDb[(size_t) b].load (std::memory_order_relaxed);

        if (coherenceOut != nullptr)
            coherenceOut[b] = coherence[(size_t) b].load (std::memory_order_relaxed);
    }
}
