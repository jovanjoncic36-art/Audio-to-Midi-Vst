#include "cutbench/PitchAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cutbench
{
//==============================================================================
AnalyzerConfig AnalyzerConfig::live (double sr)
{
    AnalyzerConfig c;
    c.sampleRate = sr;
    c.fftSize = 4096;
    c.hopSize = 1024;
    return c;
}

AnalyzerConfig AnalyzerConfig::detailed (double sr)
{
    AnalyzerConfig c;
    c.sampleRate = sr;
    c.fftSize = 8192;
    c.hopSize = 512;
    return c;
}

//==============================================================================
int AnalysisResult::frameAtTime (double t) const noexcept
{
    if (numFrames <= 0 || cfg.sampleRate <= 0.0)
        return -1;

    const long long idx = std::llround (t * cfg.sampleRate / cfg.hopSize) - firstFrameIndex;
    return (int) std::max<long long> (0, std::min<long long> (numFrames - 1, idx));
}

float AnalysisResult::rawAt (int frame, int midi) const noexcept
{
    const int n = midi - cfg.minMidi;
    if (frame < 0 || frame >= numFrames || n < 0 || n >= numNotes())
        return 0.0f;
    return raw[(size_t) frame * (size_t) numNotes() + (size_t) n];
}

float AnalysisResult::refinedAt (int frame, int midi) const noexcept
{
    const int n = midi - cfg.minMidi;
    if (frame < 0 || frame >= numFrames || n < 0 || n >= numNotes())
        return 0.0f;
    return refined[(size_t) frame * (size_t) numNotes() + (size_t) n];
}

void AnalysisResult::appendFrame (const float* rawRow, const float* refinedRow, float frameLevel)
{
    const size_t n = (size_t) numNotes();
    raw.insert (raw.end(), rawRow, rawRow + n);
    refined.insert (refined.end(), refinedRow, refinedRow + n);
    level.push_back (frameLevel);
    ++numFrames;
}

void AnalysisResult::trimFront (int framesToDrop)
{
    framesToDrop = std::max (0, std::min (framesToDrop, numFrames));
    if (framesToDrop == 0)
        return;

    const size_t n = (size_t) numNotes() * (size_t) framesToDrop;
    raw.erase (raw.begin(), raw.begin() + (std::ptrdiff_t) n);
    refined.erase (refined.begin(), refined.begin() + (std::ptrdiff_t) n);
    level.erase (level.begin(), level.begin() + framesToDrop);
    numFrames -= framesToDrop;
    firstFrameIndex += framesToDrop;
}

void AnalysisResult::clear()
{
    numFrames = 0;
    firstFrameIndex = 0;
    numSamples = 0;
    raw.clear();
    refined.clear();
    level.clear();
}

//==============================================================================
PitchAnalyzer::PitchAnalyzer (const AnalyzerConfig& config)
    : cfg (config), fft (config.fftSize)
{
    if (cfg.hopSize < 1 || cfg.hopSize > cfg.fftSize)
        throw std::invalid_argument ("hopSize must be in 1..fftSize");
    if (cfg.sampleRate <= 0.0)
        throw std::invalid_argument ("sampleRate must be positive");
    if (cfg.numNotes() < 1 || cfg.numHarmonics < 1)
        throw std::invalid_argument ("invalid note range / harmonic count");

    const int N = cfg.fftSize;
    numBins = N / 2 + 1;
    backgroundHalfWidth = std::max (8, N / 256);

    hann.resize ((size_t) N);
    double sum = 0.0;
    for (int i = 0; i < N; ++i)
    {
        hann[(size_t) i] = (float) (0.5 - 0.5 * std::cos (6.283185307179586 * i / N));
        sum += hann[(size_t) i];
    }
    ampScale = (float) (2.0 / sum); // a full-scale sine produces a peak amplitude of ~1.0

    weights.resize ((size_t) cfg.numHarmonics);
    weightSum = 0.0f;
    for (int h = 0; h < cfg.numHarmonics; ++h)
    {
        weights[(size_t) h] = std::pow (cfg.harmonicDecay, (float) h);
        weightSum += weights[(size_t) h];
    }

    // Per note and harmonic: range of FFT bins within +-35 cents of the harmonic's frequency.
    const int numNotes = cfg.numNotes();
    loBin.assign ((size_t) numNotes * (size_t) cfg.numHarmonics, -1);
    hiBin.assign ((size_t) numNotes * (size_t) cfg.numHarmonics, -1);

    const double centsRatio = std::pow (2.0, 35.0 / 1200.0);
    for (int i = 0; i < numNotes; ++i)
    {
        const double f0 = midiToHz (cfg.minMidi + i);
        for (int h = 0; h < cfg.numHarmonics; ++h)
        {
            const double f = f0 * (h + 1);
            const double centreBin = f * N / cfg.sampleRate;
            if (centreBin > numBins - 2)
                break; // this and all higher harmonics are above Nyquist

            int lo = (int) std::floor (f / centsRatio * N / cfg.sampleRate);
            int hi = (int) std::ceil (f * centsRatio * N / cfg.sampleRate);
            lo = std::max (1, lo);
            hi = std::min (numBins - 2, hi);
            if (lo > hi)
                continue;

            loBin[(size_t) i * (size_t) cfg.numHarmonics + (size_t) h] = lo;
            hiBin[(size_t) i * (size_t) cfg.numHarmonics + (size_t) h] = hi;
        }
    }

    re.assign ((size_t) N, 0.0f);
    im.assign ((size_t) N, 0.0f);
    compressed.assign ((size_t) numBins, 0.0f);
    whitened.assign ((size_t) numBins, 0.0f);
    working.assign ((size_t) numBins, 0.0f);
    windowCopy.assign ((size_t) N, 0.0f);
    unrolled.assign ((size_t) N, 0.0f);
    rawScratch.assign ((size_t) numNotes, 0.0f);
    refinedScratch.assign ((size_t) numNotes, 0.0f);
    prefix.assign ((size_t) numBins + 1, 0.0);

    ring.assign ((size_t) N, 0.0f);
    reset();
}

void PitchAnalyzer::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    ringPos = 0;
    pending = cfg.fftSize / 2; // first frame is centred on t = 0 (half a window of leading silence)
}

float PitchAnalyzer::salienceOf (const float* spectrum, int noteIndex) const noexcept
{
    const size_t base = (size_t) noteIndex * (size_t) cfg.numHarmonics;
    float sum = 0.0f;

    for (int h = 0; h < cfg.numHarmonics; ++h)
    {
        const int lo = loBin[base + (size_t) h];
        if (lo < 0)
            break;

        const int hi = hiBin[base + (size_t) h];
        float m = 0.0f;
        for (int k = lo; k <= hi; ++k)
            m = std::max (m, spectrum[k]);

        sum += weights[(size_t) h] * m;
    }

    return sum / weightSum;
}

void PitchAnalyzer::cancelNote (float* spectrum, int noteIndex) const noexcept
{
    const size_t base = (size_t) noteIndex * (size_t) cfg.numHarmonics;

    for (int h = 0; h < cfg.numHarmonics; ++h)
    {
        const int lo = loBin[base + (size_t) h];
        if (lo < 0)
            break;

        const int hi = hiBin[base + (size_t) h];
        int peak = lo;
        for (int k = lo; k <= hi; ++k)
            if (spectrum[k] > spectrum[peak])
                peak = k;

        // Hann main lobe is +-2 bins wide.
        const int from = std::max (1, peak - 2);
        const int to = std::min (numBins - 2, peak + 2);
        for (int k = from; k <= to; ++k)
            spectrum[k] *= cfg.cancellationFactor;
    }
}

void PitchAnalyzer::analyseWindow (const float* window, float* rawRow, float* refinedRow, float& level)
{
    const int N = cfg.fftSize;
    const int numNotes = cfg.numNotes();

    std::fill (rawRow, rawRow + numNotes, 0.0f);
    std::fill (refinedRow, refinedRow + numNotes, 0.0f);

    // RMS of the (sanitised) window; invalid samples are treated as silence.
    double energy = 0.0;
    for (int i = 0; i < N; ++i)
    {
        float v = window[i];
        if (! std::isfinite (v))
            v = 0.0f;
        windowCopy[(size_t) i] = v;
        energy += double (v) * v;
    }

    level = (float) std::sqrt (energy / N);
    if (level < cfg.silenceRms)
        return;

    for (int i = 0; i < N; ++i)
    {
        re[(size_t) i] = windowCopy[(size_t) i] * hann[(size_t) i];
        im[(size_t) i] = 0.0f;
    }

    fft.forward (re.data(), im.data());

    // Amplitude spectrum, log-compressed: 0 dBFS partial -> 1.0, -40 dBFS -> ~0.35, -60 dBFS -> ~0.13.
    for (int k = 0; k < numBins; ++k)
    {
        const float a = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]) * ampScale;
        compressed[(size_t) k] = std::log10 (1.0f + 1000.0f * a) / 3.0f;
    }

    // Background subtraction with a moving average (noise floor / broadband energy), plus a small
    // fixed margin so that random noise peaks and Hann sidelobes do not look like partials.
    prefix[0] = 0.0;
    for (int k = 0; k < numBins; ++k)
        prefix[(size_t) k + 1] = prefix[(size_t) k] + compressed[(size_t) k];

    for (int k = 0; k < numBins; ++k)
    {
        const int a = std::max (0, k - backgroundHalfWidth);
        const int b = std::min (numBins, k + backgroundHalfWidth + 1);
        const float bg = (float) ((prefix[(size_t) b] - prefix[(size_t) a]) / (b - a));
        whitened[(size_t) k] = std::max (0.0f, compressed[(size_t) k] - bg - cfg.noiseMargin);
    }

    // Raw harmonic salience of every semitone.
    for (int i = 0; i < numNotes; ++i)
        rawRow[i] = salienceOf (whitened.data(), i);

    // Iterative estimation: pick the strongest note, cancel its harmonics, repeat.
    std::copy (whitened.begin(), whitened.end(), working.begin());
    float firstSalience = 0.0f;

    for (int iteration = 0; iteration < cfg.maxPolyphony; ++iteration)
    {
        int best = -1;
        float bestSalience = 0.0f;

        for (int i = 0; i < numNotes; ++i)
        {
            const float s = salienceOf (working.data(), i);
            if (s > bestSalience)
            {
                bestSalience = s;
                best = i;
            }
        }

        if (best < 0 || bestSalience < cfg.pickFloor)
            break;

        if (iteration == 0)
            firstSalience = bestSalience;
        else if (bestSalience < cfg.relativePickFloor * firstSalience)
            break;

        refinedRow[best] = std::max (refinedRow[best], bestSalience);
        cancelNote (working.data(), best);
    }
}

void PitchAnalyzer::feed (const float* samples, int n, AnalysisResult& out)
{
    const int N = cfg.fftSize;

    while (n > 0)
    {
        const int take = std::min (n, pending);

        for (int i = 0; i < take; ++i)
        {
            ring[(size_t) ringPos] = samples != nullptr ? samples[i] : 0.0f;
            if (++ringPos == N)
                ringPos = 0;
        }

        if (samples != nullptr)
            samples += take;

        n -= take;
        pending -= take;

        if (pending == 0)
        {
            // ringPos now points at the oldest sample: unroll into a contiguous window.
            const int firstPart = N - ringPos;
            std::copy (ring.begin() + ringPos, ring.end(), unrolled.begin());
            std::copy (ring.begin(), ring.begin() + ringPos, unrolled.begin() + firstPart);

            float frameLevel = 0.0f;
            analyseWindow (unrolled.data(), rawScratch.data(), refinedScratch.data(), frameLevel);

            out.appendFrame (rawScratch.data(), refinedScratch.data(), frameLevel);
            pending = cfg.hopSize;
        }
    }
}

void PitchAnalyzer::process (const float* samples, int numSamples, AnalysisResult& out)
{
    if (numSamples <= 0 || samples == nullptr)
        return;

    if (out.numFrames == 0 && out.numSamples == 0)
        out.cfg = cfg;

    out.numSamples += numSamples;
    feed (samples, numSamples, out);
}

AnalysisResult PitchAnalyzer::analyzeBuffer (const float* samples, size_t numSamples,
                                             const AnalyzerConfig& config,
                                             const std::atomic<bool>* cancel)
{
    AnalysisResult result;
    result.cfg = config;

    if (samples == nullptr || numSamples == 0)
        return result;

    PitchAnalyzer analyzer (config);

    const size_t expectedFrames = numSamples / (size_t) config.hopSize + 2;
    result.raw.reserve (expectedFrames * (size_t) config.numNotes());
    result.refined.reserve (expectedFrames * (size_t) config.numNotes());
    result.level.reserve (expectedFrames);

    const size_t chunk = (size_t) config.hopSize * 4;
    for (size_t pos = 0; pos < numSamples; pos += chunk)
    {
        if (cancel != nullptr && cancel->load())
            return result;

        const int n = (int) std::min (chunk, numSamples - pos);
        analyzer.feed (samples + pos, n, result);
    }

    // Trailing half window of silence so that the frame grid reaches the end of the audio.
    int padding = config.fftSize / 2;
    while (padding > 0)
    {
        if (cancel != nullptr && cancel->load())
            return result;

        const int n = std::min (padding, (int) chunk);
        analyzer.feed (nullptr, n, result);
        padding -= n;
    }

    result.numSamples = (long long) numSamples;
    return result;
}

//==============================================================================
std::vector<float> smoothAlongTime (const std::vector<float>& map, int numFrames, int numNotes, float amount)
{
    std::vector<float> out (map);
    if (numFrames < 2 || numNotes < 1 || map.size() < (size_t) numFrames * (size_t) numNotes)
        return out;

    const float a = std::max (0.0f, std::min (1.0f, amount));

    for (int f = 0; f < numFrames; ++f)
    {
        const int fPrev = std::max (0, f - 1);
        const int fNext = std::min (numFrames - 1, f + 1);

        for (int n = 0; n < numNotes; ++n)
        {
            const float prev = map[(size_t) fPrev * (size_t) numNotes + (size_t) n];
            const float cur = map[(size_t) f * (size_t) numNotes + (size_t) n];
            const float next = map[(size_t) fNext * (size_t) numNotes + (size_t) n];
            out[(size_t) f * (size_t) numNotes + (size_t) n] = (1.0f - a) * cur + a * 0.5f * (prev + next);
        }
    }

    return out;
}
} // namespace cutbench
