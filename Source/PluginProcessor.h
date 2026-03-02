#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

//==============================================================================
struct BiquadCoeffs
{
    float a0, a1, a2, b1, b2;
};

struct BiquadState
{
    // Direct Form I: separate input (x) and output (y) delay lines
    float x1 = 0.f, x2 = 0.f; // previous inputs
    float y1 = 0.f, y2 = 0.f; // previous outputs
    
    void reset()
    {
        x1 = x2 = y1 = y2 = 0.0f;
    }
    
    float process (float x, const BiquadCoeffs& c)
    {
        float y = c.a0 * x + c.a1 * x1 + c.a2 * x2
                            - c.b1 * y1 - c.b2 * y2;
        
        // Denormal/NaN protection — only kill non-finite values, zero is valid
        if (! std::isfinite (y)) y = 0.0f;
        
        x2 = x1;  x1 = x;
        y2 = y1;  y1 = y;
        
        return y;
    }
};

struct CompressorState
{
    float envelope = 0.0f;
    float gainReduction = 1.0f;
    
    void reset()
    {
        envelope = 0.0f;
        gainReduction = 1.0f;
    }
};

//==============================================================================
class LeviathexInstantMixerAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    LeviathexInstantMixerAudioProcessor();
    ~LeviathexInstantMixerAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // Public for UI access
    std::atomic<bool> bypassed { false };
    juce::AudioProcessorValueTreeState parameters;
    
    // Level metering
    float inputLevel[2] = { 0.0f };
    float outputLevel[2] = { 0.0f };
    float inputPeak[2] = { 0.0f };
    float outputPeak[2] = { 0.0f };

private:
    //==============================================================================
    // Parameter pointers
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* inputGainParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* instrumentParam = nullptr;
    
    // Smoothing
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inputGainSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGainSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoother;
    
    // DSP state: [channel][band] — each of 6 EQ bands needs its own state per channel
    std::array<std::array<BiquadState, 6>, 2> eqStates;
    std::array<CompressorState, 2> compressorStates;
    
    // Stereo-linked compressor envelope
    float linkedEnvelope = 0.0f;
    
    // EQ coefficients (shared across channels) — 6 bands
    std::array<BiquadCoeffs, 6> eqCoeffs;
    
    // Cache for EQ rebuilding
    int lastBuiltInstrument = -1;
    float lastBuiltMix = -1.0f;
    
    // Sample rate
    float currentSampleRate = 44100.0f;
    
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    // DSP methods
    void rebuildEQ(int instrument, float mix);
    void processBiquad(float& sample, BiquadState& state, const BiquadCoeffs& coeffs);
    void applyCompressor(float& sample, float mix, int instrument);
    void applyLimiter(float& sample);
    float calculateRMS(const float* buffer, int numSamples);
    
    // Biquad coefficient calculations
    BiquadCoeffs calcHighShelf (float freq, float Q, float gain, float sampleRate);
    BiquadCoeffs calcLowShelf (float freq, float Q, float gain, float sampleRate);
    BiquadCoeffs calcPeakingEQ (float freq, float Q, float gain, float sampleRate);
    BiquadCoeffs calcHighPass (float freq, float Q, float sampleRate);
    BiquadCoeffs calcLowPass (float freq, float Q, float sampleRate);
    
    // Gain conversion
    float knobToLinear(float knobValue) const;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LeviathexInstantMixerAudioProcessor)
};
