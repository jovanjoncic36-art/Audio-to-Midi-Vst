#pragma once
// Pitch / note-number helpers. No dependencies except the C++ standard library.
//
// Convention: MIDI 60 == C4 (scientific pitch notation), so C0 == 12 and C8 == 108.
// NOTE: Ableton Live labels MIDI 60 as "C3". The plugin UI uses C4 == 60 and says so in the README.

#include <algorithm>
#include <cmath>
#include <string>

namespace cutbench
{
constexpr int kMidiC0 = 12;
constexpr int kMidiC8 = 108;

inline double midiToHz (double midiNote) noexcept
{
    return 440.0 * std::pow (2.0, (midiNote - 69.0) / 12.0);
}

// Returns a very low sentinel (-1000) for non-positive / non-finite input instead of NaN.
inline double hzToMidi (double hz) noexcept
{
    if (! (hz > 0.0) || ! std::isfinite (hz))
        return -1000.0;
    return 69.0 + 12.0 * std::log2 (hz / 440.0);
}

inline int hzToNearestMidi (double hz) noexcept
{
    return (int) std::lround (hzToMidi (hz));
}

// Deviation from the nearest equal-tempered note in cents, range [-50, +50].
inline double centsFromNearestNote (double hz) noexcept
{
    const double m = hzToMidi (hz);
    return (m - std::round (m)) * 100.0;
}

inline int pitchClass (int midi) noexcept
{
    return ((midi % 12) + 12) % 12;
}

inline int octaveOf (int midi) noexcept // C4 == 60 -> octave 4
{
    const int q = (midi >= 0) ? midi / 12 : -((-midi + 11) / 12);
    return q - 1;
}

inline bool isBlackKey (int midi) noexcept
{
    const int pc = pitchClass (midi);
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

inline std::string midiToName (int midi)
{
    static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return std::string (names[pitchClass (midi)]) + std::to_string (octaveOf (midi));
}

template <typename T>
inline T clampValue (T v, T lo, T hi) noexcept
{
    return std::max (lo, std::min (hi, v));
}
} // namespace cutbench
