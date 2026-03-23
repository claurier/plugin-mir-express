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
    // Safe to call directly: resized() runs on the message thread, and paint()
    // (GL thread) holds the MM lock while executing, so the two are mutually
    // exclusive — no concurrent access to waveformImage is possible.
    resetColumns (getWidth());
}

void WaveformDisplay::resetColumns (int newNumColumns)
{
    numColumns = juce::jmax (1, newNumColumns);
    const int height = juce::jmax (1, getHeight());

    waveformImage = juce::Image (juce::Image::ARGB, numColumns, height, true);
    juce::Graphics g (waveformImage);
    g.fillAll (juce::Colours::black);

    writeColumn    = 0;
    runningMin     = 0.0f;
    runningMax     = 0.0f;
    samplesInColumn = 0;
}

//==============================================================================
void WaveformDisplay::timerCallback()
{
    newColumnsReady = 0;
    drainFifo();
    if (newColumnsReady > 0)
        repaint();
}

//==============================================================================
void WaveformDisplay::drainFifo()
{
    if (numColumns <= 0 || !waveformImage.isValid())
        return;

    const double sr = processor.getSampleRate();
    if (sr < 1.0)
        return;

    const int newSamplesPerColumn =
        juce::jmax (1, static_cast<int> (sr * kDisplaySeconds / numColumns));

    if (newSamplesPerColumn != samplesPerColumn)
    {
        samplesPerColumn = newSamplesPerColumn;
        runningMin       = 0.0f;
        runningMax       = 0.0f;
        samplesInColumn  = 0;
    }

    auto& fifo      = processor.waveformFifo;
    const auto& buf = processor.waveformBuffer;

    const int numReady = fifo.getNumReady();
    if (numReady == 0)
        return;

    juce::Graphics ig (waveformImage);
    const float imgH = static_cast<float> (waveformImage.getHeight());

    int start1, size1, start2, size2;
    fifo.prepareToRead (numReady, start1, size1, start2, size2);

    auto pushSample = [&] (float s)
    {
        runningMin = juce::jmin (runningMin, s);
        runningMax = juce::jmax (runningMax, s);

        if (++samplesInColumn >= samplesPerColumn)
        {
            ig.setColour (juce::Colours::black);
            ig.fillRect (writeColumn, 0, 1, (int) imgH);

            const float yMin = juce::jmap (runningMin, -1.0f, 1.0f, imgH, 0.0f);
            const float yMax = juce::jmap (runningMax, -1.0f, 1.0f, imgH, 0.0f);
            ig.setColour (juce::Colours::white);
            ig.drawVerticalLine (writeColumn,
                                 juce::jmin (yMin, yMax),
                                 juce::jmax (yMin, yMax));

            writeColumn     = (writeColumn + 1) % numColumns;
            runningMin      = 0.0f;
            runningMax      = 0.0f;
            samplesInColumn = 0;
            ++newColumnsReady;
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
    if (!waveformImage.isValid() || numColumns <= 0)
    {
        g.fillAll (juce::Colours::black);
        return;
    }

    const int w = getWidth();
    const int h = getHeight();

    // Blit the circular image buffer in two halves:
    //   [writeColumn … end]  → left side of screen  (oldest data)
    //   [0 … writeColumn-1]  → right side of screen (newest data)
    const int rightWidth = numColumns - writeColumn;

    if (rightWidth > 0)
        g.drawImage (waveformImage,
                     0,           0, rightWidth,  h,
                     writeColumn, 0, rightWidth,  h);

    if (writeColumn > 0)
        g.drawImage (waveformImage,
                     rightWidth, 0, writeColumn, h,
                     0,          0, writeColumn, h);

    // Zero line drawn on top.
    g.setColour (juce::Colour (0xff303030));
    g.drawHorizontalLine (h / 2, 0.0f, static_cast<float> (w));
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

void MirExpressAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));
}

void MirExpressAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    waveformDisplay.setBounds (area.removeFromTop (area.getHeight() / 4));
}
