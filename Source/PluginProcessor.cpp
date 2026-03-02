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
    layout.add (std::make_unique<juce::AudioParameterInt> ("instrument", "Instrument", 0, 2, 0));
    
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
    
    // Initialize smoothers with current param values (Linear type avoids zero-lock)
    float initInputGain  = knobToLinear (*inputGainParam);
    float initOutputGain = knobToLinear (*outputGainParam);
    float initMixLinear  = *mixParam / 100.0f;
    float initMixAmt     = initMixLinear * initMixLinear;
    inputGainSmoother.reset  (currentSampleRate, 0.005);
    outputGainSmoother.reset (currentSampleRate, 0.005);
    // Mix smoother: 80ms ramp so EQ coefficients change gradually, not instantly
    mixSmoother.reset (currentSampleRate, 0.08);
    inputGainSmoother.setCurrentAndTargetValue  (initInputGain);
    outputGainSmoother.setCurrentAndTargetValue (initOutputGain);
    mixSmoother.setCurrentAndTargetValue (initMixAmt);
    
    // Reset all states
    for (auto& chStates : eqStates)
        for (auto& state : chStates)
            state.reset();
    for (auto& state : compressorStates)
        state.reset();
    linkedEnvelope = 0.0f;
    
    // Initialize EQ coefficients (identity passthrough)
    for (auto& coeffs : eqCoeffs)
    {
        coeffs.a0 = 1.0f;
        coeffs.a1 = coeffs.a2 = coeffs.b1 = coeffs.b2 = 0.0f;
    }
    
    // Build initial EQ
    rebuildEQ (static_cast<int> (*instrumentParam), initMixAmt);
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
    float mixValue  = *mixParam;
    float mixLinear = mixValue / 100.0f;
    float mixTarget = mixLinear * mixLinear; // exponential curve
    int instrument  = static_cast<int> (*instrumentParam);
    
    // Update smoothers
    inputGainSmoother.setTargetValue  (knobToLinear (*inputGainParam));
    outputGainSmoother.setTargetValue (knobToLinear (*outputGainParam));
    mixSmoother.setTargetValue (mixTarget);
    
    // Advance mixSmoother to mid-block value for EQ rebuild
    // (smoother is also advanced per-sample inside the loop for compressor)
    float mixAmt = mixSmoother.getTargetValue();
    
    // Rebuild EQ only when instrument or mix changes meaningfully
    if (instrument != lastBuiltInstrument || std::abs (mixAmt - lastBuiltMix) > 0.01f)
    {
        rebuildEQ (instrument, mixAmt);
        lastBuiltInstrument = instrument;
        lastBuiltMix = mixAmt;
    }
    
    // Pre-compute per-sample gain ramps ONCE — both channels use the same values
    // Calling getNextValue() inside the channel loop causes L/R mismatch (static)
    int processChannels = std::min (totalNumInputChannels, 2);
    
    juce::AudioBuffer<float> inputGainRamp  (1, numSamples);
    juce::AudioBuffer<float> outputGainRamp (1, numSamples);
    juce::AudioBuffer<float> mixRamp        (1, numSamples);
    
    for (int i = 0; i < numSamples; ++i)
    {
        inputGainRamp .getWritePointer(0)[i] = inputGainSmoother.getNextValue();
        outputGainRamp.getWritePointer(0)[i] = outputGainSmoother.getNextValue();
        mixRamp       .getWritePointer(0)[i] = mixSmoother.getNextValue();
    }
    
    const float* inGain  = inputGainRamp .getReadPointer(0);
    const float* outGain = outputGainRamp.getReadPointer(0);
    const float* mixVal  = mixRamp       .getReadPointer(0);
    
    for (int channel = 0; channel < processChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer (channel);
        
        // Calculate input levels for metering
        inputLevel[channel] = calculateRMS (channelData, numSamples);
        
        float channelPeak = 0.0f;
        for (int sample = 0; sample < numSamples; ++sample)
            channelPeak = std::max (channelPeak, std::abs (channelData[sample]));
        inputPeak[channel] = channelPeak;
        
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float x = channelData[sample];
            
            // 1. Input gain
            x *= inGain[sample];
            
            float sm = mixVal[sample];
            if (sm > 0.0f)
            {
                // 2. Compressor
                applyCompressor (x, sm, instrument);
                
                // 3. EQ (6 bands)
                for (int stage = 0; stage < 6; ++stage)
                    processBiquad (x, eqStates[channel][stage], eqCoeffs[stage]);
            }
            
            // 4. Limiter
            applyLimiter (x);
            
            // 5. Output gain
            x *= outGain[sample];
            
            // 6. NaN/Inf brickwall
            if (! std::isfinite (x)) x = 0.0f;
            
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
        case 0: // Acoustic Guitar — gentle mix curve, max +1.5dB
            eqCoeffs[0] = calcHighPass  (80.0f,    0.707f,             currentSampleRate);        // HPF
            eqCoeffs[1] = calcLowShelf  (200.0f,   0.707f, mix * 1.0f,  currentSampleRate);       // warmth
            eqCoeffs[2] = calcPeakingEQ (350.0f,   1.8f,   mix * -1.0f, currentSampleRate);       // mud cut
            eqCoeffs[3] = calcPeakingEQ (5000.0f,  1.6f,   mix * 1.5f,  currentSampleRate);       // presence
            eqCoeffs[4] = calcHighShelf (10000.0f, 0.707f, mix * 1.0f,  currentSampleRate);       // air
            eqCoeffs[5] = calcLowPass   (18000.0f, 0.707f,              currentSampleRate);        // LPF
            break;

        case 1: // Vocals — gentle mix curve, max +1.5dB
            eqCoeffs[0] = calcHighPass  (120.0f,   0.707f,             currentSampleRate);        // HPF
            eqCoeffs[1] = calcPeakingEQ (300.0f,   1.8f,   mix * -1.0f, currentSampleRate);       // boxy cut
            eqCoeffs[2] = calcPeakingEQ (1000.0f,  1.4f,   mix * 1.0f,  currentSampleRate);       // body
            eqCoeffs[3] = calcPeakingEQ (3500.0f,  1.4f,   mix * 1.5f,  currentSampleRate);       // presence
            eqCoeffs[4] = calcHighShelf (12000.0f, 0.707f, mix * 1.0f,  currentSampleRate);       // air
            eqCoeffs[5] = calcLowPass   (20000.0f, 0.707f,              currentSampleRate);        // LPF
            break;

        case 2: // Piano/Keys — gentle mix curve, max +1.5dB
            eqCoeffs[0] = calcHighPass  (60.0f,    0.707f,             currentSampleRate);        // HPF
            eqCoeffs[1] = calcLowShelf  (180.0f,   0.707f, mix * 1.0f,  currentSampleRate);       // warmth
            eqCoeffs[2] = calcPeakingEQ (400.0f,   1.6f,   mix * -1.0f, currentSampleRate);       // mud cut
            eqCoeffs[3] = calcPeakingEQ (2500.0f,  1.4f,   mix * 1.0f,  currentSampleRate);       // definition
            eqCoeffs[4] = calcHighShelf (10000.0f, 0.707f, mix * 1.5f,  currentSampleRate);       // air
            eqCoeffs[5] = calcLowPass   (20000.0f, 0.707f,              currentSampleRate);        // LPF
            break;

        default:
            for (auto& c : eqCoeffs) { c.a0=1.f; c.a1=c.a2=c.b1=c.b2=0.f; }
            break;
    }
}

void LeviathexInstantMixerAudioProcessor::processBiquad (float& sample, BiquadState& state, const BiquadCoeffs& coeffs)
{
    sample = state.process (sample, coeffs);
}

void LeviathexInstantMixerAudioProcessor::applyCompressor (float& sample, float mix, int instrument)
{
    // API 2500-modeled feed-forward compressor
    // 4:1, 0.3ms attack, 100ms release, -18 dBFS threshold, stereo-linked envelope
    const float ratio     = 4.0f;
    const float threshLin = 0.126f;   // -18 dBFS
    const float threshDb  = -18.0f;
    
    // Coefficients are constant — computed once per block call in practice,
    // but cached as statics so they are only recalculated if sampleRate changes.
    static float cachedSR = 0.0f;
    static float attCoeff = 0.0f;
    static float relCoeff = 0.0f;
    if (cachedSR != currentSampleRate)
    {
        cachedSR  = currentSampleRate;
        attCoeff  = std::exp (-1.0f / (currentSampleRate * 0.0003f));
        relCoeff  = std::exp (-1.0f / (currentSampleRate * 0.1f));
    }
    
    float envIn = std::abs (sample);
    if (! std::isfinite (envIn)) envIn = 0.0f;
    
    if (envIn > linkedEnvelope)
        linkedEnvelope = attCoeff * linkedEnvelope + (1.0f - attCoeff) * envIn;
    else
        linkedEnvelope = relCoeff * linkedEnvelope + (1.0f - relCoeff) * envIn;
    
    float gainReduction = 1.0f;
    if (linkedEnvelope > threshLin)
    {
        float inputDb  = 20.0f * std::log10 (linkedEnvelope);
        float excessDb = inputDb - threshDb;
        float reducDb  = excessDb * (1.0f - 1.0f / ratio) * mix;
        gainReduction  = juce::Decibels::decibelsToGain (-reducDb);
    }
    
    sample *= gainReduction;
    
    // Safety: kill any NaN/Inf that could crash the audio driver
    if (! std::isfinite (sample)) sample = 0.0f;
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
