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
        { "ANGRY",   juce::Colour (0xffe05252) },  // red
        { "CALM",    juce::Colour (0xff5285e0) },  // blue
        { "HAPPY",   juce::Colour (0xffe0c052) },  // warm yellow
        { "SAD",     juce::Colour (0xff7b52e0) },  // purple
    }};

    const juce::Colour kDissonanceColour { 0xff555555 };  // dark gray
}

//==============================================================================
DescriptorDisplay::DescriptorDisplay (DescriptorAnalyser& a, OscManager& osc)
    : analyser (a), oscManager (osc)
{
    // Set up the ff_meters component with the custom look-and-feel.
    meter.setLookAndFeel (&meterLAF);
    meter.setMeterSource (&analyser.getMeterSource());
    meter.setRefreshRateHz (30);
    meter.setFixedNumChannels (1);  // single-bar display matching the mood bar style
    addAndMakeVisible (meter);

    startTimerHz (kTimerHz);
}

DescriptorDisplay::~DescriptorDisplay()
{
    meter.setLookAndFeel (nullptr);
    stopTimer();
}

//==============================================================================
void DescriptorDisplay::resized()
{
    const float w     = static_cast<float> (getWidth());
    const float h     = static_cast<float> (getHeight());
    const float moodH = h - kDissonanceRowH;

    // Slot 4 — same geometry as the mood bars in paint().
    const float sectionW  = w / 5.0f;
    const float barW      = sectionW * 0.55f;
    const float gap       = (sectionW - barW) * 0.5f;
    const float stride    = sectionW;
    const float startX    = gap;
    const float topPad    = 18.0f;
    const float labelH    = 16.0f;
    const float valueH    = 14.0f;
    const float bottomPad = labelH + valueH + 10.0f;
    const float barH      = moodH - topPad - bottomPad;

    meter.setBounds (juce::roundToInt (startX + 4.0f * stride),
                     juce::roundToInt (topPad),
                     juce::roundToInt (barW),
                     juce::roundToInt (barH + bottomPad));
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

    // Spectral centroid: smooth like mood bars.
    displayCentroidMIR += kMoodSmoothAlpha * (analyser.getCentroidMIR() - displayCentroidMIR);

    // ── OSC output ────────────────────────────────────────────────────────────
    if (oscManager.isEnabled())
    {
        oscManager.sendFloat ("/mir/mood/angry", displayAngry);
        oscManager.sendFloat ("/mir/mood/calm",  displayCalm);
        oscManager.sendFloat ("/mir/mood/happy", displayHappy);
        oscManager.sendFloat ("/mir/mood/sad",   displaySad);
        oscManager.sendFloat ("/mir/dissonance", displayDissonance);

        // Centroid normalised to [0, 1] using the same 4 kHz full-scale as the UI bar.
        oscManager.sendFloat ("/mir/centroid", juce::jlimit (0.0f, 1.0f, displayCentroidMIR / 4000.0f));

        // Tempo (BPM from BTrack, raw float).
        oscManager.sendFloat ("/mir/tempo", displayBTrackBPM);

        // Beat: send 1.0 only on the frame when a new beat is detected.
        const double thisBeat = analyser.getBeatThisLastBeat();
        if (thisBeat != lastOscBeatTime)
        {
            lastOscBeatTime = thisBeat;
            oscManager.sendFloat ("/mir/beat", 1.0f);
        }

        // Level: peak from VU meter source, converted to dBFS then normalised [0, 1].
        const float peak  = analyser.getMeterSource().getMaxLevel (0);
        const float dBFS  = (peak > 0.0f) ? 20.0f * std::log10 (peak) : -60.0f;
        const float level = juce::jlimit (0.0f, 1.0f, (dBFS + 60.0f) / 60.0f);
        oscManager.sendFloat ("/mir/level", level);
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
    const float sectionW = w / 5.0f;
    const float barW     = sectionW * 0.55f;
    const float gap      = (sectionW - barW) * 0.5f;
    const float stride   = sectionW;   // one full section per bar → equal margins
    const float startX   = gap;
    const float labelW    = stride;
    const float topPad    = 18.0f;
    const float labelH    = 16.0f;
    const float valueH    = 14.0f;
    const float bottomPad = labelH + valueH + 10.0f;
    const float barH      = moodH - topPad - bottomPad;

    const std::array<float, 4> moodValues {
        displayAngry, displayCalm, displayHappy, displaySad
    };

    // ── Mood bars 0–3 (ANGRY / CALM / HAPPY / SAD) ───────────────────────
    for (int i = 0; i < 4; ++i)
    {
        const float barX  = startX + static_cast<float> (i) * stride;
        const float lcx   = barX + barW * 0.5f;
        const float value = juce::jlimit (0.0f, 1.0f, moodValues[static_cast<size_t> (i)]);

        // Background track
        g.setColour (juce::Colour (0xff2e2e2e));
        g.fillRoundedRectangle (barX, topPad, barW, barH, 5.0f);

        // Coloured fill (bottom-up)
        if (value > 0.001f)
        {
            const float fillH = barH * value;
            g.setColour (kBars[static_cast<size_t> (i)].colour);
            g.fillRoundedRectangle (barX, topPad + barH - fillH, barW, fillH, 5.0f);
        }

        const float labelY = topPad + barH + 8.0f;
        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::Font (11.0f).boldened());
        g.drawText (kBars[static_cast<size_t> (i)].label,
                    juce::Rectangle<float> (lcx - labelW * 0.5f, labelY, labelW, labelH),
                    juce::Justification::centred);

        g.setColour (kBars[static_cast<size_t> (i)].colour.brighter (0.3f));
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

    // ── Helper lambda: draw a centroid vertical bar in the bottom row ─────
    auto drawCentroidBar = [&] (int pos, float centroidHz, float maxHz, juce::Colour colour, const char* label)
    {
        const float barX  = startX + static_cast<float> (pos) * stride;
        const float lcx   = barX + barW * 0.5f;
        const float barH2 = kDissonanceRowH - topPad - bottomPad;
        const float val   = juce::jlimit (0.0f, 1.0f, centroidHz / maxHz);

        g.setColour (juce::Colour (0xff2e2e2e));
        g.fillRoundedRectangle (barX, rowY + topPad, barW, barH2, 5.0f);

        if (val > 0.001f)
        {
            const float fillH = barH2 * val;
            g.setColour (colour);
            g.fillRoundedRectangle (barX, rowY + topPad + barH2 - fillH, barW, fillH, 5.0f);
        }

        const float labelY = rowY + topPad + barH2 + 8.0f;
        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::Font (11.0f).boldened());
        g.drawText (label,
                    juce::Rectangle<float> (lcx - labelW * 0.5f, labelY, labelW, labelH),
                    juce::Justification::centred);

        const juce::String hzText = centroidHz >= 1000.0f
                                    ? juce::String (centroidHz / 1000.0f, 1) + "k"
                                    : juce::String (juce::roundToInt (centroidHz));
        g.setColour (colour.brighter (0.3f));
        g.setFont (juce::Font (10.0f));
        g.drawText (hzText,
                    juce::Rectangle<float> (lcx - labelW * 0.5f, labelY + labelH, labelW, valueH),
                    juce::Justification::centred);
    };

    // ── Dissonance bar (position 0) ───────────────────────────────────────
    {
        const float dBarX  = startX + 0.0f * stride;
        const float dBarH  = kDissonanceRowH - topPad - bottomPad;
        const float dValue = juce::jlimit (0.0f, 1.0f, displayDissonance);
        const float dlcx   = dBarX + barW * 0.5f;

        g.setColour (juce::Colour (0xff2e2e2e));
        g.fillRoundedRectangle (dBarX, rowY + topPad, barW, dBarH, 5.0f);

        if (dValue > 0.001f)
        {
            const float fillH = dBarH * dValue;
            g.setColour (kDissonanceColour);
            g.fillRoundedRectangle (dBarX, rowY + topPad + dBarH - fillH, barW, fillH, 5.0f);
        }

        const float dLabelY = rowY + topPad + dBarH + 8.0f;
        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::Font (11.0f).boldened());
        g.drawText ("DISSONANCE",
                    juce::Rectangle<float> (dlcx - labelW * 0.5f, dLabelY, labelW, labelH),
                    juce::Justification::centred);

        g.setColour (kDissonanceColour.brighter (0.4f));
        g.setFont (juce::Font (10.0f));
        g.drawText (juce::String (juce::roundToInt (dValue * 100.0f)) + "%",
                    juce::Rectangle<float> (dlcx - labelW * 0.5f, dLabelY + labelH, labelW, valueH),
                    juce::Justification::centred);
    }

    // ── Centroid bar (position 1) ─────────────────────────────────────────
    drawCentroidBar (1, displayCentroidMIR, 4000.0f, juce::Colour (0xffffa040), "CENTROID");

    // ── Tempo readout — BTrack BPM (position 2, no beat pulse) ───────────
    {
        const float tBarX = startX + 2.0f * stride;
        const float tCX   = tBarX + barW * 0.5f;
        const float tMidY = rowY + kDissonanceRowH * 0.5f;

        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::Font (11.0f).boldened());
        g.drawText ("TEMPO",
                    juce::Rectangle<float> (tCX - labelW * 0.5f, tMidY - 34.0f, labelW, 16.0f),
                    juce::Justification::centred);

        const juce::String tText = (displayBTrackBPM > 0.0f)
                                   ? juce::String (juce::roundToInt (displayBTrackBPM))
                                   : "--";
        g.setColour (juce::Colour (0xffe08852));   // amber
        g.setFont (juce::Font (38.0f).boldened());
        g.drawText (tText,
                    juce::Rectangle<float> (tCX - labelW * 0.5f, tMidY - 18.0f, labelW, 44.0f),
                    juce::Justification::centred);
    }

    // ── Beat readout — BeatThis pulse + BPM (position 3) ─────────────────
    {
        const float bBarX = startX + 3.0f * stride;
        const float bCX   = bBarX + barW * 0.5f;

        const float bMidY = rowY + kDissonanceRowH * 0.5f;

        // "BEAT" label — same vertical position as the other section labels
        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::Font (11.0f).boldened());
        g.drawText ("BEAT",
                    juce::Rectangle<float> (bCX - labelW * 0.5f, bMidY - 34.0f, labelW, 16.0f),
                    juce::Justification::centred);

        // Circle diameter matches the tempo font size (38px) → radius = 19.
        // Centred at the same y as the tempo number centre (bMidY - 18 + 44/2 = bMidY + 4).
        const float circleR  = 15.0f;
        const float circleCY = bMidY + 4.0f;
        if (displayBeatThisBeat > 0.001f)
        {
            const juce::Colour teal (0xff52e0c8);
            g.setColour (teal.withAlpha (displayBeatThisBeat));
            g.fillEllipse (bCX - circleR, circleCY - circleR,
                           circleR * 2.0f, circleR * 2.0f);
        }
        g.setColour (juce::Colour (0xff555555));
        g.drawEllipse (bCX - circleR, circleCY - circleR,
                       circleR * 2.0f, circleR * 2.0f, 1.0f);
    }
}
