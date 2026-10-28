#include "cutbench/Candidates.h"

#include <algorithm>
#include <cmath>

namespace cutbench
{
namespace
{
constexpr float kReferenceSalience = 0.20f; // roughly "a clear, single harmonic note"
}

float sensitivityToThreshold (float sensitivity) noexcept
{
    const float s = clampValue (sensitivity, 0.0f, 1.0f);
    return 0.12f * std::pow (0.1f, s);
}

float quickGrabConfidenceThreshold (float amount) noexcept
{
    const float a = clampValue (amount, 0.0f, 1.0f);
    return 0.85f + (0.05f - 0.85f) * a;
}

std::vector<Candidate> extractCandidates (const AnalysisResult& result, const CandidateParams& params)
{
    std::vector<Candidate> out;
    if (result.empty())
        return out;

    const AnalyzerConfig& cfg = result.cfg;
    const float threshold = sensitivityToThreshold (params.sensitivity);
    const double hopSec = double (cfg.hopSize) / cfg.sampleRate;
    const int gapFrames = std::max (1, (int) std::lround (params.maxGapSec / hopSec));
    const int minFrames = std::max (1, (int) std::lround (params.minDurationSec / hopSec));
    const double totalDuration = result.duration();

    const int lowMidi = std::max (params.lowMidi, cfg.minMidi);
    const int highMidi = std::min (params.highMidi, cfg.maxMidi);

    for (int midi = lowMidi; midi <= highMidi; ++midi)
    {
        int start = -1, lastActive = -1, count = 0;
        double sum = 0.0, ambiguitySum = 0.0;

        auto finish = [&]()
        {
            if (start >= 0)
            {
                // Trim weak edge frames: the log-compressed spectrum reacts to a note long before
                // it is really inside the analysis window, which would blur start/end times.
                float peak = 0.0f;
                for (int f = start; f <= lastActive; ++f)
                    peak = std::max (peak, result.refinedAt (f, midi));

                const float edgeLevel = clampValue (params.edgeTrim, 0.0f, 0.95f) * peak;
                int s = start, e = lastActive;
                while (s < e && result.refinedAt (s, midi) < edgeLevel) ++s;
                while (e > s && result.refinedAt (e, midi) < edgeLevel) --e;

                count = 0;
                sum = 0.0;
                ambiguitySum = 0.0;
                for (int f = s; f <= e; ++f)
                {
                    const float v = result.refinedAt (f, midi);
                    if (v < threshold)
                        continue;

                    ++count;
                    sum += v;

                    // Octave-below alias: ratio 0.5 is typical for a harmonic tone, close to 1 means
                    // the lower octave is nearly as plausible. Mapped so that <= 0.5 gives 0 ambiguity.
                    if (midi - 12 >= cfg.minMidi)
                    {
                        const float own = std::max (result.rawAt (f, midi), 1.0e-6f);
                        const float ratio = result.rawAt (f, midi - 12) / own;
                        ambiguitySum += clampValue ((ratio - 0.5f) / 0.4f, 0.0f, 1.0f);
                    }
                }

                if (count > 0 && e - s + 1 >= minFrames)
                {
                    Candidate c;
                    c.midi = midi;
                    // A frame stands for one hop around its centre time.
                    c.startSec = std::max (0.0, result.frameTime (s) - 0.5 * hopSec);
                    c.endSec = result.frameTime (e) + 0.5 * hopSec;
                    if (totalDuration > 0.0)
                        c.endSec = std::min (c.endSec, totalDuration);
                    c.endSec = std::max (c.endSec, c.startSec + 0.5 * hopSec);

                    c.strength = float (sum / count);
                    c.ambiguity = float (ambiguitySum / count);

                    const float durationFactor = clampValue ((float) c.duration() / 0.15f, 0.4f, 1.0f);
                    const float strengthFactor = clampValue (c.strength / kReferenceSalience, 0.0f, 1.0f);
                    c.confidence = clampValue (strengthFactor * (1.0f - 0.4f * c.ambiguity) * durationFactor, 0.0f, 1.0f);
                    out.push_back (c);
                }
            }

            start = -1;
            lastActive = -1;
            count = 0;
            sum = 0.0;
            ambiguitySum = 0.0;
        };

        for (int f = 0; f < result.numFrames; ++f)
        {
            const float v = result.refinedAt (f, midi);

            if (v >= threshold)
            {
                if (start < 0)
                    start = f;

                lastActive = f;
            }
            else if (start >= 0 && f - lastActive > gapFrames)
            {
                finish();
            }
        }

        if (start >= 0)
            finish();
    }

    // Spectral leakage: a strong note spills into its semitone neighbours (window sidelobes, onset
    // splatter). Drop a candidate if a neighbouring semitone that overlaps it in time is much stronger.
    if (params.leakageRatio > 0.0f)
    {
        std::vector<Candidate> kept;
        kept.reserve (out.size());

        for (const auto& c : out)
        {
            bool leakage = false;
            for (const auto& o : out)
            {
                if (std::abs (o.midi - c.midi) != 1)
                    continue;

                // The neighbour's interval is widened by ~half an analysis window: leakage from an
                // onset shows up slightly before / after the strong note's trimmed boundaries.
                const double slack = 0.1;
                const double overlap = std::min (c.endSec, o.endSec + slack) - std::max (c.startSec, o.startSec - slack);
                if (overlap >= 0.6 * c.duration() && c.strength < params.leakageRatio * o.strength)
                {
                    leakage = true;
                    break;
                }
            }

            if (! leakage)
                kept.push_back (c);
        }

        out.swap (kept);
    }

    std::sort (out.begin(), out.end(), [] (const Candidate& a, const Candidate& b)
               { return a.startSec != b.startSec ? a.startSec < b.startSec : a.midi < b.midi; });
    return out;
}

std::vector<Candidate> quickGrab (const std::vector<Candidate>& candidates, float amount)
{
    std::vector<Candidate> out;
    if (candidates.empty() || ! (amount > 0.0f))
        return out;

    const float minConfidence = quickGrabConfidenceThreshold (amount);
    for (const auto& c : candidates)
        if (c.confidence >= minConfidence)
            out.push_back (c);

    if (out.empty())
        out.push_back (*std::max_element (candidates.begin(), candidates.end(),
                                          [] (const Candidate& a, const Candidate& b)
                                          { return a.confidence < b.confidence; }));

    std::sort (out.begin(), out.end(), [] (const Candidate& a, const Candidate& b)
               { return a.startSec != b.startSec ? a.startSec < b.startSec : a.midi < b.midi; });
    return out;
}

Note candidateToNote (const Candidate& c)
{
    Note n;
    n.midi = clampValue (c.midi, 0, 127);
    n.startSec = std::max (0.0, c.startSec);
    n.durationSec = std::max (kMinNoteDurationSec, c.duration());
    const float level = clampValue (c.strength / kReferenceSalience, 0.0f, 1.0f);
    n.velocity = clampValue ((int) std::lround (40.0f + 80.0f * level), 1, 127);
    return n;
}

std::vector<FramePick> frameTopNotes (const AnalysisResult& result, int frame, int maxCount)
{
    std::vector<FramePick> picks;
    if (frame < 0 || frame >= result.numFrames || maxCount <= 0)
        return picks;

    for (int midi = result.cfg.minMidi; midi <= result.cfg.maxMidi; ++midi)
    {
        const float refined = result.refinedAt (frame, midi);
        if (refined > 0.0f)
            picks.push_back ({ midi, result.rawAt (frame, midi), refined });
    }

    std::sort (picks.begin(), picks.end(), [] (const FramePick& a, const FramePick& b)
               { return a.refined > b.refined; });

    if ((int) picks.size() > maxCount)
        picks.resize ((size_t) maxCount);
    return picks;
}
} // namespace cutbench
