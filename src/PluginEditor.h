#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
 * MirExpressAudioProcessorEditor
 *
 * The plugin window. The top quarter displays a live stereo waveform via
 * juce::AudioVisualiserComponent (owned by the processor so that pushBuffer()
 * can be called from processBlock() at any time, even with no editor open).
 */
class MirExpressAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit MirExpressAudioProcessorEditor (MirExpressAudioProcessor&);
    ~MirExpressAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MirExpressAudioProcessor& audioProcessor;

    static constexpr int kDefaultWidth  = 600;
    static constexpr int kDefaultHeight = 300;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MirExpressAudioProcessorEditor)
};
