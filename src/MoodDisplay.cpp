#include "MoodDisplay.h"

//==============================================================================
namespace
{
    struct MoodBar
    {
        const char*   label;
        juce::Colour  colour;
    };

    const std::array<MoodBar, 4> kBars {{
        { "ANGRY", juce::Colour (0xffe05252) },   // red
        { "CALM",  juce::Colour (0xff5285e0) },   // blue
        { "HAPPY", juce::Colour (0xffe0c052) },   // warm yellow
        { "SAD",   juce::Colour (0xff7b52e0) }    // purple
    }};
}

//==============================================================================
MoodDisplay::MoodDisplay (MoodAnalyser& a)
    : analyser (a)
{
    startTimerHz (kTimerHz);
}

MoodDisplay::~MoodDisplay()
{
    stopTimer();
}

//==============================================================================
void MoodDisplay::timerCallback()
{
    // Exponential smoothing toward the latest analysis results.
    displayAngry += kSmoothAlpha * (analyser.getAngry() - displayAngry);
    displayCalm  += kSmoothAlpha * (analyser.getCalm()  - displayCalm);
    displayHappy += kSmoothAlpha * (analyser.getHappy() - displayHappy);
    displaySad   += kSmoothAlpha * (analyser.getSad()   - displaySad);

    repaint();
}

//==============================================================================
void MoodDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();

    g.fillAll (juce::Colour (0xff1a1a1a));

    // Thin separator at the very top (boundary with waveform).
    g.setColour (juce::Colour (0xff303030));
    g.drawHorizontalLine (0, 0.0f, w);

    // Layout constants
    const int   numBars   = 4;
    const float sectionW  = w / static_cast<float> (numBars);
    const float barW      = sectionW * 0.38f;
    const float topPad    = 18.0f;
    const float labelH    = 16.0f;
    const float valueH    = 14.0f;
    const float bottomPad = labelH + valueH + 10.0f;
    const float barH      = h - topPad - bottomPad;

    const std::array<float, 4> values {
        displayAngry, displayCalm, displayHappy, displaySad
    };

    for (int i = 0; i < numBars; ++i)
    {
        const float sectionX = sectionW * static_cast<float> (i);
        const float barX     = sectionX + (sectionW - barW) * 0.5f;
        const float value    = juce::jlimit (0.0f, 1.0f, values[i]);

        // ── Background track ──────────────────────────────────────────────
        const juce::Rectangle<float> track (barX, topPad, barW, barH);
        g.setColour (juce::Colour (0xff2e2e2e));
        g.fillRoundedRectangle (track, 5.0f);

        // ── Coloured fill (bottom-up) ─────────────────────────────────────
        if (value > 0.001f)
        {
            const float fillH = barH * value;
            const juce::Rectangle<float> fill (barX,
                                               topPad + barH - fillH,
                                               barW,
                                               fillH);
            g.setColour (kBars[i].colour);
            g.fillRoundedRectangle (fill, 5.0f);
        }

        // ── Mood label ────────────────────────────────────────────────────
        const float labelY = topPad + barH + 8.0f;
        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::Font (11.0f).boldened());
        g.drawText (kBars[i].label,
                    juce::Rectangle<float> (sectionX, labelY, sectionW, labelH),
                    juce::Justification::centred);

        // ── Numeric percentage ────────────────────────────────────────────
        const int pct = juce::roundToInt (value * 100.0f);
        g.setColour (kBars[i].colour.brighter (0.3f));
        g.setFont (juce::Font (10.0f));
        g.drawText (juce::String (pct) + "%",
                    juce::Rectangle<float> (sectionX, labelY + labelH, sectionW, valueH),
                    juce::Justification::centred);
    }
}
