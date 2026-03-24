#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "DescriptorDisplay.h"

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
    DescriptorDisplay         descriptorDisplay;

    static constexpr int kDefaultWidth  = 600;
    static constexpr int kDefaultHeight = 525;  // 75 waveform + 225 mood + 225 dissonance
    static constexpr int kWaveformHeight = 75;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MirExpressAudioProcessorEditor)
};
