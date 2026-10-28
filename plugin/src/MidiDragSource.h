#pragma once

#include "Common.h"

#include <functional>

// A drag handle that hands a .mid file to the operating system's drag-and-drop (e.g. onto a MIDI
// track in Ableton Live). Whether a host accepts file drops from a plugin window depends on the
// host, so the editor always offers "Export .mid..." and "Open export folder" as a fallback.
class MidiDragSource : public juce::Component
{
public:
    // Must write the .mid file and return it, or return an empty File (after reporting why).
    std::function<juce::File()> prepareFile;

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (dragging ? ui::accent().withAlpha (0.35f) : ui::panelLight());
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (ui::accent());
        g.drawRoundedRectangle (b, 6.0f, 1.2f);
        g.setColour (ui::text());
        g.setFont (13.0f);
        g.drawText ("Drag MIDI to your DAW", getLocalBounds(), juce::Justification::centred, false);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        started = false;
        dragging = false;
        file = prepareFile ? prepareFile() : juce::File();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (started || e.getDistanceFromDragStart() < 6)
            return;

        started = true;
        if (! file.existsAsFile())
            return;

        dragging = true;
        repaint();

        juce::DragAndDropContainer::performExternalDragDropOfFiles (juce::StringArray (file.getFullPathName()),
                                                                     false, this);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragging = false;
        repaint();
    }

private:
    juce::File file;
    bool started = false;
    bool dragging = false;
};
