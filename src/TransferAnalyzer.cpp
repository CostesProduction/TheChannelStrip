#include "TransferAnalyzer.h"
#include <cmath>

// v2.47.0 (the EQ-Verite curve): TWO alphas now. The LONG one (1/30 per
// ~170 ms frame, tau ~5 s) makes the H1 estimate the energy-weighted
// truth of the whole programme - the reference simulation measured a
// median band wander of 0.9 dB (vs 4.3 dB at the v2.46 1/8) and the
// best fidelity to the exact energy-weighted response of every
// estimator tested. The FAST one (1/2, for the 8 frames (~1.4 s)
// following a flush) restores the knob reactivity a 5 s window alone
// cannot have: the shell calls requestFlush() when it KNOWS the
// console's response changed (hosted parameter moved, console
// swapped), and the estimators restart on fast averaging before the
// long window takes over again. No detection, no thresholds - the
// v2.45 double-EMA dead end below is exactly what this avoids.
//
// v2.46 kept 1/8 (~1.4 s). Clement's bypass test had settled WHY the
// waves remain at any window: in BYPASS the EQ curve is flat and
// immobile on the same dynamic music, so the waves are NOT estimator
// variance - they are the console's own programme-dependent response
// (asymmetric L/R channels summed to mono, level dependent
// saturation), an HONEST measurement. A short window chases whichever
// operating point the last second sat in; the long window averages
// over the bursts AND the quiet bed and settles on the mixture the ear
// hears. What the short window bought - knob reactivity - is now paid
// by the deterministic flush instead, and the v2.46 "EQ @ max" trace
// survives as the UI-side ANCHOR of the single displayed curve (one
// third of it, see SpectrumPanel).
//
// v2.45's documented dead end, kept for the record: a per-band
// deviation detector (fast EMA vs slow EMA) cannot work - on a
// bursty mock a real knob step measures only ~+2.9 dB at band level
// while the steady-state deviation noise is ~1.2 dB rms. And the
// frequency-domain kernel family was already exhausted: a 1-2 octave
// wave is locally cubic, so any kernel that removes it eats a genuine
// Q1 EQ peak.
static constexpr double longCrossEmaAlpha  = 1.0 / 30.0;
static constexpr double fastCrossEmaAlpha = 1.0 / 2.0;
static constexpr int    fastFramesAfterFlush = 8;

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
    for (auto& c : rawCoherence) c.store (0.0f, std::memory_order_relaxed);

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

// v2.41: the reset half of prepare(), shared with the public reset() -
// the cross-spectrum estimators restart from silence, so a re-shown
// analyser never displays a transfer measured across the hidden gap.
void TransferAnalyzer::reset()
{
    emaSxx.fill (0.0);
    emaSyy.fill (0.0);
    emaSxyRe.fill (0.0);
    emaSxyIm.fill (0.0);

    for (auto& v : transferDb) v.store (0.0f, std::memory_order_relaxed);
    for (auto& c : coherence)  c.store (0.0f, std::memory_order_relaxed);
    for (auto& c : rawCoherence) c.store (0.0f, std::memory_order_relaxed);

    fifoPos.store (0, std::memory_order_relaxed);
    fifoIn.fill (0.0f);
    fifoOut.fill (0.0f);
    pendingIn.clear();
    pendingOut.clear();

    // v2.47.0: a reset IS a restart - no stale flush may fire against
    // the freshly zeroed estimators.
    flushRequested.store (false, std::memory_order_relaxed);
    fastFramesRemaining = 0;
}

// v2.47.0: the flush handshake (see the header). One atomic store;
// consumed by the next completed frame on the audio thread.
void TransferAnalyzer::requestFlush()
{
    flushRequested.store (true, std::memory_order_release);
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
    // v2.47.0: consume a pending flush BEFORE this frame votes. The
    // estimators restart from zero, and the next fastFramesAfterFlush
    // frames average fast (1/2) so a knob turn reads within ~1.4 s;
    // afterwards the long window (1/30) takes over again and
    // re-converges undisturbed. Zero allocations, four fills.
    if (flushRequested.exchange (false, std::memory_order_acq_rel))
    {
        emaSxx.fill (0.0);
        emaSyy.fill (0.0);
        emaSxyRe.fill (0.0);
        emaSxyIm.fill (0.0);
        fastFramesRemaining = fastFramesAfterFlush;
    }

    const double alpha = (fastFramesRemaining > 0)
        ? fastCrossEmaAlpha : longCrossEmaAlpha;

    if (fastFramesRemaining > 0)
        --fastFramesRemaining;

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

        // per-frame band aggregates (power sums stay real; the cross
        // term X*conj(Y) is complex - that phase is the information)
        double sxx = 0.0, syy = 0.0, sre = 0.0, sim = 0.0;

        // v2.41 (Clement's staircase remark): below ~213 Hz at 48 kHz a
        // display band is NARROWER than one FFT bin, so the integer
        // rounding fed several consecutive bands from the SAME bin and
        // the EQ curve descended in visible stairs. Sub-bin bands now
        // sample both spectra at the band's exact low-edge frequency
        // (where the panel draws the band): the complex bin values are
        // linearly interpolated there, and H = Sxy/Sxx keeps its exact
        // meaning because X and Y are sampled at the SAME fractional
        // position with the SAME weights. Bands a bin or wider keep the
        // v2.22 band aggregation untouched.
        static constexpr double narrowBinWidth = 1.0; // in FFT bins

        if ((fHigh - fLow) * (double) fftSize / sampleRate < narrowBinWidth)
        {
            const double p  = juce::jlimit (1.0, (double) lastBin,
                                            fLow * (double) fftSize / sampleRate);
            const int    k0 = (int) std::floor (p);
            const double t  = juce::jlimit (0.0, 1.0, p - (double) k0);
            const int    k1 = juce::jmin (lastBin, k0 + 1);

            double xr0 = (double) xin [(size_t) (2 * k0)];
            double xi0 = (double) xin [(size_t) (2 * k0 + 1)];
            double xr1 = (double) xin [(size_t) (2 * k1)];
            double xi1 = (double) xin [(size_t) (2 * k1 + 1)];
            double yr0 = (double) yout[(size_t) (2 * k0)];
            double yi0 = (double) yout[(size_t) (2 * k0 + 1)];
            double yr1 = (double) yout[(size_t) (2 * k1)];
            double yi1 = (double) yout[(size_t) (2 * k1 + 1)];

            // linear interpolation of the complex spectra at p
            const double xr = (1.0 - t) * xr0 + t * xr1;
            const double xi = (1.0 - t) * xi0 + t * xi1;
            const double yr = (1.0 - t) * yr0 + t * yr1;
            const double yi = (1.0 - t) * yi0 + t * yi1;

            sxx = xr * xr + xi * xi;
            syy = yr * yr + yi * yi;
            // X * conj(Y): (xr + i.xi)(yr - i.yi)
            sre = xr * yr + xi * yi;
            sim = xi * yr - xr * yi;
        }
        else
        {
            const int kLow = juce::jlimit (1, lastBin,
                (int) std::round (fLow * fftSize / sampleRate));
            const int kHigh = juce::jlimit (kLow, lastBin,
                (int) std::round (fHigh * fftSize / sampleRate));

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
        }

        const size_t bi = (size_t) b;

        emaSxx  [bi] += alpha * (sxx - emaSxx  [bi]);
        emaSyy  [bi] += alpha * (syy - emaSyy  [bi]);
        emaSxyRe[bi] += alpha * (sre - emaSxyRe[bi]);
        emaSxyIm[bi] += alpha * (sim - emaSxyIm[bi]);
    }

    // v2.44.0 pass 2: the outputs are computed from the FREQUENCY-
    // SMOOTHED accumulators (see smoothCoeffs in the header). Because
    // ALL the EMAs were just updated by the same frame, every
    // neighbour is at the same temporal state: the kernel averages
    // estimates of the SAME instant, so no temporal lag is added. The
    // clamped index replicates the edge bands, and H = Sxy/Sxx + g2
    // are ratios of quantities filtered by the SAME kernel, so any
    // kernel scale cancels and the identity stays exactly 0 dB.
    //
    // The single Savitzky-Golay pass: every band takes the fixed
    // polynomial-fit FIR over its +/- smoothHalfWidth neighbourhood of
    // each accumulated spectrum. ~32k flops per frame, zero
    // allocations.
    const double* const emaPtrs[4] = { emaSxx.data(), emaSyy.data(),
                                       emaSxyRe.data(), emaSxyIm.data() };
    double* const sm[4] = { smoothScratch.data(),
                            smoothScratch.data() + numScopeBins,
                            smoothScratch.data() + 2 * numScopeBins,
                            smoothScratch.data() + 3 * numScopeBins };

    for (int q = 0; q < 4; ++q)
    {
        const double* const src = emaPtrs[q];
        double* const dst = sm[q];

        for (int b = 0; b < numScopeBins; ++b)
        {
            double acc = 0.0;

            for (int k = -smoothHalfWidth; k <= smoothHalfWidth; ++k)
            {
                const int nb = juce::jlimit (0, numScopeBins - 1, b + k);
                acc += smoothCoeffs[k + smoothHalfWidth] * src[nb];
            }

            dst[b] = acc;
        }
    }

    for (int b = 0; b < numScopeBins; ++b)
    {
        const size_t bi = (size_t) b;

        const double smSxx = sm[0][bi];
        const double smSyy = sm[1][bi];

        // H1 transfer estimate: H = Sxy / Sxx. The dB form uses
        // 20*log10|H| (== 10*log10|H|^2), the same scale the old power
        // ratio produced for a pure gain, so the display keeps its
        // meaning. Both are computed from the smoothed quantities, so
        // the curve is the honest neighbourhood average transfer.
        const double magH = std::sqrt (sm[2][bi] * sm[2][bi]
                                    + sm[3][bi] * sm[3][bi]);
        const double db = smSxx > 1.0e-30
            ? 20.0 * std::log10 (juce::jmax (magH / smSxx, 1.0e-30))
            : 0.0;

        transferDb[bi].store ((float) juce::jlimit (-60.0, 60.0, db),
                              std::memory_order_relaxed);

        // coherence g2 = |Sxy|^2 / (Sxx * Syy): 1 when the output band
        // is entirely explained by the input, towards 0 when it is
        // dominated by uncorrelated junk (the console's floor above
        // its lowpass). This is Smaart's coherence-blanking quantity.
        // Recomputed from the SAME smoothed spectra as H, so curve and
        // gate stay consistent (the smoothing even raises it a little:
        // the neighbourhood agrees more than the single noisy band).
        const double den = smSxx * smSyy;
        const double g2 = den > 1.0e-60
            ? juce::jlimit (0.0, 1.0, (magH * magH) / den)
            : 0.0;

        coherence[bi].store ((float) g2, std::memory_order_relaxed);

        // v2.48.0: the RAW coherence - the same quantity from the
        // UNsmoothed accumulators. g2 is a self-normalising ratio, so
        // on an honest (fully correlated) band it reads ~1 from the
        // very first frame after a flush; it only drops where the
        // band's output is genuinely explained by something other
        // than its input (console noise, dither, an uncorrelated
        // tone). The max-hold members of the EQ curve use it as their
        // junk detector (see SpectrumPanel): below 0.75 a dB-difference
        // would measure the junk, so the bin reads the cross-spectrum
        // member instead.
        const double rawDen = emaSxx[bi] * emaSyy[bi];
        const double rawMagSq = emaSxyRe[bi] * emaSxyRe[bi]
                              + emaSxyIm[bi] * emaSxyIm[bi];
        const double rawG2 = rawDen > 1.0e-60
            ? juce::jlimit (0.0, 1.0, rawMagSq / rawDen)
            : 0.0;

        rawCoherence[bi].store ((float) rawG2, std::memory_order_relaxed);
    }
}

void TransferAnalyzer::copyRawCoherenceData (float* dest, int numBins) const
{
    for (int b = 0; b < numBins && b < numScopeBins; ++b)
    {
        if (dest != nullptr)
            dest[b] = rawCoherence[(size_t) b].load (std::memory_order_relaxed);
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
