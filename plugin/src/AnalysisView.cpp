#include "AnalysisView.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr int kRulerHeight = 22;
}

AnalysisView::AnalysisView (DisplayOptions& opts, TimeAxis* timeAxis) : options (opts), axis (timeAxis)
{
    setOpaque (true);
}

void AnalysisView::setTimeAxis (TimeAxis* newAxis)
{
    axis = newAxis;
    repaint();
}

void AnalysisView::setResult (std::shared_ptr<const cutbench::AnalysisResult> newResult, bool isLive)
{
    result = std::move (newResult);
    live = isLive;
    inspectTime = -1.0;
    rebuildImage();
    repaint();
}

void AnalysisView::setCandidates (std::vector<cutbench::Candidate> all, std::vector<bool> selected, float newThreshold)
{
    candidates = std::move (all);
    candidateSelected = std::move (selected);
    candidateSelected.resize (candidates.size(), false);

    if (std::abs (newThreshold - threshold) > 1.0e-6f)
    {
        threshold = newThreshold;
        rebuildImage();
    }

    repaint();
}

void AnalysisView::setPattern (const std::vector<cutbench::Note>* notes)
{
    pattern = notes;
    repaint();
}

void AnalysisView::setPlayhead (double seconds, bool visible)
{
    if (visible != playheadVisible || (visible && std::abs (seconds - playheadSec) > 1.0e-4))
    {
        playheadSec = seconds;
        playheadVisible = visible;
        repaint();
    }
}

void AnalysisView::displayOptionsChanged()
{
    rebuildImage();
    repaint();
}

//==============================================================================
juce::Rectangle<int> AnalysisView::plotArea() const
{
    auto b = getLocalBounds();
    b.removeFromTop (kRulerHeight);
    b.removeFromLeft (ui::kKeyStripWidth);
    return b;
}

double AnalysisView::visibleSeconds() const noexcept
{
    const double pps = axis != nullptr ? axis->pixelsPerSecond : 100.0;
    return pps > 0.0 ? (double) plotArea().getWidth() / pps : 0.0;
}

double AnalysisView::contentStart() const noexcept
{
    if (result == nullptr || result->empty())
        return 0.0;

    const double hopSec = (double) result->cfg.hopSize / result->cfg.sampleRate;
    return std::max (0.0, result->frameTime (0) - 0.5 * hopSec);
}

double AnalysisView::contentEnd() const noexcept
{
    if (result == nullptr || result->empty())
        return 0.0;

    const double hopSec = (double) result->cfg.hopSize / result->cfg.sampleRate;
    const double lastFrame = result->frameTime (result->numFrames - 1) + 0.5 * hopSec;
    return live ? lastFrame : std::max (lastFrame, result->duration());
}

float AnalysisView::timeToX (double t) const noexcept
{
    const double pps = axis != nullptr ? axis->pixelsPerSecond : 100.0;
    const double start = axis != nullptr ? axis->startSec : 0.0;
    return (float) plotArea().getX() + (float) ((t - start) * pps);
}

double AnalysisView::xToTime (float x) const noexcept
{
    const double pps = axis != nullptr ? axis->pixelsPerSecond : 100.0;
    const double start = axis != nullptr ? axis->startSec : 0.0;
    return start + (double) (x - (float) plotArea().getX()) / pps;
}

float AnalysisView::rowHeight() const noexcept
{
    const int rows = std::max (1, options.highMidi - options.lowMidi + 1);
    return (float) plotArea().getHeight() / (float) rows;
}

float AnalysisView::midiToTopY (int midi) const noexcept
{
    return (float) plotArea().getY() + (float) (options.highMidi - midi) * rowHeight();
}

int AnalysisView::yToMidi (float y) const noexcept
{
    const float rowH = rowHeight();
    if (rowH <= 0.0f)
        return options.lowMidi;

    const int row = (int) std::floor ((y - (float) plotArea().getY()) / rowH);
    return juce::jlimit (options.lowMidi, options.highMidi, options.highMidi - row);
}

//==============================================================================
void AnalysisView::rebuildImage()
{
    image = juce::Image();
    if (result == nullptr || result->empty())
        return;

    const auto& r = *result;
    const int frames = r.numFrames;
    const int notes = r.numNotes();

    const std::vector<float>& source = options.showRefined ? r.refined : r.raw;
    const std::vector<float> smoothed = cutbench::smoothAlongTime (source, frames, notes, options.smoothing);

    image = juce::Image (juce::Image::ARGB, frames, notes, false);
    juce::Image::BitmapData bits (image, juce::Image::BitmapData::writeOnly);

    for (int f = 0; f < frames; ++f)
    {
        for (int n = 0; n < notes; ++n)
        {
            const float v = smoothed[(size_t) f * (size_t) notes + (size_t) n];
            float t = std::sqrt (juce::jlimit (0.0f, 1.0f, v / 0.35f));

            if (options.showRefined && v < threshold)
                t *= 0.45f; // picks below the sensitivity threshold are shown dimmed

            bits.setPixelColour (f, notes - 1 - n, ui::heatColour (t));
        }
    }
}

//==============================================================================
void AnalysisView::paint (juce::Graphics& g)
{
    g.fillAll (ui::background());

    const auto plot = plotArea();
    const int low = options.lowMidi, high = options.highMidi;
    if (axis == nullptr || plot.isEmpty() || high < low)
        return;

    const float rowH = rowHeight();
    const juce::Rectangle<int> keyArea (0, plot.getY(), ui::kKeyStripWidth, plot.getHeight());
    const juce::Rectangle<int> rulerArea (plot.getX(), 0, plot.getWidth(), plot.getY());

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (plot);

        // row stripes
        for (int m = low; m <= high; ++m)
        {
            g.setColour (cutbench::isBlackKey (m) ? ui::background().darker (0.25f) : ui::panel());
            g.fillRect (juce::Rectangle<float> ((float) plot.getX(), midiToTopY (m), (float) plot.getWidth(), rowH));

            if (cutbench::pitchClass (m) == 0)
            {
                g.setColour (ui::outline());
                g.drawHorizontalLine ((int) (midiToTopY (m) + rowH), (float) plot.getX(), (float) plot.getRight());
            }
        }

        // heat map
        if (image.isValid() && result != nullptr && ! result->empty())
        {
            const auto& cfg = result->cfg;
            const double hopSec = (double) cfg.hopSize / cfg.sampleRate;
            const double t0 = result->frameTime (0) - 0.5 * hopSec;
            const double t1 = result->frameTime (result->numFrames - 1) + 0.5 * hopSec;

            const int visibleHigh = juce::jmin (high, cfg.maxMidi);
            const int visibleLow = juce::jmax (low, cfg.minMidi);

            if (visibleHigh >= visibleLow)
            {
                const juce::Rectangle<int> sourceRect (0, cfg.maxMidi - visibleHigh, result->numFrames, visibleHigh - visibleLow + 1);
                const juce::Image part = image.getClippedImage (sourceRect);

                const float x0 = timeToX (t0), x1 = timeToX (t1);
                const juce::Rectangle<float> dest (x0, midiToTopY (visibleHigh), x1 - x0, (float) (visibleHigh - visibleLow + 1) * rowH);

                g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
                g.drawImage (part, dest, juce::RectanglePlacement::stretchToFit);
            }
        }

        // time grid
        {
            const double spacing = ui::niceTickSpacing (axis->pixelsPerSecond, 70.0);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            for (double t = std::ceil (axis->startSec / spacing) * spacing; timeToX (t) < (float) plot.getRight(); t += spacing)
                g.drawVerticalLine ((int) timeToX (t), (float) plot.getY(), (float) plot.getBottom());
        }

        // MIDI pattern overlay (read-only here; edited in the pattern editor below)
        if (pattern != nullptr)
        {
            for (const auto& n : *pattern)
            {
                if (n.midi < low || n.midi > high)
                    continue;

                const juce::Rectangle<float> r (timeToX (n.startSec), midiToTopY (n.midi),
                                                std::max (2.0f, (float) n.durationSec * (float) axis->pixelsPerSecond), rowH);
                g.setColour (ui::accent().withAlpha (n.selected ? 0.8f : 0.5f));
                g.fillRect (r);
                g.setColour (ui::accent());
                g.drawRect (r, 1.0f);
            }
        }

        // candidate notes (heuristic detections; click one to add it to the pattern)
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            const auto& c = candidates[i];
            if (c.midi < low || c.midi > high)
                continue;

            const juce::Rectangle<float> r (timeToX (c.startSec), midiToTopY (c.midi),
                                            std::max (3.0f, (float) c.duration() * (float) axis->pixelsPerSecond), rowH);

            const bool selected = candidateSelected[i];
            if (selected)
            {
                g.setColour (ui::teal().withAlpha (0.18f + 0.45f * c.confidence));
                g.fillRect (r);
            }

            g.setColour ((selected ? juce::Colours::white : ui::teal()).withAlpha (selected ? 0.9f : 0.45f));
            g.drawRect (r, 1.0f);

            if (c.isUncertain() && rowH >= 9.0f && r.getWidth() > 12.0f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.setFont (juce::jmin (11.0f, rowH));
                g.drawText ("?", r.toNearestInt(), juce::Justification::centredLeft, false);
            }
        }

        // playhead and inspect cursor
        if (playheadVisible)
        {
            g.setColour (juce::Colours::white);
            g.drawVerticalLine ((int) timeToX (playheadSec), (float) plot.getY(), (float) plot.getBottom());
        }

        if (inspectTime >= 0.0)
        {
            g.setColour (ui::accent().withAlpha (0.8f));
            g.drawVerticalLine ((int) timeToX (inspectTime), (float) plot.getY(), (float) plot.getBottom());
        }

        if (hovering)
        {
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawHorizontalLine ((int) hoverPos.y, (float) plot.getX(), (float) plot.getRight());
            g.drawVerticalLine ((int) hoverPos.x, (float) plot.getY(), (float) plot.getBottom());
        }
    }

    drawKeys (g, keyArea);
    drawRuler (g, rulerArea, plot);

    // empty state
    if (result == nullptr || result->empty())
    {
        g.setColour (ui::textDim());
        g.setFont (14.0f);
        g.drawFittedText ("No analysis yet.\nPlay audio through the plugin (live view), record a passage, or load an audio file.",
                          plot.reduced (30), juce::Justification::centred, 3);
    }

    // readout
    juce::String readout;
    if (hovering && plot.contains (hoverPos.toInt()))
        readout = describeAt (xToTime (hoverPos.x), yToMidi (hoverPos.y));
    else if (inspectTime >= 0.0)
        readout = describeAt (inspectTime, (low + high) / 2);

    if (readout.isNotEmpty())
    {
        g.setFont (12.0f);
        const int w = juce::jmin (plot.getWidth() - 10, 16 + readout.length() * 7); // ~7 px per character at 12 pt
        const juce::Rectangle<int> box (plot.getX() + 6, plot.getY() + 6, w, 20);
        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.fillRoundedRectangle (box.toFloat(), 4.0f);
        g.setColour (ui::text());
        g.drawText (readout, box.reduced (6, 0), juce::Justification::centredLeft, true);
    }

    g.setColour (ui::outline());
    g.drawRect (getLocalBounds());
}

void AnalysisView::drawKeys (juce::Graphics& g, juce::Rectangle<int> keyArea) const
{
    const float rowH = rowHeight();
    const int low = options.lowMidi, high = options.highMidi;

    for (int m = low; m <= high; ++m)
    {
        const float y = midiToTopY (m);
        const bool black = cutbench::isBlackKey (m);

        g.setColour (juce::Colour (0xffdfe2e8));
        g.fillRect (juce::Rectangle<float> ((float) keyArea.getX(), y, (float) keyArea.getWidth(), rowH));

        if (black)
        {
            g.setColour (juce::Colour (0xff22252b));
            g.fillRect (juce::Rectangle<float> ((float) keyArea.getX(), y, (float) keyArea.getWidth() * 0.62f, rowH));
        }

        g.setColour (juce::Colour (0xff8d929c));
        g.drawHorizontalLine ((int) y, (float) keyArea.getX(), (float) keyArea.getRight());

        const bool isC = cutbench::pitchClass (m) == 0;
        if (isC || (rowH >= 13.0f && ! black))
        {
            g.setColour (black ? juce::Colours::white : juce::Colour (0xff22252b));
            g.setFont (juce::jlimit (8.0f, 11.0f, rowH * 0.9f));
            g.drawText (juce::String (cutbench::midiToName (m)),
                        juce::Rectangle<float> ((float) keyArea.getX(), y + rowH * 0.5f - 6.0f, (float) keyArea.getWidth() - 4.0f, 12.0f).toNearestInt(),
                        juce::Justification::centredRight, false);
        }
    }

    g.setColour (ui::outline());
    g.drawVerticalLine (keyArea.getRight() - 1, (float) keyArea.getY(), (float) keyArea.getBottom());
}

void AnalysisView::drawRuler (juce::Graphics& g, juce::Rectangle<int> rulerArea, juce::Rectangle<int> plot) const
{
    g.setColour (ui::panelLight());
    g.fillRect (getLocalBounds().removeFromTop (kRulerHeight));

    g.setFont (11.0f);
    const double spacing = ui::niceTickSpacing (axis->pixelsPerSecond, 70.0);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (rulerArea);

    for (double t = std::ceil (axis->startSec / spacing) * spacing; timeToX (t) < (float) plot.getRight(); t += spacing)
    {
        const int x = (int) timeToX (t);
        g.setColour (ui::outline());
        g.drawVerticalLine (x, (float) kRulerHeight - 6.0f, (float) kRulerHeight);
        g.setColour (ui::textDim());
        g.drawText (ui::formatTime (t), x + 3, 0, 70, kRulerHeight - 2, juce::Justification::centredLeft, false);
    }
}

juce::String AnalysisView::describeAt (double t, int midi) const
{
    if (result == nullptr || result->empty())
        return {};

    const int frame = result->frameAtTime (t);
    juce::String s = juce::String (t, 3) + " s   " + juce::String (cutbench::midiToName (midi))
                     + "   salience " + juce::String (result->rawAt (frame, midi), 2);

    const auto picks = cutbench::frameTopNotes (*result, frame, 4);
    if (picks.empty())
    {
        s += "   no note picked here";
    }
    else
    {
        s += "   picks:";
        for (const auto& p : picks)
            s += " " + juce::String (cutbench::midiToName (p.midi)) + " (" + juce::String (p.refined, 2) + ")";
    }

    return s;
}

const cutbench::Candidate* AnalysisView::candidateAt (float x, float y) const
{
    if (axis == nullptr)
        return nullptr;

    const float rowH = rowHeight();
    for (size_t i = candidates.size(); i-- > 0;)
    {
        const auto& c = candidates[i];
        if (c.midi < options.lowMidi || c.midi > options.highMidi)
            continue;

        const juce::Rectangle<float> r (timeToX (c.startSec), midiToTopY (c.midi),
                                        std::max (3.0f, (float) c.duration() * (float) axis->pixelsPerSecond), rowH);
        if (r.contains (x, y))
            return &candidates[i];
    }

    return nullptr;
}

//==============================================================================
void AnalysisView::resized() {}

void AnalysisView::mouseMove (const juce::MouseEvent& e)
{
    hovering = plotArea().contains (e.getPosition());
    hoverPos = e.position;
    repaint();
}

void AnalysisView::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    repaint();
}

void AnalysisView::mouseDown (const juce::MouseEvent& e)
{
    if (! plotArea().contains (e.getPosition()))
        return;

    if (const auto* c = candidateAt (e.position.x, e.position.y))
    {
        if (onAddCandidate)
            onAddCandidate (*c);
        return;
    }

    inspectTime = xToTime (e.position.x);
    repaint();
}

void AnalysisView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        if (onZoom)
            onZoom (std::pow (2.0, (double) wheel.deltaY * 2.5), xToTime (e.position.x));
    }
    else
    {
        const float delta = wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY;
        if (onPan)
            onPan (-(double) delta * visibleSeconds());
    }
}
