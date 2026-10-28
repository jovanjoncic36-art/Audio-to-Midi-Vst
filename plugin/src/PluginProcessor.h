#pragma once

#include "Common.h"

#include <atomic>
#include <memory>
#include <vector>

struct Settings
{
    float sensitivity = 0.6f;
    float quickGrab = 0.5f;
    int lowNote = 24;
    int highNote = 96;
    double bpm = 120.0;
    int gridIndex = 2;   // 0 = 1/4, 1 = 1/8, 2 = 1/16, 3 = 1/32
    float smoothing = 0.4f;
    int previewMode = 2; // 0 = original passage, 1 = MIDI pattern, 2 = both
};

class CutbenchProcessor : public juce::AudioProcessor
{
public:
    CutbenchProcessor();
    ~CutbenchProcessor() override;

    //==========================================================================
    // juce::AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Cutbench Notes"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Message-thread API used by the editor
    cutbench::AnalysisEngine& engine() noexcept { return analysisEngine; }
    cutbench::NoteEditor& noteEditor() noexcept { return pattern; }
    Settings& settings() noexcept { return currentSettings; }

    // Call after every edit of noteEditor() / settings(): updates the preview and the saved state.
    void patternChanged();
    void settingsChanged();
    int stateVersion() const noexcept { return stateVer.load(); }

    // Passage = the audio that is analysed (a recording or a loaded file).
    bool loadAudioFile (const juce::File& file, juce::String& errorOut);
    void setPassage (std::vector<float>&& mono, double sampleRate, const juce::String& label);
    void adoptPassage (std::shared_ptr<const std::vector<float>> audio, double sampleRate, const juce::String& label);
    void reanalysePassage();
    bool hasPassage() const noexcept { return passageOwner != nullptr; }
    juce::String passageLabel() const { return currentPassageLabel; }
    double passageSeconds() const noexcept;

    // Recording of the host input (or the standalone input).
    void startRecording();
    void stopRecording();
    bool pollFinishedRecording(); // true once when a recording has been turned into the passage

    // Preview of passage / MIDI pattern (explicitly started by the user).
    void startPreview (double fromSeconds);
    void stopPreview();
    bool isPreviewPlaying() const noexcept { return previewPlaying.load(); }
    double previewPositionSeconds() const noexcept;
    void setPreviewMode (int mode) noexcept { previewMode.store (mode); }

    double hostBpm() const noexcept { return lastHostBpm.load(); }
    double currentSampleRate() const noexcept { return sampleRate.load(); }

    static juce::File exportDirectory();

private:
    void refreshStateCache();
    void applyState (const juce::XmlElement& xml);

    static constexpr int kMaxChunk = 4096;

    cutbench::AnalysisEngine analysisEngine;
    cutbench::NoteEditor pattern;      // message thread only
    Settings currentSettings;          // message thread only

    // audio-thread scratch (allocated in the constructor)
    std::vector<float> monoScratch, mixScratch;

    // shared with the audio thread, guarded by previewLock (audio thread only ever try-locks)
    juce::SpinLock previewLock;
    std::vector<cutbench::PreviewNote> previewNotes;
    double patternEndSec = 0.0;
    const std::vector<float>* passagePtr = nullptr; // owned by passageOwner
    double passageRate = 44100.0;

    // message thread owner of the passage audio
    std::shared_ptr<const std::vector<float>> passageOwner;
    double passageOwnerRate = 44100.0;
    juce::String currentPassageLabel;

    // transport
    std::atomic<bool> previewPlaying { false };
    std::atomic<int> previewMode { 2 };
    std::atomic<long long> previewSample { 0 };
    std::atomic<long long> seekRequest { -1 };
    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<double> lastHostBpm { 0.0 };

    // audio thread only
    cutbench::PatternPreview patternPreview;
    bool wasPlaying = false;
    bool lastPlayedPattern = false;
    long long playPosition = 0;

    // persisted state (text cache so that getStateInformation never touches message-thread data)
    juce::CriticalSection stateLock;
    juce::String stateCache;
    std::atomic<int> stateVer { 0 };

    JUCE_DECLARE_WEAK_REFERENCEABLE (CutbenchProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CutbenchProcessor)
};
