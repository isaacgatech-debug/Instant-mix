#include "PluginEditor.h"

//==============================================================================
LeviathexInstantMixerAudioProcessorEditor::LeviathexInstantMixerAudioProcessorEditor (LeviathexInstantMixerAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Create hidden sliders for parameter management
    for (int i = 0; i < NumKnobs; ++i)
    {
        auto* slider = new juce::Slider();
        slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider->setBounds (-1, -1, 1, 1); // Hide off-screen
        addAndMakeVisible (slider);
        knobs.add (slider);
    }
    
    // Attach parameters to hidden sliders via APVTS SliderAttachment
    attachments[MixKnob]    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters, "mix",          *knobs[MixKnob]);
    attachments[InputKnob]  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters, "input_gain",   *knobs[InputKnob]);
    attachments[OutputKnob] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters, "output_gain",  *knobs[OutputKnob]);
    
    // Create instrument selector buttons
    juce::String instrumentNames[] = { "Acoustic", "Vocals", "Piano/Keys" };
    
    for (int i = 0; i < NumInstruments; ++i)
    {
        auto* button = new juce::TextButton (instrumentNames[i]);
        button->setButtonText (instrumentNames[i]);
        button->setColour (juce::TextButton::buttonColourId, juce::Colours::darkgrey);
        button->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        button->setColour (juce::TextButton::buttonOnColourId, getInstrumentColor (i));
        button->setClickingTogglesState (true);
        button->setRadioGroupId (12345);
        button->addListener (this);
        addAndMakeVisible (button);
        instrumentButtons.add (button);
    }
    
    // Set initial instrument selection
    int currentInstrument = static_cast<int> (*p.parameters.getRawParameterValue ("instrument"));
    if (currentInstrument >= 0 && currentInstrument < NumInstruments)
        instrumentButtons[currentInstrument]->setToggleState (true, juce::dontSendNotification);
    
    // Create bypass button
    bypassButton.setButtonText ("BYP");
    bypassButton.setColour (juce::TextButton::buttonColourId, juce::Colours::darkgrey);
    bypassButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::red);
    bypassButton.setClickingTogglesState (true);
    bypassButton.addListener (this);
    bypassButton.setToggleState (audioProcessor.bypassed.load(), juce::dontSendNotification);
    addAndMakeVisible (bypassButton);
    
    // Set window properties (must be done last, as it triggers resized() which expects components to exist)
    setSize (560, 420);
    setResizable (true, true);
    setResizeLimits (460, 340, 900, 700);
    
    // Start timer for level meter updates (30Hz)
    startTimerHz (30);
}

LeviathexInstantMixerAudioProcessorEditor::~LeviathexInstantMixerAudioProcessorEditor()
{
}

//==============================================================================
void LeviathexInstantMixerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    
    drawHeader (g);
    drawInstrumentSelector (g);
    
    // Draw knobs
    for (int i = 0; i < NumKnobs; ++i)
        drawKnob (g, i);
    
    // Draw instrument detail text
    int currentInstrument = static_cast<int> (*audioProcessor.parameters.getRawParameterValue ("instrument"));
    juce::String detail = getInstrumentDetail (currentInstrument);
    g.setColour (juce::Colours::lightgrey);
    g.setFont (12.0f);
    g.drawText (detail, getLocalBounds().removeFromBottom (60).withTrimmedBottom (40), 
                juce::Justification::centred, true);
    
    // Draw level meters
    int meterY = getHeight() - 80;
    int meterHeight = 60;
    int meterWidth = 20;
    
    // Input meter (left)
    auto inputMeterBounds = juce::Rectangle<int> (80, meterY, meterWidth, meterHeight);
    drawLevelMeter (g, inputMeterBounds, audioProcessor.inputLevel[0], inputPeakHold[0], true);
    
    // Output meter (right)
    auto outputMeterBounds = juce::Rectangle<int> (getWidth() - 100, meterY, meterWidth, meterHeight);
    drawLevelMeter (g, outputMeterBounds, audioProcessor.outputLevel[0], outputPeakHold[0], false);
    
    // Draw value popup if active
    if (valueDisplayFrames > 0 && activeKnobIndex >= 0)
    {
        juce::String text;
        if (activeKnobIndex == MixKnob)
            text = juce::String (displayValue, 1) + "%";
        else
            text = juce::String (displayValue, 1) + "%";
        
        juce::Rectangle<int> popupRect = knobRects[activeKnobIndex].expanded (20, 40);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.fillRoundedRectangle (popupRect.toFloat(), 5.0f);
        
        g.setColour (juce::Colours::white);
        g.setFont (14.0f);
        g.drawText (text, popupRect, juce::Justification::centred);
        
        valueDisplayFrames--;
    }
}

void LeviathexInstantMixerAudioProcessorEditor::resized()
{
    // Layout instrument buttons (single row of 3)
    int buttonWidth = 110;
    int buttonHeight = 32;
    int buttonSpacing = 12;
    int totalButtonWidth = NumInstruments * buttonWidth + (NumInstruments - 1) * buttonSpacing;
    int buttonX = (getWidth() - totalButtonWidth) / 2;
    int buttonY = 80;
    
    for (int i = 0; i < NumInstruments; ++i)
    {
        int x = buttonX + i * (buttonWidth + buttonSpacing);
        instrumentButtons[i]->setBounds (x, buttonY, buttonWidth, buttonHeight);
    }
    
    // Layout bypass button (top right)
    bypassButton.setBounds (getWidth() - 60, 10, 50, 25);
    
    // Layout knobs
    int knobSize = juce::jmin (140, getWidth() / 4);
    int centerX = getWidth() / 2;
    int centerY = getHeight() / 2 + 20;
    
    // Mix knob (center)
    knobRects[MixKnob] = juce::Rectangle<int> (centerX - knobSize/2, centerY - knobSize/2, knobSize, knobSize);
    
    // Input knob (left)
    knobRects[InputKnob] = juce::Rectangle<int> (centerX - knobSize * 2, centerY - knobSize/3, knobSize/2, knobSize/2);
    
    // Output knob (right)
    knobRects[OutputKnob] = juce::Rectangle<int> (centerX + knobSize * 1.5, centerY - knobSize/3, knobSize/2, knobSize/2);
}

void LeviathexInstantMixerAudioProcessorEditor::timerCallback()
{
    // Update peak holds
    for (int ch = 0; ch < 2; ++ch)
    {
        if (audioProcessor.inputPeak[ch] > inputPeakHold[ch])
            inputPeakHold[ch] = audioProcessor.inputPeak[ch];
        else
            inputPeakHold[ch] *= peakHoldDecay;
        
        if (audioProcessor.outputPeak[ch] > outputPeakHold[ch])
            outputPeakHold[ch] = audioProcessor.outputPeak[ch];
        else
            outputPeakHold[ch] *= peakHoldDecay;
    }
    
    repaint();
}

void LeviathexInstantMixerAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &bypassButton)
    {
        audioProcessor.bypassed.store (button->getToggleState());
    }
    else
    {
        // Instrument button clicked
        for (int i = 0; i < NumInstruments; ++i)
        {
            if (button == instrumentButtons[i])
            {
                audioProcessor.parameters.getParameter ("instrument")->setValueNotifyingHost (i / 5.0f);
                break;
            }
        }
    }
}

void LeviathexInstantMixerAudioProcessorEditor::mouseDown (const juce::MouseEvent& event)
{
    // Check if mouse is over a knob
    for (int i = 0; i < NumKnobs; ++i)
    {
        if (knobRects[i].contains (event.getMouseDownPosition()))
        {
            activeKnobIndex = i;
            dragStartValue = knobs[i]->getValue();
            dragStartY = event.getMouseDownPosition().getY();
            
            // Start value display
            valueDisplayFrames = 60; // Show for 2 seconds at 30Hz
            displayValue = dragStartValue;
            
            break;
        }
    }
}

void LeviathexInstantMixerAudioProcessorEditor::mouseDrag (const juce::MouseEvent& event)
{
    if (activeKnobIndex >= 0)
    {
        updateKnobFromMouse (activeKnobIndex, event.getPosition().getY());
        displayValue = knobs[activeKnobIndex]->getValue();
        valueDisplayFrames = 60; // Reset display timer
    }
}

void LeviathexInstantMixerAudioProcessorEditor::mouseUp (const juce::MouseEvent& event)
{
    activeKnobIndex = -1;
}

void LeviathexInstantMixerAudioProcessorEditor::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    // Check if mouse is over a knob
    for (int i = 0; i < NumKnobs; ++i)
    {
        if (knobRects[i].contains (event.getPosition()))
        {
            float delta = wheel.deltaY * 2.0f;
            float newValue = knobs[i]->getValue() + delta;
            knobs[i]->setValue (juce::jlimit (0.0f, 100.0f, newValue));
            
            activeKnobIndex = i;
            valueDisplayFrames = 60;
            displayValue = knobs[i]->getValue();
            
            break;
        }
    }
}

void LeviathexInstantMixerAudioProcessorEditor::mouseDoubleClick (const juce::MouseEvent& event)
{
    // Check if mouse is over a knob and reset to default
    for (int i = 0; i < NumKnobs; ++i)
    {
        if (knobRects[i].contains (event.getPosition()))
        {
            resetKnob (i);
            break;
        }
    }
}

//==============================================================================
// Private methods

juce::Colour LeviathexInstantMixerAudioProcessorEditor::getInstrumentColor (int instrument) const
{
    switch (instrument)
    {
        case Acoustic: return juce::Colour (210, 140, 50);   // Amber
        case Vocals:   return juce::Colour (200, 80, 160);   // Pink/rose
        case Piano:    return juce::Colour (160, 120, 220);  // Purple
        default:       return juce::Colours::grey;
    }
}

juce::String LeviathexInstantMixerAudioProcessorEditor::getInstrumentDetail (int instrument) const
{
    switch (instrument)
    {
        case Acoustic: return "API 2500 comp · Warmth shelf · Mud cut · 5kHz presence · Air shelf";
        case Vocals:   return "API 2500 comp · Boxy cut · Body · 3.5kHz presence · Air shelf";
        case Piano:    return "API 2500 comp · Warmth shelf · Mud cut · 2.5kHz definition · Air shelf";
        default:       return "";
    }
}

void LeviathexInstantMixerAudioProcessorEditor::drawKnob (juce::Graphics& g, int knobIndex)
{
    auto rect = knobRects[knobIndex];
    if (rect.isEmpty()) return;
    
    float value = knobs[knobIndex]->getValue();
    float angle = knobValueToAngle (value);
    
    // Get current instrument color for accent
    int currentInstrument = static_cast<int> (*audioProcessor.parameters.getRawParameterValue ("instrument"));
    juce::Colour accentColor = getInstrumentColor (currentInstrument);
    
    // Draw knob background
    g.setColour (juce::Colours::darkgrey);
    g.fillEllipse (rect.toFloat());
    
    // Draw knob rim
    g.setColour (juce::Colours::grey);
    g.drawEllipse (rect.toFloat(), 2.0f);
    
    // Draw indicator line
    juce::Point<float> center = rect.getCentre().toFloat();
    float lineLength = rect.getWidth() * 0.4f;
    juce::Point<float> end = center + juce::Point<float> (std::sin (angle) * lineLength, -std::cos (angle) * lineLength);
    
    g.setColour (accentColor);
    g.drawLine (juce::Line<float> (center, end), 3.0f);
    
    // Draw center dot
    g.setColour (juce::Colours::white);
    g.fillEllipse (center.x - 3, center.y - 3, 6, 6);
    
    // Draw label
    juce::String label;
    switch (knobIndex)
    {
        case MixKnob:    label = "MIX IT"; break;
        case InputKnob:  label = "IN"; break;
        case OutputKnob: label = "OUT"; break;
    }
    
    g.setColour (juce::Colours::lightgrey);
    g.setFont (10.0f);
    g.drawText (label, rect.withTrimmedTop (rect.getHeight() + 5), juce::Justification::centred);
}

void LeviathexInstantMixerAudioProcessorEditor::drawLevelMeter (juce::Graphics& g, const juce::Rectangle<int>& bounds, float level, float peak, bool isInput)
{
    // Get current instrument color
    int currentInstrument = static_cast<int> (*audioProcessor.parameters.getRawParameterValue ("instrument"));
    juce::Colour meterColor = getInstrumentColor (currentInstrument);
    
    // Draw background
    g.setColour (juce::Colours::darkgrey);
    g.fillRect (bounds);
    
    // Draw level
    float levelHeight = level * bounds.getHeight();
    auto levelRect = bounds.withHeight (levelHeight).withBottom (bounds.getBottom());
    
    // Color based on level
    if (level < 0.7f)
        g.setColour (meterColor.withAlpha (0.8f));
    else if (level < 0.9f)
        g.setColour (juce::Colours::yellow);
    else
        g.setColour (juce::Colours::red);
    
    g.fillRect (levelRect);
    
    // Draw peak hold
    float peakY = bounds.getBottom() - (peak * bounds.getHeight());
    g.setColour (juce::Colours::white);
    g.drawHorizontalLine (peakY, bounds.getX(), bounds.getRight());
    
    // Draw label
    g.setColour (juce::Colours::lightgrey);
    g.setFont (10.0f);
    g.drawText (isInput ? "IN" : "OUT", bounds.withTrimmedTop (bounds.getHeight() + 5), juce::Justification::centred);
}

void LeviathexInstantMixerAudioProcessorEditor::drawInstrumentSelector (juce::Graphics& g)
{
    // Buttons are already drawn by JUCE, just add some visual enhancement
    int currentInstrument = static_cast<int> (*audioProcessor.parameters.getRawParameterValue ("instrument"));
    
    for (int i = 0; i < NumInstruments; ++i)
    {
        auto rect = instrumentButtons[i]->getBounds();
        
        if (instrumentButtons[i]->getToggleState())
        {
            // Draw glow effect for selected button
            juce::Colour glowColor = getInstrumentColor (i);
            g.setColour (glowColor.withAlpha (0.3f));
            g.drawRoundedRectangle (rect.toFloat().expanded (4), 5.0f, 2.0f);
        }
    }
}

void LeviathexInstantMixerAudioProcessorEditor::drawHeader (juce::Graphics& g)
{
    // Draw title
    g.setColour (juce::Colours::white);
    g.setFont (18.0f);
    g.drawText ("LEVIATHEX INSTANT MIXER", 20, 10, getWidth() - 100, 30, juce::Justification::centredLeft);
}

float LeviathexInstantMixerAudioProcessorEditor::knobValueToAngle (float value) const
{
    // Map 0-100 to -150deg to +150deg
    float normalized = value / 100.0f;
    return (normalized * 300.0f - 150.0f) * juce::MathConstants<float>::pi / 180.0f;
}

float LeviathexInstantMixerAudioProcessorEditor::angleToKnobValue (float angle) const
{
    // Map angle back to 0-100
    float degrees = angle * 180.0f / juce::MathConstants<float>::pi;
    float normalized = (degrees + 150.0f) / 300.0f;
    return juce::jlimit (0.0f, 100.0f, normalized * 100.0f);
}

void LeviathexInstantMixerAudioProcessorEditor::updateKnobFromMouse (int knobIndex, int mouseY)
{
    // All knobs: high sensitivity — fewer pixels needed to traverse full range
    float pixelsPerValue = (knobIndex == MixKnob) ? 30.0f : 20.0f;
    float delta = (dragStartY - mouseY) / pixelsPerValue;
    float newValue = dragStartValue + delta;
    knobs[knobIndex]->setValue (juce::jlimit (0.0f, 100.0f, newValue));
}

void LeviathexInstantMixerAudioProcessorEditor::resetKnob (int knobIndex)
{
    float defaultValue;
    switch (knobIndex)
    {
        case MixKnob:    defaultValue = 0.0f;  break;
        case InputKnob:  defaultValue = 50.0f; break;
        case OutputKnob: defaultValue = 50.0f; break;
        default:         defaultValue = 50.0f; break;
    }
    
    knobs[knobIndex]->setValue (defaultValue);
    
    // Show reset feedback
    activeKnobIndex = knobIndex;
    valueDisplayFrames = 60;
    displayValue = defaultValue;
}
