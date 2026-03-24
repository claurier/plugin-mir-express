#pragma once

#include <JuceHeader.h>
#include "MoodAnalyser.h"

//==============================================================================
/**
 * MoodDisplay
 *
 * Draws four animated vertical bars (Angry / Calm / Happy / Sad) in the lower
 * three-quarters of the plugin window.
 *
 * A 30 Hz timer reads the latest atomic results from MoodAnalyser and
 * exponentially smooths the displayed values toward them, producing a ~200 ms
 * visual response time without jarring jumps.
 */
class MoodDisplay final : public juce::Component,
                          private juce::Timer
{
public:
    explicit MoodDisplay (MoodAnalyser& analyser);
    ~MoodDisplay() override;

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    MoodAnalyser& analyser;

    // Smoothed display values (message thread only).
    float displayAngry = 0.0f;
    float displayCalm  = 0.0f;
    float displayHappy = 0.0f;
    float displaySad   = 0.0f;

    // alpha ≈ 0.15 at 30 fps gives ~200 ms exponential response.
    static constexpr float kSmoothAlpha = 0.15f;
    static constexpr int   kTimerHz     = 30;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MoodDisplay)
};
