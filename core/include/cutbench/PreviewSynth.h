#pragma once
// Tiny polyphonic preview synthesiser plus a sample-accurate pattern scheduler.
// Allocation-free after construction and safe to call from an audio callback. No JUCE dependency.

#include <cstddef>
#include <cstdint>

namespace cutbench
{
struct PreviewNote
{
    int midi = 60;
    int velocity = 100;
    double startSec = 0.0;
    double endSec = 0.25;
};

class PreviewSynth
{
public:
    static constexpr int kVoices = 16;

    void prepare (double sampleRate) noexcept;
    void noteOn (int midi, int velocity) noexcept;
    void noteOff (int midi) noexcept;
    void allNotesOff() noexcept;

    // Adds the synth output to `out` (does not clear it).
    void render (float* out, int numSamples) noexcept;

    int activeVoices() const noexcept;

private:
    struct Voice
    {
        bool active = false;
        bool releasing = false;
        int midi = 0;
        double phase = 0.0;
        double increment = 0.0;
        float gain = 0.0f;
        float env = 0.0f;
        uint64_t age = 0;
    };

    Voice voices[kVoices];
    double sr = 44100.0;
    float attackStep = 0.01f;
    float releaseCoef = 0.999f;
    uint64_t counter = 0;
};

// Plays a list of notes (seconds) through a PreviewSynth with sample-accurate note on/off.
class PatternPreview
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept; // all notes off (call when seeking or stopping)

    // Adds the pattern's audio for samples [startSample, startSample + numSamples) to `out`.
    // Blocks must be rendered consecutively; call reset() before a jump.
    void render (const PreviewNote* notes, size_t numNotes, long long startSample,
                 int numSamples, float* out) noexcept;

    int activeVoices() const noexcept { return synth.activeVoices(); }

private:
    PreviewSynth synth;
    double sr = 44100.0;
};
} // namespace cutbench
