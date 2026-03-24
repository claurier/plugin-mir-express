#include "DescriptorDisplay.h"

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

    const juce::Colour kDissonanceColour { 0xff555555 };  // dark gray
}

//==============================================================================
DescriptorDisplay::DescriptorDisplay (DescriptorAnalyser& a)
    : analyser (a)
{
    startTimerHz (kTimerHz);
}

DescriptorDisplay::~DescriptorDisplay()
{
    stopTimer();
}

//==============================================================================
void DescriptorDisplay::timerCallback()
{
    // Exponential smoothing toward the latest analysis results.
    displayAngry += kMoodSmoothAlpha * (analyser.getAngry() - displayAngry);
    displayCalm  += kMoodSmoothAlpha * (analyser.getCalm()  - displayCalm);
    displayHappy += kMoodSmoothAlpha * (analyser.getHappy() - displayHappy);
    displaySad   += kMoodSmoothAlpha * (analyser.getSad()   - displaySad);

    const float targetDissonance = juce::jlimit (0.0f, 1.0f,
                                                 analyser.getDissonance() * kDissonanceDisplayScale);
    displayDissonance += kDissonanceSmoothAlpha * (targetDissonance - displayDissonance);

    repaint();
}

//==============================================================================
void DescriptorDisplay::paint (juce::Graphics& g)
{
    const auto  bounds = getLocalBounds().toFloat();
    const float w      = bounds.getWidth();
    const float h      = bounds.getHeight();

    g.fillAll (juce::Colour (0xff1a1a1a));

    // Thin separator at the very top (boundary with waveform).
    g.setColour (juce::Colour (0xff303030));
    g.drawHorizontalLine (0, 0.0f, w);

    // ── Mood bars (top portion) ───────────────────────────────────────────
    const float moodH    = h - kDissonanceRowH;
    const int   numBars  = 4;
    const float sectionW = w / static_cast<float> (numBars);
    const float barW      = sectionW * 0.38f;              // original bar width
    const float gap       = (sectionW - barW) * 0.5f;      // half the original inter-bar gap
    const float stride    = barW + gap;
    const float groupW    = 4.0f * barW + 3.0f * gap;      // total width of all bars + gaps
    const float startX    = (w - groupW) * 0.5f;           // center the group horizontally
    const float labelW    = stride;                         // label area per bar
    const float topPad    = 18.0f;
    const float labelH    = 16.0f;
    const float valueH    = 14.0f;
    const float bottomPad = labelH + valueH + 10.0f;
    const float barH      = moodH - topPad - bottomPad;

    const std::array<float, 4> values {
        displayAngry, displayCalm, displayHappy, displaySad
    };

    for (int i = 0; i < numBars; ++i)
    {
        const float barX  = startX + static_cast<float> (i) * stride;
        const float lcx   = barX + barW * 0.5f;   // label centre x
        const float value = juce::jlimit (0.0f, 1.0f, values[i]);

        // Background track
        g.setColour (juce::Colour (0xff2e2e2e));
        g.fillRoundedRectangle (barX, topPad, barW, barH, 5.0f);

        // Coloured fill (bottom-up)
        if (value > 0.001f)
        {
            const float fillH = barH * value;
            g.setColour (kBars[i].colour);
            g.fillRoundedRectangle (barX, topPad + barH - fillH, barW, fillH, 5.0f);
        }

        // Mood label — centred under the bar
        const float labelY = topPad + barH + 8.0f;
        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::Font (11.0f).boldened());
        g.drawText (kBars[i].label,
                    juce::Rectangle<float> (lcx - labelW * 0.5f, labelY, labelW, labelH),
                    juce::Justification::centred);

        // Numeric percentage
        g.setColour (kBars[i].colour.brighter (0.3f));
        g.setFont (juce::Font (10.0f));
        g.drawText (juce::String (juce::roundToInt (value * 100.0f)) + "%",
                    juce::Rectangle<float> (lcx - labelW * 0.5f, labelY + labelH, labelW, valueH),
                    juce::Justification::centred);
    }

    // ── Dissonance row (bottom section) ──────────────────────────────────
    const float rowY = moodH;

    // Separator line
    g.setColour (juce::Colour (0xff303030));
    g.drawHorizontalLine (static_cast<int> (rowY), 0.0f, w);

    // Dissonance bar: same x as ANGRY bar (startX), same layout constants → identical size.
    const float dBarH  = kDissonanceRowH - topPad - bottomPad;
    const float dValue = juce::jlimit (0.0f, 1.0f, displayDissonance);
    const float dlcx   = startX + barW * 0.5f;   // label centre x (same as ANGRY)

    // Background track
    g.setColour (juce::Colour (0xff2e2e2e));
    g.fillRoundedRectangle (startX, rowY + topPad, barW, dBarH, 5.0f);

    // Dark-gray fill (bottom-up)
    if (dValue > 0.001f)
    {
        const float fillH = dBarH * dValue;
        g.setColour (kDissonanceColour);
        g.fillRoundedRectangle (startX, rowY + topPad + dBarH - fillH, barW, fillH, 5.0f);
    }

    // Label — centred under the bar, same y-offset as mood labels
    const float dLabelY = rowY + topPad + dBarH + 8.0f;
    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::Font (11.0f).boldened());
    g.drawText ("DISSONANCE",
                juce::Rectangle<float> (dlcx - labelW * 0.5f, dLabelY, labelW, labelH),
                juce::Justification::centred);

    // Numeric percentage
    g.setColour (kDissonanceColour.brighter (0.4f));
    g.setFont (juce::Font (10.0f));
    g.drawText (juce::String (juce::roundToInt (dValue * 100.0f)) + "%",
                juce::Rectangle<float> (dlcx - labelW * 0.5f, dLabelY + labelH, labelW, valueH),
                juce::Justification::centred);
}
