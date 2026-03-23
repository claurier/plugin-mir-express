#include "PluginEditor.h"

//==============================================================================
// WaveformDisplay
//==============================================================================

WaveformDisplay::WaveformDisplay (MirExpressAudioProcessor& p)
    : processor (p)
{
    startTimerHz (kTimerHz);
}

WaveformDisplay::~WaveformDisplay()
{
    stopTimer();
}

//==============================================================================
void WaveformDisplay::resized()
{
    resetColumns (getWidth());
}

void WaveformDisplay::resetColumns (int newNumColumns)
{
    numColumns = juce::jmax (1, newNumColumns);

    colMin.assign (static_cast<size_t> (numColumns), 0.0f);
    colMax.assign (static_cast<size_t> (numColumns), 0.0f);

    writeColumn     = 0;
    runningMin      = 0.0f;
    runningMax      = 0.0f;
    samplesInColumn = 0;
}

//==============================================================================
void WaveformDisplay::timerCallback()
{
    drainFifo();
    repaint();
}

void WaveformDisplay::drainFifo()
{
    if (numColumns <= 0)
        return;

    const double sr = processor.getSampleRate();
    if (sr < 1.0)
        return;

    // Recompute samples-per-column if sample rate or column count changed.
    const int newSamplesPerColumn =
        juce::jmax (1, static_cast<int> (sr * kDisplaySeconds / numColumns));

    if (newSamplesPerColumn != samplesPerColumn)
    {
        samplesPerColumn = newSamplesPerColumn;
        runningMin       = 0.0f;
        runningMax       = 0.0f;
        samplesInColumn  = 0;
    }

    auto& fifo   = processor.waveformFifo;
    const auto& buf = processor.waveformBuffer;

    int start1, size1, start2, size2;
    fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);

    auto pushSample = [&] (float s)
    {
        runningMin = juce::jmin (runningMin, s);
        runningMax = juce::jmax (runningMax, s);

        if (++samplesInColumn >= samplesPerColumn)
        {
            colMin[static_cast<size_t> (writeColumn)] = runningMin;
            colMax[static_cast<size_t> (writeColumn)] = runningMax;
            writeColumn     = (writeColumn + 1) % numColumns;
            runningMin      = 0.0f;
            runningMax      = 0.0f;
            samplesInColumn = 0;
        }
    };

    for (int i = 0; i < size1; ++i)
        pushSample (buf[static_cast<size_t> (start1 + i)]);
    for (int i = 0; i < size2; ++i)
        pushSample (buf[static_cast<size_t> (start2 + i)]);

    fifo.finishedRead (size1 + size2);
}

//==============================================================================
void WaveformDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.fillAll (juce::Colours::black);

    if (numColumns <= 0)
        return;

    // ── Zero / centre line ────────────────────────────────────────────────
    const float midY = bounds.getCentreY();
    g.setColour (juce::Colour (0xff303030));
    g.drawHorizontalLine (juce::roundToInt (midY), bounds.getX(), bounds.getRight());

    // ── Waveform columns ──────────────────────────────────────────────────
    // We iterate from oldest column (writeColumn) to newest (writeColumn - 1),
    // mapping to screen x left → right.
    g.setColour (juce::Colours::white);

    for (int col = 0; col < numColumns; ++col)
    {
        const int bufIdx = (writeColumn + col) % numColumns;

        const float yMin = juce::jmap (colMin[static_cast<size_t> (bufIdx)],
                                       -1.0f, 1.0f,
                                       bounds.getBottom(), bounds.getY());
        const float yMax = juce::jmap (colMax[static_cast<size_t> (bufIdx)],
                                       -1.0f, 1.0f,
                                       bounds.getBottom(), bounds.getY());

        // drawVerticalLine expects (x, top, bottom) where top <= bottom.
        g.drawVerticalLine (col,
                            juce::jmin (yMin, yMax),
                            juce::jmax (yMin, yMax));
    }
}

//==============================================================================
// MirExpressAudioProcessorEditor
//==============================================================================

MirExpressAudioProcessorEditor::MirExpressAudioProcessorEditor (MirExpressAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      waveformDisplay (p)
{
    addAndMakeVisible (waveformDisplay);
    setSize (kDefaultWidth, kDefaultHeight);
}

MirExpressAudioProcessorEditor::~MirExpressAudioProcessorEditor() = default;

//==============================================================================
void MirExpressAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));
}

void MirExpressAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    waveformDisplay.setBounds (area.removeFromTop (area.getHeight() / 4));
}
