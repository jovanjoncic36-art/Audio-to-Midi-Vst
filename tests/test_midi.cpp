#include "TestFramework.h"
#include "cutbench/MidiFile.h"

#include <cstdio>
#include <fstream>
#include <iterator>

using namespace cutbench;

namespace
{
struct ParsedEvent
{
    int64_t tick;
    bool on;
    int pitch, velocity;
};

struct ParsedMidi
{
    bool ok = false;
    int format = -1, tracks = -1, ppq = -1;
    uint32_t tempoMicros = 0;
    bool endOfTrack = false, hasTimeSig = false;
    std::string trackName;
    std::vector<ParsedEvent> events;
};

uint32_t u32 (const std::vector<uint8_t>& b, size_t p) { return (uint32_t (b[p]) << 24) | (uint32_t (b[p + 1]) << 16) | (uint32_t (b[p + 2]) << 8) | b[p + 3]; }
int u16 (const std::vector<uint8_t>& b, size_t p) { return (b[p] << 8) | b[p + 1]; }

bool readVlq (const std::vector<uint8_t>& b, size_t& p, uint32_t& v)
{
    v = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (p >= b.size()) return false;
        const uint8_t c = b[p++];
        v = (v << 7) | (c & 0x7f);
        if (! (c & 0x80)) return true;
    }
    return false;
}

ParsedMidi parse (const std::vector<uint8_t>& b)
{
    ParsedMidi m;
    if (b.size() < 22 || std::string (b.begin(), b.begin() + 4) != "MThd" || u32 (b, 4) != 6)
        return m;

    m.format = u16 (b, 8);
    m.tracks = u16 (b, 10);
    m.ppq = u16 (b, 12);

    if (std::string (b.begin() + 14, b.begin() + 18) != "MTrk")
        return m;

    const uint32_t len = u32 (b, 18);
    size_t p = 22;
    const size_t end = p + len;
    if (end != b.size())
        return m; // declared length must match the real length

    int64_t tick = 0;
    while (p < end)
    {
        uint32_t delta;
        if (! readVlq (b, p, delta) || p >= end) return m;
        tick += delta;

        const uint8_t status = b[p++];
        if (status == 0xFF)
        {
            const uint8_t type = b[p++];
            uint32_t metaLen;
            if (! readVlq (b, p, metaLen) || p + metaLen > end) return m;

            if (type == 0x51 && metaLen == 3) m.tempoMicros = (uint32_t (b[p]) << 16) | (uint32_t (b[p + 1]) << 8) | b[p + 2];
            if (type == 0x03) m.trackName.assign (b.begin() + (long) p, b.begin() + (long) (p + metaLen));
            if (type == 0x58) m.hasTimeSig = true;
            if (type == 0x2F) { m.endOfTrack = true; p += metaLen; break; }
            p += metaLen;
        }
        else if ((status & 0xF0) == 0x90 || (status & 0xF0) == 0x80)
        {
            if (p + 2 > end) return m;
            const int pitch = b[p++], vel = b[p++];
            if (pitch > 127 || vel > 127) return m;
            m.events.push_back ({ tick, (status & 0xF0) == 0x90 && vel > 0, pitch, vel });
        }
        else
            return m; // we never write anything else
    }

    m.ok = m.endOfTrack && p == end;
    return m;
}
} // namespace

TEST (midi_seconds_ticks_conversion)
{
    CHECK (secondsToTicks (1.0, 120.0, 480) == 960);
    CHECK (secondsToTicks (0.5, 120.0, 480) == 480);
    CHECK (secondsToTicks (0.125, 120.0, 480) == 120); // 1/16 note at 120 bpm
    CHECK (secondsToTicks (1.0, 60.0, 480) == 480);
    CHECK (secondsToTicks (-1.0, 120.0) == 0);
    CHECK (secondsToTicks (std::nan (""), 120.0) == 0);
    CHECK_NEAR (ticksToSeconds (960, 120.0, 480), 1.0, 1.0e-12);

    // round trip within half a tick
    for (double t : { 0.0, 0.013, 0.5, 1.2345, 7.77 })
        CHECK_NEAR (ticksToSeconds (secondsToTicks (t, 93.0, 480), 93.0, 480), t, 0.5 / (480.0 * 93.0 / 60.0) + 1.0e-9);

    // absurd tempi are clamped, not divided by zero
    CHECK (secondsToTicks (1.0, 0.0) > 0);
    CHECK (secondsToTicks (1.0, 1.0e9) > 0);
}

TEST (midi_file_structure_and_content)
{
    std::vector<Note> notes;
    Note a; a.midi = 60; a.startSec = 0.0; a.durationSec = 0.5; a.velocity = 100;
    Note b; b.midi = 64; b.startSec = 0.5; b.durationSec = 0.25; b.velocity = 80;
    Note c; c.midi = 67; c.startSec = 0.5; c.durationSec = 1.0; c.velocity = 127;
    notes = { a, b, c };

    const auto bytes = buildMidiFile (notes, 120.0, 480);
    const ParsedMidi m = parse (bytes);

    CHECK (m.ok);
    CHECK (m.format == 0);
    CHECK (m.tracks == 1);
    CHECK (m.ppq == 480);
    CHECK (m.tempoMicros == 500000u);
    CHECK (m.hasTimeSig);
    CHECK (m.trackName == "Cutbench Notes");
    CHECK (m.events.size() == 6);

    int ons = 0, offs = 0;
    for (const auto& e : m.events) { (e.on ? ons : offs)++; }
    CHECK (ons == 3 && offs == 3);

    // absolute ticks: C4 on@0 off@480, E4 on@480 off@720, G4 on@480 off@1440
    auto find = [&] (int pitch, bool on) -> int64_t
    {
        for (const auto& e : m.events) if (e.pitch == pitch && e.on == on) return e.tick;
        return -1;
    };
    CHECK (find (60, true) == 0 && find (60, false) == 480);
    CHECK (find (64, true) == 480 && find (64, false) == 720);
    CHECK (find (67, true) == 480 && find (67, false) == 1440);

    // events are in non-decreasing time order
    for (size_t i = 1; i < m.events.size(); ++i)
        CHECK (m.events[i].tick >= m.events[i - 1].tick);

    // note-off of C4 (tick 480) must precede note-ons at tick 480
    size_t offIndex = 99, onIndex = 99;
    for (size_t i = 0; i < m.events.size(); ++i)
    {
        if (m.events[i].pitch == 60 && ! m.events[i].on) offIndex = i;
        if (m.events[i].pitch == 64 && m.events[i].on) onIndex = i;
    }
    CHECK (offIndex < onIndex);
}

TEST (midi_tempo_is_written_correctly)
{
    const ParsedMidi m = parse (buildMidiFile (std::vector<Note> {}, 90.0, 480));
    CHECK (m.ok);
    CHECK (m.tempoMicros == 666667u);
    CHECK (m.events.empty());
}

TEST (midi_invalid_notes_are_repaired)
{
    std::vector<MidiNote> notes;
    notes.push_back ({ 60, 0, 0, 0 });          // zero length, velocity 0
    notes.push_back ({ 200, 0, 100, 100 });     // pitch out of range -> dropped
    notes.push_back ({ -3, 0, 100, 100 });      // pitch out of range -> dropped
    notes.push_back ({ 62, -50, 100, 500 });    // negative start, velocity too high
    notes.push_back ({ 64, 100, 50, 64 });      // end before start

    const ParsedMidi m = parse (buildMidiFile (notes, 120.0, 480));
    CHECK (m.ok);
    CHECK (m.events.size() == 6); // 3 valid notes

    for (const auto& e : m.events)
    {
        CHECK (e.pitch >= 0 && e.pitch <= 127);
        if (e.on) CHECK (e.velocity >= 1 && e.velocity <= 127);
        CHECK (e.tick >= 0);
    }

    // every note-on has a later note-off for the same pitch
    for (const auto& on : m.events)
    {
        if (! on.on) continue;
        bool closed = false;
        for (const auto& off : m.events)
            if (! off.on && off.pitch == on.pitch && off.tick > on.tick) closed = true;
        CHECK (closed);
    }
}

TEST (midi_overlapping_same_pitch_is_trimmed)
{
    std::vector<MidiNote> notes;
    notes.push_back ({ 60, 0, 1000, 100 });
    notes.push_back ({ 60, 500, 800, 90 });   // starts inside the first note
    notes.push_back ({ 60, 500, 900, 90 });   // duplicate start -> dropped

    const ParsedMidi m = parse (buildMidiFile (notes, 120.0, 480));
    CHECK (m.ok);

    int open = 0, maxOpen = 0, ons = 0;
    for (const auto& e : m.events)
    {
        open += e.on ? 1 : -1;
        ons += e.on ? 1 : 0;
        maxOpen = std::max (maxOpen, open);
        CHECK (open >= 0);
    }
    CHECK (open == 0);
    CHECK (maxOpen == 1); // never two simultaneous note-ons of the same pitch
    CHECK (ons == 2);
}

TEST (midi_long_delta_times_use_vlq)
{
    std::vector<MidiNote> notes;
    notes.push_back ({ 60, 0, 10, 100 });
    notes.push_back ({ 61, 300000, 300010, 100 }); // delta needs a 3-byte VLQ

    const ParsedMidi m = parse (buildMidiFile (notes, 120.0, 480));
    CHECK (m.ok);
    CHECK (m.events.size() == 4);
    CHECK (m.events[2].tick == 300000);
    CHECK (m.events[3].tick == 300010);
}

TEST (midi_write_file_roundtrip)
{
    Note a; a.midi = 72; a.startSec = 0.25; a.durationSec = 0.5; a.velocity = 99;
    const auto bytes = buildMidiFile (std::vector<Note> { a }, 100.0, 480);

    const std::string path = "cutbench_test_roundtrip.mid";
    CHECK (writeMidiFile (path, bytes));

    std::ifstream in (path, std::ios::binary);
    std::vector<uint8_t> back ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    CHECK (back == bytes);
    std::remove (path.c_str());

    // unwritable location reports failure instead of crashing
    CHECK (! writeMidiFile ("/nonexistent_dir_cutbench/xx.mid", bytes));
}
