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

    // -----------------------------------------------------------------
    // v2.44.0 - Clement's framing of the smoothing, which is the right
    // one: the 256 band estimates are POINTS in a bi-logarithmic plot
    // (the bands are log-spaced, so the band index IS the log-frequency
    // axis), and the display should show "le lissage malin" of those
    // points, not the points themselves. The v2.43/v2.43.1 binomial
    // kernels killed the fine band-scale jitter but let slow waves
    // through (a half-octave-period wave passes at 76% amplitude) -
    // exactly the "vaguelettes" still visible on Clement's real
    // sessions. The smart smoothing of scattered (x, y) points is the
    // SAVITZKY-GOLAY estimator: around each band, the 31-point
    // neighbourhood is fitted with a 3rd-order polynomial by least
    // squares, and the fitted value AT the band is kept. That is a
    // fixed FIR kernel (smoothCoeffs below, precomputed), so it costs
    // the same as any tap kernel, but it PRESERVES everything a cubic
    // can express - slopes (shelves, filter flanks), curvature (EQ
    // peaks) - while removing the band-scale jitter AND the slow
    // waves: a half-octave wave leaves 27% amplitude (vs 76% with the
    // v2.43.1 cascade), the display jitter is unchanged (rms 0.022 dB
    // on the regression mock, raw estimates 0.58), and the kernel is
    // time-invariant, so a knob turn settles exactly as fast (the
    // v2.43 measurement stays: ~3.2 s to +/- 0.5 dB, display pole
    // 0.5 s).
    //
    // The kernel is applied to the four ACCUMULATED spectra (Sxx, Syy,
    // SxyRe, SxyIm), not to the dB curve: H = Sxy/Sxx stays the exact
    // H1 estimator of the neighbourhood's energy-weighted average
    // transfer (a junk band carries little energy, so it barely votes
    // - smoothing the dB would give it a full vote), and the coherence
    // is recomputed from the same smoothed quantities, so curve and
    // validity gate stay consistent. All four arrays go through the
    // SAME kernel, so the identity transfer stays exactly 0 dB.
    //
    // Documented cost (reference simulation, stationary white
    // program, feature measured against its analytic response): a Q1
    // EQ peak reads 0.04 dB low, a Q2 peak 0.30 dB, a surgical Q4
    // peak 0.93 dB, a Q6 peak 1.31 dB, the knee of a steep (24
    // dB/oct) lowpass moves by 1.17 dB, a 12 dB/oct highpass flank is
    // untouched, and the smoothed input spectrum stays strictly
    // positive (no division blowups). A NARROW uncorrelated artifact
    // (a 1-band console tone) reads more coherent than it is: g2
    // 0.50 raw -> 0.93 smoothed, because the window dilutes its dip
    // with ~30 coherent neighbours - broadband junk (a console noise
    // floor), the coherence gate's real job, is unaffected. ~32k flops per ~170 ms frame,
    // zero allocations. Edges replicate the first/last band (clamped
    // index), as with every kernel since v2.43.
    // -----------------------------------------------------------------
    static constexpr int smoothHalfWidth = 15;
    static constexpr double smoothCoeffs[2 * smoothHalfWidth + 1] =
    {
       -4.10557184750718895e-02, -2.63929618768319676e-02,
       -1.27414298715740771e-02, -1.01122459298234603e-04,
        1.15279603599955163e-02,  2.21458185863072088e-02,
        3.17524522196368369e-02,  4.03478612599843939e-02,
        4.79320457073498865e-02,  5.45050055617333218e-02,
        6.00667408231346789e-02,  6.46172514915539786e-02,
        6.81565375669912071e-02,  7.06845990494463644e-02,
        7.22014359389194782e-02,  7.27070482354104930e-02,
        7.22014359389194782e-02,  7.06845990494463644e-02,
        6.81565375669912210e-02,  6.46172514915539786e-02,
        6.00667408231346928e-02,  5.45050055617333218e-02,
        4.79320457073499004e-02,  4.03478612599844078e-02,
        3.17524522196368439e-02,  2.21458185863072192e-02,
        1.15279603599955250e-02, -1.01122459298227664e-04,
       -1.27414298715740632e-02, -2.63929618768319468e-02,
       -4.10557184750719104e-02
    };

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
    // time of ~2 s on a knob turn. (v2.45 briefly tried 1/32 to calm
    // programme-driven waves; Clement's bypass test proved those waves
    // are the console's own programme-dependent response, not noise,
    // so the slow average was reverted in v2.46 - see crossEmaAlpha in
    // the .cpp for the full history.)
    std::array<double, (size_t) numScopeBins> emaSxx {}, emaSyy {};
    std::array<double, (size_t) numScopeBins> emaSxyRe {}, emaSxyIm {};

    // (the smoothing coefficients themselves are the public
    // smoothCoeffs above - see their comment for the full story)

    // v2.44.0: scratch for the single Savitzky-Golay pass (the EMAs
    // are the untouched source, the smoothed spectra land here).
    // Fixed-size member, so the audio thread never allocates.
    std::array<double, (size_t) (4 * numScopeBins)> smoothScratch {};

    std::array<std::atomic<float>, (size_t) numScopeBins> transferDb {};
    std::array<std::atomic<float>, (size_t) numScopeBins> coherence {};

    std::array<double, (size_t) numScopeBins> binFrequencies {};

    // one-block stashes of the mono mixdowns (see pushInput / pushOutput)
    std::vector<float> pendingIn, pendingOut;

    std::atomic<int> fifoPos { 0 };
};
