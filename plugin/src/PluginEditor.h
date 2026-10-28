#pragma once

#include "AnalysisView.h"
#include "Common.h"
#include "MidiDragSource.h"
#include "PatternEditor.h"
#include "PluginProcessor.h"

#include <memory>
#include <vector>

class CutbenchEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget,
                       private juce::Timer,
                       private juce::ScrollBar::Listener
{
public:
    explicit CutbenchEditor (CutbenchProcessor&);
    ~CutbenchEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    // setup
    void configureLookAndFeel();
    void initSlider (juce::Slider&, juce::Label&, const juce::String& name, double min, double max, double step);
    void wireControls();
    void syncControlsFromSettings();

    // view state
    bool showingLive() const;
    TimeAxis& activeAxis();
    void refreshDisplayedResult();
    void updateScrollBar();
    void clampPassageStart();
    void setPixelsPerSecond (double newPps, double anchorSeconds);
    void fitPassageToView();
    void repaintViews();

    // data
    void recomputeCandidates();
    void onPatternChanged();
    void addCandidateToPattern (const cutbench::Candidate&);
    void applyQuickGrab();
    double gridSeconds() const;

    // actions
    void toggleRecording();
    void startPlayback();
    void chooseAudioFile();
    void loadFile (const juce::File&);
    void chooseExportFile();
    juce::File writeExportFile (const juce::File& target);
    void setStatus (const juce::String&);
    void updateButtons();

    CutbenchProcessor& processor;
    juce::LookAndFeel_V4 lookAndFeel;

    DisplayOptions display;
    TimeAxis passageAxis, liveAxis;

    AnalysisView analysisView;
    PatternEditor patternEditor;
    MidiDragSource dragSource;
    juce::ScrollBar scrollBar { false };

    juce::TextButton loadButton { "Load audio..." }, recordButton { "Record" }, reanalyseButton { "Re-analyse" },
        playButton { "Play" }, stopButton { "Stop" }, applyButton { "Apply QuickGrab" }, hostBpmButton { "Host" },
        undoButton { "Undo" }, redoButton { "Redo" }, quantiseButton { "Quantise" },
        deleteButton { "Delete" }, clearButton { "Clear" }, exportButton { "Export .mid..." },
        folderButton { "Open folder" };

    juce::ToggleButton liveButton { "Live view" }, refinedToggle { "Harmonic filter" };
    juce::ComboBox previewModeBox, gridBox;

    juce::Slider sensitivitySlider, quickGrabSlider, lowSlider, highSlider, zoomSlider, smoothingSlider, bpmSlider;
    juce::Label sensitivityLabel, quickGrabLabel, lowLabel, highLabel, zoomLabel, smoothingLabel, bpmLabel,
        statusLabel, previewLabel, gridLabel;

    std::shared_ptr<const cutbench::AnalysisResult> liveResult, passageResult;
    std::vector<cutbench::Candidate> candidates, quickSelection;

    bool liveMode = true;
    bool updatingControls = false;
    uint32_t lastLiveVersion = 0, lastPassageVersion = 0;
    int lastStateVersion = 0;
    juce::String statusMessage;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CutbenchEditor)
};
