#pragma once
// Standard MIDI File (format 0) writer. No JUCE dependency, so it can be unit-tested anywhere.

#include "cutbench/NoteEditor.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cutbench
{
constexpr int kDefaultPpq = 480;
constexpr double kMinBpm = 20.0;
constexpr double kMaxBpm = 400.0;

struct MidiNote
{
    int pitch = 60;           // 0..127
    int64_t startTick = 0;    // >= 0
    int64_t endTick = 1;      // > startTick
    int velocity = 100;       // 1..127
};

double clampBpm (double bpm) noexcept;

// seconds <-> ticks at a constant tempo. Non-finite or negative seconds map to 0.
int64_t secondsToTicks (double seconds, double bpm, int ppq = kDefaultPpq) noexcept;
double ticksToSeconds (int64_t ticks, double bpm, int ppq = kDefaultPpq) noexcept;

std::vector<MidiNote> toMidiNotes (const std::vector<Note>& notes, double bpm, int ppq = kDefaultPpq);

// Builds the bytes of a format-0 file containing: track name, tempo, 4/4 time signature, the notes.
// Invalid notes are repaired or dropped: pitch outside 0..127 is dropped, velocity is clamped to 1..127,
// zero/negative lengths become one tick, and overlapping notes of the same pitch are trimmed so that
// every note-on is closed by its own note-off. At equal ticks note-offs are written before note-ons.
std::vector<uint8_t> buildMidiFile (const std::vector<MidiNote>& notes,
                                    double bpm,
                                    int ppq = kDefaultPpq,
                                    const std::string& trackName = "Cutbench Notes");

// Convenience: Note list -> file bytes.
std::vector<uint8_t> buildMidiFile (const std::vector<Note>& notes, double bpm, int ppq = kDefaultPpq);

// Writes bytes to disk. Returns false on any I/O failure. Not for use on an audio thread.
bool writeMidiFile (const std::string& path, const std::vector<uint8_t>& bytes);
} // namespace cutbench
