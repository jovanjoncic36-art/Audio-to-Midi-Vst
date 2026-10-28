#include "cutbench/PreviewSynth.h"
#include "cutbench/PitchMath.h"

#include <algorithm>
#include <cmath>

namespace cutbench
{
namespace
{
constexpr double kTwoPi = 6.283185307179586476925286766559;
}

void PreviewSynth::prepare (double sampleRate) noexcept
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    attackStep = (float) (1.0 / (0.005 * sr));                    // 5 ms attack
    releaseCoef = (float) std::exp (-1.0 / (0.025 * sr));          // ~25 ms release time constant
    allNotesOff();
    for (auto& v : voices)
        v = Voice();
    counter = 0;
}

void PreviewSynth::noteOn (int midi, int velocity) noexcept
{
    midi = clampValue (midi, 0, 127);
    velocity = clampValue (velocity, 1, 127);

    // Retrigger: release a sounding voice of the same pitch first.
    noteOff (midi);

    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.active)
        {
            target = &v;
            break;
        }

    if (target == nullptr) // steal: quietest releasing voice, else the oldest
    {
        for (auto& v : voices)
            if (v.releasing && (target == nullptr || v.env < target->env))
                target = &v;

        if (target == nullptr)
            for (auto& v : voices)
                if (target == nullptr || v.age < target->age)
                    target = &v;
    }

    const double hz = midiToHz (midi);
    target->active = true;
    target->releasing = false;
    target->midi = midi;
    target->phase = 0.0;
    target->increment = kTwoPi * hz / sr;
    target->gain = 0.14f * std::pow ((float) velocity / 127.0f, 1.5f);
    target->env = 0.0f;
    target->age = ++counter;
}

void PreviewSynth::noteOff (int midi) noexcept
{
    for (auto& v : voices)
        if (v.active && ! v.releasing && v.midi == midi)
            v.releasing = true;
}

void PreviewSynth::allNotesOff() noexcept
{
    for (auto& v : voices)
        if (v.active)
            v.releasing = true;
}

int PreviewSynth::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        n += v.active ? 1 : 0;
    return n;
}

void PreviewSynth::render (float* out, int numSamples) noexcept
{
    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        for (int i = 0; i < numSamples; ++i)
        {
            if (v.releasing)
            {
                v.env *= releaseCoef;
                if (v.env < 0.0005f)
                {
                    v.active = false;
                    break;
                }
            }
            else if (v.env < 1.0f)
            {
                v.env = std::min (1.0f, v.env + attackStep);
            }

            const float p = (float) v.phase;
            const float s = (std::sin (p) + 0.35f * std::sin (2.0f * p) + 0.15f * std::sin (3.0f * p)) * (1.0f / 1.5f);
            out[i] += s * v.gain * v.env;

            v.phase += v.increment;
            if (v.phase >= kTwoPi)
                v.phase -= kTwoPi;
        }
    }
}

//==============================================================================
void PatternPreview::prepare (double sampleRate) noexcept
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    synth.prepare (sr);
}

void PatternPreview::reset() noexcept
{
    synth.allNotesOff();
}

void PatternPreview::render (const PreviewNote* notes, size_t numNotes, long long startSample,
                             int numSamples, float* out) noexcept
{
    struct Event
    {
        int offset;
        int order; // 0 = off, 1 = on
        int midi;
        int velocity;
    };

    constexpr int kMaxEvents = 512;
    Event events[kMaxEvents];
    int count = 0;

    const long long endSample = startSample + numSamples;

    for (size_t i = 0; i < numNotes && count < kMaxEvents - 1; ++i)
    {
        const long long on = std::llround (notes[i].startSec * sr);
        long long off = std::llround (notes[i].endSec * sr);
        if (off <= on)
            off = on + 1;

        if (on >= startSample && on < endSample)
            events[count++] = { (int) (on - startSample), 1, notes[i].midi, notes[i].velocity };

        if (off >= startSample && off < endSample && count < kMaxEvents)
            events[count++] = { (int) (off - startSample), 0, notes[i].midi, 0 };
    }

    std::sort (events, events + count, [] (const Event& a, const Event& b)
               { return a.offset != b.offset ? a.offset < b.offset : a.order < b.order; });

    int cursor = 0;
    for (int e = 0; e < count; ++e)
    {
        if (events[e].offset > cursor)
        {
            synth.render (out + cursor, events[e].offset - cursor);
            cursor = events[e].offset;
        }

        if (events[e].order == 1)
            synth.noteOn (events[e].midi, events[e].velocity);
        else
            synth.noteOff (events[e].midi);
    }

    if (cursor < numSamples)
        synth.render (out + cursor, numSamples - cursor);
}
} // namespace cutbench
