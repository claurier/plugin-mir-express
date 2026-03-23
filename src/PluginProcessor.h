#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
 * MirExpressAudioProcessor
 *
 * A bypass audio effect that exposes a lock-free FIFO so the UI thread can
 * read audio samples for the scrolling waveform display.
 *
 * Thread-safety contract:
 *   - processBlock()   writes to waveformFifo  (audio thread, single producer)
 *   - WaveformDisplay  reads from waveformFifo (message thread, single consumer)
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
    bool  acceptsMidi() const override;
    bool  producesMidi() const override;
    bool  isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int  getNumPrograms() override;
    int  getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Waveform FIFO — consumed by WaveformDisplay on the message thread.
    // Sized for 3 s at the maximum expected sample rate (192 kHz).
    static constexpr int kWaveformFifoSize = 3 * 192000;

    juce::AbstractFifo          waveformFifo  { kWaveformFifoSize };
    std::array<float, kWaveformFifoSize> waveformBuffer {};

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MirExpressAudioProcessor)
};
