#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "InstantMixLookAndFeel.h"

//==============================================================================
class LeviathexInstantMixerAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                                  public juce::Timer,
                                                  public juce::Button::Listener,
                                                  public juce::KeyListener,
                                                  public juce::ValueTree::Listener
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
    
    // Keyboard shortcuts
    bool keyPressed (const juce::KeyPress& key, juce::Component* originatingComponent) override;
    
    // ValueTree listener for undo/redo
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override {}
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override {}
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged (juce::ValueTree&) override {}

private:
    // Layout constants (declared first so they can be used in array sizes below)
    enum KnobIndex { MixKnob, InputKnob, OutputKnob, WidthKnob, NumKnobs };
#if INSTANT_MIX_PRO
    enum InstrumentIndex { Acoustic, Vocals, Piano, ElectricGuitar, Drums, Bass, NumInstruments };
#else
    enum InstrumentIndex { Acoustic, Vocals, Piano, NumInstruments };
#endif

    // Reference to processor
    LeviathexInstantMixerAudioProcessor& audioProcessor;
    
    // Custom LookAndFeel
    InstantMixLookAndFeel customLookAndFeel;
    
    // Hidden sliders + APVTS attachments
    juce::OwnedArray<juce::Slider> knobs;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, NumKnobs> attachments;
    
    // UI components
    juce::OwnedArray<juce::TextButton> instrumentButtons;
    juce::TextButton bugsButton;
    juce::TextButton undoButton;
    juce::TextButton redoButton;
    
    // Undo/Redo system
    juce::UndoManager undoManager;
    
    // UI scaling (80% to 200%)
    float uiScale = 1.0f;
    void setUIScale (float newScale);
    
    // Log viewer window
    std::unique_ptr<juce::DocumentWindow> logViewerWindow;
    void showLogViewer();
    void addLogEntry (const juce::String& message, const juce::String& level = "INFO");
    
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
    enum MeterMode { RMS_PEAK, LUFS_TRUE_PEAK, CORRELATION };
    MeterMode currentMeterMode = RMS_PEAK;
    
    float inputPeakHold[2] = { 0.0f };
    float outputPeakHold[2] = { 0.0f };
    const float peakHoldDecay = 0.97f;
    
    // Clipping indicators
    bool inputClipping[2] = { false };
    bool outputClipping[2] = { false };
    int clippingFrames[4] = { 0 }; // Input L, Input R, Output L, Output R
    const int clippingHoldFrames = 60; // 2 seconds at 30Hz
    
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
