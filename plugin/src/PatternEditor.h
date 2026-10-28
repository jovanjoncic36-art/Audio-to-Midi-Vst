#pragma once

#include "Common.h"

#include <functional>
#include <vector>

// Piano-roll editor for the MIDI pattern. Shares the time axis with the analysis display.
// Mouse:  click = select (Shift = add to selection), drag = move in time/pitch,
//         drag right edge = change duration, double-click empty space = add a note,
//         velocity lane at the bottom: click/drag a bar to change velocity,
//         Delete/Backspace = remove selected notes. Alt while dragging disables grid snapping.
class PatternEditor : public juce::Component
{
public:
    PatternEditor (cutbench::NoteEditor& editor, TimeAxis& axis);

    std::function<void()> onChanged;        // called after every completed edit
    std::function<double()> gridSeconds;    // grid for snapping; <= 0 means no snapping

    // Recomputes the visible pitch range from the notes (called after edits, never during a drag).
    void refreshRange();
    void setPlayhead (double seconds, bool visible);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Drag { none, move, resize, velocity };

    juce::Rectangle<int> rollArea() const;
    juce::Rectangle<int> velocityArea() const;
    float timeToX (double t) const noexcept;
    double xToTime (float x) const noexcept;
    float rowHeight() const noexcept;
    float midiToTopY (int midi) const noexcept;
    int yToMidi (float y) const noexcept;
    juce::Rectangle<float> noteRect (const cutbench::Note& n) const;
    double snap (double seconds, bool allowSnap) const;
    void applyVelocityFromY (int noteId, float y);
    void finishEdit();

    struct Origin
    {
        int id;
        double start, duration;
        int midi;
    };

    cutbench::NoteEditor& editor;
    TimeAxis& axis;

    int lowMidi = 48, highMidi = 72;
    Drag drag = Drag::none;
    std::vector<Origin> origins;
    int primaryId = -1;
    int grabRow = 0;
    bool changedDuringDrag = false;

    double playheadSec = 0.0;
    bool playheadVisible = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternEditor)
};
