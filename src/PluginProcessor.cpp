#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
MirExpressAudioProcessor::MirExpressAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    visualiser.setSamplesPerBlock (256);
    visualiser.setBufferSize (1024);
}

MirExpressAudioProcessor::~MirExpressAudioProcessor() = default;

//==============================================================================
const juce::String MirExpressAudioProcessor::getName() const      { return JucePlugin_Name; }
bool  MirExpressAudioProcessor::acceptsMidi() const                { return false; }
bool  MirExpressAudioProcessor::producesMidi() const               { return false; }
bool  MirExpressAudioProcessor::isMidiEffect() const               { return false; }
double MirExpressAudioProcessor::getTailLengthSeconds() const      { return 0.0; }
int   MirExpressAudioProcessor::getNumPrograms()                   { return 1; }
int   MirExpressAudioProcessor::getCurrentProgram()                { return 0; }
void  MirExpressAudioProcessor::setCurrentProgram (int)            {}
const juce::String MirExpressAudioProcessor::getProgramName (int)  { return {}; }
void  MirExpressAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void MirExpressAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    visualiser.clear();
    descriptorAnalyser.prepare (sampleRate);
}

void MirExpressAudioProcessor::releaseResources() {}

//==============================================================================
void MirExpressAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    visualiser.pushBuffer (buffer);       // waveform display
    descriptorAnalyser.pushSamples (buffer);    // descriptor analysis ring buffer

    // Bypass: audio passes through unchanged.
}

//==============================================================================
bool MirExpressAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* MirExpressAudioProcessor::createEditor()
{
    return new MirExpressAudioProcessorEditor (*this);
}

//==============================================================================
void MirExpressAudioProcessor::getStateInformation (juce::MemoryBlock& /*destData*/) {}
void MirExpressAudioProcessor::setStateInformation (const void* /*data*/, int /*sizeInBytes*/) {}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MirExpressAudioProcessor();
}
