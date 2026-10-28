#include "PatternEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr int kVelocityLaneHeight = 46;
}

PatternEditor::PatternEditor (cutbench::NoteEditor& noteEditor, TimeAxis& timeAxis)
    : editor (noteEditor), axis (timeAxis)
{
    setWantsKeyboardFocus (true);
    refreshRange();
}

//==============================================================================
juce::Rectangle<int> PatternEditor::rollArea() const
{
    auto b = getLocalBounds();
    b.removeFromBottom (kVelocityLaneHeight);
    b.removeFromLeft (ui::kKeyStripWidth);
    return b;
}

juce::Rectangle<int> PatternEditor::velocityArea() const
{
    auto b = getLocalBounds();
    b.removeFromLeft (ui::kKeyStripWidth);
    return b.removeFromBottom (kVelocityLaneHeight);
}

float PatternEditor::timeToX (double t) const noexcept
{
    return (float) rollArea().getX() + (float) ((t - axis.startSec) * axis.pixelsPerSecond);
}

double PatternEditor::xToTime (float x) const noexcept
{
    return axis.startSec + (double) (x - (float) rollArea().getX()) / axis.pixelsPerSecond;
}

float PatternEditor::rowHeight() const noexcept
{
    return (float) rollArea().getHeight() / (float) std::max (1, highMidi - lowMidi + 1);
}

float PatternEditor::midiToTopY (int midi) const noexcept
{
    return (float) rollArea().getY() + (float) (highMidi - midi) * rowHeight();
}

int PatternEditor::yToMidi (float y) const noexcept
{
    const float rowH = rowHeight();
    if (rowH <= 0.0f)
        return lowMidi;

    const int row = (int) std::floor ((y - (float) rollArea().getY()) / rowH);
    return juce::jlimit (lowMidi, highMidi, highMidi - row);
}

juce::Rectangle<float> PatternEditor::noteRect (const cutbench::Note& n) const
{
    const float rowH = rowHeight();
    return { timeToX (n.startSec),
             midiToTopY (n.midi) + 1.0f,
             std::max (4.0f, (float) n.durationSec * (float) axis.pixelsPerSecond),
             std::max (3.0f, rowH - 2.0f) };
}

double PatternEditor::snap (double seconds, bool allowSnap) const
{
    const double grid = gridSeconds ? gridSeconds() : 0.0;
    if (! allowSnap || ! (grid > 0.0))
        return std::max (0.0, seconds);

    return std::max (0.0, std::round (seconds / grid) * grid);
}

void PatternEditor::refreshRange()
{
    int lo = 55, hi = 79; // around middle C when there are no notes
    const auto& notes = editor.notes();

    if (! notes.empty())
    {
        lo = 127;
        hi = 0;
        for (const auto& n : notes)
        {
            lo = std::min (lo, n.midi);
            hi = std::max (hi, n.midi);
        }
        lo -= 4;
        hi += 4;
    }

    if (hi - lo < 23)
    {
        const int extra = 23 - (hi - lo);
        lo -= extra / 2;
        hi += extra - extra / 2;
    }

    if (lo < 0) { hi -= lo; lo = 0; }
    if (hi > 127) { lo -= hi - 127; hi = 127; }

    lowMidi = std::max (0, lo);
    highMidi = std::min (127, hi);
    repaint();
}

void PatternEditor::setPlayhead (double seconds, bool visible)
{
    if (visible != playheadVisible || (visible && std::abs (seconds - playheadSec) > 1.0e-4))
    {
        playheadSec = seconds;
        playheadVisible = visible;
        repaint();
    }
}

//==============================================================================
void PatternEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::background());

    const auto roll = rollArea();
    const auto lane = velocityArea();
    if (roll.isEmpty())
        return;

    const float rowH = rowHeight();

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (roll);

        for (int m = lowMidi; m <= highMidi; ++m)
        {
            g.setColour (cutbench::isBlackKey (m) ? ui::background().darker (0.25f) : ui::panel());
            g.fillRect (juce::Rectangle<float> ((float) roll.getX(), midiToTopY (m), (float) roll.getWidth(), rowH));

            if (cutbench::pitchClass (m) == 0)
            {
                g.setColour (ui::outline());
                g.drawHorizontalLine ((int) (midiToTopY (m) + rowH), (float) roll.getX(), (float) roll.getRight());
            }
        }

        const double grid = gridSeconds ? gridSeconds() : 0.0;
        if (grid > 0.0 && grid * axis.pixelsPerSecond >= 8.0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            for (double t = std::ceil (axis.startSec / grid) * grid; timeToX (t) < (float) roll.getRight(); t += grid)
                g.drawVerticalLine ((int) timeToX (t), (float) roll.getY(), (float) roll.getBottom());
        }

        for (const auto& n : editor.notes())
        {
            if (n.midi < lowMidi || n.midi > highMidi)
                continue;

            const auto r = noteRect (n);
            const float shade = 0.35f + 0.5f * (float) n.velocity / 127.0f;
            g.setColour (ui::accent().withAlpha (n.selected ? 1.0f : shade));
            g.fillRect (r);
            g.setColour (n.selected ? juce::Colours::white : ui::accent().darker (0.4f));
            g.drawRect (r, n.selected ? 1.5f : 1.0f);

            // resize handle
            g.setColour (juce::Colours::black.withAlpha (0.3f));
            g.fillRect (juce::Rectangle<float> (r.getRight() - 4.0f, r.getY(), 4.0f, r.getHeight()));
        }

        if (playheadVisible)
        {
            g.setColour (juce::Colours::white);
            g.drawVerticalLine ((int) timeToX (playheadSec), (float) roll.getY(), (float) roll.getBottom());
        }
    }

    // key strip
    for (int m = lowMidi; m <= highMidi; ++m)
    {
        const float y = midiToTopY (m);
        const bool black = cutbench::isBlackKey (m);

        g.setColour (juce::Colour (0xffdfe2e8));
        g.fillRect (juce::Rectangle<float> (0.0f, y, (float) ui::kKeyStripWidth, rowH));
        if (black)
        {
            g.setColour (juce::Colour (0xff22252b));
            g.fillRect (juce::Rectangle<float> (0.0f, y, (float) ui::kKeyStripWidth * 0.62f, rowH));
        }

        g.setColour (juce::Colour (0xff8d929c));
        g.drawHorizontalLine ((int) y, 0.0f, (float) ui::kKeyStripWidth);

        if (cutbench::pitchClass (m) == 0 || (rowH >= 11.0f && ! black))
        {
            g.setColour (juce::Colour (0xff22252b));
            g.setFont (juce::jlimit (8.0f, 11.0f, rowH * 0.9f));
            g.drawText (juce::String (cutbench::midiToName (m)),
                        juce::Rectangle<float> (0.0f, y + rowH * 0.5f - 6.0f, (float) ui::kKeyStripWidth - 4.0f, 12.0f).toNearestInt(),
                        juce::Justification::centredRight, false);
        }
    }

    // velocity lane
    g.setColour (ui::panelLight());
    g.fillRect (lane);
    g.setColour (ui::textDim());
    g.setFont (10.0f);
    g.drawText ("Velocity", 4, lane.getY(), ui::kKeyStripWidth - 6, lane.getHeight(), juce::Justification::centredLeft, false);

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (lane);

        for (const auto& n : editor.notes())
        {
            const float x = timeToX (n.startSec);
            if (x < (float) lane.getX() - 6.0f || x > (float) lane.getRight())
                continue;

            const float h = (float) (lane.getHeight() - 6) * (float) n.velocity / 127.0f;
            g.setColour (n.selected ? juce::Colours::white : ui::accent());
            g.fillRect (juce::Rectangle<float> (x, (float) lane.getBottom() - 3.0f - h, 5.0f, h));
        }
    }

    if (editor.notes().empty())
    {
        g.setColour (ui::textDim());
        g.setFont (13.0f);
        g.drawFittedText ("No notes yet. Use QuickGrab and Apply, click a detected note in the display above,\nor double-click here to add a note.",
                          roll.reduced (20), juce::Justification::centred, 3);
    }

    g.setColour (ui::outline());
    g.drawRect (getLocalBounds());
}

//==============================================================================
void PatternEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    drag = Drag::none;
    origins.clear();
    primaryId = -1;
    changedDuringDrag = false;

    if (velocityArea().contains (e.getPosition()))
    {
        int best = -1;
        float bestDistance = 9.0f;
        for (const auto& n : editor.notes())
        {
            const float d = std::abs (timeToX (n.startSec) + 2.5f - e.position.x);
            if (d < bestDistance)
            {
                bestDistance = d;
                best = n.id;
            }
        }

        if (best >= 0)
        {
            primaryId = best;
            drag = Drag::velocity;
            editor.beginGesture();
            applyVelocityFromY (best, e.position.y);
            changedDuringDrag = true;
            repaint();
        }
        return;
    }

    if (! rollArea().contains (e.getPosition()))
        return;

    int hit = -1;
    bool onResizeHandle = false;
    const auto& notes = editor.notes();
    for (size_t i = notes.size(); i-- > 0;)
    {
        const auto r = noteRect (notes[i]);
        if (r.contains (e.position))
        {
            hit = notes[i].id;
            onResizeHandle = e.position.x > r.getRight() - 6.0f;
            break;
        }
    }

    if (hit < 0)
    {
        editor.selectAll (false);
        repaint();
        return;
    }

    if (e.mods.isShiftDown())
        editor.setSelected (hit, ! editor.find (hit)->selected);
    else if (! editor.find (hit)->selected)
    {
        editor.selectAll (false);
        editor.setSelected (hit, true);
    }

    if (! editor.find (hit)->selected)
    {
        repaint();
        return;
    }

    primaryId = hit;
    drag = onResizeHandle ? Drag::resize : Drag::move;
    grabRow = yToMidi (e.position.y);

    for (const auto& n : editor.notes())
        if (n.selected)
            origins.push_back ({ n.id, n.startSec, n.durationSec, n.midi });

    editor.beginGesture();
    repaint();
}

void PatternEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::none || primaryId < 0)
        return;

    if (drag == Drag::velocity)
    {
        applyVelocityFromY (primaryId, e.position.y);
        changedDuringDrag = true;
        repaint();
        return;
    }

    const Origin* primary = nullptr;
    for (const auto& o : origins)
        if (o.id == primaryId)
            primary = &o;

    if (primary == nullptr)
        return;

    const bool allowSnap = ! e.mods.isAltDown();
    const double dt = (double) e.getDistanceFromDragStartX() / axis.pixelsPerSecond;

    if (drag == Drag::move)
    {
        double delta = snap (primary->start + dt, allowSnap) - primary->start;

        double earliest = primary->start;
        for (const auto& o : origins)
            earliest = std::min (earliest, o.start);
        delta = std::max (delta, -earliest); // nothing may move before time zero

        const int rows = yToMidi (e.position.y) - grabRow;
        for (const auto& o : origins)
        {
            editor.moveNote (o.id, o.start + delta);
            editor.setPitch (o.id, juce::jlimit (0, 127, o.midi + rows));
        }
    }
    else // resize
    {
        const double newEnd = snap (primary->start + primary->duration + dt, allowSnap);
        const double deltaDuration = std::max (cutbench::kMinNoteDurationSec, newEnd - primary->start) - primary->duration;

        for (const auto& o : origins)
            editor.setDuration (o.id, std::max (cutbench::kMinNoteDurationSec, o.duration + deltaDuration));
    }

    changedDuringDrag = true;
    repaint();
}

void PatternEditor::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::none)
        return;

    editor.endGesture();
    drag = Drag::none;
    origins.clear();

    if (changedDuringDrag)
        finishEdit();
}

void PatternEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! rollArea().contains (e.getPosition()))
        return;

    for (const auto& n : editor.notes())
        if (noteRect (n).contains (e.position))
            return;

    const double grid = gridSeconds ? gridSeconds() : 0.0;
    double t = xToTime (e.position.x);
    if (grid > 0.0)
        t = std::floor (t / grid) * grid;
    t = std::max (0.0, t);

    editor.selectAll (false);
    const int id = editor.addNote (yToMidi (e.position.y), t, grid > 0.0 ? grid : 0.25, 100);
    editor.setSelected (id, true);
    finishEdit();
}

bool PatternEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (editor.removeSelected() > 0)
            finishEdit();
        return true;
    }

    return false;
}

void PatternEditor::applyVelocityFromY (int noteId, float y)
{
    const auto lane = velocityArea();
    if (lane.getHeight() <= 0)
        return;

    const float fraction = 1.0f - (y - (float) lane.getY()) / (float) lane.getHeight();
    editor.setVelocity (noteId, juce::jlimit (1, 127, (int) std::lround (1.0f + 126.0f * fraction)));
}

void PatternEditor::finishEdit()
{
    refreshRange();
    if (onChanged)
        onChanged();
    repaint();
}
