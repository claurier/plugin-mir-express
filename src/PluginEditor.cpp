#include "PluginEditor.h"

//==============================================================================
MirExpressAudioProcessorEditor::MirExpressAudioProcessorEditor (MirExpressAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      moodDisplay (p.moodAnalyser)
{
    addAndMakeVisible (audioProcessor.visualiser);
    addAndMakeVisible (moodDisplay);
    setSize (kDefaultWidth, kDefaultHeight);
}

MirExpressAudioProcessorEditor::~MirExpressAudioProcessorEditor()
{
    // visualiser is owned by the processor — just remove it from our hierarchy.
    audioProcessor.visualiser.setVisible (false);
}

//==============================================================================
void MirExpressAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));
}

void MirExpressAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    audioProcessor.visualiser.setBounds (area.removeFromTop (area.getHeight() / 4));
    moodDisplay.setBounds (area);   // remaining three-quarters
}
