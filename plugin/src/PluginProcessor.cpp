#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

//==============================================================================
CutbenchProcessor::CutbenchProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    monoScratch.assign ((size_t) kMaxChunk, 0.0f);
    mixScratch.assign ((size_t) kMaxChunk, 0.0f);
    patternPreview.prepare (44100.0);
    refreshStateCache();
}

CutbenchProcessor::~CutbenchProcessor() = default;

//==============================================================================
void CutbenchProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate.store (newSampleRate);
    analysisEngine.setSampleRate (newSampleRate);
    patternPreview.prepare (newSampleRate);
    previewPlaying.store (false);
    wasPlaying = false;
    lastPlayedPattern = false;
}

bool CutbenchProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void CutbenchProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (auto bpm = position->getBpm())
                lastHostBpm.store (*bpm);

    // 1. Analysis tap: down-mix the untouched input and hand it to the analysis thread.
    //    Lock-free, no allocation, no I/O.
    for (int start = 0; start < numSamples; start += kMaxChunk)
    {
        const int n = juce::jmin (kMaxChunk, numSamples - start);
        const float* left = buffer.getReadPointer (0) + start;
        float* mono = monoScratch.data();

        if (numChannels >= 2)
        {
            const float* right = buffer.getReadPointer (1) + start;
            for (int i = 0; i < n; ++i)
                mono[i] = 0.5f * (left[i] + right[i]);
        }
        else
        {
            for (int i = 0; i < n; ++i)
                mono[i] = left[i];
        }

        analysisEngine.pushAudio (mono, n);
    }

    // 2. Preview. Unless the user pressed Play the host audio passes through bit-exact.
    if (! previewPlaying.load())
    {
        if (wasPlaying)
        {
            patternPreview.prepare (sampleRate.load()); // hard reset of the preview voices
            wasPlaying = false;
            lastPlayedPattern = false;
        }
        return;
    }

    wasPlaying = true;

    // Never wait for the message thread: if it is swapping data right now, skip this block's preview.
    const juce::SpinLock::ScopedTryLockType lock (previewLock);
    if (! lock.isLocked())
        return;

    const double sr = sampleRate.load();

    const long long seek = seekRequest.exchange (-1);
    if (seek >= 0)
    {
        playPosition = seek;
        patternPreview.prepare (sr);
        lastPlayedPattern = false;
    }

    const int mode = previewMode.load();
    const bool haveOriginal = passagePtr != nullptr && ! passagePtr->empty();
    const bool playOriginal = haveOriginal && (mode == 0 || mode == 2);
    const bool playPattern = ! previewNotes.empty() && (mode == 1 || mode == 2);

    if (! playPattern && lastPlayedPattern)
        patternPreview.prepare (sr);
    lastPlayedPattern = playPattern;

    const double ratio = passageRate / sr; // passage samples per output sample
    long long endSample = 0;
    if (playOriginal)
        endSample = (long long) std::ceil ((double) passagePtr->size() / ratio);
    if (playPattern)
        endSample = std::max (endSample, (long long) std::ceil ((patternEndSec + 0.3) * sr));

    // While the captured passage is auditioned it replaces the host input (A/B comparison).
    // Without a passage the pattern is simply mixed on top of the host audio.
    if (haveOriginal)
        buffer.clear();

    for (int start = 0; start < numSamples; start += kMaxChunk)
    {
        const int n = juce::jmin (kMaxChunk, numSamples - start);
        float* mix = mixScratch.data();
        std::fill (mix, mix + n, 0.0f);

        if (playOriginal)
        {
            const std::vector<float>& audio = *passagePtr;
            const size_t size = audio.size();

            for (int i = 0; i < n; ++i)
            {
                const double index = (double) (playPosition + i) * ratio;
                if (index < 0.0)
                    continue;

                const size_t i0 = (size_t) index;
                if (i0 >= size)
                    break;

                const float frac = (float) (index - (double) i0);
                const float a = audio[i0];
                const float b = i0 + 1 < size ? audio[i0 + 1] : 0.0f;
                mix[i] = 0.8f * (a + (b - a) * frac);
            }
        }

        if (playPattern)
            patternPreview.render (previewNotes.data(), previewNotes.size(), playPosition, n, mix);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* dest = buffer.getWritePointer (ch) + start;
            for (int i = 0; i < n; ++i)
                dest[i] += mix[i];
        }

        playPosition += n;
    }

    previewSample.store (playPosition);

    if (playPosition >= endSample)
        previewPlaying.store (false);
}

juce::AudioProcessorEditor* CutbenchProcessor::createEditor()
{
    return new CutbenchEditor (*this);
}

//==============================================================================
void CutbenchProcessor::patternChanged()
{
    std::vector<cutbench::PreviewNote> notes;
    notes.reserve (pattern.notes().size());
    double end = 0.0;

    for (const auto& n : pattern.notes())
    {
        notes.push_back ({ n.midi, n.velocity, n.startSec, n.endSec() });
        end = std::max (end, n.endSec());
    }

    {
        const juce::SpinLock::ScopedLockType lock (previewLock);
        previewNotes.swap (notes);
        patternEndSec = end;
    }
    // `notes` now holds the previous data and is freed here, outside the lock and off the audio thread.

    refreshStateCache();
}

void CutbenchProcessor::settingsChanged()
{
    previewMode.store (currentSettings.previewMode);
    refreshStateCache();
}

//==============================================================================
bool CutbenchProcessor::loadAudioFile (const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Could not open this file as audio (unsupported format or damaged file).";
        return false;
    }

    if (reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0 || reader->numChannels == 0)
    {
        error = "The file does not contain any audio.";
        return false;
    }

    constexpr double maxSeconds = 300.0;
    if ((double) reader->lengthInSamples / reader->sampleRate > maxSeconds)
    {
        error = "The file is longer than 5 minutes. Please load a shorter excerpt.";
        return false;
    }

    const int length = (int) reader->lengthInSamples;
    const int numCh = (int) juce::jmin (reader->numChannels, 2u);

    juce::AudioBuffer<float> temp (numCh, length);
    if (! reader->read (&temp, 0, length, 0, true, true))
    {
        error = "Reading the audio data failed.";
        return false;
    }

    std::vector<float> mono ((size_t) length, 0.0f);
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* src = temp.getReadPointer (ch);
        for (int i = 0; i < length; ++i)
            mono[(size_t) i] += src[i];
    }

    const float scale = 1.0f / (float) numCh;
    for (auto& s : mono)
        s *= scale;

    setPassage (std::move (mono), reader->sampleRate, file.getFileName());
    return true;
}

void CutbenchProcessor::setPassage (std::vector<float>&& mono, double rate, const juce::String& label)
{
    adoptPassage (std::make_shared<const std::vector<float>> (std::move (mono)), rate, label);
}

void CutbenchProcessor::adoptPassage (std::shared_ptr<const std::vector<float>> audio, double rate, const juce::String& label)
{
    stopPreview();

    if (rate <= 0.0)
        rate = 44100.0;

    {
        const juce::SpinLock::ScopedLockType lock (previewLock);
        passagePtr = audio.get();
        passageRate = rate;
    }

    passageOwner = audio; // the previous passage is released here, after the audio thread stopped seeing it
    passageOwnerRate = rate;
    currentPassageLabel = label;

    analysisEngine.analysePassage (std::move (audio), rate);
}

void CutbenchProcessor::reanalysePassage()
{
    if (passageOwner != nullptr)
        analysisEngine.analysePassage (passageOwner, passageOwnerRate);
}

double CutbenchProcessor::passageSeconds() const noexcept
{
    return passageOwner != nullptr && passageOwnerRate > 0.0
               ? (double) passageOwner->size() / passageOwnerRate
               : 0.0;
}

void CutbenchProcessor::startRecording()
{
    stopPreview();
    analysisEngine.startRecording();
}

void CutbenchProcessor::stopRecording()
{
    analysisEngine.stopRecording();
}

bool CutbenchProcessor::pollFinishedRecording()
{
    double rate = 0.0;
    auto recording = analysisEngine.takeFinishedRecording (rate);
    if (recording == nullptr)
        return false;

    adoptPassage (std::move (recording), rate, "Recorded passage");
    return true;
}

//==============================================================================
void CutbenchProcessor::startPreview (double fromSeconds)
{
    if (analysisEngine.isRecording())
        return;

    const long long s = (long long) std::llround (juce::jmax (0.0, fromSeconds) * sampleRate.load());
    previewSample.store (s);
    seekRequest.store (s);
    previewPlaying.store (true);
}

void CutbenchProcessor::stopPreview()
{
    previewPlaying.store (false);
}

double CutbenchProcessor::previewPositionSeconds() const noexcept
{
    const double sr = sampleRate.load();
    return sr > 0.0 ? (double) previewSample.load() / sr : 0.0;
}

juce::File CutbenchProcessor::exportDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Cutbench Notes")
        .getChildFile ("Exports");
}

//==============================================================================
void CutbenchProcessor::refreshStateCache()
{
    juce::XmlElement xml ("CutbenchNotesState");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("sensitivity", (double) currentSettings.sensitivity);
    xml.setAttribute ("quickGrab", (double) currentSettings.quickGrab);
    xml.setAttribute ("lowNote", currentSettings.lowNote);
    xml.setAttribute ("highNote", currentSettings.highNote);
    xml.setAttribute ("bpm", currentSettings.bpm);
    xml.setAttribute ("grid", currentSettings.gridIndex);
    xml.setAttribute ("smoothing", (double) currentSettings.smoothing);
    xml.setAttribute ("previewMode", currentSettings.previewMode);

    auto* notes = xml.createNewChildElement ("Notes");
    for (const auto& n : pattern.notes())
    {
        auto* e = notes->createNewChildElement ("Note");
        e->setAttribute ("midi", n.midi);
        e->setAttribute ("start", n.startSec);
        e->setAttribute ("dur", n.durationSec);
        e->setAttribute ("vel", n.velocity);
    }

    const juce::String text = xml.toString();
    const juce::ScopedLock lock (stateLock);
    stateCache = text;
}

void CutbenchProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::String text;
    {
        const juce::ScopedLock lock (stateLock);
        text = stateCache;
    }

    if (auto xml = juce::parseXML (text))
        copyXmlToBinary (*xml, destData);
}

void CutbenchProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("CutbenchNotesState"))
        return;

    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        applyState (*xml);
    }
    else
    {
        juce::MessageManager::callAsync ([weak = juce::WeakReference<CutbenchProcessor> (this), text = xml->toString()]
        {
            if (weak != nullptr)
                if (auto parsed = juce::parseXML (text))
                    weak->applyState (*parsed);
        });
    }
}

void CutbenchProcessor::applyState (const juce::XmlElement& xml)
{
    Settings s;
    s.sensitivity = juce::jlimit (0.0f, 1.0f, (float) xml.getDoubleAttribute ("sensitivity", 0.6));
    s.quickGrab = juce::jlimit (0.0f, 1.0f, (float) xml.getDoubleAttribute ("quickGrab", 0.5));
    s.lowNote = juce::jlimit (cutbench::kMidiC0, cutbench::kMidiC8, xml.getIntAttribute ("lowNote", 24));
    s.highNote = juce::jlimit (cutbench::kMidiC0, cutbench::kMidiC8, xml.getIntAttribute ("highNote", 96));
    if (s.highNote <= s.lowNote)
        s.highNote = juce::jmin (cutbench::kMidiC8, s.lowNote + 12);
    s.bpm = cutbench::clampBpm (xml.getDoubleAttribute ("bpm", 120.0));
    s.gridIndex = juce::jlimit (0, 3, xml.getIntAttribute ("grid", 2));
    s.smoothing = juce::jlimit (0.0f, 1.0f, (float) xml.getDoubleAttribute ("smoothing", 0.4));
    s.previewMode = juce::jlimit (0, 2, xml.getIntAttribute ("previewMode", 2));
    currentSettings = s;

    std::vector<cutbench::Note> notes;
    if (auto* list = xml.getChildByName ("Notes"))
    {
        for (auto* e : list->getChildIterator())
        {
            cutbench::Note n;
            n.midi = e->getIntAttribute ("midi", 60);
            n.startSec = e->getDoubleAttribute ("start", 0.0);
            n.durationSec = e->getDoubleAttribute ("dur", 0.25);
            n.velocity = e->getIntAttribute ("vel", 100);
            notes.push_back (n);

            if (notes.size() >= 4096)
                break;
        }
    }

    pattern.replaceAll (notes); // clamps every value
    pattern.clearHistory();
    previewMode.store (currentSettings.previewMode);
    patternChanged();
    stateVer.fetch_add (1);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CutbenchProcessor();
}
