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
 * - Driven by a juce::Timer at ~30 fps.
 * - Drains the processor's waveformFifo on the message thread.
 * - Stores one min/max pair per pixel column; oldest column is on the left,
 *   newest (current instant) is on the right.
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

    // One min/max pair per horizontal pixel column.
    int   numColumns      = 0;
    int   writeColumn     = 0;          ///< Index of the next column to be written
    std::vector<float> colMin, colMax;

    // Sample accumulation state for the column currently being filled.
    float runningMin      = 0.0f;
    float runningMax      = 0.0f;
    int   samplesInColumn = 0;
    int   samplesPerColumn = 1;         ///< Recomputed whenever sample rate / size changes

    static constexpr double kDisplaySeconds = 3.0;
    static constexpr int    kTimerHz        = 30;

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
