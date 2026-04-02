#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "DescriptorDisplay.h"
#include "OscManager.h"
#include "OscSetupOverlay.h"

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
    OscManager                oscManager;
    DescriptorDisplay         descriptorDisplay;
    juce::TextButton          oscButton { "OSC" };
    OscSetupOverlay           oscOverlay;

    static constexpr int kDefaultWidth   = 560;
    static constexpr int kDefaultHeight  = 525;
    static constexpr int kWaveformHeight = 75;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MirExpressAudioProcessorEditor)
};
