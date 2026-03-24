#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MoodDisplay.h"

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
    MoodDisplay               moodDisplay;

    static constexpr int kDefaultWidth  = 600;
    static constexpr int kDefaultHeight = 300;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MirExpressAudioProcessorEditor)
};
