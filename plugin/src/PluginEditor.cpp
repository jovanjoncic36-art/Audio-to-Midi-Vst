#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
const char* const kDefaultStatus =
    "Detections are heuristic estimates - overlapping pitches and octaves can be wrong. Check them by ear before exporting.";

bool isAudioFileName (const juce::String& name)
{
    const auto ext = juce::File (name).getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac";
}
} // namespace

//==============================================================================
CutbenchEditor::CutbenchEditor (CutbenchProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processor (p),
      analysisView (display, &liveAxis),
      patternEditor (p.noteEditor(), passageAxis)
{
    setLookAndFeel (&lookAndFeel);
    configureLookAndFeel();

    const Settings& s = processor.settings();
    display.lowMidi = s.lowNote;
    display.highMidi = s.highNote;
    display.smoothing = s.smoothing;

    addAndMakeVisible (analysisView);
    addAndMakeVisible (patternEditor);
    addAndMakeVisible (dragSource);
    addAndMakeVisible (scrollBar);
    scrollBar.addListener (this);

    for (auto* b : { &loadButton, &recordButton, &reanalyseButton, &playButton, &stopButton, &applyButton, &hostBpmButton,
                     &undoButton, &redoButton, &quantiseButton, &deleteButton, &clearButton, &exportButton, &folderButton })
        addAndMakeVisible (*b);

    addAndMakeVisible (liveButton);
    addAndMakeVisible (refinedToggle);
    addAndMakeVisible (previewModeBox);
    addAndMakeVisible (gridBox);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (previewLabel);
    addAndMakeVisible (gridLabel);

    initSlider (sensitivitySlider, sensitivityLabel, "Sensitivity", 0.0, 1.0, 0.01);
    initSlider (quickGrabSlider, quickGrabLabel, "QuickGrab", 0.0, 1.0, 0.01);
    initSlider (lowSlider, lowLabel, "Lowest note", cutbench::kMidiC0, cutbench::kMidiC8, 1.0);
    initSlider (highSlider, highLabel, "Highest note", cutbench::kMidiC0, cutbench::kMidiC8, 1.0);
    initSlider (zoomSlider, zoomLabel, "Zoom", 10.0, 1000.0, 1.0);
    initSlider (smoothingSlider, smoothingLabel, "Smoothing", 0.0, 1.0, 0.01);
    initSlider (bpmSlider, bpmLabel, "BPM", 20.0, 300.0, 0.1);

    zoomSlider.setSkewFactorFromMidPoint (120.0);
    zoomSlider.setNumDecimalPlacesToDisplay (0);
    zoomSlider.setTextValueSuffix (" px/s");
    for (auto* noteSlider : { &lowSlider, &highSlider })
    {
        noteSlider->setTextBoxIsEditable (false);
        noteSlider->textFromValueFunction = [] (double v) { return juce::String (cutbench::midiToName ((int) v)); };
    }

    gridBox.addItem ("1/4", 1);
    gridBox.addItem ("1/8", 2);
    gridBox.addItem ("1/16", 3);
    gridBox.addItem ("1/32", 4);
    previewModeBox.addItem ("Original", 1);
    previewModeBox.addItem ("MIDI", 2);
    previewModeBox.addItem ("Original + MIDI", 3);
    previewLabel.setText ("Preview", juce::dontSendNotification);
    gridLabel.setText ("Grid", juce::dontSendNotification);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setMinimumHorizontalScale (0.7f);

    liveButton.setToggleState (true, juce::dontSendNotification);
    refinedToggle.setToggleState (display.showRefined, juce::dontSendNotification);

    analysisView.setPattern (&processor.noteEditor().notes());
    patternEditor.gridSeconds = [this] { return gridSeconds(); };

    wireControls();
    syncControlsFromSettings();
    lastStateVersion = processor.stateVersion();

    // Pick up whatever the engine already has (editor re-opened, passage present).
    liveResult = processor.engine().liveSnapshot();
    lastLiveVersion = processor.engine().liveVersion();
    passageResult = processor.engine().passageResult();
    lastPassageVersion = processor.engine().passageVersion();
    if (passageResult != nullptr && ! passageResult->empty())
    {
        liveMode = false;
        liveButton.setToggleState (false, juce::dontSendNotification);
    }

    setStatus (kDefaultStatus);
    setResizable (true, true);
    setResizeLimits (900, 640, 2000, 1300);
    setSize (1060, 720);

    recomputeCandidates();
    refreshDisplayedResult();
    fitPassageToView();
    updateButtons();
    startTimerHz (25);
}

CutbenchEditor::~CutbenchEditor()
{
    stopTimer();
    scrollBar.removeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void CutbenchEditor::configureLookAndFeel()
{
    auto& lf = lookAndFeel;
    lf.setColour (juce::ResizableWindow::backgroundColourId, ui::background());
    lf.setColour (juce::TextButton::buttonColourId, ui::panelLight());
    lf.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc0392b));
    lf.setColour (juce::TextButton::textColourOffId, ui::text());
    lf.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    lf.setColour (juce::Slider::thumbColourId, ui::accent());
    lf.setColour (juce::Slider::trackColourId, ui::teal().darker (0.3f));
    lf.setColour (juce::Slider::backgroundColourId, ui::panelLight());
    lf.setColour (juce::Slider::textBoxTextColourId, ui::text());
    lf.setColour (juce::Slider::textBoxOutlineColourId, ui::outline());
    lf.setColour (juce::Label::textColourId, ui::text());
    lf.setColour (juce::ComboBox::backgroundColourId, ui::panelLight());
    lf.setColour (juce::ComboBox::textColourId, ui::text());
    lf.setColour (juce::ComboBox::outlineColourId, ui::outline());
    lf.setColour (juce::ComboBox::arrowColourId, ui::text());
    lf.setColour (juce::PopupMenu::backgroundColourId, ui::panel());
    lf.setColour (juce::PopupMenu::textColourId, ui::text());
    lf.setColour (juce::ToggleButton::textColourId, ui::text());
    lf.setColour (juce::ToggleButton::tickColourId, ui::accent());
    lf.setColour (juce::ScrollBar::thumbColourId, ui::outline().brighter (0.3f));
}

void CutbenchEditor::initSlider (juce::Slider& slider, juce::Label& label, const juce::String& name,
                                 double min, double max, double step)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 20);
    slider.setRange (min, max, step);
    addAndMakeVisible (slider);

    label.setText (name, juce::dontSendNotification);
    label.setColour (juce::Label::textColourId, ui::textDim());
    addAndMakeVisible (label);
}

void CutbenchEditor::wireControls()
{
    sensitivitySlider.onValueChange = [this]
    {
        if (updatingControls) return;
        processor.settings().sensitivity = (float) sensitivitySlider.getValue();
        processor.settingsChanged();
        recomputeCandidates();
    };

    quickGrabSlider.onValueChange = [this]
    {
        if (updatingControls) return;
        processor.settings().quickGrab = (float) quickGrabSlider.getValue();
        processor.settingsChanged();
        recomputeCandidates();
    };

    auto rangeChanged = [this] (bool lowMoved)
    {
        if (updatingControls) return;

        int lo = (int) lowSlider.getValue(), hi = (int) highSlider.getValue();
        if (hi <= lo)
        {
            const juce::ScopedValueSetter<bool> guard (updatingControls, true);

            if (lowMoved)
            {
                hi = juce::jmin (cutbench::kMidiC8, lo + 1);
                highSlider.setValue (hi, juce::dontSendNotification);
            }
            else
            {
                lo = juce::jmax (cutbench::kMidiC0, hi - 1);
                lowSlider.setValue (lo, juce::dontSendNotification);
            }
        }

        auto& s = processor.settings();
        s.lowNote = lo;
        s.highNote = hi;
        display.lowMidi = lo;
        display.highMidi = hi;
        processor.settingsChanged();
        analysisView.displayOptionsChanged();
        recomputeCandidates();
    };
    lowSlider.onValueChange = [rangeChanged] { rangeChanged (true); };
    highSlider.onValueChange = [rangeChanged] { rangeChanged (false); };

    zoomSlider.onValueChange = [this]
    {
        if (updatingControls) return;
        const double center = activeAxis().startSec + analysisView.visibleSeconds() * 0.5;
        setPixelsPerSecond (zoomSlider.getValue(), center);
    };

    smoothingSlider.onValueChange = [this]
    {
        if (updatingControls) return;
        display.smoothing = (float) smoothingSlider.getValue();
        processor.settings().smoothing = display.smoothing;
        processor.settingsChanged();
        analysisView.displayOptionsChanged();
    };

    bpmSlider.onValueChange = [this]
    {
        if (updatingControls) return;
        processor.settings().bpm = bpmSlider.getValue();
        processor.settingsChanged();
        patternEditor.repaint();
    };

    gridBox.onChange = [this]
    {
        if (updatingControls) return;
        processor.settings().gridIndex = gridBox.getSelectedId() - 1;
        processor.settingsChanged();
        patternEditor.repaint();
    };

    previewModeBox.onChange = [this]
    {
        if (updatingControls) return;
        processor.settings().previewMode = previewModeBox.getSelectedId() - 1;
        processor.setPreviewMode (processor.settings().previewMode);
        processor.settingsChanged();
    };

    refinedToggle.onClick = [this]
    {
        display.showRefined = refinedToggle.getToggleState();
        analysisView.displayOptionsChanged();
    };

    liveButton.onClick = [this]
    {
        liveMode = liveButton.getToggleState();
        refreshDisplayedResult();
        recomputeCandidates();
    };

    analysisView.onAddCandidate = [this] (const cutbench::Candidate& c) { addCandidateToPattern (c); };

    analysisView.onPan = [this] (double delta)
    {
        if (showingLive()) return;
        passageAxis.startSec += delta;
        clampPassageStart();
        updateScrollBar();
        repaintViews();
    };

    analysisView.onZoom = [this] (double factor, double anchor)
    {
        setPixelsPerSecond (activeAxis().pixelsPerSecond * factor, anchor);
    };

    patternEditor.onChanged = [this] { onPatternChanged(); };

    loadButton.onClick = [this] { chooseAudioFile(); };
    recordButton.onClick = [this] { toggleRecording(); };

    reanalyseButton.onClick = [this]
    {
        if (! processor.hasPassage())
        {
            setStatus ("There is no recorded or loaded passage to analyse yet.");
            return;
        }
        processor.reanalysePassage();
        setStatus ("Analysing passage...");
    };

    playButton.onClick = [this] { startPlayback(); };
    stopButton.onClick = [this] { processor.stopPreview(); };
    applyButton.onClick = [this] { applyQuickGrab(); };

    hostBpmButton.onClick = [this]
    {
        const double host = processor.hostBpm();
        if (host > 0.0)
        {
            bpmSlider.setValue (host, juce::sendNotificationSync);
            setStatus ("Tempo taken from the host: " + juce::String (host, 2) + " BPM.");
        }
        else
        {
            setStatus ("The host did not report a tempo (or the standalone app is running). Set the BPM manually.");
        }
    };

    undoButton.onClick = [this] { processor.noteEditor().undo(); onPatternChanged(); };
    redoButton.onClick = [this] { processor.noteEditor().redo(); onPatternChanged(); };

    quantiseButton.onClick = [this]
    {
        processor.noteEditor().quantise (gridSeconds(), 1.0, true, false);
        onPatternChanged();
    };

    deleteButton.onClick = [this]
    {
        processor.noteEditor().removeSelected();
        onPatternChanged();
    };

    clearButton.onClick = [this]
    {
        processor.noteEditor().replaceAll ({});
        onPatternChanged();
    };

    exportButton.onClick = [this] { chooseExportFile(); };

    folderButton.onClick = [this]
    {
        const auto dir = CutbenchProcessor::exportDirectory();
        dir.createDirectory();
        dir.startAsProcess();
    };

    dragSource.prepareFile = [this]
    {
        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S");
        return writeExportFile (CutbenchProcessor::exportDirectory().getChildFile ("CutbenchNotes_" + stamp + ".mid"));
    };
}

void CutbenchEditor::syncControlsFromSettings()
{
    const juce::ScopedValueSetter<bool> guard (updatingControls, true);
    const Settings& s = processor.settings();

    sensitivitySlider.setValue (s.sensitivity, juce::dontSendNotification);
    quickGrabSlider.setValue (s.quickGrab, juce::dontSendNotification);
    lowSlider.setValue (s.lowNote, juce::dontSendNotification);
    highSlider.setValue (s.highNote, juce::dontSendNotification);
    smoothingSlider.setValue (s.smoothing, juce::dontSendNotification);
    bpmSlider.setValue (s.bpm, juce::dontSendNotification);
    zoomSlider.setValue (passageAxis.pixelsPerSecond, juce::dontSendNotification);
    gridBox.setSelectedId (s.gridIndex + 1, juce::dontSendNotification);
    previewModeBox.setSelectedId (s.previewMode + 1, juce::dontSendNotification);

    display.lowMidi = s.lowNote;
    display.highMidi = s.highNote;
    display.smoothing = s.smoothing;
    processor.setPreviewMode (s.previewMode);
}

//==============================================================================
void CutbenchEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::background());

    auto top = getLocalBounds().reduced (10).removeFromTop (30);
    g.setColour (ui::accent());
    g.setFont (15.0f);
    g.drawText ("CUTBENCH NOTES", top.removeFromRight (150), juce::Justification::centredRight, false);
}

void CutbenchEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    // top bar
    {
        auto row = area.removeFromTop (30);
        row.removeFromRight (160); // title is painted here

        auto place = [&row] (juce::Component& c, int width)
        {
            c.setBounds (row.removeFromLeft (width));
            row.removeFromLeft (6);
        };

        place (loadButton, 104);
        place (recordButton, 84);
        place (reanalyseButton, 96);
        place (liveButton, 92);
        row.removeFromLeft (10);
        place (playButton, 64);
        place (stopButton, 64);
        place (previewLabel, 52);
        place (previewModeBox, 130);
        place (refinedToggle, 128);
    }

    area.removeFromTop (6);

    // controls
    {
        auto row = area.removeFromTop (44);
        struct Column { juce::Slider* slider; juce::Label* label; float weight; };
        const Column columns[] = {
            { &sensitivitySlider, &sensitivityLabel, 1.1f }, { &quickGrabSlider, &quickGrabLabel, 1.1f },
            { &lowSlider, &lowLabel, 0.9f },                 { &highSlider, &highLabel, 0.9f },
            { &zoomSlider, &zoomLabel, 1.1f },               { &smoothingSlider, &smoothingLabel, 0.9f },
            { &bpmSlider, &bpmLabel, 1.0f }
        };

        const int applyWidth = 118, hostWidth = 44;
        float totalWeight = 0.0f;
        for (const auto& c : columns)
            totalWeight += c.weight;

        const int flexWidth = row.getWidth() - applyWidth - hostWidth - 8 * 8;

        for (const auto& c : columns)
        {
            auto cell = row.removeFromLeft ((int) ((float) flexWidth * c.weight / totalWeight));
            c.label->setBounds (cell.removeFromTop (16));
            c.slider->setBounds (cell);
            row.removeFromLeft (8);

            if (c.slider == &quickGrabSlider)
            {
                applyButton.setBounds (row.removeFromLeft (applyWidth).removeFromBottom (26));
                row.removeFromLeft (8);
            }

            if (c.slider == &bpmSlider)
                hostBpmButton.setBounds (row.removeFromLeft (hostWidth).removeFromBottom (26));
        }
    }

    area.removeFromTop (4);
    statusLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (2);

    // bottom bar
    auto bottom = area.removeFromBottom (32);
    area.removeFromBottom (6);

    const int scrollHeight = 14;
    const int fixed = scrollHeight + 6;
    const int flexible = juce::jmax (200, area.getHeight() - fixed);
    const int analysisHeight = (int) ((float) flexible * 0.58f);

    analysisView.setBounds (area.removeFromTop (analysisHeight));
    scrollBar.setBounds (area.removeFromTop (scrollHeight).withTrimmedLeft (ui::kKeyStripWidth));
    area.removeFromTop (6);
    patternEditor.setBounds (area);

    // bottom bar contents
    {
        auto place = [&bottom] (juce::Component& c, int width)
        {
            c.setBounds (bottom.removeFromLeft (width));
            bottom.removeFromLeft (6);
        };

        place (undoButton, 62);
        place (redoButton, 62);
        place (quantiseButton, 84);
        place (gridLabel, 34);
        place (gridBox, 70);
        place (deleteButton, 68);
        place (clearButton, 60);

        folderButton.setBounds (bottom.removeFromRight (104));
        bottom.removeFromRight (6);
        exportButton.setBounds (bottom.removeFromRight (116));
        bottom.removeFromRight (6);
        dragSource.setBounds (bottom.removeFromRight (190));
    }
}

bool CutbenchEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());

    if (mods.isCommandDown() && c == 'z')
    {
        if (mods.isShiftDown())
            processor.noteEditor().redo();
        else
            processor.noteEditor().undo();

        onPatternChanged();
        return true;
    }

    return false;
}

//==============================================================================
bool CutbenchEditor::showingLive() const
{
    return liveMode || passageResult == nullptr || passageResult->empty();
}

TimeAxis& CutbenchEditor::activeAxis()
{
    return showingLive() ? liveAxis : passageAxis;
}

void CutbenchEditor::refreshDisplayedResult()
{
    const float threshold = cutbench::sensitivityToThreshold (processor.settings().sensitivity);

    if (showingLive())
    {
        analysisView.setTimeAxis (&liveAxis);
        analysisView.setResult (liveResult, true);
        analysisView.setCandidates ({}, {}, threshold);

        // follow the newest audio: latest frame at the right edge
        liveAxis.startSec = analysisView.contentEnd() - analysisView.visibleSeconds();
        analysisView.repaint();
    }
    else
    {
        analysisView.setTimeAxis (&passageAxis);
        analysisView.setResult (passageResult, false);
        clampPassageStart();
    }

    updateScrollBar();
}

void CutbenchEditor::updateScrollBar()
{
    const double start = analysisView.contentStart();
    const double end = analysisView.contentEnd();

    scrollBar.setEnabled (! showingLive());
    scrollBar.setRangeLimits (start, juce::jmax (end, start + 0.1));
    scrollBar.setCurrentRange (activeAxis().startSec, analysisView.visibleSeconds(), juce::dontSendNotification);
}

void CutbenchEditor::clampPassageStart()
{
    const double start = analysisView.contentStart();
    const double maxStart = juce::jmax (start, analysisView.contentEnd() - analysisView.visibleSeconds());
    passageAxis.startSec = juce::jlimit (start, maxStart, passageAxis.startSec);
}

void CutbenchEditor::setPixelsPerSecond (double newPps, double anchorSeconds)
{
    newPps = juce::jlimit (10.0, 1000.0, newPps);

    if (! showingLive())
        passageAxis.startSec = anchorSeconds - (anchorSeconds - passageAxis.startSec) * (passageAxis.pixelsPerSecond / newPps);

    passageAxis.pixelsPerSecond = newPps;
    liveAxis.pixelsPerSecond = newPps;

    {
        const juce::ScopedValueSetter<bool> guard (updatingControls, true);
        zoomSlider.setValue (newPps, juce::dontSendNotification);
    }

    if (showingLive())
        liveAxis.startSec = analysisView.contentEnd() - analysisView.visibleSeconds();
    else
        clampPassageStart();

    updateScrollBar();
    repaintViews();
}

void CutbenchEditor::fitPassageToView()
{
    if (passageResult == nullptr || passageResult->empty())
        return;

    const double plotPixels = analysisView.visibleSeconds() * passageAxis.pixelsPerSecond;
    const double duration = juce::jmax (0.5, passageResult->duration());

    passageAxis.startSec = 0.0;
    if (plotPixels > 10.0)
        setPixelsPerSecond (plotPixels / duration, 0.0);
}

void CutbenchEditor::repaintViews()
{
    analysisView.repaint();
    patternEditor.repaint();
}

//==============================================================================
void CutbenchEditor::recomputeCandidates()
{
    candidates.clear();
    quickSelection.clear();

    const Settings& s = processor.settings();
    const float threshold = cutbench::sensitivityToThreshold (s.sensitivity);

    if (passageResult != nullptr && ! passageResult->empty())
    {
        cutbench::CandidateParams params;
        params.sensitivity = s.sensitivity;
        params.lowMidi = s.lowNote;
        params.highMidi = s.highNote;

        candidates = cutbench::extractCandidates (*passageResult, params);
        quickSelection = cutbench::quickGrab (candidates, s.quickGrab);
    }

    std::vector<bool> selected (candidates.size(), false);
    for (size_t i = 0; i < candidates.size(); ++i)
        for (const auto& q : quickSelection)
            if (q.midi == candidates[i].midi && q.startSec == candidates[i].startSec)
            {
                selected[i] = true;
                break;
            }

    if (showingLive())
        analysisView.setCandidates ({}, {}, threshold);
    else
        analysisView.setCandidates (candidates, selected, threshold);

    updateButtons();
}

void CutbenchEditor::onPatternChanged()
{
    processor.patternChanged();
    patternEditor.refreshRange();
    repaintViews();
    updateButtons();
}

void CutbenchEditor::addCandidateToPattern (const cutbench::Candidate& c)
{
    auto& editor = processor.noteEditor();

    for (const auto& n : editor.notes())
        if (n.midi == c.midi && std::abs (n.startSec - c.startSec) < 0.02)
        {
            setStatus ("That note is already in the pattern.");
            return;
        }

    const cutbench::Note n = cutbench::candidateToNote (c);
    editor.addNote (n.midi, n.startSec, n.durationSec, n.velocity);
    onPatternChanged();

    setStatus ("Added " + juce::String (cutbench::midiToName (c.midi)) + " (score " + juce::String (c.confidence, 2)
               + (c.isUncertain() ? ", uncertain" : "") + "). Detections can be wrong - listen to the preview.");
}

void CutbenchEditor::applyQuickGrab()
{
    if (quickSelection.empty())
    {
        setStatus (passageResult == nullptr || passageResult->empty()
                       ? "Record a passage or load an audio file first."
                       : "QuickGrab found nothing at this setting. Raise QuickGrab or Sensitivity.");
        return;
    }

    std::vector<cutbench::Note> notes;
    notes.reserve (quickSelection.size());
    for (const auto& q : quickSelection)
        notes.push_back (cutbench::candidateToNote (q));

    processor.noteEditor().replaceAll (notes);
    onPatternChanged();

    setStatus (juce::String ((int) notes.size()) + " notes applied. These are heuristic detections - check them against the original (Preview: Original + MIDI).");
}

double CutbenchEditor::gridSeconds() const
{
    static const double beats[4] = { 1.0, 0.5, 0.25, 0.125 };
    const Settings& s = processor.settings();
    const double beat = 60.0 / cutbench::clampBpm (s.bpm);
    return beat * beats[juce::jlimit (0, 3, s.gridIndex)];
}

//==============================================================================
void CutbenchEditor::toggleRecording()
{
    auto& engine = processor.engine();

    if (! engine.isRecording())
    {
        const double host = processor.hostBpm();
        if (host > 0.0)
            bpmSlider.setValue (host, juce::sendNotificationSync);

        liveMode = true;
        liveButton.setToggleState (true, juce::dontSendNotification);
        refreshDisplayedResult();

        processor.startRecording();
        setStatus ("Recording the audio that reaches the plugin... press the button again to stop (maximum "
                   + juce::String ((int) cutbench::AnalysisEngine::kMaxRecordSeconds) + " s).");
    }
    else
    {
        processor.stopRecording();
        setStatus ("Finishing recording...");
    }
}

void CutbenchEditor::startPlayback()
{
    if (processor.engine().isRecording())
    {
        setStatus ("Stop the recording first.");
        return;
    }

    const int mode = processor.settings().previewMode;
    const bool havePattern = ! processor.noteEditor().notes().empty();

    if (mode == 0 && ! processor.hasPassage())
    {
        setStatus ("There is no recorded or loaded passage to play. Record or load audio, or choose MIDI.");
        return;
    }

    if (mode == 1 && ! havePattern)
    {
        setStatus ("The pattern is empty - nothing to preview.");
        return;
    }

    if (! processor.hasPassage() && ! havePattern)
    {
        setStatus ("Nothing to play yet.");
        return;
    }

    processor.setPreviewMode (mode);
    processor.startPreview (0.0);
}

void CutbenchEditor::chooseAudioFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose an audio file to analyse",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac");

    juce::Component::SafePointer<CutbenchEditor> safeThis (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (safeThis != nullptr && file.existsAsFile())
                                  safeThis->loadFile (file);
                          });
}

void CutbenchEditor::loadFile (const juce::File& file)
{
    if (processor.engine().isRecording())
    {
        setStatus ("Stop the recording before loading a file.");
        return;
    }

    juce::String error;
    if (processor.loadAudioFile (file, error))
        setStatus ("Loaded " + file.getFileName() + " - analysing...");
    else
        setStatus (error);
}

bool CutbenchEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isAudioFileName (f))
            return true;
    return false;
}

void CutbenchEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
        if (isAudioFileName (f))
        {
            loadFile (juce::File (f));
            return;
        }
}

void CutbenchEditor::chooseExportFile()
{
    if (processor.noteEditor().notes().empty())
    {
        setStatus ("The pattern is empty - nothing to export.");
        return;
    }

    const auto dir = CutbenchProcessor::exportDirectory();
    dir.createDirectory();

    chooser = std::make_unique<juce::FileChooser> ("Export MIDI file", dir.getChildFile ("CutbenchNotes.mid"), "*.mid");

    juce::Component::SafePointer<CutbenchEditor> safeThis (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safeThis] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (safeThis != nullptr && file != juce::File())
                                  safeThis->writeExportFile (file.withFileExtension ("mid"));
                          });
}

juce::File CutbenchEditor::writeExportFile (const juce::File& target)
{
    const auto& notes = processor.noteEditor().notes();
    if (notes.empty())
    {
        setStatus ("The pattern is empty - nothing to export.");
        return {};
    }

    const double bpm = cutbench::clampBpm (processor.settings().bpm);
    const auto bytes = cutbench::buildMidiFile (notes, bpm, cutbench::kDefaultPpq);

    const auto parent = target.getParentDirectory();
    if (! parent.exists() && ! parent.createDirectory().wasOk())
    {
        setStatus ("Could not create the folder " + parent.getFullPathName());
        return {};
    }

    if (! target.replaceWithData (bytes.data(), bytes.size()))
    {
        setStatus ("Could not write " + target.getFullPathName());
        return {};
    }

    setStatus ("Exported " + juce::String ((int) notes.size()) + " notes at " + juce::String (bpm, 1)
               + " BPM to " + target.getFullPathName() + ". Time 0 = start of the analysed passage.");
    return target;
}

void CutbenchEditor::setStatus (const juce::String& message)
{
    statusMessage = message;
    statusLabel.setText (message, juce::dontSendNotification);
}

void CutbenchEditor::updateButtons()
{
    const auto& editor = processor.noteEditor();
    undoButton.setEnabled (editor.canUndo());
    redoButton.setEnabled (editor.canRedo());
    deleteButton.setEnabled (editor.anySelected());
    applyButton.setEnabled (! quickSelection.empty());
    reanalyseButton.setEnabled (processor.hasPassage());
}

//==============================================================================
void CutbenchEditor::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    if (showingLive())
        return;

    passageAxis.startSec = newRangeStart;
    clampPassageStart();
    repaintViews();
}

void CutbenchEditor::timerCallback()
{
    auto& engine = processor.engine();

    // saved state was loaded by the host
    if (processor.stateVersion() != lastStateVersion)
    {
        lastStateVersion = processor.stateVersion();
        syncControlsFromSettings();
        patternEditor.refreshRange();
        recomputeCandidates();
        repaintViews();
    }

    // recording -> passage
    const bool recording = engine.isRecording();
    recordButton.setButtonText (recording ? "Stop rec" : "Record");
    recordButton.setToggleState (recording, juce::dontSendNotification);

    if (processor.pollFinishedRecording())
        setStatus ("Recording finished - analysing...");

    // new offline result
    const uint32_t passageVersion = engine.passageVersion();
    if (passageVersion != lastPassageVersion)
    {
        lastPassageVersion = passageVersion;
        passageResult = engine.passageResult();
        updateButtons();

        if (passageResult != nullptr && ! passageResult->empty())
        {
            liveMode = false;
            liveButton.setToggleState (false, juce::dontSendNotification);
            refreshDisplayedResult();
            recomputeCandidates();
            fitPassageToView();

            setStatus ("Analysis complete: " + juce::String ((int) candidates.size()) + " candidate notes at this sensitivity, "
                       + juce::String ((int) quickSelection.size()) + " selected by QuickGrab. Review before exporting.");
        }
        else
        {
            refreshDisplayedResult();
            recomputeCandidates();
            const juce::String error (engine.lastError());
            setStatus (error.isNotEmpty() ? error : juce::String ("The passage contains no analysable audio."));
        }
    }

    // live view
    const uint32_t liveVersion = engine.liveVersion();
    if (liveVersion != lastLiveVersion)
    {
        lastLiveVersion = liveVersion;
        liveResult = engine.liveSnapshot();
        if (showingLive())
            refreshDisplayedResult();
    }

    // status line while busy
    if (recording)
    {
        const double seconds = engine.recordedSeconds();
        statusLabel.setText ("Recording... " + ui::formatTime (seconds) + " / "
                                 + ui::formatTime (cutbench::AnalysisEngine::kMaxRecordSeconds),
                             juce::dontSendNotification);
    }
    else if (engine.isAnalysing())
    {
        statusLabel.setText ("Analysing passage...", juce::dontSendNotification);
    }
    else if (engine.droppedSamples() > 0 && statusMessage == kDefaultStatus)
    {
        statusLabel.setText ("Warning: the analysis thread fell behind and skipped some audio (the host audio itself is unaffected).",
                             juce::dontSendNotification);
    }
    else if (statusLabel.getText() != statusMessage)
    {
        statusLabel.setText (statusMessage, juce::dontSendNotification);
    }

    // playhead
    const bool playing = processor.isPreviewPlaying();
    const double position = processor.previewPositionSeconds();
    patternEditor.setPlayhead (position, playing);
    analysisView.setPlayhead (position, playing && ! showingLive());

    updateButtons();
}
