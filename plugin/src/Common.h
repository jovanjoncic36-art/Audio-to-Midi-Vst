#pragma once
// JUCE module headers used by the plugin layer (CMake-style includes, no JuceHeader.h).

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "cutbench/AnalysisEngine.h"
#include "cutbench/Candidates.h"
#include "cutbench/MidiFile.h"
#include "cutbench/NoteEditor.h"
#include "cutbench/PitchAnalyzer.h"
#include "cutbench/PitchMath.h"
#include "cutbench/PreviewSynth.h"

// Shared view settings (message thread only).
struct TimeAxis
{
    double startSec = 0.0;
    double pixelsPerSecond = 120.0;
};

struct DisplayOptions
{
    int lowMidi = 24;   // C1
    int highMidi = 96;  // C7
    float smoothing = 0.4f;
    bool showRefined = true; // true: harmonic-filtered picks, false: raw salience
};

namespace ui
{
constexpr int kKeyStripWidth = 58;

inline juce::Colour background()  { return juce::Colour (0xff15171b); }
inline juce::Colour panel()       { return juce::Colour (0xff1d2025); }
inline juce::Colour panelLight()  { return juce::Colour (0xff272b32); }
inline juce::Colour outline()     { return juce::Colour (0xff3a3f48); }
inline juce::Colour text()        { return juce::Colour (0xffd5d9e0); }
inline juce::Colour textDim()     { return juce::Colour (0xff8a909c); }
inline juce::Colour accent()      { return juce::Colour (0xffffb454); }  // note highlight
inline juce::Colour teal()        { return juce::Colour (0xff4cc2c4); }

inline juce::Colour heatColour (float t)
{
    const juce::Colour c0 (0xff15171b), c1 (0xff2a7a86), c2 (0xffffd166);
    t = juce::jlimit (0.0f, 1.0f, t);
    return t < 0.5f ? c0.interpolatedWith (c1, t * 2.0f) : c1.interpolatedWith (c2, (t - 0.5f) * 2.0f);
}

// Picks "nice" tick spacing (seconds) so labels are at least minPixels apart.
inline double niceTickSpacing (double pixelsPerSecond, double minPixels)
{
    static const double steps[] = { 0.01, 0.02, 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0, 120.0, 300.0 };
    for (double s : steps)
        if (s * pixelsPerSecond >= minPixels)
            return s;
    return 600.0;
}

inline juce::String formatTime (double seconds)
{
    const int minutes = (int) (seconds / 60.0);
    const double rest = seconds - minutes * 60.0;
    return minutes > 0 ? juce::String (minutes) + ":" + juce::String (rest, 1).paddedLeft ('0', 4)
                       : juce::String (seconds, 2) + " s";
}
} // namespace ui
