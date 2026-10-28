#include "cutbench/MidiFile.h"
#include "cutbench/PitchMath.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <tuple>

namespace cutbench
{
namespace
{
void appendVlq (std::vector<uint8_t>& out, uint32_t value)
{
    uint8_t bytes[5];
    int count = 0;
    bytes[count++] = (uint8_t) (value & 0x7f);
    value >>= 7;

    while (value > 0)
    {
        bytes[count++] = (uint8_t) ((value & 0x7f) | 0x80);
        value >>= 7;
    }

    while (count > 0)
        out.push_back (bytes[--count]);
}

void appendU32 (std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back ((uint8_t) (v >> 24));
    out.push_back ((uint8_t) (v >> 16));
    out.push_back ((uint8_t) (v >> 8));
    out.push_back ((uint8_t) v);
}

void appendU16 (std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back ((uint8_t) (v >> 8));
    out.push_back ((uint8_t) v);
}

struct Event
{
    int64_t tick;
    int order; // 0 = note-off, 1 = note-on (offs first at equal ticks)
    int pitch;
    int velocity;
};
} // namespace

double clampBpm (double bpm) noexcept
{
    if (! std::isfinite (bpm))
        return 120.0;
    return clampValue (bpm, kMinBpm, kMaxBpm);
}

int64_t secondsToTicks (double seconds, double bpm, int ppq) noexcept
{
    if (! std::isfinite (seconds) || seconds <= 0.0 || ppq <= 0)
        return 0;

    return (int64_t) std::llround (seconds * (clampBpm (bpm) / 60.0) * ppq);
}

double ticksToSeconds (int64_t ticks, double bpm, int ppq) noexcept
{
    if (ppq <= 0)
        return 0.0;

    return double (ticks) / (double (ppq) * clampBpm (bpm) / 60.0);
}

std::vector<MidiNote> toMidiNotes (const std::vector<Note>& notes, double bpm, int ppq)
{
    std::vector<MidiNote> out;
    out.reserve (notes.size());

    for (const auto& n : notes)
    {
        MidiNote m;
        m.pitch = n.midi;
        m.startTick = secondsToTicks (n.startSec, bpm, ppq);
        m.endTick = std::max<int64_t> (m.startTick + 1, secondsToTicks (n.endSec(), bpm, ppq));
        m.velocity = n.velocity;
        out.push_back (m);
    }

    return out;
}

std::vector<uint8_t> buildMidiFile (const std::vector<MidiNote>& input, double bpm, int ppq, const std::string& trackName)
{
    if (ppq <= 0 || ppq > 0x7fff)
        ppq = kDefaultPpq;

    // 1. Repair / filter notes.
    std::vector<MidiNote> notes;
    notes.reserve (input.size());

    for (MidiNote n : input)
    {
        if (n.pitch < 0 || n.pitch > 127)
            continue;

        n.velocity = clampValue (n.velocity, 1, 127);
        n.startTick = std::max<int64_t> (0, n.startTick);
        n.endTick = std::max<int64_t> (n.startTick + 1, n.endTick);
        notes.push_back (n);
    }

    // 2. Make same-pitch notes non-overlapping so each note-on gets its own note-off.
    std::sort (notes.begin(), notes.end(), [] (const MidiNote& a, const MidiNote& b)
               { return std::tie (a.pitch, a.startTick, a.endTick) < std::tie (b.pitch, b.startTick, b.endTick); });

    std::vector<MidiNote> clean;
    clean.reserve (notes.size());

    for (const auto& n : notes)
    {
        if (! clean.empty() && clean.back().pitch == n.pitch && clean.back().endTick > n.startTick)
        {
            MidiNote& prev = clean.back();
            prev.endTick = std::max<int64_t> (prev.startTick + 1, n.startTick);

            if (prev.endTick > n.startTick) // identical start: drop the duplicate
                continue;
        }

        clean.push_back (n);
    }

    // 3. Events.
    std::vector<Event> events;
    events.reserve (clean.size() * 2);
    for (const auto& n : clean)
    {
        events.push_back ({ n.startTick, 1, n.pitch, n.velocity });
        events.push_back ({ n.endTick, 0, n.pitch, 0 });
    }

    std::sort (events.begin(), events.end(), [] (const Event& a, const Event& b)
               { return std::tie (a.tick, a.order, a.pitch) < std::tie (b.tick, b.order, b.pitch); });

    // 4. Track data.
    std::vector<uint8_t> track;

    // Track name
    {
        const std::string name = trackName.substr (0, 100);
        track.push_back (0x00);
        track.push_back (0xFF);
        track.push_back (0x03);
        appendVlq (track, (uint32_t) name.size());
        track.insert (track.end(), name.begin(), name.end());
    }

    // Tempo
    {
        const uint32_t micros = (uint32_t) clampValue<long long> (std::llround (60000000.0 / clampBpm (bpm)), 1LL, 0xFFFFFFLL);
        track.push_back (0x00);
        track.push_back (0xFF);
        track.push_back (0x51);
        track.push_back (0x03);
        track.push_back ((uint8_t) (micros >> 16));
        track.push_back ((uint8_t) (micros >> 8));
        track.push_back ((uint8_t) micros);
    }

    // Time signature 4/4
    track.insert (track.end(), { 0x00, 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08 });

    int64_t lastTick = 0;
    for (const auto& e : events)
    {
        appendVlq (track, (uint32_t) (e.tick - lastTick));
        lastTick = e.tick;

        if (e.order == 1)
        {
            track.push_back (0x90); // note-on, channel 1
            track.push_back ((uint8_t) e.pitch);
            track.push_back ((uint8_t) e.velocity);
        }
        else
        {
            track.push_back (0x80); // note-off, channel 1
            track.push_back ((uint8_t) e.pitch);
            track.push_back (0x40);
        }
    }

    // End of track
    track.insert (track.end(), { 0x00, 0xFF, 0x2F, 0x00 });

    // 5. File.
    std::vector<uint8_t> file;
    file.reserve (track.size() + 22);
    file.insert (file.end(), { 'M', 'T', 'h', 'd' });
    appendU32 (file, 6);
    appendU16 (file, 0); // format 0
    appendU16 (file, 1); // one track
    appendU16 (file, (uint16_t) ppq);
    file.insert (file.end(), { 'M', 'T', 'r', 'k' });
    appendU32 (file, (uint32_t) track.size());
    file.insert (file.end(), track.begin(), track.end());
    return file;
}

std::vector<uint8_t> buildMidiFile (const std::vector<Note>& notes, double bpm, int ppq)
{
    return buildMidiFile (toMidiNotes (notes, bpm, ppq), bpm, ppq);
}

bool writeMidiFile (const std::string& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream f (path, std::ios::binary | std::ios::trunc);
    if (! f)
        return false;

    f.write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
    f.flush();
    return f.good();
}
} // namespace cutbench
