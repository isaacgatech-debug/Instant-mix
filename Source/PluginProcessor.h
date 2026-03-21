#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>
#include <mutex>

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

struct ExciterState
{
    // Tape-style saturation state variables
    float dcBlockerX1 = 0.0f;
    float dcBlockerY1 = 0.0f;
    float preEmphasisX1 = 0.0f;
    float preEmphasisY1 = 0.0f;
    float deEmphasisX1 = 0.0f;
    float deEmphasisY1 = 0.0f;
    
    void reset()
    {
        dcBlockerX1 = dcBlockerY1 = 0.0f;
        preEmphasisX1 = preEmphasisY1 = 0.0f;
        deEmphasisX1 = deEmphasisY1 = 0.0f;
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
    std::atomic<float> gainReductionDb { 0.0f }; // Current gain reduction in dB
    
    // Logging system for error reporting (public for UI access)
    void addLogMessage (const juce::String& message, const juce::String& level = "INFO");
    juce::String getLogAsString() const;
    void clearLog();

private:
    //==============================================================================
    // Parameter pointers
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* inputGainParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* instrumentParam = nullptr;
    std::atomic<float>* stereoWidthParam = nullptr;
    std::atomic<float>* reverbEnabledParam = nullptr;
    std::atomic<float>* reverbLengthParam = nullptr;
    std::atomic<float>* reverbSendToBusParam = nullptr;
    std::atomic<float>* autoMakeupParam = nullptr;
    std::atomic<float>* outputTrimParam = nullptr;
    
    // Smoothing
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inputGainSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGainSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> makeupGainSmoother;
    
    // DSP state: [channel][band] — each of 6 EQ bands needs its own state per channel
    std::array<std::array<BiquadState, 6>, 2> eqStates;
    std::array<CompressorState, 2> compressorStates;
    std::array<ExciterState, 2> exciterStates;
    
    // Stereo-linked compressor envelope
    float linkedEnvelope = 0.0f;
    
    // Compressor coefficients (moved from static locals)
    float cachedSampleRate = 0.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
    
    // EQ coefficients (shared across channels) — 6 bands
    std::array<BiquadCoeffs, 6> eqCoeffs;
    
    // Cache for EQ rebuilding
    int lastBuiltInstrument = -1;
    float lastBuiltMix = -1.0f;
    
    // Sample rate
    float currentSampleRate = 44100.0f;
    
    // Pre-allocated ramp buffers (avoid heap allocation in audio thread)
    std::vector<float> inputGainRamp;
    std::vector<float> outputGainRamp;
    std::vector<float> mixRamp;
    juce::AudioBuffer<float> reverbBuffer;
    
    // Reverb processor
    juce::Reverb reverbProcessor;
    juce::Reverb::Parameters reverbParams;
    
    // 2x Oversampling for exciter (always-on for cleaner saturation)
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    // DSP methods
    void rebuildEQ(int instrument, float mix);
    void processBiquad(float& sample, BiquadState& state, const BiquadCoeffs& coeffs);
    void applyExciter(float& sample, ExciterState& state, float intensity);
    void applyCompressor(float& sampleL, float& sampleR, float mix, int instrument);
    void applyLimiter(float& sample);
    float calculateRMS(const float* buffer, int numSamples);
    void configureReverbForInstrument (int instrument, float length);
    void processReverb (juce::AudioBuffer<float>& dryBuffer, juce::AudioBuffer<float>& reverbBuffer, int numSamples, float wetLevel);
    
    // Loudness compensation
    float currentMakeupGain = 1.0f;
    float measuredInputLUFS = -70.0f;
    float measuredOutputLUFS = -70.0f;
    static constexpr int lufsWindowSize = 3; // 3 seconds for short-term LUFS
    std::vector<float> lufsInputHistory;
    std::vector<float> lufsOutputHistory;
    size_t lufsHistoryIndex = 0;
    
    // Logging system for error reporting
    struct LogEntry
    {
        juce::String timestamp;
        juce::String level;  // INFO, WARN, ERROR
        juce::String message;
    };
    std::vector<LogEntry> logEntries;
    mutable std::mutex logMutex;
    
    float calculateShortTermLUFS (const float* left, const float* right, int numSamples);
    void updateMakeupGain (float mixValue, bool autoMakeupEnabled);
    
    // Gain conversion
    float knobToLinear (float knobValue) const;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LeviathexInstantMixerAudioProcessor)
};
