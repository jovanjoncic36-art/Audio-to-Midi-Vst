#include "cutbench/AnalysisEngine.h"

#include <algorithm>
#include <chrono>
#include <exception>

namespace cutbench
{
//==============================================================================
SpscFloatFifo::SpscFloatFifo (size_t capacityPowerOfTwo)
{
    size_t cap = 2;
    while (cap < capacityPowerOfTwo)
        cap <<= 1;

    buffer.assign (cap, 0.0f);
    mask = cap - 1;
}

size_t SpscFloatFifo::available() const noexcept
{
    return writeIndex.load (std::memory_order_acquire) - readIndex.load (std::memory_order_acquire);
}

size_t SpscFloatFifo::write (const float* src, size_t n) noexcept
{
    const size_t w = writeIndex.load (std::memory_order_relaxed);
    const size_t r = readIndex.load (std::memory_order_acquire);
    const size_t freeSpace = buffer.size() - (w - r);
    n = std::min (n, freeSpace);

    for (size_t i = 0; i < n; ++i)
        buffer[(w + i) & mask] = src[i];

    writeIndex.store (w + n, std::memory_order_release);
    return n;
}

size_t SpscFloatFifo::read (float* dst, size_t n) noexcept
{
    const size_t r = readIndex.load (std::memory_order_relaxed);
    const size_t w = writeIndex.load (std::memory_order_acquire);
    n = std::min (n, w - r);

    for (size_t i = 0; i < n; ++i)
        dst[i] = buffer[(r + i) & mask];

    readIndex.store (r + n, std::memory_order_release);
    return n;
}

//==============================================================================
AnalysisEngine::AnalysisEngine() : fifo (size_t (1) << 18)
{
    analysisThread = std::thread ([this] { analysisLoop(); });
}

AnalysisEngine::~AnalysisEngine()
{
    quit.store (true);
    cancelJob.store (true);

    if (analysisThread.joinable())
        analysisThread.join();

    std::lock_guard<std::mutex> lock (jobMutex);
    if (worker.joinable())
        worker.join();
}

void AnalysisEngine::setSampleRate (double newRate)
{
    if (newRate >= 8000.0 && newRate <= 384000.0)
        sampleRate.store (newRate);
}

void AnalysisEngine::pushAudio (const float* mono, int numSamples) noexcept
{
    if (mono == nullptr || numSamples <= 0)
        return;

    const size_t written = fifo.write (mono, (size_t) numSamples);
    if (written < (size_t) numSamples)
        dropped.fetch_add ((uint64_t) numSamples - written, std::memory_order_relaxed);
}

void AnalysisEngine::startRecording()
{
    recordCommand.store (1);
}

void AnalysisEngine::stopRecording()
{
    recordCommand.store (2);
}

double AnalysisEngine::recordedSeconds() const noexcept
{
    const double rate = recordedRate.load();
    return rate > 0.0 ? double (recordedCount.load()) / rate : 0.0;
}

std::shared_ptr<const std::vector<float>> AnalysisEngine::takeFinishedRecording (double& sampleRateOut)
{
    std::lock_guard<std::mutex> lock (resultMutex);
    sampleRateOut = finishedRecordingRate;
    auto out = std::move (finishedRecording);
    finishedRecording.reset();
    return out;
}

std::shared_ptr<const AnalysisResult> AnalysisEngine::liveSnapshot() const
{
    std::lock_guard<std::mutex> lock (resultMutex);
    return liveSnap;
}

std::shared_ptr<const AnalysisResult> AnalysisEngine::passageResult() const
{
    std::lock_guard<std::mutex> lock (resultMutex);
    return passageSnap;
}

std::string AnalysisEngine::lastError() const
{
    std::lock_guard<std::mutex> lock (resultMutex);
    return errorText;
}

void AnalysisEngine::setError (const std::string& message)
{
    std::lock_guard<std::mutex> lock (resultMutex);
    errorText = message;
}

void AnalysisEngine::publishLive (const AnalysisResult& live)
{
    auto snapshot = std::make_shared<const AnalysisResult> (live);

    {
        std::lock_guard<std::mutex> lock (resultMutex);
        liveSnap = std::move (snapshot);
    }

    liveVer.fetch_add (1);
}

void AnalysisEngine::analysePassage (std::shared_ptr<const std::vector<float>> mono, double rate)
{
    std::lock_guard<std::mutex> lock (jobMutex);

    cancelJob.store (true);
    if (worker.joinable())
        worker.join();
    cancelJob.store (false);

    if (! (rate >= 8000.0 && rate <= 384000.0))
        rate = 44100.0;

    if (mono == nullptr || mono->empty())
    {
        auto empty = std::make_shared<AnalysisResult>();
        empty->cfg = AnalyzerConfig::detailed (rate);

        {
            std::lock_guard<std::mutex> resultLock (resultMutex);
            passageSnap = std::move (empty);
            errorText = "There is no audio to analyse.";
        }

        passageVer.fetch_add (1);
        return;
    }

    analysing.store (true);
    worker = std::thread ([this, mono = std::move (mono), rate]
    {
        try
        {
            AnalysisResult result = PitchAnalyzer::analyzeBuffer (mono->data(), mono->size(),
                                                                   AnalyzerConfig::detailed (rate), &cancelJob);
            if (! cancelJob.load())
            {
                auto shared = std::make_shared<const AnalysisResult> (std::move (result));

                {
                    std::lock_guard<std::mutex> resultLock (resultMutex);
                    passageSnap = std::move (shared);
                    errorText.clear();
                }

                passageVer.fetch_add (1);
            }
        }
        catch (const std::exception& e)
        {
            setError (std::string ("Analysis failed: ") + e.what());
        }
        catch (...)
        {
            setError ("Analysis failed.");
        }

        analysing.store (false);
    });
}

//==============================================================================
void AnalysisEngine::analysisLoop()
{
    using Clock = std::chrono::steady_clock;

    std::vector<float> chunk (4096);
    std::unique_ptr<PitchAnalyzer> analyzer;
    AnalysisResult live;
    double configuredRate = 0.0;

    std::vector<float> recorded;
    bool recording = false;
    double recordRate = 44100.0;
    size_t maxRecordSamples = 0;

    bool dirty = false;
    auto lastPublish = Clock::now();

    auto finishRecording = [&]
    {
        auto audio = std::make_shared<const std::vector<float>> (std::move (recorded));
        recorded = std::vector<float>();

        {
            std::lock_guard<std::mutex> lock (resultMutex);
            finishedRecording = std::move (audio);
            finishedRecordingRate = recordRate;
        }

        recording = false;
        recordingActive.store (false);
    };

    // Reads everything currently in the FIFO. Feeds the live analyser and, if recording, the capture buffer.
    auto drain = [&]
    {
        size_t n;
        while ((n = fifo.read (chunk.data(), chunk.size())) > 0)
        {
            analyzer->process (chunk.data(), (int) n, live);
            dirty = true;

            if (recording)
            {
                const size_t room = maxRecordSamples > recorded.size() ? maxRecordSamples - recorded.size() : 0;
                const size_t take = std::min (n, room);
                recorded.insert (recorded.end(), chunk.begin(), chunk.begin() + (std::ptrdiff_t) take);
                recordedCount.store (recorded.size());

                if (recorded.size() >= maxRecordSamples)
                    finishRecording(); // hit the length limit: stop automatically
            }
        }
    };

    while (! quit.load())
    {
        // (Re)configure for the current sample rate.
        const double rate = sampleRate.load();
        if (analyzer == nullptr || rate != configuredRate)
        {
            if (recording)
                finishRecording();

            analyzer = std::make_unique<PitchAnalyzer> (AnalyzerConfig::live (rate));
            live.clear();
            live.cfg = analyzer->config();
            configuredRate = rate;

            float discard[256];
            while (fifo.read (discard, 256) > 0) {}

            dirty = true;
        }

        const int command = recordCommand.exchange (0);

        if (command == 1)
        {
            drain(); // samples queued before the request do not belong to the recording
            recorded.clear();
            recordRate = configuredRate;
            maxRecordSamples = (size_t) (kMaxRecordSeconds * recordRate);
            recorded.reserve (maxRecordSamples); // allocation happens here, never on the audio thread
            recordedRate.store (recordRate);
            recordedCount.store (0);
            recording = true;
            recordingActive.store (true);
        }

        drain();

        if (command == 2 && recording)
            finishRecording();

        const int maxLiveFrames = std::max (16, (int) (kLiveWindowSeconds * configuredRate / live.cfg.hopSize));
        if (live.numFrames > maxLiveFrames + 64)
            live.trimFront (live.numFrames - maxLiveFrames);

        const auto now = Clock::now();
        if (dirty && now - lastPublish >= std::chrono::milliseconds (40))
        {
            publishLive (live);
            lastPublish = now;
            dirty = false;
        }

        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
}
} // namespace cutbench
