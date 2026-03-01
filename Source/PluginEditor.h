#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

//==============================================================================
class LeviathexInstantMixerAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                                  public juce::Timer,
                                                  public juce::Button::Listener
{
public:
    LeviathexInstantMixerAudioProcessorEditor (LeviathexInstantMixerAudioProcessor&);
    ~LeviathexInstantMixerAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void buttonClicked (juce::Button*) override;
    
    // Mouse events for custom knobs
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;

private:
    // Layout constants (declared first so they can be used in array sizes below)
    enum KnobIndex { MixKnob, InputKnob, OutputKnob, NumKnobs };
    enum InstrumentIndex { Acoustic, ElectricGuitar, Vocals, Bass, Piano, Drums, NumInstruments };

    // Reference to processor
    LeviathexInstantMixerAudioProcessor& audioProcessor;
    
    // Hidden sliders + APVTS attachments
    juce::OwnedArray<juce::Slider> knobs;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, NumKnobs> attachments;
    
    // UI components
    juce::OwnedArray<juce::TextButton> instrumentButtons;
    juce::TextButton bypassButton;
    
    // Knob rectangles for hit testing
    juce::Rectangle<int> knobRects[NumKnobs];
    
    // Drag state
    int activeKnobIndex = -1;
    float dragStartValue = 0.0f;
    int dragStartY = 0;
    
    // Value display popup
    int valueDisplayFrames = 0;
    float displayValue = 0.0f;
    
    // Level metering
    float inputPeakHold[2] = { 0.0f };
    float outputPeakHold[2] = { 0.0f };
    const float peakHoldDecay = 0.97f;
    
    // Instrument colors
    juce::Colour getInstrumentColor (int instrument) const;
    juce::String getInstrumentDetail (int instrument) const;
    
    // Drawing methods
    void drawKnob (juce::Graphics& g, int knobIndex);
    void drawLevelMeter (juce::Graphics& g, const juce::Rectangle<int>& bounds, float level, float peak, bool isInput);
    void drawInstrumentSelector (juce::Graphics& g);
    void drawHeader (juce::Graphics& g);
    
    // Helper methods
    float knobValueToAngle (float value) const;
    float angleToKnobValue (float angle) const;
    void updateKnobFromMouse (int knobIndex, int mouseY);
    void resetKnob (int knobIndex);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LeviathexInstantMixerAudioProcessorEditor)
};
