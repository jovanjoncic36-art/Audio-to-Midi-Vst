#include "TestFramework.h"
#include "TestSignals.h"
#include "cutbench/Candidates.h"
#include "cutbench/PitchAnalyzer.h"
#include "cutbench/PreviewSynth.h"

#include <cmath>
#include <vector>

using namespace cutbench;

namespace
{
std::vector<float> renderPattern (const std::vector<PreviewNote>& notes, double sr, int totalSamples, int blockSize)
{
    PatternPreview preview;
    preview.prepare (sr);

    std::vector<float> out ((size_t) totalSamples, 0.0f);
    for (int pos = 0; pos < totalSamples; pos += blockSize)
        preview.render (notes.data(), notes.size(), pos, std::min (blockSize, totalSamples - pos), out.data() + pos);
    return out;
}
} // namespace

TEST (preview_pattern_sounds_at_the_right_pitches_and_times)
{
    const double sr = 44100.0;
    std::vector<PreviewNote> notes = { { 60, 100, 0.2, 0.9 }, { 67, 100, 1.1, 1.8 } };
    const auto audio = renderPattern (notes, sr, (int) (2.0 * sr), 512);

    // feed the preview back into the analyser: it must hear what the pattern says
    const auto result = PitchAnalyzer::analyzeBuffer (audio.data(), audio.size(), AnalyzerConfig::detailed (sr));
    CandidateParams p;
    p.lowMidi = kMidiC0;
    p.highMidi = kMidiC8;
    const auto cands = extractCandidates (result, p);

    const Candidate* c4 = nullptr;
    const Candidate* g4 = nullptr;
    for (const auto& c : cands)
    {
        if (c.midi == 60) c4 = &c;
        if (c.midi == 67) g4 = &c;
    }

    CHECK (c4 != nullptr);
    CHECK (g4 != nullptr);
    if (c4 != nullptr) { CHECK_NEAR (c4->startSec, 0.2, 0.09); CHECK_NEAR (c4->endSec, 0.9, 0.12); }
    if (g4 != nullptr) { CHECK_NEAR (g4->startSec, 1.1, 0.09); CHECK_NEAR (g4->endSec, 1.8, 0.12); }

    // silence before the first note
    float maxBefore = 0.0f;
    for (int i = 0; i < (int) (0.19 * sr); ++i)
        maxBefore = std::max (maxBefore, std::fabs (audio[(size_t) i]));
    CHECK (maxBefore == 0.0f);
}

TEST (preview_output_is_independent_of_block_size)
{
    const double sr = 48000.0;
    std::vector<PreviewNote> notes;
    for (int i = 0; i < 12; ++i)
        notes.push_back ({ 48 + i * 2, 40 + i * 6, 0.05 * i, 0.05 * i + 0.4 });

    const int total = (int) (1.5 * sr);
    const auto reference = renderPattern (notes, sr, total, 4096);

    for (int block : { 1, 7, 64, 509, 1000 })
    {
        const auto other = renderPattern (notes, sr, total, block);
        bool identical = true;
        for (size_t i = 0; i < reference.size(); ++i)
            identical = identical && (reference[i] == other[i]);

        if (! identical)
            std::printf ("    block size %d differs\n", block);
        CHECK (identical);
    }
}

TEST (preview_is_finite_bounded_and_survives_extreme_input)
{
    const double sr = 44100.0;
    std::vector<PreviewNote> notes;

    // 40 simultaneous notes (more than voices), invalid pitches / velocities / negative and zero lengths
    for (int i = 0; i < 40; ++i)
        notes.push_back ({ 30 + i, 127, 0.0, 1.0 });
    notes.push_back ({ -50, 0, 0.1, 0.1 });
    notes.push_back ({ 500, 9999, -1.0, -2.0 });
    notes.push_back ({ 60, 100, 0.5, 0.5 }); // zero length

    const auto audio = renderPattern (notes, sr, (int) (1.5 * sr), 333);

    float peak = 0.0f;
    for (float s : audio)
    {
        CHECK (std::isfinite (s));
        peak = std::max (peak, std::fabs (s));
    }
    CHECK (peak > 0.01f);
    CHECK (peak < 2.5f);

    // empty pattern renders silence and does nothing
    const auto silent = renderPattern ({}, sr, 4410, 128);
    for (float s : silent)
        CHECK (s == 0.0f);
}

TEST (preview_voices_release_after_notes_end)
{
    PatternPreview preview;
    preview.prepare (44100.0);
    std::vector<PreviewNote> notes = { { 60, 100, 0.0, 0.1 }, { 64, 100, 0.0, 0.1 } };
    std::vector<float> out (44100, 0.0f);

    preview.render (notes.data(), notes.size(), 0, 44100, out.data());
    CHECK (preview.activeVoices() == 0); // released and faded out within one second

    // reset() releases everything immediately
    std::vector<PreviewNote> held = { { 60, 100, 0.0, 100.0 } };
    preview.render (held.data(), held.size(), 0, 1000, out.data());
    CHECK (preview.activeVoices() == 1);
    preview.reset();
    std::vector<float> tail (44100, 0.0f);
    preview.render (nullptr, 0, 1000, 44100, tail.data());
    CHECK (preview.activeVoices() == 0);
}
