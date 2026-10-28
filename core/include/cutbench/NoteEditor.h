#pragma once
// Pattern model: notes in seconds relative to the start of the analysed passage,
// with snapshot-based undo/redo and quantisation. No JUCE dependency.

#include <vector>

namespace cutbench
{
constexpr double kMinNoteDurationSec = 0.01;

struct Note
{
    int id = 0;
    int midi = 60;           // 0..127
    double startSec = 0.0;   // >= 0
    double durationSec = 0.25; // >= kMinNoteDurationSec
    int velocity = 100;      // 1..127
    bool selected = false;

    double endSec() const noexcept { return startSec + durationSec; }
};

class NoteEditor
{
public:
    const std::vector<Note>& notes() const noexcept { return current; }
    const Note* find (int id) const noexcept;

    // Editing (each call is one undo step unless inside beginGesture()/endGesture()).
    // Values are clamped to valid ranges. add returns the new id.
    int addNote (int midi, double startSec, double durationSec, int velocity);
    bool removeNote (int id);
    int removeSelected();                       // returns number removed
    bool setPitch (int id, int midi);
    bool moveNote (int id, double newStartSec);
    bool setDuration (int id, double durationSec);
    bool setVelocity (int id, int velocity);
    void replaceAll (const std::vector<Note>& newNotes); // ids are reassigned; one undo step

    // Quantise note starts to a grid (seconds). strength 1 = hard snap, 0 = no change.
    // If onlySelected is true and at least one note is selected, only selected notes move.
    // If alsoDuration is true durations snap to multiples of the grid (min one grid step).
    void quantise (double gridSec, double strength = 1.0, bool onlySelected = false, bool alsoDuration = false);

    // Selection is not part of the undo history.
    void setSelected (int id, bool selected);
    void selectAll (bool selected);
    bool anySelected() const noexcept;

    // Group many small edits (e.g. a mouse drag) into a single undo step.
    void beginGesture();
    void endGesture();

    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    bool undo();
    bool redo();
    void clearHistory();

private:
    Note* findMutable (int id) noexcept;
    void checkpoint();

    std::vector<Note> current;
    std::vector<std::vector<Note>> undoStack, redoStack;
    int nextId = 1;
    bool inGesture = false;
    bool gesturePending = false; // gesture started but no edit has been made yet
};
} // namespace cutbench
