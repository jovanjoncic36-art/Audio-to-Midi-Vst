#include "TestFramework.h"
#include "cutbench/Candidates.h"
#include "cutbench/MidiFile.h"
#include "cutbench/NoteEditor.h"

using namespace cutbench;

TEST (notes_add_remove_and_clamping)
{
    NoteEditor ed;
    const int a = ed.addNote (200, -1.0, 0.0, 999);
    const Note* n = ed.find (a);
    CHECK (n != nullptr);
    CHECK (n->midi == 127);
    CHECK_NEAR (n->startSec, 0.0, 1e-12);
    CHECK (n->durationSec >= kMinNoteDurationSec);
    CHECK (n->velocity == 127);

    const int b = ed.addNote (60, 1.0, 0.5, 0);
    CHECK (ed.find (b)->velocity == 1);
    CHECK (ed.notes().size() == 2);

    CHECK (ed.removeNote (a));
    CHECK (! ed.removeNote (a));
    CHECK (ed.notes().size() == 1);
}

TEST (notes_edit_pitch_move_duration_velocity)
{
    NoteEditor ed;
    const int id = ed.addNote (60, 1.0, 0.5, 100);
    CHECK (ed.setPitch (id, 64));
    CHECK (ed.moveNote (id, 2.0));
    CHECK (ed.setDuration (id, 0.75));
    CHECK (ed.setVelocity (id, 55));

    const Note* n = ed.find (id);
    CHECK (n->midi == 64);
    CHECK_NEAR (n->startSec, 2.0, 1e-12);
    CHECK_NEAR (n->durationSec, 0.75, 1e-12);
    CHECK (n->velocity == 55);
    CHECK_NEAR (n->endSec(), 2.75, 1e-12);

    CHECK (! ed.setPitch (9999, 60)); // unknown id
}

TEST (notes_undo_redo)
{
    NoteEditor ed;
    CHECK (! ed.canUndo());
    CHECK (! ed.undo());

    const int id = ed.addNote (60, 0.0, 1.0, 100); // step 1
    ed.setPitch (id, 62);                           // step 2
    ed.moveNote (id, 3.0);                          // step 3

    CHECK (ed.undo());
    CHECK_NEAR (ed.find (id)->startSec, 0.0, 1e-12);
    CHECK (ed.find (id)->midi == 62);
    CHECK (ed.undo());
    CHECK (ed.find (id)->midi == 60);
    CHECK (ed.undo());
    CHECK (ed.notes().empty());
    CHECK (! ed.canUndo());

    CHECK (ed.redo());
    CHECK (ed.notes().size() == 1);
    CHECK (ed.redo());
    CHECK (ed.redo());
    CHECK (ed.find (id)->midi == 62);
    CHECK_NEAR (ed.find (id)->startSec, 3.0, 1e-12);
    CHECK (! ed.canRedo());

    // a new edit clears the redo stack
    ed.undo();
    CHECK (ed.canRedo());
    ed.setVelocity (id, 10);
    CHECK (! ed.canRedo());
}

TEST (notes_no_op_edits_do_not_create_undo_steps)
{
    NoteEditor ed;
    const int id = ed.addNote (60, 0.0, 1.0, 100);
    ed.clearHistory();

    ed.setPitch (id, 60);
    ed.setVelocity (id, 100);
    ed.moveNote (id, 0.0);
    CHECK (! ed.canUndo());
}

TEST (notes_gesture_is_one_undo_step)
{
    NoteEditor ed;
    const int id = ed.addNote (60, 0.0, 1.0, 100);
    ed.clearHistory();

    ed.beginGesture();
    for (int i = 1; i <= 20; ++i)
        ed.moveNote (id, i * 0.1);
    ed.endGesture();

    CHECK_NEAR (ed.find (id)->startSec, 2.0, 1e-9);
    CHECK (ed.undo());
    CHECK_NEAR (ed.find (id)->startSec, 0.0, 1e-9);
    CHECK (! ed.canUndo()); // exactly one step

    // a gesture that changes nothing adds no step
    ed.beginGesture();
    ed.endGesture();
    CHECK (! ed.canUndo());
}

TEST (notes_selection_and_remove_selected)
{
    NoteEditor ed;
    const int a = ed.addNote (60, 0.0, 1.0, 100);
    const int b = ed.addNote (62, 1.0, 1.0, 100);
    ed.addNote (64, 2.0, 1.0, 100);

    ed.setSelected (a, true);
    ed.setSelected (b, true);
    CHECK (ed.anySelected());
    CHECK (ed.removeSelected() == 2);
    CHECK (ed.notes().size() == 1);
    CHECK (ed.undo());
    CHECK (ed.notes().size() == 3);
}

TEST (notes_quantise_to_sixteenths_at_120bpm)
{
    const double grid = 0.125; // 1/16 at 120 bpm
    NoteEditor ed;
    const int a = ed.addNote (60, 0.13, 0.2, 100);
    const int b = ed.addNote (62, 0.49, 0.2, 100);
    const int c = ed.addNote (64, 1.07, 0.3, 100);

    ed.quantise (grid);
    CHECK_NEAR (ed.find (a)->startSec, 0.125, 1e-9);
    CHECK_NEAR (ed.find (b)->startSec, 0.5, 1e-9);
    CHECK_NEAR (ed.find (c)->startSec, 1.125, 1e-9);
    CHECK_NEAR (ed.find (a)->durationSec, 0.2, 1e-9); // durations untouched by default

    CHECK (ed.undo());
    CHECK_NEAR (ed.find (a)->startSec, 0.13, 1e-9);

    // half strength moves half-way
    ed.quantise (grid, 0.5);
    CHECK_NEAR (ed.find (a)->startSec, 0.1275, 1e-9);

    // invalid grid is ignored
    ed.quantise (0.0);
    ed.quantise (-1.0);
}

TEST (notes_quantise_selected_only_and_duration)
{
    NoteEditor ed;
    const int a = ed.addNote (60, 0.13, 0.2, 100);
    const int b = ed.addNote (62, 0.49, 0.2, 100);
    ed.setSelected (a, true);

    ed.quantise (0.125, 1.0, true, true);
    CHECK_NEAR (ed.find (a)->startSec, 0.125, 1e-9);
    CHECK_NEAR (ed.find (a)->durationSec, 0.25, 1e-9); // snapped to 2 grid steps
    CHECK_NEAR (ed.find (b)->startSec, 0.49, 1e-9);     // untouched

    // never snaps to a start before zero
    NoteEditor ed2;
    const int c = ed2.addNote (60, 0.01, 0.1, 100);
    ed2.quantise (0.5);
    CHECK_NEAR (ed2.find (c)->startSec, 0.0, 1e-12);
}

TEST (notes_replace_all_assigns_fresh_ids_and_is_undoable)
{
    NoteEditor ed;
    ed.addNote (60, 0.0, 1.0, 100);

    std::vector<Note> fresh (2);
    fresh[0].midi = 70; fresh[1].midi = 300;
    ed.replaceAll (fresh);

    CHECK (ed.notes().size() == 2);
    CHECK (ed.notes()[0].id != ed.notes()[1].id);
    CHECK (ed.notes()[1].midi == 127);
    CHECK (ed.undo());
    CHECK (ed.notes().size() == 1);
    CHECK (ed.notes()[0].midi == 60);
}

TEST (notes_export_keeps_timing_of_pattern)
{
    // 2 beats at 90 bpm == 1.3333 s; the exported tick positions must match the note times.
    NoteEditor ed;
    ed.addNote (60, 0.0, 60.0 / 90.0, 100);
    ed.addNote (62, 60.0 / 90.0, 60.0 / 90.0, 100);

    const auto midiNotes = toMidiNotes (ed.notes(), 90.0, 480);
    CHECK (midiNotes.size() == 2);
    CHECK (midiNotes[0].startTick == 0 && midiNotes[0].endTick == 480);
    CHECK (midiNotes[1].startTick == 480 && midiNotes[1].endTick == 960);
}
