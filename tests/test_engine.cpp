#include "TestFramework.h"
#include "TestSignals.h"
#include "cutbench/AnalysisEngine.h"
#include "cutbench/Candidates.h"

#include <chrono>
#include <thread>

using namespace cutbench;

namespace
{
template <typename Pred>
bool waitFor (Pred pred, int timeoutMs = 15000)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds (timeoutMs);
    while (std::chrono::steady_clock::now() < end)
    {
        if (pred())
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    return pred();
}

void pushInBlocks (AnalysisEngine& engine, const std::vector<float>& x, int blockSize = 512)
{
    for (size_t pos = 0; pos < x.size(); pos += (size_t) blockSize)
        engine.pushAudio (x.data() + pos, (int) std::min<size_t> ((size_t) blockSize, x.size() - pos));
}

std::vector<float> tone (double sr, double seconds, int midi)
{
    auto x = sig::silence (sr, seconds);
    sig::addTone (x, sr, midiToHz (midi), 0.0, seconds, 0.4, 6, 1.0);
    return x;
}

bool hasNote (const AnalysisResult& r, int midi)
{
    CandidateParams p;
    p.lowMidi = kMidiC0;
    p.highMidi = kMidiC8;
    for (const auto& c : extractCandidates (r, p))
        if (c.midi == midi)
            return true;
    return false;
}
} // namespace

TEST (fifo_wraps_and_reports_overflow)
{
    SpscFloatFifo fifo (8);
    CHECK (fifo.capacity() == 8);

    float in[6] = { 1, 2, 3, 4, 5, 6 }, out[8] = {};
    CHECK (fifo.write (in, 6) == 6);
    CHECK (fifo.available() == 6);
    CHECK (fifo.read (out, 4) == 4);
    CHECK (out[0] == 1 && out[3] == 4);

    // wrap around the end of the buffer
    float in2[6] = { 7, 8, 9, 10, 11, 12 };
    CHECK (fifo.write (in2, 6) == 6);
    CHECK (fifo.write (in2, 6) == 0);  // full: nothing written
    CHECK (fifo.read (out, 8) == 8);
    CHECK (out[0] == 5 && out[1] == 6 && out[2] == 7 && out[7] == 12);
    CHECK (fifo.read (out, 8) == 0);
    CHECK (SpscFloatFifo (5).capacity() == 8); // rounded up to a power of two
}

TEST (fifo_two_threads_preserve_order_without_loss)
{
    SpscFloatFifo fifo (1024);
    const size_t total = 2000000;
    std::atomic<bool> orderOk { true };
    std::atomic<size_t> received { 0 };

    std::thread consumer ([&]
    {
        float buf[200];
        size_t expected = 0;
        while (expected < total)
        {
            const size_t n = fifo.read (buf, 200);
            for (size_t i = 0; i < n; ++i)
                if (buf[i] != (float) ((expected + i) % 100000))
                    orderOk = false;
            expected += n;
            received = expected;
            if (n == 0)
                std::this_thread::yield();
        }
    });

    size_t sent = 0;
    float block[64];
    while (sent < total)
    {
        const size_t n = std::min<size_t> (64, total - sent);
        for (size_t i = 0; i < n; ++i)
            block[i] = (float) ((sent + i) % 100000);

        size_t offset = 0;
        while (offset < n)
        {
            const size_t w = fifo.write (block + offset, n - offset);
            offset += w;
            if (w == 0)
                std::this_thread::yield();
        }
        sent += n;
    }

    consumer.join();
    CHECK (orderOk.load());
    CHECK (received.load() == total);
}

TEST (engine_live_view_sees_pushed_audio)
{
    AnalysisEngine engine;
    engine.setSampleRate (44100.0);
    CHECK (engine.liveSnapshot() == nullptr);

    pushInBlocks (engine, tone (44100.0, 1.5, 69));

    CHECK (waitFor ([&] { auto s = engine.liveSnapshot(); return s && s->numSamples >= 66000; }));

    const auto snap = engine.liveSnapshot();
    CHECK (snap != nullptr);
    if (snap != nullptr)
    {
        CHECK (snap->numFrames > 20);
        CHECK (snap->cfg.sampleRate == 44100.0);
        CHECK (hasNote (*snap, 69));
    }

    CHECK (engine.liveVersion() > 0);
    CHECK (engine.droppedSamples() == 0);
}

TEST (engine_follows_sample_rate_changes)
{
    AnalysisEngine engine;
    engine.setSampleRate (44100.0);
    pushInBlocks (engine, tone (44100.0, 0.5, 60));
    CHECK (waitFor ([&] { auto s = engine.liveSnapshot(); return s && s->numFrames > 0; }));

    engine.setSampleRate (48000.0);
    CHECK (waitFor ([&] { auto s = engine.liveSnapshot(); return s && s->cfg.sampleRate == 48000.0; }));

    pushInBlocks (engine, tone (48000.0, 1.0, 64));
    CHECK (waitFor ([&] { auto s = engine.liveSnapshot(); return s && s->numSamples >= 45000 && hasNote (*s, 64); }));

    engine.setSampleRate (-5.0); // invalid values are ignored
    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    CHECK (engine.liveSnapshot()->cfg.sampleRate == 48000.0);
}

TEST (engine_recording_and_offline_analysis)
{
    AnalysisEngine engine;
    engine.setSampleRate (44100.0);

    double rate = 0.0;
    CHECK (engine.takeFinishedRecording (rate) == nullptr);

    engine.startRecording();
    CHECK (waitFor ([&] { return engine.isRecording(); }));

    const auto x = tone (44100.0, 1.0, 62);
    pushInBlocks (engine, x);
    CHECK (waitFor ([&] { return engine.recordedSeconds() >= 0.99; }));

    engine.stopRecording();
    CHECK (waitFor ([&] { return ! engine.isRecording(); }));

    std::shared_ptr<const std::vector<float>> recording;
    CHECK (waitFor ([&] { recording = engine.takeFinishedRecording (rate); return recording != nullptr; }));
    CHECK (recording != nullptr);
    CHECK (rate == 44100.0);
    CHECK (engine.takeFinishedRecording (rate) == nullptr); // only delivered once

    if (recording != nullptr)
    {
        CHECK (recording->size() == x.size());           // nothing lost
        bool identical = true;
        for (size_t i = 0; i < x.size(); ++i)
            identical = identical && ((*recording)[i] == x[i]);
        CHECK (identical);                                // bit-exact capture

        const uint32_t before = engine.passageVersion();
        engine.analysePassage (recording, rate);
        CHECK (waitFor ([&] { return engine.passageVersion() != before; }));
        CHECK (waitFor ([&] { return ! engine.isAnalysing(); }));

        const auto result = engine.passageResult();
        CHECK (result != nullptr);
        if (result != nullptr)
        {
            CHECK (result->cfg.fftSize == 8192);
            CHECK_NEAR (result->duration(), 1.0, 1e-3);
            CHECK (hasNote (*result, 62));
        }
    }
}

TEST (engine_empty_recording_is_delivered_and_handled)
{
    AnalysisEngine engine;
    engine.setSampleRate (44100.0);
    engine.startRecording();
    CHECK (waitFor ([&] { return engine.isRecording(); }));
    engine.stopRecording();
    CHECK (waitFor ([&] { return ! engine.isRecording(); }));

    double rate = 0.0;
    std::shared_ptr<const std::vector<float>> rec;
    CHECK (waitFor ([&] { rec = engine.takeFinishedRecording (rate); return rec != nullptr; }));
    CHECK (rec != nullptr && rec->empty()); // empty recording is delivered, analysis handles it
    engine.analysePassage (rec, rate);
    CHECK (engine.passageResult() != nullptr);
    CHECK (engine.passageResult()->empty());
    CHECK (! engine.lastError().empty());
}

TEST (engine_overflow_is_counted_and_survivable)
{
    AnalysisEngine engine;
    engine.setSampleRate (44100.0);

    const auto big = sig::whiteNoise (44100.0, 20.0, 0.1); // 882000 samples >> FIFO capacity
    engine.pushAudio (big.data(), (int) big.size());
    CHECK (engine.droppedSamples() > 0);

    // still alive and analysing afterwards
    CHECK (waitFor ([&] { auto s = engine.liveSnapshot(); return s && s->numFrames > 0; }));
    const uint32_t v = engine.liveVersion();
    pushInBlocks (engine, tone (44100.0, 1.0, 69));
    CHECK (waitFor ([&] { return engine.liveVersion() != v; }));
}

TEST (engine_ignores_invalid_push_arguments)
{
    AnalysisEngine engine;
    engine.pushAudio (nullptr, 100);
    float x = 0.5f;
    engine.pushAudio (&x, 0);
    engine.pushAudio (&x, -4);
    CHECK (engine.droppedSamples() == 0);
}

TEST (engine_new_job_replaces_running_job_and_destructor_does_not_hang)
{
    auto big = std::make_shared<std::vector<float>> (sig::whiteNoise (44100.0, 90.0, 0.2));
    auto small = std::make_shared<std::vector<float>> (tone (44100.0, 1.0, 67));

    {
        AnalysisEngine engine;
        engine.analysePassage (big, 44100.0);   // long job
        engine.analysePassage (small, 44100.0); // must cancel it and run this one
        CHECK (waitFor ([&] { return engine.passageResult() != nullptr && ! engine.isAnalysing(); }));
        CHECK (hasNote (*engine.passageResult(), 67));

        engine.analysePassage (big, 44100.0);   // start another long job and destroy the engine immediately
    }
    CHECK (true); // reaching this line means the destructor cancelled and joined the worker
}

TEST (engine_invalid_rate_for_passage_falls_back_safely)
{
    AnalysisEngine engine;
    auto x = std::make_shared<std::vector<float>> (tone (44100.0, 0.5, 60));
    engine.analysePassage (x, 0.0);
    CHECK (waitFor ([&] { return engine.passageResult() != nullptr && ! engine.isAnalysing(); }));
    CHECK (engine.passageResult()->cfg.sampleRate == 44100.0);
}

TEST (engine_recording_stops_automatically_at_the_length_limit)
{
    AnalysisEngine engine;
    engine.setSampleRate (44100.0);
    engine.startRecording();
    CHECK (waitFor ([&] { return engine.isRecording(); }));

    const auto oneSecond = tone (44100.0, 1.0, 60);
    const int seconds = (int) AnalysisEngine::kMaxRecordSeconds + 5; // push more than the limit

    for (int s = 0; s < seconds && engine.isRecording(); ++s)
    {
        pushInBlocks (engine, oneSecond);
        const double target = std::min ((double) s + 1.0, AnalysisEngine::kMaxRecordSeconds) - 0.01;
        waitFor ([&] { return engine.recordedSeconds() >= target || ! engine.isRecording(); }, 20000);
    }

    CHECK (waitFor ([&] { return ! engine.isRecording(); }));
    CHECK (engine.droppedSamples() == 0);

    double rate = 0.0;
    std::shared_ptr<const std::vector<float>> rec;
    CHECK (waitFor ([&] { rec = engine.takeFinishedRecording (rate); return rec != nullptr; }));
    CHECK (rec != nullptr);
    if (rec != nullptr)
        CHECK (rec->size() == (size_t) (AnalysisEngine::kMaxRecordSeconds * 44100.0));
}
