#include "PluginProcessor.h"
#include "PluginEditor.h"

// Define JUCE plugin macros that are missing in cmake setup
#define JucePlugin_Name "Leviathex Instant Mixer"

juce::AudioProcessorValueTreeState::ParameterLayout LeviathexInstantMixerAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    layout.add (std::make_unique<juce::AudioParameterFloat> ("mix", "Mix", 0.0f, 100.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("input_gain", "Input Gain", 0.0f, 100.0f, 50.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("output_gain", "Output Gain", 0.0f, 100.0f, 50.0f));
    layout.add (std::make_unique<juce::AudioParameterInt> ("instrument", "Instrument", 0, 5, 0));
    
    return layout;
}

//==============================================================================
LeviathexInstantMixerAudioProcessor::LeviathexInstantMixerAudioProcessor()
     : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
       parameters (*this, nullptr, juce::Identifier ("LeviathexInstantMixer"), createParameterLayout())
{
    mixParam = parameters.getRawParameterValue ("mix");
    inputGainParam = parameters.getRawParameterValue ("input_gain");
    outputGainParam = parameters.getRawParameterValue ("output_gain");
    instrumentParam = parameters.getRawParameterValue ("instrument");
}

LeviathexInstantMixerAudioProcessor::~LeviathexInstantMixerAudioProcessor()
{
}

//==============================================================================
const juce::String LeviathexInstantMixerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool LeviathexInstantMixerAudioProcessor::acceptsMidi() const
{
    return false;
}

bool LeviathexInstantMixerAudioProcessor::producesMidi() const
{
    return false;
}

bool LeviathexInstantMixerAudioProcessor::isMidiEffect() const
{
    return false;
}

double LeviathexInstantMixerAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int LeviathexInstantMixerAudioProcessor::getNumPrograms()
{
    return 1;
}

int LeviathexInstantMixerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void LeviathexInstantMixerAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String LeviathexInstantMixerAudioProcessor::getProgramName (int index)
{
    return {};
}

void LeviathexInstantMixerAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void LeviathexInstantMixerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = (float) sampleRate;
    
    // Initialize smoothers
    inputGainSmoother.reset (currentSampleRate, 0.005);
    outputGainSmoother.reset (currentSampleRate, 0.005);
    
    // Reset all states
    for (auto& state : eqStates)
        state.reset();
    for (auto& state : compressorStates)
        state.reset();
    for (auto& state : gateStates)
        state.reset();
    
    // Initialize EQ coefficients
    for (auto& coeffs : eqCoeffs)
    {
        coeffs.a0 = 1.0f;
        coeffs.a1 = coeffs.a2 = coeffs.b1 = coeffs.b2 = 0.0f;
    }
    
    // Build initial EQ
    rebuildEQ (static_cast<int> (*instrumentParam), *mixParam / 100.0f);
}

void LeviathexInstantMixerAudioProcessor::releaseResources()
{
}

bool LeviathexInstantMixerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void LeviathexInstantMixerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    
    if (bypassed.load())
    {
        // Hard bypass - just pass through
        return;
    }
    
    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();
    
    // Clear any output channels that don't contain input data
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);
    
    // Get current parameter values
    // Mix knob: 0 = no processing, 100 = full processing, exponential (squared) curve
    float mixValue = *mixParam;
    float mixLinear = mixValue / 100.0f;
    float mixAmt = mixLinear * mixLinear; // exponential: subtle at low end, strong at top
    int instrument = static_cast<int> (*instrumentParam);
    
    // Update input/output gain smoothers
    float inputGainLinear = knobToLinear (*inputGainParam);
    float outputGainLinear = knobToLinear (*outputGainParam);
    inputGainSmoother.setTargetValue (inputGainLinear);
    outputGainSmoother.setTargetValue (outputGainLinear);
    
    // Rebuild EQ if instrument or mix changed significantly
    if (instrument != lastBuiltInstrument || std::abs (mixAmt - lastBuiltMix) > 0.005f)
    {
        rebuildEQ (instrument, mixAmt);
        lastBuiltInstrument = instrument;
        lastBuiltMix = mixAmt;
    }
    
    // Process each channel (capped to 2 for stereo memory safety)
    int processChannels = std::min (totalNumInputChannels, 2);
    
    for (int channel = 0; channel < processChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer (channel);
        
        // Calculate input levels for metering
        inputLevel[channel] = calculateRMS (channelData, numSamples);
        
        // Find peak for metering
        float channelPeak = 0.0f;
        for (int sample = 0; sample < numSamples; ++sample)
            channelPeak = std::max (channelPeak, std::abs (channelData[sample]));
        inputPeak[channel] = channelPeak;
        
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float x = channelData[sample];
            
            // 1. Input gain (smoothed)
            float inputGain = inputGainSmoother.getNextValue();
            x *= inputGain;
            
            if (mixAmt > 0.0f)
            {
                // 2. Gate
                applyGate (x, gateStates[channel], mixAmt, instrument);
                
                // 3. Compressor
                applyCompressor (x, compressorStates[channel], mixAmt, instrument);
                
                // 4. Saturation
                applySaturation (x, mixAmt, instrument);
                
                // 5. EQ
                for (int stage = 0; stage < 4; ++stage)
                    processBiquad (x, eqStates[channel], eqCoeffs[stage]);
                    
                // 6. Global EQ Gain Compensation (-1dB fixed to counter sum of boosts)
                x *= 0.891f;
            }
            
            // 7. Limiter
            applyLimiter (x);
            
            // 8. Output gain (smoothed)
            float outputGain = outputGainSmoother.getNextValue();
            x *= outputGain;
            
            channelData[sample] = x;
        }
        
        // Calculate output levels for metering
        outputLevel[channel] = calculateRMS (channelData, numSamples);
        
        // Find output peak
        float outputChannelPeak = 0.0f;
        for (int sample = 0; sample < numSamples; ++sample)
            outputChannelPeak = std::max (outputChannelPeak, std::abs (channelData[sample]));
        outputPeak[channel] = outputChannelPeak;
    }
}

//==============================================================================
void LeviathexInstantMixerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("bypassed", bypassed.load(), nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void LeviathexInstantMixerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName (parameters.state.getType()))
        {
            parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
            bypassed.store (xmlState->getBoolAttribute ("bypassed", false));
        }
}

//==============================================================================
// DSP Implementation

void LeviathexInstantMixerAudioProcessor::rebuildEQ (int instrument, float mix)
{
    switch (instrument)
    {
        case 0: // Acoustic Guitar — max +2dB boosts
            eqCoeffs[0] = calcHighPass (80.0f, 0.707f, currentSampleRate);
            eqCoeffs[1] = calcPeakingEQ (4000.0f, 1.4f, mix * 2.0f, currentSampleRate);
            eqCoeffs[2] = calcHighShelf (10000.0f, 0.707f, mix * 1.5f, currentSampleRate);
            eqCoeffs[3] = calcLowShelf (200.0f, 0.707f, mix * 1.0f, currentSampleRate);
            break;
            
        case 1: // Electric Guitar — max +2dB boosts
            eqCoeffs[0] = calcHighPass (100.0f, 0.707f, currentSampleRate);
            eqCoeffs[1] = calcPeakingEQ (400.0f, 2.0f, mix * -2.0f, currentSampleRate);
            eqCoeffs[2] = calcPeakingEQ (2500.0f, 1.6f, mix * 2.0f, currentSampleRate);
            eqCoeffs[3] = calcHighShelf (8000.0f, 0.707f, mix * 1.5f, currentSampleRate);
            break;
            
        case 2: // Vocals — max +2dB boosts
            eqCoeffs[0] = calcHighPass (120.0f, 0.707f, currentSampleRate);
            eqCoeffs[1] = calcPeakingEQ (300.0f, 1.8f, mix * -1.5f, currentSampleRate);
            eqCoeffs[2] = calcPeakingEQ (3000.0f, 1.4f, mix * 2.0f, currentSampleRate);
            eqCoeffs[3] = calcHighShelf (12000.0f, 0.707f, mix * 1.5f, currentSampleRate);
            break;
            
        case 3: // Bass — max +2dB boosts
            eqCoeffs[0] = calcHighPass (40.0f, 0.707f, currentSampleRate);
            eqCoeffs[1] = calcLowShelf (80.0f, 0.707f, mix * 2.0f, currentSampleRate);
            eqCoeffs[2] = calcPeakingEQ (250.0f, 1.6f, mix * -2.0f, currentSampleRate);
            eqCoeffs[3] = calcLowPass (8000.0f - mix * 1000.0f, 0.707f, currentSampleRate);
            break;
            
        case 4: // Piano/Keys — max +2dB boosts
            eqCoeffs[0] = calcHighPass (60.0f, 0.707f, currentSampleRate);
            eqCoeffs[1] = calcLowShelf (200.0f, 0.707f, mix * 1.5f, currentSampleRate);
            eqCoeffs[2] = calcPeakingEQ (1000.0f, 1.2f, mix * 1.0f, currentSampleRate);
            eqCoeffs[3] = calcHighShelf (10000.0f, 0.707f, mix * 1.5f, currentSampleRate);
            break;
            
        case 5: // Drums — max +2dB boosts
            eqCoeffs[0] = calcHighPass (60.0f, 0.707f, currentSampleRate);
            eqCoeffs[1] = calcLowShelf (100.0f, 0.707f, mix * 2.0f, currentSampleRate);
            eqCoeffs[2] = calcPeakingEQ (350.0f, 2.0f, mix * -2.0f, currentSampleRate);
            eqCoeffs[3] = calcPeakingEQ (5000.0f, 1.8f, mix * 2.0f, currentSampleRate);
            break;
    }
}

void LeviathexInstantMixerAudioProcessor::processBiquad (float& sample, BiquadState& state, const BiquadCoeffs& coeffs)
{
    sample = state.process (sample, coeffs);
}

void LeviathexInstantMixerAudioProcessor::applyCompressor (float& sample, CompressorState& state, float mix, int instrument)
{
    // Instrument-specific compressor settings
    float ratio, threshold, attackSec, releaseSec;
    
    switch (instrument)
    {
        case 0: // Acoustic Guitar
            ratio = 1.0f + mix * 2.0f; // 1:1 to 3:1
            threshold = 0.9f - mix * 0.55f; // 0.9 to 0.35
            attackSec = 0.015f;
            releaseSec = 0.2f;
            break;
        case 1: // Electric Guitar
            ratio = 1.0f + mix * 3.0f; // 1:1 to 4:1
            threshold = 0.8f - mix * 0.55f; // 0.8 to 0.25
            attackSec = 0.005f;
            releaseSec = 0.12f;
            break;
        case 2: // Vocals
            ratio = 1.0f + mix * 3.0f; // 1:1 to 4:1
            threshold = 0.85f - mix * 0.5f; // 0.85 to 0.35
            attackSec = 0.02f;
            releaseSec = 0.25f;
            break;
        case 3: // Bass
            ratio = 1.0f + mix * 3.5f; // 1:1 to 4.5:1
            threshold = 0.75f - mix * 0.5f; // 0.75 to 0.25
            attackSec = 0.005f;
            releaseSec = 0.1f;
            break;
        case 4: // Piano/Keys
            ratio = 1.0f + mix * 2.0f; // 1:1 to 3:1
            threshold = 0.9f - mix * 0.5f; // 0.9 to 0.4
            attackSec = 0.03f;
            releaseSec = 0.3f;
            break;
        case 5: // Drums
            ratio = 1.0f + mix * 5.0f; // 1:1 to 6:1
            threshold = 0.7f - mix * 0.5f; // 0.7 to 0.2
            attackSec = 0.001f;
            releaseSec = 0.06f;
            break;
        default:
            ratio = 2.0f;
            threshold = 0.5f;
            attackSec = 0.01f;
            releaseSec = 0.1f;
            break;
    }
    
    // Envelope follower
    float attCoeff = std::exp (-1.0f / (currentSampleRate * attackSec));
    float relCoeff = std::exp (-1.0f / (currentSampleRate * releaseSec));
    
    float envIn = std::abs (sample);
    if (envIn > state.envelope)
        state.envelope = attCoeff * state.envelope + (1.0f - attCoeff) * envIn;
    else
        state.envelope = relCoeff * state.envelope + (1.0f - relCoeff) * envIn;
    
    // Gain reduction
    if (state.envelope > threshold)
    {
        float gr = std::pow (threshold / state.envelope, 1.0f - 1.0f / ratio);
        state.gainReduction = gr;
    }
    else
    {
        state.gainReduction = 1.0f;
    }
    
    // Apply gain reduction (no makeup gain — avoids pumping/distortion)
    sample *= state.gainReduction;
}

void LeviathexInstantMixerAudioProcessor::applyGate (float& sample, GateState& state, float mix, int instrument)
{
    // Instrument-specific gate thresholds
    float baseThreshold;
    switch (instrument)
    {
        case 0: case 3: case 4: // Acoustic Guitar, Bass, Piano/Keys
            baseThreshold = 0.008f;
            break;
        case 2: // Vocals
            baseThreshold = 0.015f;
            break;
        case 1: // Electric Guitar
            baseThreshold = 0.04f;
            break;
        case 5: // Drums
            baseThreshold = 0.03f;
            break;
        default:
            baseThreshold = 0.01f;
            break;
    }
    
    float threshold = mix * baseThreshold;
    
    // Envelope follower (10ms attack, 80ms release)
    float attCoeff = std::exp (-1.0f / (currentSampleRate * 0.01f));
    float relCoeff = std::exp (-1.0f / (currentSampleRate * 0.08f));
    
    float envIn = std::abs (sample);
    if (envIn > state.envelope)
        state.envelope = attCoeff * state.envelope + (1.0f - attCoeff) * envIn;
    else
        state.envelope = relCoeff * state.envelope + (1.0f - relCoeff) * envIn;
    
    // Soft-knee gate
    if (state.envelope < threshold)
        state.gain = (state.envelope / threshold) * (state.envelope / threshold); // Squared for gentle knee
    else
        state.gain = 1.0f;
    
    sample *= state.gain;
}

void LeviathexInstantMixerAudioProcessor::applySaturation (float& sample, float mix, int instrument)
{
    // Instrument-specific drive amounts
    float baseDrive;
    switch (instrument)
    {
        case 0: // Acoustic Guitar
            baseDrive = 0.8f;
            break;
        case 1: // Electric Guitar
            baseDrive = 2.5f;
            break;
        case 2: // Vocals
            baseDrive = 0.6f;
            break;
        case 3: // Bass
            baseDrive = 1.2f;
            break;
        case 4: // Piano/Keys
            baseDrive = 0.4f;
            break;
        case 5: // Drums
            baseDrive = 1.8f;
            break;
        default:
            baseDrive = 1.0f;
            break;
    }
    
    float drive = mix * baseDrive;
    
    // tanh waveshaper — naturally limits peaks, no makeup gain needed
    // Drive kept gentle so it colours tone without adding loudness
    float d = 1.0f + drive * 2.0f;
    sample = std::tanh (sample * d) / d; // divide by d to preserve input level
}

void LeviathexInstantMixerAudioProcessor::applyLimiter (float& sample)
{
    // Soft clip curve before hard limit to avoid nasty digital distortion
    // Tanh curve that smoothly approaches limit
    const float limit = 0.891251f; // -1 dBFS
    const float threshold = 0.6f;  // Start soft clipping around -4.5 dB
    
    float absSample = std::abs(sample);
    
    if (absSample > threshold)
    {
        float overshoot = absSample - threshold;
        // Smoothly map the overshoot
        float soft = threshold + (limit - threshold) * std::tanh (overshoot / (limit - threshold));
        sample = (sample > 0.0f) ? soft : -soft;
    }
    
    // Final safety brickwall
    if (sample > limit)
        sample = limit;
    else if (sample < -limit)
        sample = -limit;
}

float LeviathexInstantMixerAudioProcessor::calculateRMS (const float* buffer, int numSamples)
{
    float sum = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        sum += buffer[i] * buffer[i];
    
    return std::sqrt (sum / numSamples);
}

//==============================================================================
// Biquad coefficient calculations

BiquadCoeffs LeviathexInstantMixerAudioProcessor::calcHighShelf (float freq, float Q, float gain, float sampleRate)
{
    float A = std::pow (10.0f, gain / 40.0f);
    float w = 2.0f * juce::MathConstants<float>::pi * freq / sampleRate;
    float cosw = std::cos (w);
    float sinw = std::sin (w);
    float alpha = sinw / (2.0f * Q);
    
    BiquadCoeffs c;
    c.a0 = A * ((A + 1.0f) + (A - 1.0f) * cosw + 2.0f * std::sqrt (A) * alpha);
    c.a1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw);
    c.a2 = A * ((A + 1.0f) + (A - 1.0f) * cosw - 2.0f * std::sqrt (A) * alpha);
    c.b1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw);
    c.b2 = ((A + 1.0f) - (A - 1.0f) * cosw - 2.0f * std::sqrt (A) * alpha);
    
    float b0 = ((A + 1.0f) - (A - 1.0f) * cosw + 2.0f * std::sqrt (A) * alpha);
    
    c.a0 /= b0;
    c.a1 /= b0;
    c.a2 /= b0;
    c.b1 /= b0;
    c.b2 /= b0;
    
    return c;
}

BiquadCoeffs LeviathexInstantMixerAudioProcessor::calcLowShelf (float freq, float Q, float gain, float sampleRate)
{
    float A = std::pow (10.0f, gain / 40.0f);
    float w = 2.0f * juce::MathConstants<float>::pi * freq / sampleRate;
    float cosw = std::cos (w);
    float sinw = std::sin (w);
    float alpha = sinw / (2.0f * Q);
    
    BiquadCoeffs c;
    c.a0 = A * ((A + 1.0f) - (A - 1.0f) * cosw + 2.0f * std::sqrt (A) * alpha);
    c.a1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw);
    c.a2 = A * ((A + 1.0f) - (A - 1.0f) * cosw - 2.0f * std::sqrt (A) * alpha);
    c.b1 = 2.0f * ((A - 1.0f) + (A + 1.0f) * cosw);
    c.b2 = ((A + 1.0f) + (A - 1.0f) * cosw - 2.0f * std::sqrt (A) * alpha);
    
    float b0 = ((A + 1.0f) + (A - 1.0f) * cosw + 2.0f * std::sqrt (A) * alpha);
    
    c.a0 /= b0;
    c.a1 /= b0;
    c.a2 /= b0;
    c.b1 /= b0;
    c.b2 /= b0;
    
    return c;
}

BiquadCoeffs LeviathexInstantMixerAudioProcessor::calcPeakingEQ (float freq, float Q, float gain, float sampleRate)
{
    float A = std::pow (10.0f, gain / 40.0f);
    float w = 2.0f * juce::MathConstants<float>::pi * freq / sampleRate;
    float cosw = std::cos (w);
    float sinw = std::sin (w);
    float alpha = sinw / (2.0f * Q);
    
    BiquadCoeffs c;
    c.a0 = 1.0f + alpha * A;
    c.a1 = -2.0f * cosw;
    c.a2 = 1.0f - alpha * A;
    c.b1 = 2.0f * cosw;
    c.b2 = 1.0f - alpha / A;
    
    float b0 = 1.0f + alpha / A;
    
    c.a0 /= b0;
    c.a1 /= b0;
    c.a2 /= b0;
    c.b1 /= b0;
    c.b2 /= b0;
    
    return c;
}

BiquadCoeffs LeviathexInstantMixerAudioProcessor::calcHighPass (float freq, float Q, float sampleRate)
{
    float w = 2.0f * juce::MathConstants<float>::pi * freq / sampleRate;
    float cosw = std::cos (w);
    float sinw = std::sin (w);
    float alpha = sinw / (2.0f * Q);
    
    BiquadCoeffs c;
    c.a0 = (1.0f + cosw) / 2.0f;
    c.a1 = -(1.0f + cosw);
    c.a2 = (1.0f + cosw) / 2.0f;
    c.b1 = -2.0f * cosw;
    c.b2 = 1.0f - alpha;
    
    float b0 = 1.0f + alpha;
    
    c.a0 /= b0;
    c.a1 /= b0;
    c.a2 /= b0;
    c.b1 /= b0;
    c.b2 /= b0;
    
    return c;
}

BiquadCoeffs LeviathexInstantMixerAudioProcessor::calcLowPass (float freq, float Q, float sampleRate)
{
    float w = 2.0f * juce::MathConstants<float>::pi * freq / sampleRate;
    float cosw = std::cos (w);
    float sinw = std::sin (w);
    float alpha = sinw / (2.0f * Q);
    
    BiquadCoeffs c;
    c.a0 = (1.0f - cosw) / 2.0f;
    c.a1 = 1.0f - cosw;
    c.a2 = (1.0f - cosw) / 2.0f;
    c.b1 = -2.0f * cosw;
    c.b2 = 1.0f - alpha;
    
    float b0 = 1.0f + alpha;
    
    c.a0 /= b0;
    c.a1 /= b0;
    c.a2 /= b0;
    c.b1 /= b0;
    c.b2 /= b0;
    
    return c;
}

float LeviathexInstantMixerAudioProcessor::knobToLinear (float knobValue) const
{
    if (knobValue <= 0.0f)
        return 0.0f;
    else if (knobValue <= 50.0f)
    {
        // 0-50: -60dB to 0dB
        float db = -60.0f + (knobValue / 50.0f) * 60.0f;
        return juce::Decibels::decibelsToGain (db);
    }
    else
    {
        // 50-100: 0dB to +12dB
        float db = (knobValue - 50.0f) / 50.0f * 12.0f;
        return juce::Decibels::decibelsToGain (db);
    }
}

//==============================================================================
juce::AudioProcessorEditor* LeviathexInstantMixerAudioProcessor::createEditor()
{
    return new LeviathexInstantMixerAudioProcessorEditor (*this);
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LeviathexInstantMixerAudioProcessor();
}
