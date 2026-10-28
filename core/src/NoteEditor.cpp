#include "cutbench/NoteEditor.h"
#include "cutbench/PitchMath.h"

#include <algorithm>
#include <cmath>

namespace cutbench
{
namespace
{
constexpr size_t kMaxUndoSteps = 200;

double sanitiseTime (double t) noexcept
{
    return std::isfinite (t) ? std::max (0.0, t) : 0.0;
}

double sanitiseDuration (double d) noexcept
{
    return std::isfinite (d) ? std::max (kMinNoteDurationSec, d) : kMinNoteDurationSec;
}
} // namespace

const Note* NoteEditor::find (int id) const noexcept
{
    for (const auto& n : current)
        if (n.id == id)
            return &n;
    return nullptr;
}

Note* NoteEditor::findMutable (int id) noexcept
{
    for (auto& n : current)
        if (n.id == id)
            return &n;
    return nullptr;
}

void NoteEditor::checkpoint()
{
    if (inGesture)
    {
        if (! gesturePending)
            return;
        gesturePending = false;
    }

    undoStack.push_back (current);
    if (undoStack.size() > kMaxUndoSteps)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

int NoteEditor::addNote (int midi, double startSec, double durationSec, int velocity)
{
    checkpoint();

    Note n;
    n.id = nextId++;
    n.midi = clampValue (midi, 0, 127);
    n.startSec = sanitiseTime (startSec);
    n.durationSec = sanitiseDuration (durationSec);
    n.velocity = clampValue (velocity, 1, 127);
    current.push_back (n);
    return n.id;
}

bool NoteEditor::removeNote (int id)
{
    const auto it = std::find_if (current.begin(), current.end(), [id] (const Note& n) { return n.id == id; });
    if (it == current.end())
        return false;

    checkpoint();
    current.erase (std::find_if (current.begin(), current.end(), [id] (const Note& n) { return n.id == id; }));
    return true;
}

int NoteEditor::removeSelected()
{
    const auto count = std::count_if (current.begin(), current.end(), [] (const Note& n) { return n.selected; });
    if (count == 0)
        return 0;

    checkpoint();
    current.erase (std::remove_if (current.begin(), current.end(), [] (const Note& n) { return n.selected; }), current.end());
    return (int) count;
}

bool NoteEditor::setPitch (int id, int midi)
{
    Note* n = findMutable (id);
    if (n == nullptr)
        return false;

    const int v = clampValue (midi, 0, 127);
    if (v != n->midi)
    {
        checkpoint();
        n = findMutable (id);
        n->midi = v;
    }
    return true;
}

bool NoteEditor::moveNote (int id, double newStartSec)
{
    Note* n = findMutable (id);
    if (n == nullptr)
        return false;

    const double v = sanitiseTime (newStartSec);
    if (v != n->startSec)
    {
        checkpoint();
        n = findMutable (id);
        n->startSec = v;
    }
    return true;
}

bool NoteEditor::setDuration (int id, double durationSec)
{
    Note* n = findMutable (id);
    if (n == nullptr)
        return false;

    const double v = sanitiseDuration (durationSec);
    if (v != n->durationSec)
    {
        checkpoint();
        n = findMutable (id);
        n->durationSec = v;
    }
    return true;
}

bool NoteEditor::setVelocity (int id, int velocity)
{
    Note* n = findMutable (id);
    if (n == nullptr)
        return false;

    const int v = clampValue (velocity, 1, 127);
    if (v != n->velocity)
    {
        checkpoint();
        n = findMutable (id);
        n->velocity = v;
    }
    return true;
}

void NoteEditor::replaceAll (const std::vector<Note>& newNotes)
{
    checkpoint();
    current.clear();
    current.reserve (newNotes.size());

    for (Note n : newNotes)
    {
        n.id = nextId++;
        n.midi = clampValue (n.midi, 0, 127);
        n.startSec = sanitiseTime (n.startSec);
        n.durationSec = sanitiseDuration (n.durationSec);
        n.velocity = clampValue (n.velocity, 1, 127);
        current.push_back (n);
    }
}

void NoteEditor::quantise (double gridSec, double strength, bool onlySelected, bool alsoDuration)
{
    if (! (gridSec > 0.0) || ! std::isfinite (gridSec))
        return;

    strength = clampValue (strength, 0.0, 1.0);
    const bool useSelection = onlySelected && anySelected();

    std::vector<Note> result = current;
    for (auto& n : result)
    {
        if (useSelection && ! n.selected)
            continue;

        const double snapped = std::round (n.startSec / gridSec) * gridSec;
        n.startSec = sanitiseTime (n.startSec + (snapped - n.startSec) * strength);

        if (alsoDuration)
        {
            const double snappedDur = std::max (gridSec, std::round (n.durationSec / gridSec) * gridSec);
            n.durationSec = sanitiseDuration (n.durationSec + (snappedDur - n.durationSec) * strength);
        }
    }

    bool changed = false;
    for (size_t i = 0; i < result.size(); ++i)
        if (result[i].startSec != current[i].startSec || result[i].durationSec != current[i].durationSec)
            changed = true;

    if (changed)
    {
        checkpoint();
        current = std::move (result);
    }
}

void NoteEditor::setSelected (int id, bool selected)
{
    if (Note* n = findMutable (id))
        n->selected = selected;
}

void NoteEditor::selectAll (bool selected)
{
    for (auto& n : current)
        n.selected = selected;
}

bool NoteEditor::anySelected() const noexcept
{
    return std::any_of (current.begin(), current.end(), [] (const Note& n) { return n.selected; });
}

void NoteEditor::beginGesture()
{
    inGesture = true;
    gesturePending = true;
}

void NoteEditor::endGesture()
{
    inGesture = false;
    gesturePending = false;
}

bool NoteEditor::undo()
{
    endGesture();
    if (undoStack.empty())
        return false;

    redoStack.push_back (current);
    current = std::move (undoStack.back());
    undoStack.pop_back();
    return true;
}

bool NoteEditor::redo()
{
    endGesture();
    if (redoStack.empty())
        return false;

    undoStack.push_back (current);
    current = std::move (redoStack.back());
    redoStack.pop_back();
    return true;
}

void NoteEditor::clearHistory()
{
    undoStack.clear();
    redoStack.clear();
}
} // namespace cutbench
