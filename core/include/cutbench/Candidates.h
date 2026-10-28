#pragma once
// Turns the frame-wise note picks of an AnalysisResult into candidate notes (segments over time),
// scores them, and implements the one-knob "QuickGrab" selection.
//
// IMPORTANT: `confidence` is a heuristic ranking score in 0..1, NOT a probability of being correct.
// It combines how strong the harmonic evidence is, how long the note lasts, and whether an octave
// alias (n-12) is almost as plausible. Chords, shared partials, noise and missing fundamentals can
// all fool the estimator, so results must be reviewed by ear before they are used.

#include "cutbench/NoteEditor.h"
#include "cutbench/PitchAnalyzer.h"

#include <vector>

namespace cutbench
{
struct CandidateParams
{
    float sensitivity = 0.6f;       // 0..1, higher = weaker notes are accepted
    int lowMidi = 24;               // pitch range filter (inclusive)
    int highMidi = 96;
    double minDurationSec = 0.06;   // shorter segments are discarded
    double maxGapSec = 0.05;        // gaps up to this length are bridged (temporal smoothing)
    float edgeTrim = 0.5f;          // leading/trailing frames weaker than this fraction of the segment peak
                                    // are cut off (the log-compressed spectrum reacts to onsets very early)
    float leakageRatio = 0.45f;     // a candidate is dropped as spectral leakage if a semitone neighbour
                                    // overlapping it in time is stronger by more than 1/leakageRatio (0 = off)
};

struct Candidate
{
    int midi = 60;
    double startSec = 0.0;
    double endSec = 0.0;
    float strength = 0.0f;   // mean refined salience over active frames (absolute, ~0.2 = clear note)
    float ambiguity = 0.0f;  // 0..1, how plausible an octave-below alias is
    float confidence = 0.0f; // 0..1 heuristic score, see above

    double duration() const noexcept { return endSec - startSec; }
    bool isUncertain() const noexcept { return confidence < 0.35f || ambiguity > 0.5f; }
};

// Salience threshold used for a sensitivity value (exponential: 0 -> 0.12, 1 -> 0.012).
float sensitivityToThreshold (float sensitivity) noexcept;

// All candidates passing the sensitivity threshold, minimum duration and pitch range, sorted by start time.
std::vector<Candidate> extractCandidates (const AnalysisResult& result, const CandidateParams& params);

// QuickGrab: amount in 0..1. 0 selects nothing; higher values admit lower-confidence candidates.
// The best candidate is always included for amount > 0. Result sorted by start time.
std::vector<Candidate> quickGrab (const std::vector<Candidate>& candidates, float amount);

// Minimum confidence a candidate needs at a given QuickGrab amount.
float quickGrabConfidenceThreshold (float amount) noexcept;

// Candidate -> pattern note (velocity derived from strength: 40..120).
Note candidateToNote (const Candidate& c);

// Strongest picks in one frame, for the "inspect at time position" readout. Sorted by refined strength.
struct FramePick
{
    int midi = 0;
    float raw = 0.0f;
    float refined = 0.0f;
};

std::vector<FramePick> frameTopNotes (const AnalysisResult& result, int frame, int maxCount);
} // namespace cutbench
