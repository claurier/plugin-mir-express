#pragma once

#include <JuceHeader.h>

/**
 * MirExpressLevelMeterLAF
 *
 * Custom ff_meters LookAndFeel styled to match the mood-bar design:
 *   • Full-width bar (no half-width split for tick marks)
 *   • Rounded dark background matching 0xff2e2e2e
 *   • Green / yellow / red colour zones on a −60 dBFS logarithmic scale
 *   • 2 px peak-hold line, colour-coded to the active zone
 *   • Tick marks overlaid on the right edge of the bar
 *   • "dBFS" label + current dB value in the bottom label row
 *   • No clip LED, no max-overall number display
 */
class MirExpressLevelMeterLAF : public foleys::LevelMeterLookAndFeel
{
public:
    MirExpressLevelMeterLAF() = default;

    // ── Bounds helpers ────────────────────────────────────────────────────

    /** Tiny inset from the outer component bounds. */
    juce::Rectangle<float> getMeterInnerBounds (juce::Rectangle<float> bounds,
                                                foleys::LevelMeter::MeterFlags) const override
    {
        return bounds.reduced (2.0f);
    }

    /** Bar fills the left 68 % of the channel slot; the remaining 32 % is the
     *  tick-label column. */
    juce::Rectangle<float> getMeterBarBounds (juce::Rectangle<float> bounds,
                                              foleys::LevelMeter::MeterFlags) const override
    {
        return bounds.withTrimmedBottom (kLabelRowH).withTrimmedRight (bounds.getWidth() * 0.32f);
    }

    /** Disable the clip indicator. */
    juce::Rectangle<float> getMeterClipIndicatorBounds (juce::Rectangle<float>,
                                                        foleys::LevelMeter::MeterFlags) const override
    {
        return {};
    }

    /** Disable the max-overall number. */
    juce::Rectangle<float> getMeterMaxNumberBounds (juce::Rectangle<float>,
                                                    foleys::LevelMeter::MeterFlags) const override
    {
        return {};
    }

    /** No separate tick-mark strip — we draw them inside drawMeterBar. */
    juce::Rectangle<float> getMeterTickmarksBounds (juce::Rectangle<float>,
                                                    foleys::LevelMeter::MeterFlags) const override
    {
        return {};
    }

    // ── Drawing ───────────────────────────────────────────────────────────

    /** Outer rounded background. Returns reduced inner bounds for the rest. */
    juce::Rectangle<float> drawBackground (juce::Graphics& g,
                                           foleys::LevelMeter::MeterFlags,
                                           juce::Rectangle<float> bounds) override
    {
        g.setColour (juce::Colour (0xff2e2e2e));
        g.fillRoundedRectangle (bounds, kCorner);
        return bounds.reduced (2.0f);
    }

    /** Dark inner background for the bar area. */
    void drawMeterBarBackground (juce::Graphics& g,
                                 foleys::LevelMeter::MeterFlags,
                                 juce::Rectangle<float> bounds) override
    {
        g.setColour (juce::Colour (0xff1a1a1a));
        g.fillRoundedRectangle (bounds, kCorner);
    }

    /** The level bar: green / yellow / red fill + peak line + tick scale. */
    void drawMeterBar (juce::Graphics& g,
                       foleys::LevelMeter::MeterFlags,
                       juce::Rectangle<float> bounds,
                       float rms, float peak) override
    {
        const float barH   = bounds.getHeight();
        const float barW   = bounds.getWidth();
        const float barX   = bounds.getX();
        const float barTop = bounds.getY();
        const float barBot = bounds.getBottom();

        // Map linear gain → normalised dB position (0 = −60 dBFS, 1 = 0 dBFS).
        auto dbNorm = [](float gain) {
            const float db = juce::Decibels::gainToDecibels (gain, kFloorDb);
            return juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / (-kFloorDb));
        };

        const float levelPos = dbNorm (rms);
        const float peakPos  = dbNorm (peak);

        // ── Fill (bottom-up, colour zones) ────────────────────────────────
        if (levelPos > 0.001f)
        {
            const float greenH  = std::min (levelPos, kYellowThresh)              * barH;
            const float yellowH = std::max (0.0f, std::min (levelPos, kRedThresh)
                                             - kYellowThresh)                     * barH;
            const float redH    = std::max (0.0f, levelPos - kRedThresh)          * barH;

            g.saveState();
            g.reduceClipRegion (bounds.toNearestInt());

            if (greenH > 0.0f)
            {
                g.setColour (kGreen);
                g.fillRect (juce::Rectangle<float> (barX, barBot - greenH, barW, greenH));
            }
            if (yellowH > 0.0f)
            {
                g.setColour (kYellow);
                g.fillRect (juce::Rectangle<float> (barX, barBot - greenH - yellowH, barW, yellowH));
            }
            if (redH > 0.0f)
            {
                g.setColour (kRed);
                g.fillRect (juce::Rectangle<float> (barX, barBot - greenH - yellowH - redH, barW, redH));
            }

            g.restoreState();
        }

        // ── Peak-hold line ────────────────────────────────────────────────
        if (peakPos > 0.001f)
        {
            const float peakY   = barTop + barH * (1.0f - peakPos);
            const juce::Colour peakCol = peakPos > kRedThresh    ? kRed
                                       : peakPos > kYellowThresh ? kYellow
                                                                  : kGreen;
            g.setColour (peakCol.withAlpha (0.9f));
            g.fillRect  (juce::Rectangle<float> (barX, peakY - 1.0f, barW, 2.0f));
        }

        // ── Tick marks (right-side column, same width as bar) ─────────────
        // We draw into the column to the right of the bar (getMeterBarBounds
        // left 32 % of the channel slot free for this).
        const float colX  = barX + barW + 3.0f;
        const float colW  = 24.0f;

        struct Mark { float norm; const char* label; };
        constexpr std::array<Mark, 6> kMarks {{
            { 1.000f, "0"   },
            { 0.900f, "-6"  },
            { 0.800f, "-12" },
            { 0.667f, "-20" },
            { 0.333f, "-40" },
            { 0.000f, "-60" },
        }};

        g.setFont (juce::Font (7.5f));

        for (const auto& m : kMarks)
        {
            const float tickY   = barTop + barH * (1.0f - m.norm);
            const juce::Colour  tc = m.norm > kRedThresh    ? kRed
                                   : m.norm > kYellowThresh ? kYellow
                                                            : kGreen;
            g.setColour (tc.withAlpha (0.55f));
            g.drawHorizontalLine (juce::roundToInt (tickY), colX, colX + 4.0f);

            g.setColour (juce::Colour (0xff888888));
            g.drawText  (juce::String (m.label),
                         juce::Rectangle<float> (colX + 5.0f, tickY - 4.5f, colW, 9.0f),
                         juce::Justification::centredLeft);
        }
    }

    /** Draw the "dBFS" label and current dB value in the bottom row.
     *  Called from our drawMeterChannel override below. */
    void drawLabelRow (juce::Graphics& g,
                       juce::Rectangle<float> channelBounds,
                       float rms) const
    {
        const float lcx    = channelBounds.getCentreX();
        const float labelY = channelBounds.getBottom() - kLabelRowH + 4.0f;

        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::Font (11.0f).boldened());
        g.drawText ("dBFS",
                    juce::Rectangle<float> (lcx - 20.0f, labelY, 40.0f, 15.0f),
                    juce::Justification::centred);

        const float db = juce::Decibels::gainToDecibels (rms, kFloorDb);
        const float norm = juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / (-kFloorDb));
        const juce::Colour valCol = norm > kRedThresh    ? kRed
                                  : norm > kYellowThresh ? kYellow
                                                         : kGreen;
        g.setColour (valCol);
        g.setFont (juce::Font (10.0f));
        const juce::String dBStr = rms < 1.0e-6f         ? "-\u221e dB"
                                 : db  >= -0.5f           ? "0 dB"
                                                          : juce::String (juce::roundToInt (db)) + " dB";
        g.drawText (dBStr,
                    juce::Rectangle<float> (lcx - 20.0f, labelY + 15.0f, 40.0f, 12.0f),
                    juce::Justification::centred);
    }

    /** Override the full channel draw so we can append the label row. */
    void drawMeterChannel (juce::Graphics& g,
                           foleys::LevelMeter::MeterFlags meterType,
                           juce::Rectangle<float> bounds,
                           const foleys::LevelMeterSource* source,
                           int selectedChannel) override
    {
        if (source == nullptr)
            return;

        const float rms = source->getRMSLevel (selectedChannel);

        // Background + bar
        auto barBounds = getMeterBarBounds (bounds, meterType);
        drawMeterBarBackground (g, meterType, barBounds);
        drawMeterBar (g, meterType, barBounds, rms, source->getMaxLevel (selectedChannel));

        // Label row below the bar
        drawLabelRow (g, bounds, rms);
    }

    /** No separate clip indicator. */
    void drawClipIndicator (juce::Graphics&, foleys::LevelMeter::MeterFlags,
                            juce::Rectangle<float>, bool) override {}
    void drawClipIndicatorBackground (juce::Graphics&, foleys::LevelMeter::MeterFlags,
                                      juce::Rectangle<float>) override {}

    /** No max-overall number. */
    void drawMaxNumber (juce::Graphics&, foleys::LevelMeter::MeterFlags,
                        juce::Rectangle<float>, float) override {}
    void drawMaxNumberBackground (juce::Graphics&, foleys::LevelMeter::MeterFlags,
                                  juce::Rectangle<float>) override {}

    /** Tick marks are drawn inside drawMeterBar — disable the default strip. */
    void drawTickMarks (juce::Graphics&, foleys::LevelMeter::MeterFlags,
                        juce::Rectangle<float>) override {}

    // hit-test overrides (required pure virtuals)
    int hitTestClipIndicator (juce::Point<int>, foleys::LevelMeter::MeterFlags,
                              juce::Rectangle<float>,
                              const foleys::LevelMeterSource*) const override { return -1; }
    int hitTestMaxNumber (juce::Point<int>, foleys::LevelMeter::MeterFlags,
                          juce::Rectangle<float>,
                          const foleys::LevelMeterSource*) const override { return -1; }

private:
    static constexpr float kFloorDb      = -60.0f;
    static constexpr float kYellowThresh = 0.800f;  // −12 dBFS
    static constexpr float kRedThresh    = 0.900f;  // −6 dBFS
    static constexpr float kCorner       = 5.0f;
    static constexpr float kLabelRowH    = 32.0f;   // height reserved for dBFS label

    static const juce::Colour kGreen;
    static const juce::Colour kYellow;
    static const juce::Colour kRed;
};

inline const juce::Colour MirExpressLevelMeterLAF::kGreen  { 0xff4caf7d };
inline const juce::Colour MirExpressLevelMeterLAF::kYellow { 0xffe0c052 };
inline const juce::Colour MirExpressLevelMeterLAF::kRed    { 0xffe05252 };
