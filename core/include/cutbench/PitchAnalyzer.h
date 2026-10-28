#pragma once
// STFT-based, harmonic-aware pitch analysis.
//
// Pipeline for every STFT frame:
//   Hann window -> FFT -> amplitude spectrum -> log compression -> background (noise-floor)
//   subtraction ("whitening") -> harmonic-sum salience for every semitone ->
//   iterative pick-and-cancel of the strongest note (Klapuri-style) to suppress harmonics
//   of notes that were already explained.
//
// Two note maps are produced per frame:
//   raw     - harmonic salience of every semitone before cancellation (what the spectrum "suggests")
//   refined - sparse: only notes picked by the iterative cancellation step (what the engine believes)
//
// This is a heuristic estimator. Overlapping partials, missing fundamentals and noise can all
// produce wrong or ambiguous results; see Candidates.h for how that uncertainty is reported.

#include "cutbench/Fft.h"
#include "cutbench/PitchMath.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cutbench
{
struct AnalyzerConfig
{
    double sampleRate = 44100.0;
    int fftSize = 8192;          // power of two
    int hopSize = 1024;          // 1 .. fftSize
    int minMidi = kMidiC0;       // analysed note range (inclusive)
    int maxMidi = kMidiC8;
    int numHarmonics = 8;        // harmonics summed per candidate note
    float harmonicDecay = 0.8f;  // weight of harmonic h is decay^(h-1)
    float silenceRms = 0.0005f;  // frames below this RMS (about -66 dBFS) are treated as silent
    int maxPolyphony = 6;        // max notes picked per frame
    float pickFloor = 0.01f;     // absolute minimum salience for a pick
    float relativePickFloor = 0.25f; // later picks must reach this fraction of the first pick
    float cancellationFactor = 0.2f; // spectrum is scaled by this around the harmonics of a pick
    float noiseMargin = 0.1f;    // subtracted (on the log-compressed scale) after background removal;
                                 // removes random noise peaks and window sidelobes

    int numNotes() const noexcept { return maxMidi - minMidi + 1; }

    // Low latency / lower pitch resolution (used for the live view).
    static AnalyzerConfig live (double sampleRate);
    // Better pitch resolution, finer time steps (used for recorded passages and loaded files).
    static AnalyzerConfig detailed (double sampleRate);
};

struct AnalysisResult
{
    AnalyzerConfig cfg;
    int numFrames = 0;
    long long firstFrameIndex = 0; // absolute index of frame 0 (non-zero after trimFront)
    long long numSamples = 0;      // number of real input samples analysed so far
    std::vector<float> raw;        // numFrames * numNotes, row-major (frame, note)
    std::vector<float> refined;    // numFrames * numNotes
    std::vector<float> level;      // numFrames: RMS of the analysis window

    int numNotes() const noexcept { return cfg.numNotes(); }
    bool empty() const noexcept { return numFrames <= 0; }

    // Frame f is centred on this time (seconds from the start of the analysed audio).
    double frameTime (int f) const noexcept
    {
        return double (firstFrameIndex + f) * cfg.hopSize / cfg.sampleRate;
    }

    double duration() const noexcept
    {
        return cfg.sampleRate > 0.0 ? double (numSamples) / cfg.sampleRate : 0.0;
    }

    // Nearest frame to a time, clamped to a valid frame, or -1 if there are no frames.
    int frameAtTime (double t) const noexcept;

    float rawAt (int frame, int midi) const noexcept;
    float refinedAt (int frame, int midi) const noexcept;

    void appendFrame (const float* rawRow, const float* refinedRow, float frameLevel);
    void trimFront (int framesToDrop);
    void clear();
};

class PitchAnalyzer
{
public:
    // Throws std::invalid_argument for an invalid configuration (e.g. FFT size not a power of two).
    explicit PitchAnalyzer (const AnalyzerConfig& config);

    const AnalyzerConfig& config() const noexcept { return cfg; }

    // Clears the internal sample history so that the next process() call starts a new stream.
    void reset();

    // Streaming interface: feed any number of samples; a frame is appended to `out` every hop.
    // Allocates only when `out` grows. Frame 0 is centred on t = 0 (half a window of
    // implicit leading silence), so frameTime(f) == f * hop / sampleRate.
    void process (const float* samples, int numSamples, AnalysisResult& out);

    // Analyses a single window of cfg.fftSize samples (exposed for tests and tools).
    void analyseWindow (const float* window, float* rawRow, float* refinedRow, float& level);

    // Offline analysis of a whole buffer. Returns an empty result for empty input.
    // If `cancel` becomes true the (partial) result so far is returned.
    static AnalysisResult analyzeBuffer (const float* samples, size_t numSamples,
                                         const AnalyzerConfig& config,
                                         const std::atomic<bool>* cancel = nullptr);

private:
    void feed (const float* samples, int numSamples, AnalysisResult& out); // samples == nullptr -> zeros
    float salienceOf (const float* spectrum, int noteIndex) const noexcept;
    void cancelNote (float* spectrum, int noteIndex) const noexcept;

    AnalyzerConfig cfg;
    Fft fft;
    int numBins = 0;
    int backgroundHalfWidth = 8;
    float ampScale = 1.0f;
    float weightSum = 1.0f;

    std::vector<float> hann, weights;
    std::vector<int> loBin, hiBin; // numNotes * numHarmonics, -1 if the harmonic is not usable

    // scratch (allocated once)
    std::vector<float> re, im, compressed, whitened, working, windowCopy, unrolled, rawScratch, refinedScratch;
    std::vector<double> prefix;

    // streaming state
    std::vector<float> ring;
    int ringPos = 0;
    int pending = 0;
};

// Temporal smoothing for display: out[f] = (1-a)*x[f] + a*0.5*(x[f-1]+x[f+1]), a in [0,1].
std::vector<float> smoothAlongTime (const std::vector<float>& map, int numFrames, int numNotes, float amount);
} // namespace cutbench
