#include "PluginEditor.h"

//==============================================================================
MirExpressAudioProcessorEditor::MirExpressAudioProcessorEditor (MirExpressAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      oscManager(),
      descriptorDisplay (p.descriptorAnalyser, oscManager),
      oscOverlay (oscManager)
{
    addAndMakeVisible (audioProcessor.visualiser);
    addAndMakeVisible (descriptorDisplay);

    // OSC button — top-right corner of the waveform strip
    oscButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff2a2a2a));
    oscButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff52e0c8));
    oscButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xff52e0c8));
    oscButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff1a1a1a));
    oscButton.onClick = [this]
    {
        oscOverlay.setVisible (! oscOverlay.isVisible());
        oscOverlay.toFront (false);
    };
    addAndMakeVisible (oscButton);

    // Overlay — hidden until OSC button is clicked, sits on top of everything
    addChildComponent (oscOverlay);

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

    auto waveformArea = area.removeFromTop (kWaveformHeight);
    audioProcessor.visualiser.setBounds (waveformArea);
    descriptorDisplay.setBounds (area);

    // OSC button — bottom-right corner, 10 px margin
    oscButton.setBounds (getWidth() - 58, getHeight() - 34, 48, 24);

    // Overlay covers the full editor
    oscOverlay.setBounds (getLocalBounds());
}
