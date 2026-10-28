#include "TransferAnalyzer.h"
#include <cmath>

// v2.45: the EMA factor per completed frame. One frame every 8192
// samples (~170 ms at 48 kHz); 1/32 gives an effective average over
// the last ~32 frames (~5.4 s).
//
// Why 1/32 (the full story, v2.44 -> v2.45): after v2.44's
// Savitzky-Golay pass Clement still saw slow 1-2-octave waves ride
// the EQ curve - but ONLY on real dynamic music, never on stationary
// pink noise. That is the signature of ESTIMATOR VARIANCE on
// intermittent energy, not of a smoothing problem: a band only
// measures while its instruments play, and on bursty material each
// burst leaves a slightly different energy-weighted H in the EMA, so
// the estimate wanders with the programme. A 1-2-octave wave is
// locally cubic, so NO frequency-domain kernel can remove it without
// also eating a genuine Q1 EQ peak - the kernel family is exhausted;
// the only remaining discriminator is TIME (a console response is
// fixed, the variance waves move with the programme).
//
// A deviation detector (fast EMA vs slow EMA per band, tracking on
// |dev| > threshold) was simulated and REJECTED: on a 12-instrument
// bursty mock (duty ~15%) a real +6 dB Q1 knob step measures only
// ~+2.9 dB at band level (energy-weighted aggregation dilutes the
// peak), while the steady-state |dev| noise is ~1.2 dB rms -
// indistinguishable. So v2.45 simply lengthens the ONE average every
// band estimate needs. Reference simulation (that bursty mock,
// console step +12 -> +18 dB Q1 @ 1.6 kHz): the typical band's wave
// drops from 2.67 dB pp (1/8) to 0.19 dB pp (1/32, x14 calmer) and
// 0.10 dB pp (1/64); the knob settles to +/-0.5 dB at the peak band
// in 1.9 s (1/8), 9.4 s (1/32), 19.5 s (1/64) - on that mock, which
// is harsher than real music (a real band gets energy far more
// often). 1/32 is the calm/fast corner: the waves become invisible
// while a knob move still settles within Clement's accepted 5-10 s
// budget.
//
// Documented costs: a knob turn now needs ~5-10 s to fully settle
// (was ~2 s) - the price of a calm curve on dynamic material. The
// longer window only IMPROVES the random-phase junk cancellation in
// Sxy (it cancels across frames, so more frames = less junk), and
// the coherence gate gets a steadier g2. Warmup is unchanged: the
// EMAs start empty and the accumulated complex estimate is valid as
// soon as a band carries energy (the ratios are self-normalising).
static constexpr double crossEmaAlpha = 1.0 / 32.0;

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

    fifoPos.store (0, std::memory_order_relaxed);
    fifoIn.fill (0.0f);
    fifoOut.fill (0.0f);
    pendingIn.clear();
    pendingOut.clear();
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

        emaSxx  [bi] += crossEmaAlpha * (sxx - emaSxx  [bi]);
        emaSyy  [bi] += crossEmaAlpha * (syy - emaSyy  [bi]);
        emaSxyRe[bi] += crossEmaAlpha * (sre - emaSxyRe[bi]);
        emaSxyIm[bi] += crossEmaAlpha * (sim - emaSxyIm[bi]);
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
