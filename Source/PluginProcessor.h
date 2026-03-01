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
    float z1, z2;
    
    void reset()
    {
        z1 = z2 = 0.0f;
    }
    
    float process(float x, const BiquadCoeffs& c)
    {
        float y = c.a0 * x + c.a1 * z1 + c.a2 * z2 - c.b1 * z1 - c.b2 * z2;
        z2 = z1;
        z1 = y;
        
        // Denormal protection
        if (std::abs(y) < 1e-15f) y = 0.0f;
        
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
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGainSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGainSmoother;
    
    // DSP state per channel (Max 2 for stereo)
    std::array<BiquadState, 2> eqStates;
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
