#include "InstantMixLookAndFeel.h"

//==============================================================================
InstantMixLookAndFeel::InstantMixLookAndFeel()
{
    // Set default look and feel colors
    setColour (juce::TextButton::buttonColourId, getPanelColor());
    setColour (juce::TextButton::textColourOffId, getTextColor());
    setColour (juce::TextButton::buttonOnColourId, getKnobHighlightColor());
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    
    setColour (juce::Slider::rotarySliderFillColourId, getKnobHighlightColor());
    setColour (juce::Slider::rotarySliderOutlineColourId, getKnobFaceColor());
    setColour (juce::Slider::thumbColourId, getTextColor());
}

//==============================================================================
// Color scheme - updated per user request
juce::Colour InstantMixLookAndFeel::getInstrumentColor (int instrument)
{
    switch (instrument)
    {
        case 0: return juce::Colour (255, 193, 7);    // Acoustic - Amber/Yellow
        case 1: return juce::Colour (33, 150, 243);   // Vocal - Blue
        case 2: return juce::Colour (156, 39, 176);   // Piano - Purple
        case 3: return juce::Colour (76, 175, 80);    // Electric Guitar - Green (Pro)
        case 4: return juce::Colour (244, 67, 54);    // Drums - Red (Pro)
        case 5: return juce::Colour (255, 152, 0);    // Bass - Orange (Pro)
        default: return juce::Colours::grey;
    }
}

juce::Colour InstantMixLookAndFeel::getBackgroundColor()
{
    // Deep dark background - Slate Digital style
    return juce::Colour (18, 18, 24);
}

juce::Colour InstantMixLookAndFeel::getPanelColor()
{
    // Slightly lighter panel background
    return juce::Colour (35, 35, 45);
}

juce::Colour InstantMixLookAndFeel::getKnobFaceColor()
{
    // Dark metallic knob face
    return juce::Colour (55, 55, 65);
}

juce::Colour InstantMixLookAndFeel::getKnobHighlightColor()
{
    // Metallic highlight
    return juce::Colour (180, 185, 195);
}

juce::Colour InstantMixLookAndFeel::getTextColor()
{
    // Bright white text
    return juce::Colour (240, 240, 245);
}

juce::Colour InstantMixLookAndFeel::getMutedTextColor()
{
    // Dimmed text
    return juce::Colour (140, 140, 150);
}

//==============================================================================
void InstantMixLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                                  float sliderPosProportional, float rotaryStartAngle,
                                                  float rotaryEndAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    auto centre = bounds.getCentre();
    auto radius = juce::jmin (bounds.getWidth() * 0.5f, bounds.getHeight() * 0.5f) - 4.0f;
    
    // Calculate angle
    float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    
    // Get accent color from slider properties (instrument color)
    auto* instrumentParam = slider.getProperties().getVarPointer ("instrument");
    juce::Colour accentColor = getInstrumentColor (instrumentParam ? static_cast<int> (*instrumentParam) : 1);
    
    // Draw 3D knob face
    draw3DKnobFace (g, bounds, getKnobFaceColor());
    
    // Draw indicator
    drawKnobIndicator (g, centre, radius * 0.85f, angle, accentColor);
    
    // Draw arc track
    float lineThickness = radius * 0.12f;
    juce::Path trackPath;
    trackPath.addCentredArc (centre.x, centre.y, radius * 0.75f, radius * 0.75f,
                              0.0f, rotaryStartAngle, rotaryEndAngle, true);
    
    // Track background
    g.setColour (getBackgroundColor().withAlpha (0.8f));
    g.strokePath (trackPath, juce::PathStrokeType (lineThickness));
    
    // Active arc (from start to current angle)
    juce::Path activePath;
    activePath.addCentredArc (centre.x, centre.y, radius * 0.75f, radius * 0.75f,
                               0.0f, rotaryStartAngle, angle, true);
    
    // Create gradient for active arc
    juce::ColourGradient arcGradient (accentColor.brighter (0.3f), centre.x - radius, centre.y,
                                       accentColor.darker (0.2f), centre.x + radius, centre.y, false);
    g.setGradientFill (arcGradient);
    g.strokePath (activePath, juce::PathStrokeType (lineThickness, 
                                                       juce::PathStrokeType::JointStyle::curved, 
                                                       juce::PathStrokeType::EndCapStyle::rounded));
}

void InstantMixLookAndFeel::draw3DKnobFace (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour baseColor)
{
    auto centre = bounds.getCentre();
    auto radius = juce::jmin (bounds.getWidth() * 0.5f, bounds.getHeight() * 0.5f) - 2.0f;
    
    // Outer shadow (depth effect)
    juce::Path shadowPath;
    shadowPath.addEllipse (centre.x - radius - 2, centre.y - radius + 2, radius * 2 + 4, radius * 2 + 4);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillPath (shadowPath);
    
    // Main knob face with gradient for 3D effect
    juce::ColourGradient faceGradient (baseColor.brighter (0.2f), centre.x, centre.y - radius * 0.3f,
                                         baseColor.darker (0.3f), centre.x, centre.y + radius * 0.5f, false);
    g.setGradientFill (faceGradient);
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2, radius * 2);
    
    // Inner highlight ring (top-left for lighting)
    float highlightRadius = radius * 0.92f;
    juce::Path highlightRing;
    highlightRing.addEllipse (centre.x - highlightRadius, centre.y - highlightRadius,
                               highlightRadius * 2, highlightRadius * 2);
    juce::ColourGradient highlightGradient (juce::Colours::white.withAlpha (0.15f), centre.x - highlightRadius * 0.5f, centre.y - highlightRadius * 0.5f,
                                             juce::Colours::transparentWhite, centre.x, centre.y, false);
    g.setGradientFill (highlightGradient);
    g.fillPath (highlightRing);
    
    // Outer rim
    g.setColour (baseColor.darker (0.4f));
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2, radius * 2, 1.5f);
    
    // Center depression
    float centerRadius = radius * 0.25f;
    juce::ColourGradient centerGradient (baseColor.darker (0.2f), centre.x, centre.y - centerRadius * 0.5f,
                                          baseColor.brighter (0.1f), centre.x, centre.y + centerRadius * 0.5f, false);
    g.setGradientFill (centerGradient);
    g.fillEllipse (centre.x - centerRadius, centre.y - centerRadius, centerRadius * 2, centerRadius * 2);
}

void InstantMixLookAndFeel::drawKnobIndicator (juce::Graphics& g, juce::Point<float> center, float radius,
                                                 float angle, juce::Colour indicatorColor)
{
    // Calculate indicator position
    float indicatorLength = radius * 0.7f;
    float indicatorWidth = radius * 0.12f;
    
    juce::Point<float> endPoint (
        center.x + std::sin (angle) * indicatorLength,
        center.y - std::cos (angle) * indicatorLength
    );
    
    // Draw indicator line with glow
    juce::Path indicatorPath;
    indicatorPath.addLineSegment (juce::Line<float> (center, endPoint), indicatorWidth);
    
    // Glow effect
    g.setColour (indicatorColor.withAlpha (0.4f));
    g.strokePath (indicatorPath, juce::PathStrokeType (indicatorWidth * 2.0f, 
                                                         juce::PathStrokeType::JointStyle::mitered, 
                                                         juce::PathStrokeType::EndCapStyle::rounded));
    
    // Main indicator
    juce::ColourGradient indicatorGradient (indicatorColor.brighter (0.4f), center.x, center.y,
                                            indicatorColor.darker (0.2f), endPoint.x, endPoint.y, false);
    g.setGradientFill (indicatorGradient);
    g.strokePath (indicatorPath, juce::PathStrokeType (indicatorWidth, 
                                                         juce::PathStrokeType::JointStyle::mitered, 
                                                         juce::PathStrokeType::EndCapStyle::rounded));
    
    // Tip highlight
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.fillEllipse (endPoint.x - indicatorWidth * 0.3f, endPoint.y - indicatorWidth * 0.3f,
                    indicatorWidth * 0.6f, indicatorWidth * 0.6f);
}

//==============================================================================
void InstantMixLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                   const juce::Colour& backgroundColour,
                                                   bool shouldDrawButtonAsHighlighted,
                                                   bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat();
    
    // 3D button effect
    float cornerSize = 4.0f;
    
    // Shadow (depth)
    if (!button.getToggleState())
    {
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRoundedRectangle (bounds.translated (0, 2.0f), cornerSize);
    }
    
    // Base color with gradient for 3D effect
    juce::Colour baseColor = button.getToggleState() ? 
                             button.findColour (juce::TextButton::buttonOnColourId) :
                             backgroundColour;
    
    if (shouldDrawButtonAsDown)
        baseColor = baseColor.darker (0.2f);
    else if (shouldDrawButtonAsHighlighted)
        baseColor = baseColor.brighter (0.1f);
    
    juce::ColourGradient gradient (baseColor.brighter (0.15f), bounds.getX(), bounds.getY(),
                                    baseColor.darker (0.15f), bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill (gradient);
    g.fillRoundedRectangle (bounds, cornerSize);
    
    // Top highlight line
    if (!button.getToggleState() && !shouldDrawButtonAsDown)
    {
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.drawHorizontalLine (bounds.getY() + 1, bounds.getX() + cornerSize, bounds.getRight() - cornerSize);
    }
    
    // Border
    g.setColour (baseColor.darker (0.3f));
    g.drawRoundedRectangle (bounds, cornerSize, 1.0f);
}

void InstantMixLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                            bool shouldDrawButtonAsHighlighted,
                                            bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds();
    
    juce::Colour textColor = button.getToggleState() ?
                             button.findColour (juce::TextButton::textColourOnId) :
                             button.findColour (juce::TextButton::textColourOffId);
    
    if (shouldDrawButtonAsDown)
        textColor = textColor.darker (0.2f);
    
    g.setColour (textColor);
    g.setFont (juce::Font ("Arial", 12.0f, juce::Font::bold));
    
    // Slight offset when pressed for 3D effect
    auto textBounds = bounds;
    if (shouldDrawButtonAsDown)
        textBounds = textBounds.translated (0, 1);
    
    g.drawText (button.getButtonText(), textBounds, juce::Justification::centred, false);
}

juce::Font InstantMixLookAndFeel::getLabelFont (juce::Label& label)
{
    return juce::Font ("Arial", 11.0f, juce::Font::plain);
}
