#pragma once

#include <JuceHeader.h>
#include "OscManager.h"

//==============================================================================
/**
 * Full-size overlay that dims the plugin and shows the OSC setup panel.
 * Add it as the topmost child of the editor and toggle its visibility.
 * Holds a reference to OscManager and applies settings whenever the toggle
 * changes or the panel is dismissed.
 */
class OscSetupOverlay : public juce::Component
{
public:
    //==========================================================================
    explicit OscSetupOverlay (OscManager& osc) : oscManager (osc)
    {
        // Close button
        closeButton.setButtonText ("x");
        closeButton.onClick = [this] { applyAndClose(); };
        addAndMakeVisible (closeButton);

        // OSC on/off toggle (hidden — drawn manually, hit-tested in mouseDown)
        oscToggle.setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (oscToggle);

        // IP field
        ipEditor.setText ("127.0.0.1", false);
        ipEditor.setInputRestrictions (64, "0123456789.");
        ipEditor.setJustification (juce::Justification::centredLeft);
        addAndMakeVisible (ipEditor);

        // Port field
        portEditor.setText ("9000", false);
        portEditor.setInputRestrictions (5, "0123456789");
        portEditor.setJustification (juce::Justification::centredLeft);
        addAndMakeVisible (portEditor);

        styleComponents();
        applySettings();   // connect immediately with default IP/port
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        // Dim background
        g.fillAll (juce::Colour (0xaa000000));

        const auto p = panelBounds.toFloat();

        // Panel shadow
        g.setColour (juce::Colour (0x55000000));
        g.fillRoundedRectangle (p.translated (0.0f, 3.0f), 10.0f);

        // Panel background
        g.setColour (juce::Colour (0xff242424));
        g.fillRoundedRectangle (p, 10.0f);

        // Panel border
        g.setColour (juce::Colour (0xff404040));
        g.drawRoundedRectangle (p, 10.0f, 1.0f);

        // Title
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (15.0f).boldened());
        g.drawText ("OSC SETUP",
                    panelBounds.withHeight (kTitleH).toFloat(),
                    juce::Justification::centred);

        // Separator under title
        g.setColour (juce::Colour (0xff383838));
        g.drawHorizontalLine (panelBounds.getY() + kTitleH,
                              static_cast<float> (panelBounds.getX() + 1),
                              static_cast<float> (panelBounds.getRight() - 1));

        // Row labels
        g.setColour (juce::Colour (0xffaaaaaa));
        g.setFont (juce::Font (12.0f).boldened());
        g.drawText ("OSC",  labelRow (0).withWidth (kLabelW).toFloat(), juce::Justification::centredLeft);
        g.drawText ("IP",   labelRow (1).withWidth (kLabelW).toFloat(), juce::Justification::centredLeft);
        g.drawText ("PORT", labelRow (2).withWidth (kLabelW).toFloat(), juce::Justification::centredLeft);

        // Pill toggle
        drawPillToggle (g);
    }

    void resized() override
    {
        constexpr int pw = 320;
        constexpr int ph = kTitleH + 3 * kRowH + kPadY * 2;
        panelBounds = juce::Rectangle<int> (
            (getWidth()  - pw) / 2,
            (getHeight() - ph) / 2,
            pw, ph);

        closeButton.setBounds (panelBounds.getRight() - 36, panelBounds.getY() + 4, 32, 32);

        // Pill bounds (row 0 field area)
        const auto r0 = fieldRow (0);
        pillBounds = juce::Rectangle<int> (r0.getX(), r0.getCentreY() - 11, 44, 22);

        oscToggle.setBounds (0, 0, 0, 0);   // invisible, hit-tested manually

        styleEditor (ipEditor);
        styleEditor (portEditor);
        ipEditor.setBounds   (fieldRow (1));
        portEditor.setBounds (fieldRow (2));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! panelBounds.contains (e.getPosition()))
        {
            applyAndClose();
            return;
        }

        if (pillBounds.contains (e.getPosition()))
        {
            oscToggle.setToggleState (! oscToggle.getToggleState(),
                                      juce::dontSendNotification);
            applySettings();   // toggle takes effect immediately
            repaint();
        }
    }

private:
    //==========================================================================
    static constexpr int kTitleH = 44;
    static constexpr int kRowH   = 52;
    static constexpr int kPadX   = 24;
    static constexpr int kPadY   = 12;
    static constexpr int kLabelW = 52;

    OscManager&          oscManager;
    juce::Rectangle<int> panelBounds;
    juce::Rectangle<int> pillBounds;

    juce::TextButton   closeButton;
    juce::ToggleButton oscToggle;
    juce::TextEditor   ipEditor;
    juce::TextEditor   portEditor;

    //==========================================================================
    void applySettings()
    {
        oscManager.configure (ipEditor.getText(),
                              portEditor.getText().getIntValue(),
                              oscToggle.getToggleState());
    }

    void applyAndClose()
    {
        applySettings();
        setVisible (false);
    }

    //==========================================================================
    juce::Rectangle<int> labelRow (int row) const
    {
        return { panelBounds.getX() + kPadX,
                 panelBounds.getY() + kTitleH + kPadY + row * kRowH,
                 kLabelW,
                 kRowH };
    }

    juce::Rectangle<int> fieldRow (int row) const
    {
        const int fx = panelBounds.getX() + kPadX + kLabelW + 8;
        const int fw = panelBounds.getWidth() - kPadX * 2 - kLabelW - 8;
        const int fh = 30;
        const int fy = panelBounds.getY() + kTitleH + kPadY + row * kRowH
                       + (kRowH - fh) / 2;
        return { fx, fy, fw, fh };
    }

    //==========================================================================
    void drawPillToggle (juce::Graphics& g) const
    {
        const auto  r   = pillBounds.toFloat();
        const bool  on  = oscToggle.getToggleState();
        const float rad = r.getHeight() * 0.5f;

        g.setColour (on ? juce::Colour (0xff52e0c8) : juce::Colour (0xff3a3a3a));
        g.fillRoundedRectangle (r, rad);

        g.setColour (on ? juce::Colour (0xff52e0c8).darker (0.2f) : juce::Colour (0xff555555));
        g.drawRoundedRectangle (r, rad, 1.0f);

        const float thumbD = r.getHeight() - 4.0f;
        const float thumbX = on ? r.getRight() - 2.0f - thumbD : r.getX() + 2.0f;
        g.setColour (juce::Colours::white);
        g.fillEllipse (thumbX, r.getY() + 2.0f, thumbD, thumbD);
    }

    //==========================================================================
    void styleComponents()
    {
        closeButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff2e2e2e));
        closeButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff444444));
        closeButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffaaaaaa));
        closeButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
    }

    void styleEditor (juce::TextEditor& ed)
    {
        ed.setColour (juce::TextEditor::backgroundColourId,    juce::Colour (0xff1a1a1a));
        ed.setColour (juce::TextEditor::textColourId,           juce::Colours::white);
        ed.setColour (juce::TextEditor::outlineColourId,        juce::Colour (0xff404040));
        ed.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xff52e0c8));
        ed.setColour (juce::TextEditor::highlightColourId,      juce::Colour (0x4452e0c8));
        ed.setFont (juce::Font (13.0f));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscSetupOverlay)
};
