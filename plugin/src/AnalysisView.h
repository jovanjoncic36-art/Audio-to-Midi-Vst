#pragma once

#include "Common.h"

#include <functional>
#include <memory>
#include <vector>

// Pitch-analysis display: piano keys on the left, time ruler on top, and a heat map of the
// analysis data (cutbench::AnalysisResult) in between. Candidate notes, the MIDI pattern and the
// playhead are drawn on top. Everything shown comes from real analysis data.
class AnalysisView : public juce::Component
{
public:
    AnalysisView (DisplayOptions& options, TimeAxis* axis);

    void setTimeAxis (TimeAxis* axis);
    void setResult (std::shared_ptr<const cutbench::AnalysisResult> result, bool isLive);
    void setCandidates (std::vector<cutbench::Candidate> all, std::vector<bool> selected, float threshold);
    void setPattern (const std::vector<cutbench::Note>* notes);
    void setPlayhead (double seconds, bool visible);

    // Call after DisplayOptions / threshold changed.
    void displayOptionsChanged();

    // Visible time span of the plot area in seconds.
    double visibleSeconds() const noexcept;
    double contentStart() const noexcept;
    double contentEnd() const noexcept;

    std::function<void (const cutbench::Candidate&)> onAddCandidate;
    std::function<void (double deltaSeconds)> onPan;
    std::function<void (double factor, double anchorSeconds)> onZoom;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<int> plotArea() const;
    float timeToX (double t) const noexcept;
    double xToTime (float x) const noexcept;
    float midiToTopY (int midi) const noexcept;
    float rowHeight() const noexcept;
    int yToMidi (float y) const noexcept;

    void rebuildImage();
    void drawKeys (juce::Graphics&, juce::Rectangle<int> keyArea) const;
    void drawRuler (juce::Graphics&, juce::Rectangle<int> rulerArea, juce::Rectangle<int> plot) const;
    juce::String describeAt (double t, int midi) const;
    const cutbench::Candidate* candidateAt (float x, float y) const;

    DisplayOptions& options;
    TimeAxis* axis = nullptr;

    std::shared_ptr<const cutbench::AnalysisResult> result;
    bool live = false;
    juce::Image image;

    std::vector<cutbench::Candidate> candidates;
    std::vector<bool> candidateSelected;
    float threshold = 0.03f;

    const std::vector<cutbench::Note>* pattern = nullptr;
    double playheadSec = 0.0;
    bool playheadVisible = false;

    bool hovering = false;
    juce::Point<float> hoverPos;
    double inspectTime = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisView)
};
