#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
MirExpressAudioProcessor::MirExpressAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

MirExpressAudioProcessor::~MirExpressAudioProcessor() = default;

//==============================================================================
const juce::String MirExpressAudioProcessor::getName() const       { return JucePlugin_Name; }
bool  MirExpressAudioProcessor::acceptsMidi() const                 { return false; }
bool  MirExpressAudioProcessor::producesMidi() const                { return false; }
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
    // Reset the FIFO when the session restarts.
    waveformFifo.reset();
}

void MirExpressAudioProcessor::releaseResources() {}

//==============================================================================
void MirExpressAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // ── Push mono-mixed samples to the waveform FIFO ──────────────────────
    // We only push as many samples as the FIFO has free space for; if the UI
    // thread is slow we drop the oldest data rather than blocking.
    if (numChannels > 0)
    {
        const int toWrite = juce::jmin (numSamples, waveformFifo.getFreeSpace());

        if (toWrite > 0)
        {
            int start1, size1, start2, size2;
            waveformFifo.prepareToWrite (toWrite, start1, size1, start2, size2);

            const float channelScale = 1.0f / static_cast<float> (numChannels);

            for (int i = 0; i < size1; ++i)
            {
                float sum = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    sum += buffer.getSample (ch, i);
                waveformBuffer[static_cast<size_t> (start1 + i)] = sum * channelScale;
            }
            for (int i = 0; i < size2; ++i)
            {
                float sum = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    sum += buffer.getSample (ch, size1 + i);
                waveformBuffer[static_cast<size_t> (start2 + i)] = sum * channelScale;
            }

            waveformFifo.finishedWrite (size1 + size2);
        }
    }

    // ── Bypass: audio passes through unchanged ────────────────────────────
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
