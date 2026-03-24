#pragma once

#include <JuceHeader.h>
#include "MoodAnalyser.h"

//==============================================================================
/**
 * MirExpressAudioProcessor
 *
 * A bypass audio effect that:
 *   - Feeds a juce::AudioVisualiserComponent for the scrolling waveform.
 *   - Feeds a MoodAnalyser for real-time mood classification via MIRLib's
 *     ExtractorMood (runs in a dedicated background thread).
 */
class MirExpressAudioProcessor final : public juce::AudioProcessor
{
public:
    MirExpressAudioProcessor();
    ~MirExpressAudioProcessor() override;

    //==========================================================================
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
    juce::AudioVisualiserComponent visualiser { 2 };   // 2 channels (stereo)

    // Mood analyser — runs ExtractorMood on a background thread.
    MoodAnalyser moodAnalyser;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MirExpressAudioProcessor)
};
