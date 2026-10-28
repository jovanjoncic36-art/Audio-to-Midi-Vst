#pragma once
// Threading around the analyser. No JUCE dependency (std::thread / std::atomic only), so the
// real-time behaviour can be unit-tested on any platform.
//
// Threads and rules:
//   audio thread   : pushAudio() only. Lock-free, wait-free, no allocation, no I/O. Drops samples
//                    (and counts them) if the analysis thread falls behind.
//   analysis thread: drains the FIFO, runs the streaming analyser for the live view, captures the
//                    recording, publishes immutable snapshots.
//   worker thread  : one offline analysis job at a time (recorded passage or loaded file).
//   message thread : everything else (start/stop recording, fetch snapshots).

#include "cutbench/PitchAnalyzer.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cutbench
{
// Single-producer / single-consumer lock-free ring buffer of floats.
class SpscFloatFifo
{
public:
    explicit SpscFloatFifo (size_t capacityPowerOfTwo);

    // Producer side. Returns the number of samples actually stored (may be < n when full).
    size_t write (const float* src, size_t n) noexcept;
    // Consumer side. Returns the number of samples read.
    size_t read (float* dst, size_t n) noexcept;

    size_t capacity() const noexcept { return buffer.size(); }
    size_t available() const noexcept;

private:
    std::vector<float> buffer;
    size_t mask = 0;
    std::atomic<size_t> writeIndex { 0 }, readIndex { 0 };
};

class AnalysisEngine
{
public:
    static constexpr double kMaxRecordSeconds = 120.0;
    static constexpr double kLiveWindowSeconds = 30.0;

    AnalysisEngine();
    ~AnalysisEngine();

    AnalysisEngine (const AnalysisEngine&) = delete;
    AnalysisEngine& operator= (const AnalysisEngine&) = delete;

    // Any thread (not the audio callback). Restarts the live view when the rate changes.
    void setSampleRate (double sampleRate);

    // AUDIO THREAD. `mono` holds already down-mixed samples.
    void pushAudio (const float* mono, int numSamples) noexcept;

    // --- recording (message thread) ---
    void startRecording();
    void stopRecording();
    bool isRecording() const noexcept { return recordingActive.load(); }
    double recordedSeconds() const noexcept;
    // Returns the finished recording once (null when there is none). Rate = sample rate it was captured at.
    std::shared_ptr<const std::vector<float>> takeFinishedRecording (double& sampleRateOut);

    // --- offline analysis (message thread) ---
    // Starts a detailed analysis in the background; replaces any running job.
    void analysePassage (std::shared_ptr<const std::vector<float>> mono, double sampleRate);
    bool isAnalysing() const noexcept { return analysing.load(); }

    // --- results (any thread except audio) ---
    std::shared_ptr<const AnalysisResult> liveSnapshot() const;
    uint32_t liveVersion() const noexcept { return liveVer.load(); }
    std::shared_ptr<const AnalysisResult> passageResult() const;
    uint32_t passageVersion() const noexcept { return passageVer.load(); }

    uint64_t droppedSamples() const noexcept { return dropped.load(); }
    std::string lastError() const;

private:
    void analysisLoop();
    void publishLive (const AnalysisResult& live);
    void setError (const std::string& message);

    SpscFloatFifo fifo;
    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<uint64_t> dropped { 0 };

    std::atomic<bool> quit { false };
    std::atomic<int> recordCommand { 0 }; // 1 = start, 2 = stop
    std::atomic<bool> recordingActive { false };
    std::atomic<uint64_t> recordedCount { 0 };
    std::atomic<double> recordedRate { 44100.0 };
    std::thread analysisThread;

    mutable std::mutex resultMutex;
    std::shared_ptr<const AnalysisResult> liveSnap, passageSnap;
    std::shared_ptr<const std::vector<float>> finishedRecording;
    double finishedRecordingRate = 44100.0;
    std::string errorText;
    std::atomic<uint32_t> liveVer { 0 }, passageVer { 0 };

    std::mutex jobMutex;
    std::thread worker;
    std::atomic<bool> cancelJob { false };
    std::atomic<bool> analysing { false };
};
} // namespace cutbench
