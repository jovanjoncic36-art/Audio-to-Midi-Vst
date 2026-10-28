#include "TestFramework.h"
#include "TestSignals.h"
#include "cutbench/Candidates.h"
#include "cutbench/PitchAnalyzer.h"

#include <atomic>
#include <limits>
#include <stdexcept>

using namespace cutbench;

namespace
{
constexpr double kSr = 44100.0;

CandidateParams fullRange()
{
    CandidateParams p;
    p.lowMidi = kMidiC0;
    p.highMidi = kMidiC8;
    return p;
}

AnalysisResult analyse (const std::vector<float>& x, const AnalyzerConfig& cfg)
{
    return PitchAnalyzer::analyzeBuffer (x.data(), x.size(), cfg);
}

const Candidate* find (const std::vector<Candidate>& c, int midi)
{
    for (const auto& k : c)
        if (k.midi == midi)
            return &k;
    return nullptr;
}
} // namespace

TEST (analysis_empty_and_degenerate_input)
{
    const auto cfg = AnalyzerConfig::detailed (kSr);

    const AnalysisResult empty = PitchAnalyzer::analyzeBuffer (nullptr, 0, cfg);
    CHECK (empty.empty());
    CHECK (extractCandidates (empty, fullRange()).empty());
    CHECK (empty.frameAtTime (1.0) == -1);

    // a single sample, and audio much shorter than the FFT window
    const std::vector<float> one { 0.5f };
    CHECK (analyse (one, cfg).numFrames >= 1);

    auto shortTone = sig::silence (kSr, 0.02);
    sig::addTone (shortTone, kSr, 440.0, 0.0, 0.02, 0.5);
    const AnalysisResult shortResult = analyse (shortTone, cfg);
    CHECK (shortResult.numFrames >= 1);
    CHECK (shortResult.raw.size() == (size_t) shortResult.numFrames * (size_t) cfg.numNotes());
    extractCandidates (shortResult, fullRange()); // must not crash
}

TEST (analysis_invalid_samples_do_not_poison_result)
{
    auto x = sig::silence (kSr, 1.0);
    sig::addTone (x, kSr, 440.0, 0.0, 1.0, 0.5, 6);
    x[10000] = std::numeric_limits<float>::quiet_NaN();
    x[20000] = std::numeric_limits<float>::infinity();
    x[30000] = -std::numeric_limits<float>::infinity();

    const AnalysisResult r = analyse (x, AnalyzerConfig::detailed (kSr));
    for (float v : r.raw)
        CHECK (std::isfinite (v));
    for (float v : r.refined)
        CHECK (std::isfinite (v));
    for (float v : r.level)
        CHECK (std::isfinite (v));

    CHECK (find (extractCandidates (r, fullRange()), 69) != nullptr);
}

TEST (analysis_invalid_configs_throw)
{
    bool threw = false;
    try { auto c = AnalyzerConfig::detailed (kSr); c.fftSize = 1000; PitchAnalyzer a (c); }
    catch (const std::invalid_argument&) { threw = true; }
    CHECK (threw);

    threw = false;
    try { auto c = AnalyzerConfig::detailed (kSr); c.hopSize = 0; PitchAnalyzer a (c); }
    catch (const std::invalid_argument&) { threw = true; }
    CHECK (threw);

    threw = false;
    try { auto c = AnalyzerConfig::detailed (0.0); PitchAnalyzer a (c); }
    catch (const std::invalid_argument&) { threw = true; }
    CHECK (threw);
}

TEST (analysis_silence_gives_nothing)
{
    const auto x = sig::silence (kSr, 1.0);
    const AnalysisResult r = analyse (x, AnalyzerConfig::detailed (kSr));
    CHECK (r.numFrames > 0);
    CHECK (extractCandidates (r, fullRange()).empty());

    for (float v : r.raw)
        CHECK (v == 0.0f);
}

TEST (analysis_pure_sine_finds_the_note_and_flags_octave_ambiguity)
{
    auto x = sig::silence (kSr, 1.0);
    sig::addTone (x, kSr, 440.0, 0.0, 1.0, 0.5, 1);

    const auto cands = extractCandidates (analyse (x, AnalyzerConfig::detailed (kSr)), fullRange());
    CHECK (cands.size() == 1);

    const Candidate* a4 = find (cands, 69);
    CHECK (a4 != nullptr);
    if (a4 != nullptr)
    {
        CHECK_NEAR (a4->startSec, 0.0, 0.08);
        CHECK_NEAR (a4->endSec, 1.0, 0.08);
        CHECK (a4->confidence > 0.4f);
        // A lone sine is also consistent with a lower note whose fundamental is missing:
        // the engine must say so instead of reporting full certainty.
        CHECK (a4->ambiguity > 0.5f);
        CHECK (a4->confidence < 0.9f);
    }
}

TEST (analysis_harmonic_notes_across_the_range)
{
    for (int midi : { 36, 40, 45, 52, 57, 60, 64, 69, 76, 81, 88, 93 })
    {
        auto x = sig::silence (kSr, 1.0);
        sig::addTone (x, kSr, midiToHz (midi), 0.0, 1.0, 0.4, 8, 1.0);

        const auto cands = extractCandidates (analyse (x, AnalyzerConfig::detailed (kSr)), fullRange());

        // the highest-confidence candidate must be the true note
        const Candidate* best = nullptr;
        for (const auto& c : cands)
            if (best == nullptr || c.confidence > best->confidence)
                best = &c;

        if (best == nullptr || best->midi != midi)
        {
            std::printf ("    note %s: best candidate %s\n", midiToName (midi).c_str(),
                         best != nullptr ? midiToName (best->midi).c_str() : "none");
            CHECK (best != nullptr && best->midi == midi);
        }
    }
}

TEST (analysis_c_major_chord_is_resolved_into_three_notes)
{
    auto x = sig::silence (kSr, 1.5);
    for (int midi : { 60, 64, 67 })
        sig::addTone (x, kSr, midiToHz (midi), 0.0, 1.5, 0.25, 6, 1.0);

    const auto cands = extractCandidates (analyse (x, AnalyzerConfig::detailed (kSr)), fullRange());
    CHECK (find (cands, 60) != nullptr);
    CHECK (find (cands, 64) != nullptr);
    CHECK (find (cands, 67) != nullptr);
    CHECK (cands.size() == 3); // no octave / harmonic ghosts for this simple chord
}

TEST (analysis_note_timing_is_within_expected_resolution)
{
    // Note from 0.5 s to 1.5 s inside 2 s of silence.
    auto x = sig::silence (kSr, 2.0);
    sig::addTone (x, kSr, midiToHz (57), 0.5, 1.5, 0.4, 6, 1.0);

    const auto cands = extractCandidates (analyse (x, AnalyzerConfig::detailed (kSr)), fullRange());
    const Candidate* a3 = find (cands, 57);
    CHECK (a3 != nullptr);
    if (a3 != nullptr)
    {
        // Detailed config: 8192-sample window (186 ms). Documented accuracy is about +-60 ms.
        CHECK_NEAR (a3->startSec, 0.5, 0.06);
        CHECK_NEAR (a3->endSec, 1.5, 0.06);
    }
}

TEST (analysis_melody_gives_notes_in_order)
{
    auto x = sig::silence (kSr, 1.5);
    const int midis[3] = { 60, 64, 67 };
    for (int i = 0; i < 3; ++i)
        sig::addTone (x, kSr, midiToHz (midis[i]), 0.5 * i, 0.5 * (i + 1), 0.4, 6, 1.0);

    const auto cands = extractCandidates (analyse (x, AnalyzerConfig::detailed (kSr)), fullRange());

    for (int i = 0; i < 3; ++i)
    {
        const Candidate* c = find (cands, midis[i]);
        CHECK (c != nullptr);
        if (c != nullptr)
        {
            CHECK_NEAR (c->startSec, 0.5 * i, 0.09);
            CHECK_NEAR (c->endSec, 0.5 * (i + 1), 0.09);
        }
    }
}

TEST (analysis_noise_does_not_produce_confident_notes)
{
    for (double amp : { 0.05, 0.1, 0.5 })
    {
        const auto x = sig::whiteNoise (kSr, 2.0, amp);
        const auto cands = extractCandidates (analyse (x, AnalyzerConfig::detailed (kSr)), fullRange());

        for (const auto& c : cands)
            CHECK (c.confidence < 0.35f);

        // QuickGrab at a moderate setting must not hand noise to the user as notes.
        CHECK (quickGrab (cands, 0.5f).size() <= 1);
    }
}

TEST (analysis_streaming_matches_offline)
{
    auto x = sig::silence (kSr, 1.0);
    sig::addTone (x, kSr, midiToHz (62), 0.1, 0.9, 0.3, 6, 1.0);

    const auto cfg = AnalyzerConfig::live (kSr);
    const AnalysisResult offline = analyse (x, cfg);

    // feed in awkward chunk sizes
    PitchAnalyzer streaming (cfg);
    AnalysisResult live;
    live.cfg = cfg;
    size_t pos = 0;
    const int chunks[] = { 1, 7, 513, 64, 1000, 3 };
    size_t ci = 0;
    while (pos < x.size())
    {
        const int n = (int) std::min<size_t> (chunks[ci++ % 6], x.size() - pos);
        streaming.process (x.data() + pos, n, live);
        pos += (size_t) n;
    }

    CHECK (live.numFrames > 0);
    CHECK (live.numFrames <= offline.numFrames);
    CHECK ((long long) live.numSamples == (long long) x.size());

    const size_t count = (size_t) live.numFrames * (size_t) cfg.numNotes();
    for (size_t i = 0; i < count; ++i)
    {
        CHECK_NEAR (live.raw[i], offline.raw[i], 1.0e-5);
        CHECK_NEAR (live.refined[i], offline.refined[i], 1.0e-5);
    }
}

TEST (analysis_trim_front_keeps_absolute_time)
{
    auto x = sig::silence (kSr, 2.0);
    sig::addTone (x, kSr, 440.0, 0.0, 2.0, 0.4, 6);
    AnalysisResult r = analyse (x, AnalyzerConfig::live (kSr));

    const int frames = r.numFrames;
    const double t40 = r.frameTime (40);
    const float v40 = r.rawAt (40, 69);

    r.trimFront (30);
    CHECK (r.numFrames == frames - 30);
    CHECK (r.firstFrameIndex == 30);
    CHECK_NEAR (r.frameTime (10), t40, 1.0e-12);
    CHECK_NEAR (r.rawAt (10, 69), v40, 1.0e-12);
    CHECK (r.frameAtTime (t40) == 10);

    r.trimFront (100000); // more than available -> clears, no crash
    CHECK (r.numFrames == 0);
}

TEST (analysis_smoothing_behaves)
{
    const int frames = 5, notes = 3;
    std::vector<float> m ((size_t) frames * notes, 0.0f);
    m[(size_t) 2 * notes + 1] = 1.0f; // impulse

    const auto s0 = smoothAlongTime (m, frames, notes, 0.0f);
    CHECK (s0 == m);

    const auto s1 = smoothAlongTime (m, frames, notes, 1.0f);
    CHECK_NEAR (s1[(size_t) 2 * notes + 1], 0.0, 1e-6);
    CHECK_NEAR (s1[(size_t) 1 * notes + 1], 0.5, 1e-6);
    CHECK_NEAR (s1[(size_t) 3 * notes + 1], 0.5, 1e-6);

    std::vector<float> flat ((size_t) frames * notes, 0.3f);
    for (float v : smoothAlongTime (flat, frames, notes, 0.7f))
        CHECK_NEAR (v, 0.3, 1e-6);
}

TEST (analysis_cancel_flag_stops_early)
{
    const auto x = sig::whiteNoise (kSr, 5.0, 0.2);
    std::atomic<bool> cancel { true };
    const AnalysisResult r = PitchAnalyzer::analyzeBuffer (x.data(), x.size(), AnalyzerConfig::detailed (kSr), &cancel);
    CHECK (r.numFrames == 0);
}

TEST (quickgrab_is_monotonic_sorted_and_keeps_best)
{
    auto x = sig::silence (kSr, 1.5);
    for (int midi : { 60, 64, 67 })
        sig::addTone (x, kSr, midiToHz (midi), 0.0, 1.5, 0.25, 6, 1.0);

    auto params = fullRange();
    params.sensitivity = 0.9f;
    const auto cands = extractCandidates (analyse (x, AnalyzerConfig::detailed (kSr)), params);
    CHECK (cands.size() >= 3);
    CHECK (quickGrab (cands, 0.0f).empty());

    size_t previous = 0;
    for (float amount = 0.05f; amount <= 1.0001f; amount += 0.05f)
    {
        const auto picked = quickGrab (cands, amount);
        CHECK (! picked.empty()); // best candidate is always kept for amount > 0
        CHECK (picked.size() >= previous);
        previous = picked.size();

        for (size_t i = 1; i < picked.size(); ++i)
            CHECK (picked[i].startSec >= picked[i - 1].startSec);
    }
    CHECK (previous == cands.size()); // everything is admitted at 100 %

    CHECK (quickGrab ({}, 0.5f).empty());
}

TEST (quickgrab_threshold_logic_with_known_confidences)
{
    std::vector<Candidate> list;
    const float confs[5] = { 0.95f, 0.60f, 0.40f, 0.20f, 0.08f };
    for (int i = 0; i < 5; ++i)
    {
        Candidate c;
        c.midi = 60 + i;
        c.startSec = 0.1 * i;
        c.endSec = c.startSec + 0.5;
        c.confidence = confs[i];
        list.push_back (c);
    }

    CHECK (quickGrabConfidenceThreshold (0.0f) > quickGrabConfidenceThreshold (0.5f));
    CHECK (quickGrabConfidenceThreshold (0.5f) > quickGrabConfidenceThreshold (1.0f));
    CHECK_NEAR (quickGrabConfidenceThreshold (0.0f), 0.85, 1e-6);
    CHECK_NEAR (quickGrabConfidenceThreshold (1.0f), 0.05, 1e-6);
    CHECK_NEAR (quickGrabConfidenceThreshold (2.0f), 0.05, 1e-6); // clamped

    CHECK (quickGrab (list, 0.0f).empty());
    CHECK (quickGrab (list, -1.0f).empty());
    CHECK (quickGrab (list, 0.02f).size() == 1);  // threshold 0.834 -> only 0.95
    CHECK (quickGrab (list, 0.5f).size() == 2);   // threshold 0.45 -> 0.95, 0.60
    CHECK (quickGrab (list, 0.8f).size() == 3);   // threshold 0.21 -> + 0.40
    CHECK (quickGrab (list, 0.9f).size() == 4);   // threshold 0.13 -> 0.95, 0.60, 0.40, 0.20
    CHECK (quickGrab (list, 1.0f).size() == 5);   // threshold 0.05 -> everything, including 0.08

    // a list where nothing reaches the threshold still yields its single best candidate
    std::vector<Candidate> weak (2);
    weak[0].confidence = 0.10f; weak[0].midi = 50;
    weak[1].confidence = 0.30f; weak[1].midi = 52;
    const auto picked = quickGrab (weak, 0.1f);
    CHECK (picked.size() == 1 && picked[0].midi == 52);
}

TEST (sensitivity_changes_candidate_count_monotonically)
{
    auto x = sig::silence (kSr, 1.0);
    sig::addTone (x, kSr, midiToHz (60), 0.0, 1.0, 0.3, 6, 1.0);
    sig::addTone (x, kSr, midiToHz (67), 0.0, 1.0, 0.01, 6, 1.0);
    const auto r = analyse (x, AnalyzerConfig::detailed (kSr));

    size_t previous = 0;
    for (float s = 0.0f; s <= 1.0001f; s += 0.1f)
    {
        auto p = fullRange();
        p.sensitivity = s;
        const size_t n = extractCandidates (r, p).size();
        CHECK (n >= previous);
        previous = n;
    }

    CHECK (sensitivityToThreshold (0.0f) > sensitivityToThreshold (1.0f));
}

TEST (candidates_respect_pitch_range_filter)
{
    auto x = sig::silence (kSr, 1.0);
    sig::addTone (x, kSr, midiToHz (48), 0.0, 1.0, 0.3, 6, 1.0);
    sig::addTone (x, kSr, midiToHz (72), 0.0, 1.0, 0.3, 6, 1.0);
    const auto r = analyse (x, AnalyzerConfig::detailed (kSr));

    auto p = fullRange();
    p.lowMidi = 60;
    p.highMidi = 84;
    const auto cands = extractCandidates (r, p);
    CHECK (find (cands, 72) != nullptr);
    CHECK (find (cands, 48) == nullptr);
}

TEST (candidate_to_note_gives_valid_note)
{
    Candidate c;
    c.midi = 200;
    c.startSec = -1.0;
    c.endSec = -1.0;
    c.strength = 5.0f;

    const Note n = candidateToNote (c);
    CHECK (n.midi == 127);
    CHECK (n.startSec >= 0.0);
    CHECK (n.durationSec >= kMinNoteDurationSec);
    CHECK (n.velocity >= 1 && n.velocity <= 127);

    Candidate weak;
    weak.endSec = 1.0;
    weak.strength = 0.0f;
    const Note w = candidateToNote (weak);
    CHECK (w.velocity >= 1 && w.velocity < n.velocity);
}

TEST (frame_inspection_lists_strongest_notes)
{
    auto x = sig::silence (kSr, 1.5);
    for (int midi : { 60, 64, 67 })
        sig::addTone (x, kSr, midiToHz (midi), 0.0, 1.5, 0.25, 6, 1.0);
    const auto r = analyse (x, AnalyzerConfig::detailed (kSr));

    const auto picks = frameTopNotes (r, r.frameAtTime (0.75), 5);
    CHECK (picks.size() == 3);
    for (size_t i = 1; i < picks.size(); ++i)
        CHECK (picks[i - 1].refined >= picks[i].refined);

    CHECK (frameTopNotes (r, -1, 5).empty());
    CHECK (frameTopNotes (r, r.numFrames, 5).empty());
    CHECK (frameTopNotes (r, 0, 0).empty());
}
