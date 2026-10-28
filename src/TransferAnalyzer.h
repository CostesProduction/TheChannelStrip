#pragma once

#include <array>
#include <atomic>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

// v2.22: cross-spectrum TRANSFER-FUNCTION analyser - the dual-FFT
// measurement a Smaart-class system performs, run on the audio thread.
//
// The v2.21.1 EQ curve was the ratio of two power averages
// (10*log10 (Pout/Pin)). That ratio is an HONEST measurement of
// everything that lands in the output band - including the hosted
// console's own noise / dither floor. With a console lowpass engaged
// (SSL J LP at 5 kHz), the treble output is the attenuated signal PLUS
// that floor; once the floor exceeds the attenuated signal, Pout > Pin
// and the curve measured a real power gain that is not a transfer at
// all - the +3 dB rise above 10 kHz on Clement's screenshot.
//
// The professional answer is the cross-spectrum estimate:
//
//   Sxx = E[|X|^2]      (input auto-spectrum, per band)
//   Syy = E[|Y|^2]      (output auto-spectrum)
//   Sxy = E[X . conj(Y)]  (CROSS-spectrum, accumulated COMPLEX)
//   H   = Sxy / Sxx       (H1 estimator: optimal for noise at the output)
//   EQ  = 20*log10|H|     (the true magnitude response)
//   g2  = |Sxy|^2 / (Sxx.Syy)   (coherence: how much of Y is X's fault)
//
// The complex accumulation is the whole trick: an uncorrelated component
// (console noise, dither, digital junk) has a RANDOM phase, so its
// contribution to the AVERAGE of X*conj(Y) cancels over frames, while
// the correlated part (the signal that went through the console, however
// deeply filtered) keeps a stable phase and accumulates. The console
// floor lands in Syy - visible on the OUT curve - but NOT in Sxy, so it
// no longer votes on the EQ. And where the output is noise-dominated
// the coherence g2 collapses towards zero, which gates the display
// exactly there (Smaart's coherence blanking): the curve shows the true
// rolloff down to where the signal sinks under the floor, then stops
// instead of rising back.
//
// Pure delays (the console's reported latency) only rotate the phase of
// Sxy; |H| and g2 are magnitude quantities and stay exact - no delay
// compensation needed, unlike a phase display.
//
// The frames use the same 8192-point Blackman-Harris window as
// SpectrumAnalyzer (identical coefficients, computed here too), and the
// same 256 log-spaced display bands, so IN / OUT spectra and the EQ
// curve always speak of the same frequencies.
class TransferAnalyzer
{
public:
    static constexpr int fftOrder = 13;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int numScopeBins = 256;

    // maxBlockSize is only used to pre-reserve the one-block stashes,
    // so the audio thread never allocates.
    void prepare (double newSampleRate, int maxBlockSize = 4096);

    // v2.41: forget every trace of past audio (fifos, estimators,
    // atomics). Called when the editor re-opens or the analyser strip
    // is re-shown - a frame computed half from pre-hide samples would
    // be a phantom measurement.
    void reset();

    // Mono mixdown of the buffer as it arrives (call BEFORE the console
    // runs). The two pushes always receive the same block sizes, so the
    // two fifos fill in lockstep and frame k of IN covers exactly the
    // same program window as frame k of OUT.
    void pushInput (const juce::AudioBuffer<float>& buffer);
    // Mono mixdown of the console's output (call AFTER it). When this
    // fifo completes a frame the cross-spectrum is computed.
    void pushOutput (const juce::AudioBuffer<float>& buffer);

    // UI-thread snapshot: per display bin, the H1 transfer magnitude in
    // dB (20*log10|H|, clamped to +/- 60) and the coherence g2 in [0, 1].
    void copyTransferData (float* db, float* coherence, int numBins) const;

    double getBinFrequency (int bin) const
    {
        return binFrequencies[(size_t) juce::jlimit (0, numScopeBins - 1, bin)];
    }

private:
    void compute();

    juce::dsp::FFT fft { fftOrder };
    double sampleRate = 48000.0;

    std::array<float, (size_t) fftSize> window {};
    std::array<float, (size_t) fftSize> fifoIn {};
    std::array<float, (size_t) fftSize> fifoOut {};
    // performRealOnlyForwardTransform: real input in the first half,
    // interleaved complex bins [re, im] on return (JUCE FFT contract).
    std::array<float, (size_t) (2 * fftSize)> fftIn {};
    std::array<float, (size_t) (2 * fftSize)> fftOut {};

    // per-display-band EMAs of the band-aggregated spectra. The complex
    // pair (SxyRe, SxyIm) is accumulated complex - that is what cancels
    // the uncorrelated output junk over time. alpha = 1/8 per ~170 ms
    // frame: an effective average over the last ~8 frames (~1.4 s),
    // enough for the random-phase junk to average down, and a settle
    // time close to the v2.21.1 power EMAs.
    std::array<double, (size_t) numScopeBins> emaSxx {}, emaSyy {};
    std::array<double, (size_t) numScopeBins> emaSxyRe {}, emaSxyIm {};

    // v2.43 (Clement's "the raw EQ curve looks a bit noisy" remark):
    // every display band is an INDEPENDENT H1 measurement, so on real
    // material the curve jitters along the frequency axis - the
    // estimators' residual variance, not the console's response (a
    // real console response is smooth in frequency). The fix smooths
    // the four accumulated spectra over the neighbouring bands.
    // Smoothing the ACCUMULATORS (not the dB curve) is what keeps the
    // physics honest: H = Sxy/Sxx stays the exact H1 estimator of the
    // energy-weighted average transfer over the neighbourhood, and the
    // coherence is recomputed from the same smoothed quantities.
    // Because the neighbours' EMAs are at the SAME temporal state, NO
    // temporal lag is added - knob turns settle exactly as fast as
    // before. Edges replicate the first/last band (clamped index), so
    // each pass's weight sum stays a constant 16.
    //
    // v2.43.1 (Clement's "vaguelettes" remark): the v2.43 single pass
    // (5-tap, ~1/6 octave) killed the fine band-scale jitter, but on
    // real material small waves remained (the per-frame estimates
    // wobble with the program). The kernel now runs smoothPasses
    // CASCADES of the same 5-tap binomial [1 4 6 4 1]/16: cascading
    // convolutions convolve, so 3 passes = the 13-tap binomial(12)
    // [1 12 66 ... 12 1]/4096, sigma ~1.7 bands, span 12 bands ~ half
    // an octave. The v2.43.1 reference simulation (an exact offline
    // twin of this analyzer, mock scenario of the regression test):
    // band-to-band jaggedness max 1.35 dB raw / 0.27 (1 pass) /
    // 0.08 dB (3 passes) - 3.3x calmer than v2.43.0. Feature cost on
    // real console EQs: a Q1 peak loses 0.02 dB, a tight Q2 peak
    // 0.06 dB, a steep lowpass flank is untouched; only a pathological
    // Q8/-18 dB notch fills ~3 dB more (documented, no console EQ is
    // that narrow). Convergence after a +6 dB step: unchanged (3.16 s
    // measured, same as v2.43.0). Cost: ~15k flops per ~170 ms frame,
    // zero allocations.
    static constexpr double smoothTapWeights[5] { 1.0, 4.0, 6.0, 4.0, 1.0 };
    static constexpr int smoothPasses = 3;

    // v2.43.1: ping-pong scratch for the smoothing cascade (two
    // buffers are enough: pass N reads the EMAs, then each pass reads
    // the buffer the previous pass wrote). Fixed-size members, so the
    // audio thread never allocates.
    std::array<double, (size_t) (4 * numScopeBins)> smoothScratch[2] {};

    std::array<std::atomic<float>, (size_t) numScopeBins> transferDb {};
    std::array<std::atomic<float>, (size_t) numScopeBins> coherence {};

    std::array<double, (size_t) numScopeBins> binFrequencies {};

    // one-block stashes of the mono mixdowns (see pushInput / pushOutput)
    std::vector<float> pendingIn, pendingOut;

    std::atomic<int> fifoPos { 0 };
};
