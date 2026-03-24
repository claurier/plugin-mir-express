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

    // BPM: snap directly — no smoothing needed, updates every ~2 s.
    // 0.0f means silence/unavailable → "--" is shown.
    displayBPM           = analyser.getBPM();
    displayBPMConfidence = analyser.getBPMConfidence();

    // BTrack BPM: snap directly (continuous estimate).
    displayBTrackBPM = analyser.getBTrackBPM();

    // beat_this: snap BPM, then compute continuous beat flash via BPM extrapolation.
    // Since beat_this is batch (runs every ~4 s), we phase-lock to the last detected
    // beat and let fmod() produce a regular metronome flash between analysis runs.
    displayBeatThisBPM      = analyser.getBeatThisBPM();
    displayBeatThisLastBeat = analyser.getBeatThisLastBeat();
    if (displayBeatThisBPM > 0.0f && displayBeatThisLastBeat > -1.0e8)
    {
        const double periodMs  = 60000.0 / static_cast<double> (displayBeatThisBPM);
        const double nowMs     = juce::Time::getMillisecondCounterHiRes();
        const double elapsed   = nowMs - displayBeatThisLastBeat;
        if (elapsed >= 0.0)
        {
            // Phase within the current beat period: 0 = on beat, periodMs = next beat.
            const double phase = std::fmod (elapsed, periodMs);
            constexpr double kFlashMs = 100.0;  // visible for 100 ms after each beat
            displayBeatThisBeat = (phase < kFlashMs)
                                  ? static_cast<float> (1.0 - phase / kFlashMs)
                                  : 0.0f;
        }
        else
        {
            displayBeatThisBeat = 0.0f;
        }
    }
    else
    {
        displayBeatThisBeat = 0.0f;
    }

    // Beat flash: compute fade intensity from time elapsed since last beat.
    // The beat timestamp is back-dated to the actual audio time, so it can
    // already be up to ~250 ms old when we first read it (worker wake latency).
    // 500 ms gives a visible flash even for the earliest hop in a 250 ms batch,
    // while still fading well before the next beat at typical tempi (≥ 60 BPM).
    constexpr double kFlashDurationMs = 500.0;
    const double beatTime = analyser.getBTrackBeatTime();
    const double elapsed  = juce::Time::getMillisecondCounterHiRes() - beatTime;
    displayBTrackBeat = (elapsed >= 0.0 && elapsed < kFlashDurationMs)
                        ? static_cast<float> (1.0 - elapsed / kFlashDurationMs)
                        : 0.0f;

    // aubio: back-dated beat timestamps → direct flash, same pattern as BTrack.
    displayAubioBPM        = analyser.getAubioBPM();
    displayAubioConfidence = analyser.getAubioConfidence();
    {
        constexpr double kFlashDurationMs = 500.0;
        const double beatTime = analyser.getAubioBeatTime();
        const double elapsed  = juce::Time::getMillisecondCounterHiRes() - beatTime;
        displayAubioBeat = (elapsed >= 0.0 && elapsed < kFlashDurationMs)
                           ? static_cast<float> (1.0 - elapsed / kFlashDurationMs)
                           : 0.0f;
    }

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
    const float startX    = gap;                            // left-align with gap as left margin
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

    // ── BPM readout (second position in the dissonance row) ──────────────
    const float bpmBarX = startX + stride;
    const float bpmCX   = bpmBarX + barW * 0.5f;
    const float bpmMidY = rowY + kDissonanceRowH * 0.5f;

    // "BPM" label
    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::Font (11.0f).boldened());
    g.drawText ("BPM",
                juce::Rectangle<float> (bpmCX - labelW * 0.5f, bpmMidY - 34.0f, labelW, 16.0f),
                juce::Justification::centred);

    // Large numeric value
    const juce::String bpmText = (displayBPM > 0.0f)
                                 ? juce::String (juce::roundToInt (displayBPM))
                                 : "--";
    g.setColour (juce::Colour (0xff52c8e0));   // cyan
    g.setFont (juce::Font (38.0f).boldened());
    g.drawText (bpmText,
                juce::Rectangle<float> (bpmCX - labelW * 0.5f, bpmMidY - 18.0f, labelW, 44.0f),
                juce::Justification::centred);

    // ── BTrack readout (third position in the dissonance row) ────────────
    {
        const float btBarX = startX + 2.0f * stride;
        const float btCX   = btBarX + barW * 0.5f;

        // Beat flash circle — drawn above the "BTRACK" label.
        // The circle fades from amber to transparent over 150 ms.
        const float circleR  = 8.0f;
        const float circleCY = rowY + kDissonanceRowH * 0.5f - 46.0f;
        if (displayBTrackBeat > 0.001f)
        {
            const juce::Colour amber (0xffe08852);
            g.setColour (amber.withAlpha (displayBTrackBeat));
            g.fillEllipse (btCX - circleR, circleCY - circleR,
                           circleR * 2.0f, circleR * 2.0f);
        }
        // Outline always visible (subtle)
        g.setColour (juce::Colour (0xff555555));
        g.drawEllipse (btCX - circleR, circleCY - circleR,
                       circleR * 2.0f, circleR * 2.0f, 1.0f);

        const float btMidY = rowY + kDissonanceRowH * 0.5f;

        // "BTRACK" label
        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::Font (11.0f).boldened());
        g.drawText ("BTRACK",
                    juce::Rectangle<float> (btCX - labelW * 0.5f, btMidY - 34.0f, labelW, 16.0f),
                    juce::Justification::centred);

        // Large numeric value (amber)
        const juce::String btText = (displayBTrackBPM > 0.0f)
                                    ? juce::String (juce::roundToInt (displayBTrackBPM))
                                    : "--";
        g.setColour (juce::Colour (0xffe08852));   // amber
        g.setFont (juce::Font (38.0f).boldened());
        g.drawText (btText,
                    juce::Rectangle<float> (btCX - labelW * 0.5f, btMidY - 18.0f, labelW, 44.0f),
                    juce::Justification::centred);
    }

    // ── beat_this readout (fourth position in the dissonance row) ────────
    {
        const float btBarX = startX + 3.0f * stride;
        const float btCX   = btBarX + barW * 0.5f;

        // Beat flash circle — fades over 100 ms, driven by BPM extrapolation.
        const float circleR  = 8.0f;
        const float circleCY = rowY + kDissonanceRowH * 0.5f - 46.0f;
        if (displayBeatThisBeat > 0.001f)
        {
            const juce::Colour teal (0xff52e0c8);
            g.setColour (teal.withAlpha (displayBeatThisBeat));
            g.fillEllipse (btCX - circleR, circleCY - circleR,
                           circleR * 2.0f, circleR * 2.0f);
        }
        g.setColour (juce::Colour (0xff555555));
        g.drawEllipse (btCX - circleR, circleCY - circleR,
                       circleR * 2.0f, circleR * 2.0f, 1.0f);

        const float btMidY = rowY + kDissonanceRowH * 0.5f;

        // "BEAT THIS" label
        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::Font (11.0f).boldened());
        g.drawText ("BEAT THIS",
                    juce::Rectangle<float> (btCX - labelW * 0.5f, btMidY - 34.0f, labelW, 16.0f),
                    juce::Justification::centred);

        // Large numeric BPM value (teal)
        const juce::String btText = (displayBeatThisBPM > 0.0f)
                                    ? juce::String (juce::roundToInt (displayBeatThisBPM))
                                    : "--";
        g.setColour (juce::Colour (0xff52e0c8));
        g.setFont (juce::Font (38.0f).boldened());
        g.drawText (btText,
                    juce::Rectangle<float> (btCX - labelW * 0.5f, btMidY - 18.0f, labelW, 44.0f),
                    juce::Justification::centred);
    }

    // ── aubio readout (fifth position in the dissonance row) ─────────────
    {
        const float abBarX = startX + 4.0f * stride;
        const float abCX   = abBarX + barW * 0.5f;

        // Beat flash circle
        const float circleR  = 8.0f;
        const float circleCY = rowY + kDissonanceRowH * 0.5f - 46.0f;
        if (displayAubioBeat > 0.001f)
        {
            const juce::Colour orange (0xffe08040);
            g.setColour (orange.withAlpha (displayAubioBeat));
            g.fillEllipse (abCX - circleR, circleCY - circleR,
                           circleR * 2.0f, circleR * 2.0f);
        }
        g.setColour (juce::Colour (0xff555555));
        g.drawEllipse (abCX - circleR, circleCY - circleR,
                       circleR * 2.0f, circleR * 2.0f, 1.0f);

        const float abMidY = rowY + kDissonanceRowH * 0.5f;

        // "AUBIO" label
        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::Font (11.0f).boldened());
        g.drawText ("AUBIO",
                    juce::Rectangle<float> (abCX - labelW * 0.5f, abMidY - 34.0f, labelW, 16.0f),
                    juce::Justification::centred);

        // Large BPM value (orange)
        const juce::String abText = (displayAubioBPM > 0.0f)
                                    ? juce::String (juce::roundToInt (displayAubioBPM))
                                    : "--";
        g.setColour (juce::Colour (0xffe08040));
        g.setFont (juce::Font (38.0f).boldened());
        g.drawText (abText,
                    juce::Rectangle<float> (abCX - labelW * 0.5f, abMidY - 18.0f, labelW, 44.0f),
                    juce::Justification::centred);

        // Confidence bar (same layout as TempoTap confidence)
        const float confBarY = abMidY + 30.0f;
        const float confBarW = labelW * 0.8f;
        const float confBarH = 4.0f;
        const float confBarX = abCX - confBarW * 0.5f;
        const float confVal  = juce::jlimit (0.0f, 1.0f, displayAubioConfidence);

        g.setColour (juce::Colour (0xff2e2e2e));
        g.fillRoundedRectangle (confBarX, confBarY, confBarW, confBarH, 2.0f);

        if (confVal > 0.0f)
        {
            const float dist = std::abs (displayAubioConfidence - 1.0f);
            const juce::Colour confColour = dist < 0.25f ? juce::Colour (0xff52e08a)
                                           : dist < 0.60f ? juce::Colour (0xffe0c052)
                                                          : juce::Colour (0xffe05252);
            g.setColour (confColour);
            g.fillRoundedRectangle (confBarX, confBarY, confBarW * confVal, confBarH, 2.0f);
        }

        g.setColour (juce::Colour (0xff888888));
        g.setFont (juce::Font (10.0f));
        g.drawText (juce::String (juce::roundToInt (displayAubioConfidence * 100.0f)) + "%",
                    juce::Rectangle<float> (abCX - labelW * 0.5f, confBarY + confBarH + 3.0f, labelW, 12.0f),
                    juce::Justification::centred);
    }

    // ── Confidence indicator (debug) ─────────────────────────────────────
    // Ratio: detected onset peaks / expected beats from BPM.
    //   ≈ 1.0  well-supported   |  < 0.5  unreliable   |  > 1.0  over-detected
    const float confBarY    = bpmMidY + 30.0f;
    const float confBarW    = labelW * 0.8f;
    const float confBarH    = 4.0f;
    const float confBarX    = bpmCX - confBarW * 0.5f;
    const float confClamped = juce::jlimit (0.0f, 1.0f, displayBPMConfidence);

    // Track
    g.setColour (juce::Colour (0xff2e2e2e));
    g.fillRoundedRectangle (confBarX, confBarY, confBarW, confBarH, 2.0f);

    // Fill — colour shifts green→yellow→red as ratio moves away from 1.0
    if (confClamped > 0.0f)
    {
        const float distFromIdeal = std::abs (displayBPMConfidence - 1.0f);
        const juce::Colour confColour = distFromIdeal < 0.25f ? juce::Colour (0xff52e08a)   // green
                                      : distFromIdeal < 0.60f ? juce::Colour (0xffe0c052)   // yellow
                                                              : juce::Colour (0xffe05252);  // red
        g.setColour (confColour);
        g.fillRoundedRectangle (confBarX, confBarY, confBarW * confClamped, confBarH, 2.0f);
    }

    // Percentage label (shows raw ratio so > 100 % is visible)
    g.setColour (juce::Colour (0xff888888));
    g.setFont (juce::Font (10.0f));
    g.drawText (juce::String (juce::roundToInt (displayBPMConfidence * 100.0f)) + "%",
                juce::Rectangle<float> (bpmCX - labelW * 0.5f, confBarY + confBarH + 3.0f, labelW, 12.0f),
                juce::Justification::centred);
}
