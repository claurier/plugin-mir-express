#pragma once

#include <JuceHeader.h>
#include "DescriptorAnalyser.h"

//==============================================================================
/**
 * DescriptorDisplay
 *
 * Upper section — four animated vertical bars (Angry / Calm / Happy / Sad).
 * Lower section — one dissonance bar (dark gray) below the leftmost mood bar.
 *
 * A 30 Hz timer reads the latest atomic results from DescriptorAnalyser and
 * exponentially smooths all displayed values, producing a fluid response
 * without jarring jumps.
 */
class DescriptorDisplay final : public juce::Component,
                                private juce::Timer
{
public:
    explicit DescriptorDisplay (DescriptorAnalyser& analyser);
    ~DescriptorDisplay() override;

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    DescriptorAnalyser& analyser;

    // Smoothed display values (message thread only).
    float displayAngry      = 0.0f;
    float displayCalm       = 0.0f;
    float displayHappy      = 0.0f;
    float displaySad        = 0.0f;
    float displayDissonance = 0.0f;
    float displayBTrackBPM    = 0.0f;  // BTrack continuous tempo estimate
    float displayBTrackBeat   = 0.0f;  // 0..1 flash intensity, fades over ~150 ms

    float  displayBeatThisBPM      = 0.0f;
    double displayBeatThisLastBeat = -1.0e9;
    float  displayBeatThisBeat     = 0.0f;

    float  displayAubioBPM        = 0.0f;
    float  displayAubioConfidence = 0.0f;
    double displayAubioBeatTime   = -1.0e9;
    float  displayAubioBeat       = 0.0f;  // 0..1 flash intensity

    float  displayOnsetDensity    = 0.0f;  // onsets / s, smoothed
    float  displayRMS             = 0.0f;  // normalised dBFS, smoothed
    float  displayCentroidMIR     = 0.0f;  // Hz, smoothed
    float  displayCentroidAubio   = 0.0f;  // Hz, smoothed

    // Mood bars:      alpha ≈ 0.15 at 30 fps  → ~200 ms response.
    // Dissonance bar: alpha ≈ 0.25 at 30 fps  → ~120 ms response (slightly faster).
    static constexpr float kMoodSmoothAlpha       = 0.15f;
    static constexpr float kDissonanceSmoothAlpha = 0.25f;

    // Raw dissonance values from the Plomp-Levelt model are typically in [0, 0.5].
    // Multiply by this factor to map to a [0, 1] display range.
    static constexpr float kDissonanceDisplayScale = 5.0f;

    // Height of the dissonance row at the bottom of this component.
    static constexpr float kDissonanceRowH = 225.0f;

    static constexpr int kTimerHz = 30;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DescriptorDisplay)
};
