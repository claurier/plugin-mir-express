#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
 * WaveformDisplay
 *
 * Renders a scrolling peak-hold waveform (min/max per pixel column) in the
 * top quarter of the plugin window.
 *
 * Rendering strategy — Timer + offscreen image circular buffer:
 *
 *   A 60 Hz Timer drives updates entirely on the message thread:
 *     1. drainFifo()  — consumes all pending samples from the processor's
 *                       lock-free AbstractFifo, accumulates min/max, and once
 *                       a column is complete draws it directly into waveformImage
 *                       at writeColumn, then advances writeColumn.
 *     2. repaint()    — called only when at least one new column was written,
 *                       keeping CPU use proportional to actual audio activity.
 *
 *   Inside paint() (message thread):
 *     The image is composited in two halves to implement the circular-buffer
 *     scroll with no pixel shifting at all.
 *
 *   Thread safety:
 *     - waveformImage is only ever touched on the message thread (timer +
 *       paint), so no additional locking is required.
 *     - The AbstractFifo is single-producer (audio thread) / single-consumer
 *       (message thread via timer), which is exactly its designed use case.
 *     - resized() is also on the message thread, so resetColumns() is safe.
 */
class WaveformDisplay final : public juce::Component,
                              private juce::Timer
{
public:
    explicit WaveformDisplay (MirExpressAudioProcessor& processor);
    ~WaveformDisplay() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;
    void drainFifo();
    void resetColumns (int newNumColumns);

    MirExpressAudioProcessor& processor;

    // Offscreen image acting as a circular column buffer.
    juce::Image waveformImage;
    int   numColumns       = 0;
    int   writeColumn      = 0;  ///< Next column to be overwritten in waveformImage
    int   newColumnsReady  = 0;  ///< Columns written since last repaint; repaint only if > 0

    // Sample accumulation for the column currently being filled.
    float runningMin       = 0.0f;
    float runningMax       = 0.0f;
    int   samplesInColumn  = 0;
    int   samplesPerColumn = 1;  ///< Recomputed whenever sample rate / column count changes

    static constexpr double kDisplaySeconds = 3.0;
    static constexpr int    kTimerHz        = 60;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformDisplay)
};

//==============================================================================
class MirExpressAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit MirExpressAudioProcessorEditor (MirExpressAudioProcessor&);
    ~MirExpressAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MirExpressAudioProcessor& audioProcessor;
    WaveformDisplay waveformDisplay;

    static constexpr int kDefaultWidth  = 600;
    static constexpr int kDefaultHeight = 300;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MirExpressAudioProcessorEditor)
};
