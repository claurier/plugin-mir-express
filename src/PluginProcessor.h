#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
 * MirExpressAudioProcessor
 *
 * A bypass audio effect. Audio data is forwarded to an AudioVisualiserComponent
 * so the editor can display a live scrolling waveform.
 *
 * AudioVisualiserComponent is thread-safe: pushBuffer() can be called from the
 * audio thread while the component repaints on the message thread.
 */
class MirExpressAudioProcessor final : public juce::AudioProcessor
{
public:
    MirExpressAudioProcessor();
    ~MirExpressAudioProcessor() override;

    //==========================================================================
    // AudioProcessor interface
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool   acceptsMidi() const override;
    bool   producesMidi() const override;
    bool   isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int  getNumPrograms() override;
    int  getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Waveform visualiser — fed from the audio thread, rendered by the editor.
    juce::AudioVisualiserComponent visualiser { 2 };  // 2 channels (stereo)

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MirExpressAudioProcessor)
};
