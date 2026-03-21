#include "PluginEditor.h"

//==============================================================================
LeviathexInstantMixerAudioProcessorEditor::LeviathexInstantMixerAudioProcessorEditor (LeviathexInstantMixerAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Set custom LookAndFeel
    setLookAndFeel (&customLookAndFeel);
    
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
    attachments[WidthKnob]  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters, "stereo_width", *knobs[WidthKnob]);
    
    // Create instrument selector buttons
#if INSTANT_MIX_PRO
    juce::String instrumentNames[] = { "Acoustic", "Vocals", "Piano/Keys", "E-Guitar", "Drums", "Bass" };
#else
    juce::String instrumentNames[] = { "Acoustic", "Vocals", "Piano/Keys" };
#endif
    
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
    
    // Bypass button removed - redundant with DAW functionality
    
    // Create Bugs button for error reporting (beta testing feature)
    bugsButton.setButtonText ("BUGS");
    bugsButton.setColour (juce::TextButton::buttonColourId, InstantMixLookAndFeel::getPanelColor());
    bugsButton.setColour (juce::TextButton::textColourOffId, InstantMixLookAndFeel::getMutedTextColor());
    bugsButton.addListener (this);
    bugsButton.setTooltip ("View console logs and error messages for debugging");
    addAndMakeVisible (bugsButton);
    
    // Create Undo/Redo buttons (top left)
    undoButton.setButtonText ("←");
    undoButton.setColour (juce::TextButton::buttonColourId, InstantMixLookAndFeel::getPanelColor());
    undoButton.setColour (juce::TextButton::textColourOffId, InstantMixLookAndFeel::getTextColor());
    undoButton.addListener (this);
    undoButton.setTooltip ("Undo (Cmd+Z)");
    addAndMakeVisible (undoButton);
    
    redoButton.setButtonText ("→");
    redoButton.setColour (juce::TextButton::buttonColourId, InstantMixLookAndFeel::getPanelColor());
    redoButton.setColour (juce::TextButton::textColourOffId, InstantMixLookAndFeel::getTextColor());
    redoButton.addListener (this);
    redoButton.setTooltip ("Redo (Cmd+Shift+Z)");
    addAndMakeVisible (redoButton);
    
    // Set undo manager for APVTS (max 50 steps)
    p.parameters.state.addListener (this);
    undoManager.clearUndoHistory();
    
    // Enable keyboard focus for shortcuts
    setWantsKeyboardFocus (true);
    addKeyListener (this);
    
    // Update instrument colors to new scheme
    for (int i = 0; i < NumInstruments; ++i)
    {
        auto color = InstantMixLookAndFeel::getInstrumentColor (i);
        instrumentButtons[i]->setColour (juce::TextButton::buttonOnColourId, color);
        instrumentButtons[i]->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    }
    
    // Set window properties (must be done last, as it triggers resized() which expects components to exist)
    setSize (700, 550); // Larger default size to prevent button crowding
    
    // Make UI resizable (80% to 200%)
    setResizable (true, true);
    setResizeLimits (560, 440, 1400, 1100); // 80% to 200% of base 700x550
    
    // Start timer for level meter updates (30Hz)
    startTimerHz (30);
}

LeviathexInstantMixerAudioProcessorEditor::~LeviathexInstantMixerAudioProcessorEditor()
{
    // Remove LookAndFeel to avoid dangling pointer
    setLookAndFeel (nullptr);
}

//==============================================================================
void LeviathexInstantMixerAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    
    // PREMIUM HARDWARE BACKDROP
    // 1. Outer frame with brushed metal texture
    juce::ColourGradient outerGradient (
        juce::Colour (65, 70, 80),  // Top - lighter blue-gray metallic
        0, 0,
        juce::Colour (45, 50, 60),  // Bottom - darker
        0, bounds.getHeight(),
        false
    );
    g.setGradientFill (outerGradient);
    g.fillAll();
    
    // 2. Brushed metal texture effect (subtle horizontal lines)
    g.setColour (juce::Colours::white.withAlpha (0.02f));
    for (int y = 0; y < bounds.getHeight(); y += 2)
    {
        g.drawHorizontalLine (y, 0, bounds.getWidth());
    }
    
    // 3. Outer rim lighting (top edge highlight)
    g.setGradientFill (juce::ColourGradient (
        juce::Colours::white.withAlpha (0.15f), 0, 0,
        juce::Colours::transparentBlack, 0, 8,
        false
    ));
    g.fillRect (bounds.removeFromTop (8));
    
    // 4. Inset panel with beveled edges
    auto panelBounds = bounds.reduced (12, 12);
    
    // Outer bevel shadow (top-left)
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawRect (panelBounds.expanded (1), 1);
    
    // Inner bevel highlight (bottom-right)
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.drawLine (panelBounds.getX(), panelBounds.getBottom(), 
                panelBounds.getRight(), panelBounds.getBottom(), 1.0f);
    g.drawLine (panelBounds.getRight(), panelBounds.getY(), 
                panelBounds.getRight(), panelBounds.getBottom(), 1.0f);
    
    // 5. Main panel with dark blue-gray gradient
    juce::ColourGradient panelGradient (
        juce::Colour (28, 32, 38),  // Top
        panelBounds.getX(), panelBounds.getY(),
        juce::Colour (22, 26, 32),  // Bottom - darker
        panelBounds.getX(), panelBounds.getBottom(),
        false
    );
    g.setGradientFill (panelGradient);
    g.fillRoundedRectangle (panelBounds.toFloat(), 6.0f);
    
    // 6. Subtle inner glow
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.drawRoundedRectangle (panelBounds.toFloat().reduced (1), 5.0f, 2.0f);
    
    // Draw header with metallic separator
    drawHeader (g);
    
    // Add metallic separator line below header
    int separatorY = 55;
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawHorizontalLine (separatorY, 20, bounds.getWidth() - 20);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine (separatorY + 1, 20, bounds.getWidth() - 20);
    drawInstrumentSelector (g);
    
    // Draw knobs using new LookAndFeel
    for (int i = 0; i < NumKnobs; ++i)
        drawKnob (g, i);
    
    // Draw instrument detail text
    int currentInstrument = static_cast<int> (*audioProcessor.parameters.getRawParameterValue ("instrument"));
    juce::String detail = getInstrumentDetail (currentInstrument);
    g.setColour (InstantMixLookAndFeel::getMutedTextColor());
    g.setFont (11.0f);
    g.drawText (detail, getLocalBounds().removeFromBottom (50).withTrimmedBottom (30), 
                juce::Justification::centred, true);
    
    // Draw full-height level meters (FabFilter style)
    int meterWidth = 45;
    int meterY = 130; // Below header and instrument buttons
    int meterHeight = getHeight() - meterY - 60; // Leave space at bottom
    
    // Input meter (left edge)
    auto inputMeterBounds = juce::Rectangle<int> (10, meterY, meterWidth, meterHeight);
    drawLevelMeter (g, inputMeterBounds, audioProcessor.inputLevel[0], inputPeakHold[0], true);
    
    // Output meter (right edge)
    auto outputMeterBounds = juce::Rectangle<int> (getWidth() - meterWidth - 10, meterY, meterWidth, meterHeight);
    drawLevelMeter (g, outputMeterBounds, audioProcessor.outputLevel[0], outputPeakHold[0], false);
    
    // Draw gain reduction meter (horizontal bar below mix knob)
    float grDb = audioProcessor.gainReductionDb.load();
    if (grDb > 0.1f) // Only show if actively compressing
    {
        int grMeterWidth = 120;
        int grMeterHeight = 20;
        int grMeterX = (getWidth() - grMeterWidth) / 2;
        int grMeterY = getHeight() / 2 + 120;
        
        auto grBounds = juce::Rectangle<int> (grMeterX, grMeterY, grMeterWidth, grMeterHeight);
        
        // Background
        g.setColour (juce::Colour (25, 25, 30));
        g.fillRoundedRectangle (grBounds.toFloat(), 3.0f);
        
        // Calculate fill (0 to -20dB range)
        float grNormalized = juce::jlimit (0.0f, 1.0f, grDb / 20.0f);
        int fillWidth = static_cast<int> (grMeterWidth * grNormalized);
        
        if (fillWidth > 0)
        {
            auto fillBounds = grBounds.withWidth (fillWidth);
            int currentInstrument = static_cast<int> (*audioProcessor.parameters.getRawParameterValue ("instrument"));
            juce::Colour grColor = getInstrumentColor (currentInstrument);
            g.setColour (grColor.withAlpha (0.8f));
            g.fillRoundedRectangle (fillBounds.toFloat(), 3.0f);
        }
        
        // Border
        g.setColour (juce::Colour (50, 50, 55));
        g.drawRoundedRectangle (grBounds.toFloat(), 3.0f, 1.0f);
        
        // Label and value
        g.setColour (InstantMixLookAndFeel::getTextColor());
        g.setFont (9.0f);
        g.drawText ("GR", grBounds.getX() - 25, grBounds.getY(), 20, grBounds.getHeight(), juce::Justification::right);
        g.drawText (juce::String (grDb, 1) + "dB", grBounds.getRight() + 5, grBounds.getY(), 40, grBounds.getHeight(), juce::Justification::left);
    }
    
    // Draw value popup if active
    if (valueDisplayFrames > 0 && activeKnobIndex >= 0)
    {
        juce::String text;
        if (activeKnobIndex == MixKnob)
            text = juce::String (displayValue, 1) + "%";
        else
            text = juce::String (displayValue, 1) + "%";
        
        juce::Rectangle<int> popupRect = knobRects[activeKnobIndex].expanded (20, 35);
        
        // Popup shadow
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (popupRect.translated (2, 2).toFloat(), 6.0f);
        
        // Popup background
        g.setColour (InstantMixLookAndFeel::getPanelColor().withAlpha (0.95f));
        g.fillRoundedRectangle (popupRect.toFloat(), 6.0f);
        
        // Popup border
        g.setColour (InstantMixLookAndFeel::getKnobHighlightColor().withAlpha (0.5f));
        g.drawRoundedRectangle (popupRect.toFloat(), 6.0f, 1.0f);
        
        g.setColour (InstantMixLookAndFeel::getTextColor());
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
    
    // Bypass button removed
    
    // Layout bugs button (top right)
    bugsButton.setBounds (getWidth() - 65, 10, 50, 25);
    
    // Layout undo/redo buttons (top left)
    undoButton.setBounds (15, 10, 35, 25);
    redoButton.setBounds (55, 10, 35, 25);
    
    // Layout knobs - larger hardware-style sizes
    int mixKnobSize = 180;  // Large center mix knob
    int ioKnobSize = 100;   // Smaller input/output knobs
    int centerX = getWidth() / 2;
    int centerY = getHeight() / 2 + 10;
    
    // Mix knob (center) - leave space below for label
    knobRects[MixKnob] = juce::Rectangle<int> (centerX - mixKnobSize/2, centerY - mixKnobSize/2 - 40, mixKnobSize, mixKnobSize);
    
    // Stereo width knob (below mix knob) - smaller size
    int widthKnobSize = 80;
    knobRects[WidthKnob] = juce::Rectangle<int> (centerX - widthKnobSize/2, centerY + mixKnobSize/2 + 20, widthKnobSize, widthKnobSize);
    
    // Input knob (left) - leave space below for label
    knobRects[InputKnob] = juce::Rectangle<int> (centerX - mixKnobSize - 40, centerY - ioKnobSize/3, ioKnobSize, ioKnobSize);
    
    // Output knob (right) - leave space below for label
    knobRects[OutputKnob] = juce::Rectangle<int> (centerX + mixKnobSize - 60, centerY - ioKnobSize/3, ioKnobSize, ioKnobSize);
}

void LeviathexInstantMixerAudioProcessorEditor::timerCallback()
{
    const float clippingThreshold = 0.95f; // -0.3dBFS
    
    // Update peak holds and check for clipping
    for (int ch = 0; ch < 2; ++ch)
    {
        // Input metering
        if (audioProcessor.inputPeak[ch] > inputPeakHold[ch])
            inputPeakHold[ch] = audioProcessor.inputPeak[ch];
        else
            inputPeakHold[ch] *= peakHoldDecay;
        
        // Check input clipping
        if (audioProcessor.inputPeak[ch] >= clippingThreshold)
        {
            clippingFrames[ch] = clippingHoldFrames;
            inputClipping[ch] = true;
            audioProcessor.addLogMessage ("Input clipping detected on channel " + juce::String(ch + 1), "WARN");
        }
        else if (clippingFrames[ch] > 0)
        {
            clippingFrames[ch]--;
            if (clippingFrames[ch] == 0)
                inputClipping[ch] = false;
        }
        
        // Output metering
        if (audioProcessor.outputPeak[ch] > outputPeakHold[ch])
            outputPeakHold[ch] = audioProcessor.outputPeak[ch];
        else
            outputPeakHold[ch] *= peakHoldDecay;
        
        // Check output clipping
        if (audioProcessor.outputPeak[ch] >= clippingThreshold)
        {
            clippingFrames[ch + 2] = clippingHoldFrames;
            outputClipping[ch] = true;
            audioProcessor.addLogMessage ("Output clipping detected on channel " + juce::String(ch + 1), "WARN");
        }
        else if (clippingFrames[ch + 2] > 0)
        {
            clippingFrames[ch + 2]--;
            if (clippingFrames[ch + 2] == 0)
                outputClipping[ch] = false;
        }
    }
    
    repaint();
}

void LeviathexInstantMixerAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &bugsButton)
    {
        showLogViewer();
    }
    else if (button == &undoButton)
    {
        if (undoManager.canUndo())
            undoManager.undo();
    }
    else if (button == &redoButton)
    {
        if (undoManager.canRedo())
            undoManager.redo();
    }
    else
    {
        // Instrument button clicked
        for (int i = 0; i < NumInstruments; ++i)
        {
            if (button == instrumentButtons[i])
            {
                audioProcessor.parameters.getParameter ("instrument")->setValueNotifyingHost (i / 5.0f);
                
                // Update knob properties and force repaint to show new colors
                for (int k = 0; k < NumKnobs; ++k)
                {
                    knobs[k]->getProperties().set ("instrument", i);
                }
                repaint();
                break;
            }
        }
    }
}

void LeviathexInstantMixerAudioProcessorEditor::mouseDown (const juce::MouseEvent& event)
{
    // Check for right-click on meters to show mode menu
    if (event.mods.isRightButtonDown())
    {
        int meterWidth = 45;
        int meterY = 130;
        int meterHeight = getHeight() - meterY - 60;
        
        auto inputMeterBounds = juce::Rectangle<int> (10, meterY, meterWidth, meterHeight);
        auto outputMeterBounds = juce::Rectangle<int> (getWidth() - meterWidth - 10, meterY, meterWidth, meterHeight);
        
        if (inputMeterBounds.contains (event.getPosition()) || outputMeterBounds.contains (event.getPosition()))
        {
            juce::PopupMenu menu;
            menu.addItem (1, "RMS + Peak", true, currentMeterMode == RMS_PEAK);
            menu.addItem (2, "LUFS + True Peak", true, currentMeterMode == LUFS_TRUE_PEAK);
            menu.addItem (3, "Stereo Correlation", true, currentMeterMode == CORRELATION);
            
            menu.showMenuAsync (juce::PopupMenu::Options(), [this] (int result) {
                if (result == 1) currentMeterMode = RMS_PEAK;
                else if (result == 2) currentMeterMode = LUFS_TRUE_PEAK;
                else if (result == 3) currentMeterMode = CORRELATION;
                repaint();
            });
            return;
        }
    }
    
    // Check if mouse is over a knob
    for (int i = 0; i < NumKnobs; ++i)
    {
        if (knobRects[i].contains (event.getPosition()))
        {
            activeKnobIndex = i;
            dragStartValue = knobs[i]->getValue();
            dragStartY = event.getPosition().getY();
            
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
    return InstantMixLookAndFeel::getInstrumentColor (instrument);
}

juce::String LeviathexInstantMixerAudioProcessorEditor::getInstrumentDetail (int instrument) const
{
    switch (instrument)
    {
        case Acoustic: return "API 2500 comp · Warmth shelf · Mud cut · 5kHz presence · Air shelf";
        case Vocals:   return "API 2500 comp · Boxy cut · Body · 3.5kHz presence · Air shelf";
        case Piano:    return "API 2500 comp · Warmth shelf · Mud cut · 2.5kHz definition · Air shelf";
#if INSTANT_MIX_PRO
        case ElectricGuitar: return "API 2500 comp · Warmth · Mud cut · 4kHz presence · Air";
        case Drums:         return "API 2500 comp · Punchy warmth · Tight mud cut · Crackle · Air";
        case Bass:          return "API 2500 comp · Deep warmth · Mud cut · 800Hz punch · Air";
#endif
        default:       return "";
    }
}

void LeviathexInstantMixerAudioProcessorEditor::drawKnob (juce::Graphics& g, int knobIndex)
{
    auto rect = knobRects[knobIndex];
    if (rect.isEmpty()) return;
    
    float value = knobs[knobIndex]->getValue();
    
    // Get current instrument color for accent
    int currentInstrument = static_cast<int> (*audioProcessor.parameters.getRawParameterValue ("instrument"));
    juce::Colour accentColor = getInstrumentColor (currentInstrument);
    
    // Use LookAndFeel to draw 3D knob
    // Set instrument property for the slider so LookAndFeel can access it
    knobs[knobIndex]->getProperties().set ("instrument", currentInstrument);
    
    customLookAndFeel.drawRotarySlider (g, rect.getX(), rect.getY(), rect.getWidth(), rect.getHeight(),
                                          value / 100.0f, 
                                          -150.0f * juce::MathConstants<float>::pi / 180.0f,
                                          150.0f * juce::MathConstants<float>::pi / 180.0f,
                                          *knobs[knobIndex]);
    
    // Draw label with modern styling
    juce::String label;
    juce::String subLabel;
    switch (knobIndex)
    {
        case MixKnob:    label = "MIX"; subLabel = "IT"; break;
        case InputKnob:  label = "INPUT"; subLabel = "GAIN"; break;
        case OutputKnob: label = "OUTPUT"; subLabel = "GAIN"; break;
        case WidthKnob:  label = "WIDTH"; subLabel = "STEREO"; break;
    }
    
    // Draw label BELOW knob (hardware style)
    auto labelBounds = rect.withY (rect.getBottom() + 8).withHeight (40);
    
    // Main label
    g.setColour (InstantMixLookAndFeel::getTextColor());
    g.setFont (juce::Font ("Arial", knobIndex == MixKnob ? 16.0f : 12.0f, juce::Font::bold));
    g.drawText (label, labelBounds.withHeight (18), juce::Justification::centred);
    
    // Sublabel
    if (subLabel.isNotEmpty())
    {
        g.setColour (InstantMixLookAndFeel::getMutedTextColor());
        g.setFont (juce::Font ("Arial", 10.0f, juce::Font::plain));
        g.drawText (subLabel, labelBounds.withTrimmedTop (18), juce::Justification::centred);
    }
}

void LeviathexInstantMixerAudioProcessorEditor::drawLevelMeter (juce::Graphics& g, const juce::Rectangle<int>& bounds, float level, float peak, bool isInput)
{
    // Get current instrument color
    int currentInstrument = static_cast<int> (*audioProcessor.parameters.getRawParameterValue ("instrument"));
    juce::Colour meterColor = getInstrumentColor (currentInstrument);
    
    auto boundsF = bounds.toFloat();
    
    // Draw dark background
    g.setColour (juce::Colour (25, 25, 30));
    g.fillRoundedRectangle (boundsF, 4.0f);
    
    // Convert RMS to dB for display (-60dB to +6dB range)
    float levelDb = level > 0.0001f ? juce::Decibels::gainToDecibels (level) : -60.0f;
    float peakDb = peak > 0.0001f ? juce::Decibels::gainToDecibels (peak) : -60.0f;
    
    // Map dB to height (0dB at 85% height, +6dB at top)
    auto dbToHeight = [&](float db) {
        float normalized = juce::jmap (db, -60.0f, 6.0f, 0.0f, 1.0f);
        return bounds.getHeight() * (1.0f - normalized);
    };
    
    // Draw gradient meter fill
    if (levelDb > -60.0f)
    {
        float fillY = dbToHeight (levelDb);
        auto fillRect = bounds.toFloat().withTop (fillY);
        
        // Create gradient: Green -> Yellow -> Orange -> Red
        juce::ColourGradient gradient (juce::Colours::red, 0, bounds.getY(),
                                       juce::Colours::green, 0, bounds.getBottom(), false);
        gradient.addColour (0.15, juce::Colours::orange);  // -6dB
        gradient.addColour (0.30, juce::Colours::yellow);  // -12dB
        gradient.addColour (0.50, meterColor);             // -20dB
        
        g.setGradientFill (gradient);
        g.fillRoundedRectangle (fillRect, 3.0f);
    }
    
    // Draw dB scale markings
    float dbMarks[] = { 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -12.0f, -20.0f, -30.0f, -40.0f, -60.0f };
    g.setColour (juce::Colours::white.withAlpha (0.3f));
    g.setFont (8.0f);
    
    for (float db : dbMarks)
    {
        float y = bounds.getY() + dbToHeight (db);
        g.drawHorizontalLine (static_cast<int>(y), bounds.getX(), bounds.getX() + 8);
        
        // Draw dB labels on every other mark
        if (db == 0.0f || db == -6.0f || db == -12.0f || db == -20.0f || db == -40.0f || db == -60.0f)
        {
            juce::String label = (db > 0 ? "+" : "") + juce::String (static_cast<int>(db));
            g.drawText (label, bounds.getX() + 10, static_cast<int>(y) - 6, 25, 12, juce::Justification::left);
        }
    }
    
    // Draw peak hold line
    if (peakDb > -60.0f)
    {
        float peakY = bounds.getY() + dbToHeight (peakDb);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawHorizontalLine (static_cast<int>(peakY), bounds.getX() + 2, bounds.getRight() - 2);
    }
    
    // Draw clipping indicator (red LED at top)
    bool isClipping = isInput ? inputClipping[0] : outputClipping[0];
    auto ledBounds = bounds.withHeight (12).withY (bounds.getY() - 18);
    g.setColour (isClipping ? juce::Colours::red : juce::Colour (60, 60, 65));
    g.fillEllipse (ledBounds.toFloat());
    if (isClipping)
    {
        g.setColour (juce::Colours::red.withAlpha (0.3f));
        g.fillEllipse (ledBounds.expanded (3).toFloat());
    }
    
    // Border
    g.setColour (juce::Colour (50, 50, 55));
    g.drawRoundedRectangle (boundsF, 4.0f, 1.0f);
    
    // Draw label with mode indicator
    g.setColour (InstantMixLookAndFeel::getTextColor());
    g.setFont (juce::Font ("Arial", 10.0f, juce::Font::bold));
    
    juce::String label = isInput ? "INPUT" : "OUTPUT";
    juce::String modeLabel;
    switch (currentMeterMode)
    {
        case RMS_PEAK: modeLabel = "RMS"; break;
        case LUFS_TRUE_PEAK: modeLabel = "LUFS"; break;
        case CORRELATION: modeLabel = "CORR"; break;
    }
    
    g.drawText (label, bounds.withY (bounds.getBottom() + 5).withHeight (14), juce::Justification::centred);
    g.setFont (8.0f);
    g.setColour (InstantMixLookAndFeel::getMutedTextColor());
    g.drawText (modeLabel, bounds.withY (bounds.getBottom() + 18).withHeight (12), juce::Justification::centred);
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
    // Undo/Redo buttons will go in top left (replacing "INSTANT MIX" text)
    // For now, just draw a subtle top bar
    
    // Add embossed branding in bottom center
    auto brandBounds = getLocalBounds().removeFromBottom (25);
    
    // Embossed effect - shadow first
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.setFont (juce::Font ("Arial", 11.0f, juce::Font::bold));
    g.drawText ("INSTANT MIX", brandBounds.translated (1, 1), juce::Justification::centred);
    
    // Highlight
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawText ("INSTANT MIX", brandBounds.translated (-1, -1), juce::Justification::centred);
    
    // Main text (subtle)
    g.setColour (juce::Colour (80, 85, 95));
    g.drawText ("INSTANT MIX", brandBounds, juce::Justification::centred);
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
    int deltaY = dragStartY - mouseY;
    
    // All knobs: 7.5 pixels for full 0-100 range (4x faster than original, 2x faster than previous)
    float pixelsForFullRange = 7.5f;
    float delta = (dragStartY - mouseY) / pixelsForFullRange;
    float newValue = dragStartValue + delta;
    knobs[knobIndex]->setValue (juce::jlimit (0.0f, 100.0f, newValue));
}

// ... (rest of the code remains the same)

void LeviathexInstantMixerAudioProcessorEditor::showLogViewer()
{
    if (logViewerWindow == nullptr)
    {
        logViewerWindow = std::make_unique<juce::DocumentWindow> ("Debug Logs", 
                                                                   juce::Colours::darkgrey,
                                                                   juce::DocumentWindow::closeButton);
        
        auto* textEditor = new juce::TextEditor();
        textEditor->setMultiLine (true);
        textEditor->setReadOnly (true);
        textEditor->setFont (juce::Font ("Courier New", 12.0f, juce::Font::plain));
        textEditor->setColour (juce::TextEditor::backgroundColourId, juce::Colours::black);
        textEditor->setColour (juce::TextEditor::textColourId, juce::Colours::lightgreen);
        
        // Get logs from processor
        textEditor->setText (audioProcessor.getLogAsString());
        
        logViewerWindow->setContentOwned (textEditor, true);
        logViewerWindow->setSize (600, 400);
        logViewerWindow->setResizable (true, true);
    }
    
    logViewerWindow->setVisible (true);
    logViewerWindow->toFront (true);
}

void LeviathexInstantMixerAudioProcessorEditor::addLogEntry (const juce::String& message, const juce::String& level)
{
    audioProcessor.addLogMessage (message, level);
}

void LeviathexInstantMixerAudioProcessorEditor::resetKnob (int knobIndex)
{
    float defaultValue;
    switch (knobIndex)
    {
        case MixKnob:    defaultValue = 0.0f;   break;
        case InputKnob:  defaultValue = 50.0f;  break;
        case OutputKnob: defaultValue = 50.0f;  break;
        case WidthKnob:  defaultValue = 100.0f; break;
        default:         defaultValue = 50.0f;  break;
    }
    
    knobs[knobIndex]->setValue (defaultValue);
    
    // Show reset feedback
    activeKnobIndex = knobIndex;
    valueDisplayFrames = 60;
    displayValue = defaultValue;
}

bool LeviathexInstantMixerAudioProcessorEditor::keyPressed (const juce::KeyPress& key, juce::Component* originatingComponent)
{
    // Cmd+Z for undo
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        if (undoManager.canUndo())
        {
            undoManager.undo();
            return true;
        }
    }
    // Cmd+Shift+Z for redo
    else if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        if (undoManager.canRedo())
        {
            undoManager.redo();
            return true;
        }
    }
    
    return false;
}

void LeviathexInstantMixerAudioProcessorEditor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    // Track parameter changes for undo/redo
    // This is called when APVTS parameters change
    ignoreUnused (tree, property);
}

void LeviathexInstantMixerAudioProcessorEditor::setUIScale (float newScale)
{
    uiScale = juce::jlimit (0.8f, 2.0f, newScale);
    setSize (static_cast<int> (560 * uiScale), static_cast<int> (420 * uiScale));
}
