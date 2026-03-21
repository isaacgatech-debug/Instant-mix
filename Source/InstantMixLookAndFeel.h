#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
// Modern LookAndFeel with Slate Digital-inspired 3D aesthetics
class InstantMixLookAndFeel : public juce::LookAndFeel_V4
{
public:
    InstantMixLookAndFeel();
    
    // Color scheme - updated per user request
    static juce::Colour getInstrumentColor (int instrument);
    static juce::Colour getBackgroundColor();
    static juce::Colour getPanelColor();
    static juce::Colour getKnobFaceColor();
    static juce::Colour getKnobHighlightColor();
    static juce::Colour getTextColor();
    static juce::Colour getMutedTextColor();
    
    // Custom 3D knob drawing
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override;
    
    // Custom button styling
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;
    
    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;
    
    // Font preferences
    juce::Font getLabelFont (juce::Label& label) override;
    
private:
    void draw3DKnobFace (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour baseColor);
    void drawKnobIndicator (juce::Graphics& g, juce::Point<float> center, float radius,
                              float angle, juce::Colour indicatorColor);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstantMixLookAndFeel)
};
