#include "PluginEditor.h"

//==============================================================================
MirExpressAudioProcessorEditor::MirExpressAudioProcessorEditor (MirExpressAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p)
{
    addAndMakeVisible (audioProcessor.visualiser);
    setSize (kDefaultWidth, kDefaultHeight);
}

MirExpressAudioProcessorEditor::~MirExpressAudioProcessorEditor()
{
    // Remove the visualiser from the component hierarchy before the editor is
    // destroyed; the component itself is owned by the processor and must
    // outlive any number of editor open/close cycles.
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
}
