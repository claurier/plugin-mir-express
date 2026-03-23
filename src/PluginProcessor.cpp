#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
MirExpressAudioProcessor::MirExpressAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // Each display column averages 256 audio samples; 1024 columns are kept,
    // giving roughly 6 s of history at 44.1 kHz (1024 * 256 / 44100 ≈ 5.9 s).
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
void MirExpressAudioProcessor::prepareToPlay (double /*sampleRate*/, int /*samplesPerBlock*/)
{
    visualiser.clear();
}

void MirExpressAudioProcessor::releaseResources() {}

//==============================================================================
void MirExpressAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    // Feed the visualiser (thread-safe; pushBuffer is audio-thread safe).
    visualiser.pushBuffer (buffer);

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
